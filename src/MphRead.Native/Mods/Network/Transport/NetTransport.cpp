#include "Mods/Network/Transport/NetTransport.hpp"

#include "Mods/Network/Transport/BoundedInbox.hpp"
#include "Mods/Network/Transport/InboxShedding.hpp"
#include "Mods/Network/Transport/NetLag.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Net.hpp"
#include "NativeRuntime/System/Stopwatch.hpp"
#include "NativeRuntime/System/Tasks.hpp"

#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <cerrno>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#endif

using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::ManagedAt;

using ::MphRead::NativeRuntime::SetCurrentThreadName;
using ::MphRead::NativeRuntime::StopwatchGetTimestamp;
using ::MphRead::NativeRuntime::UdpSocket;

namespace MphRead::Mods::Network
{
    ReceivedPacket::ReceivedPacket(std::shared_ptr<System::Net::IPEndPoint> sender,
        std::shared_ptr<std::vector<std::uint8_t>> data, std::int32_t length, std::int64_t arrivedAt) noexcept
        : Sender(std::move(sender)), Data(std::move(data)), Length(length),
          ArrivedAt(arrivedAt == 0 ? StopwatchGetTimestamp() : arrivedAt)
    {
    }

    ReceivedPacket::ReceivedPacket(ReceivedPacket&& other) noexcept
        : Sender(other.Sender), Data(other.Data), Length(other.Length), ArrivedAt(other.ArrivedAt)
    {
    }

    ReceivedPacket& ReceivedPacket::operator=(const ReceivedPacket& other) noexcept
    {
        if (this != &other)
        {
            this->~ReceivedPacket();
            new (this) ReceivedPacket(other);
        }
        return *this;
    }

    ReceivedPacket& ReceivedPacket::operator=(ReceivedPacket&& other) noexcept
    {
        if (this != &other)
        {
            this->~ReceivedPacket();
            new (this) ReceivedPacket(std::move(other));
        }
        return *this;
    }

    PacketType ReceivedPacket::Type() const
    {
        if (Length <= 0)
        {
            return static_cast<PacketType>(0);
        }
        if (!Data)
        {
            throw System::NullReferenceException();
        }
        return static_cast<PacketType>(ManagedAt(*Data, 0));
    }

    std::span<const std::uint8_t> ReceivedPacket::Payload() const
    {
        if (!Data || Length < 1
            || static_cast<std::size_t>(Length) > Data->size())
        {
            throw System::ArgumentOutOfRangeException("start");
        }
        return std::span<const std::uint8_t>(Data->data() + 1,
            static_cast<std::size_t>(Length - 1));
    }

    struct NetTransport::State
    {
        // (IPEndPoint Target, byte[] Data, int Length)
        struct HeldOut
        {
            std::shared_ptr<System::Net::IPEndPoint> Target{};
            std::shared_ptr<std::vector<std::uint8_t>> Data{};
            std::int32_t Length = 0;
        };

        [[nodiscard]] static double NowMs() noexcept
        {
            return static_cast<double>(StopwatchGetTimestamp()) * 1000.0
                / static_cast<double>(::MphRead::NativeRuntime::StopwatchFrequency());
        }

        [[nodiscard]] bool TryDequeueInbox(std::optional<ReceivedPacket>& packet)
        {
            return Inbox.TryDequeue(packet);
        }

        [[nodiscard]] InboxEnqueueResult TryEnqueueInbox(ReceivedPacket&& packet)
        {
            const InboxEnqueueResult result = Inbox.TryEnqueue(std::move(packet),
                [](const ReceivedPacket& queued)
                {
                    return queued.Type();
                });
            if (result != InboxEnqueueResult::Enqueued)
            {
                // PacketsDropped keeps its historical meaning: packets lost
                // to the production inbox's finite capacity, not simulated
                // NetLag loss in either held scheduler.
                Dropped.fetch_add(1, std::memory_order_seq_cst);
                NetTransport::_totalPacketsDropped.fetch_add(1, std::memory_order_seq_cst);
            }
            return result;
        }

        [[nodiscard]] NetTransportQueueStats QueueStats()
        {
            NetTransportQueueStats stats{};
            const BoundedInboxStatistics inboxStats = Inbox.Statistics();
            stats.Inbox = {
                inboxStats.Depth,
                inboxStats.Capacity,
                inboxStats.ExistingSupersededDrops,
                inboxStats.IncomingSupersededDrops,
                inboxStats.CriticalOverflowDrops
            };
            {
                std::lock_guard lock(HeldLock);
                stats.HeldInput = {
                    HeldInput.Count(), HeldInput.Capacity(),
                    HeldInput.DroppedByLoss(), HeldInput.DroppedByCapacity()
                };
                stats.HeldOutput = {
                    HeldOutput.Count(), HeldOutput.Capacity(),
                    HeldOutput.DroppedByLoss(), HeldOutput.DroppedByCapacity()
                };
            }
            return stats;
        }

        void PromoteHeldArrivals()
        {
            const double now = NowMs();
            while (true)
            {
                ReceivedPacket packet;
                {
                    std::lock_guard lock(HeldLock);
                    if (!HeldInput.TryDequeue(now, packet))
                    {
                        return;
                    }
                }
                (void)TryEnqueueInbox(std::move(packet));
            }
        }

        void SendNow(const std::shared_ptr<System::Net::IPEndPoint>& target,
            std::span<const std::uint8_t> datagram)
        {
            try
            {
                Socket->Send(datagram, target);
                NetTransport::_totalPacketsSent.fetch_add(1, std::memory_order_seq_cst);
            }
            catch (const ::MphRead::NativeRuntime::SocketException&)
            {
            }
            catch (const System::ObjectDisposedException&)
            {
            }
        }

        void SendPacket(const std::shared_ptr<System::Net::IPEndPoint>& target,
            PacketType type, std::span<const std::uint8_t> payload,
            std::int64_t extraHoldTicks)
        {
            if (payload.size() > static_cast<std::size_t>(NetConfig::MaxPacketSize - 1))
            {
                throw System::ArgumentException(
                    "Destination is too short. (Parameter 'destination')");
            }

            std::array<std::uint8_t, NetConfig::MaxPacketSize> buffer;
            buffer[0] = static_cast<std::uint8_t>(type);
            if (!payload.empty())
            {
                std::memcpy(buffer.data() + 1, payload.data(), payload.size());
            }
            const std::int32_t length = static_cast<std::int32_t>(payload.size() + 1);

            if (LagWorkerPresent.load(std::memory_order_seq_cst))
            {
                auto copy = std::make_shared<std::vector<std::uint8_t>>(buffer.begin(), buffer.begin() + length);
                std::lock_guard lock(HeldLock);
                HeldOutput.Enqueue(NowMs(), HeldOut{target, std::move(copy), length},
                    static_cast<double>(extraHoldTicks) * 1000.0
                        / static_cast<double>(::MphRead::NativeRuntime::StopwatchFrequency()));
                return;
            }

            SendNow(target,
                std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(length)));
        }

        void LagLoop()
        {
            SetCurrentThreadName("MphRead net lag");
            while (Running.load(std::memory_order_seq_cst))
            {
                const double now = NowMs();
                while (true)
                {
                    HeldOut held;
                    {
                        std::lock_guard lock(HeldLock);
                        if (!HeldOutput.TryDequeue(now, held))
                        {
                            break;
                        }
                    }
                    SendNow(held.Target,
                        std::span<const std::uint8_t>(held.Data->data(), static_cast<std::size_t>(held.Length)));
                }
                ::MphRead::NativeRuntime::ThreadSleep(1);
            }
        }

        void SignalWorkerExited()
        {
            {
                std::lock_guard lock(WorkerExitLock);
                WorkerExited = true;
            }
            WorkerExitCondition.notify_all();
        }

        void ReceiveLoopCore()
        {
            SetCurrentThreadName("MphRead net");
            const std::shared_ptr<System::Net::IPEndPoint> any
                = System::Net::IPEndPoint::Any(0);

            while (Running.load(std::memory_order_seq_cst))
            {
                try
                {
                    std::shared_ptr<System::Net::IPEndPoint> sender = any;
                    std::shared_ptr<std::vector<std::uint8_t>> data;
                    try
                    {
                        data = Socket->Receive(sender);
                    }
                    catch (const ::MphRead::NativeRuntime::SocketException& ex)
                    {
                        if (::MphRead::NativeRuntime::SocketErrorIsTimeout(ex))
                        {
                            continue;
                        }
                        throw;
                    }

                    if (data->empty() || data->size() > static_cast<std::size_t>(NetConfig::MaxPacketSize))
                    {
                        continue;
                    }

                    if (AutoPong.load(std::memory_order_seq_cst)
                        && static_cast<PacketType>((*data)[0]) == PacketType::Ping)
                    {
                        const std::int64_t extraHold = LagWorkerPresent.load(std::memory_order_seq_cst)
                            ? static_cast<std::int64_t>(NetLag::RoundTripMs() / 2.0
                                * static_cast<double>(::MphRead::NativeRuntime::StopwatchFrequency()) / 1000)
                            : 0;
                        SendPacket(sender, PacketType::Pong,
                            std::span<const std::uint8_t>(data->data() + 1, data->size() - 1),
                            extraHold);
                        continue;
                    }

                    if (LagWorkerPresent.load(std::memory_order_seq_cst))
                    {
                        std::lock_guard lock(HeldLock);
                        HeldInput.Enqueue(NowMs(),
                            ReceivedPacket(sender, data, static_cast<std::int32_t>(data->size())));
                        continue;
                    }

                    (void)TryEnqueueInbox(ReceivedPacket(sender, data,
                        static_cast<std::int32_t>(data->size())));
                }
                catch (const ::MphRead::NativeRuntime::SocketException&)
                {
                }
                catch (const System::ObjectDisposedException&)
                {
                    break;
                }
            }
        }

        void ReceiveLoop()
        {
            try
            {
                ReceiveLoopCore();
            }
            catch (...)
            {
                SignalWorkerExited();
                throw;
            }
            SignalWorkerExited();
        }

        std::shared_ptr<UdpSocket> Socket{};
        std::atomic<bool> Running{false};
        std::atomic<bool> AutoPong{false};
        std::atomic<bool> LagWorkerPresent{false};
        std::atomic<std::int64_t> Dropped{0};
        std::int32_t LocalPort = 0;

        BoundedInbox<ReceivedPacket> Inbox{NetTransport::MaxQueuedPackets};

        std::mutex HeldLock;
        NetFaultQueue<ReceivedPacket> HeldInput = NetLag::CreateQueue<ReceivedPacket>(false);
        NetFaultQueue<HeldOut> HeldOutput = NetLag::CreateQueue<HeldOut>(true);

        std::mutex WorkerExitLock;
        std::condition_variable WorkerExitCondition;
        bool WorkerExited = false;

        std::mutex CancellationLock;
        bool CancellationDisposed = false;
        bool CancellationRequested = false;
    };

    std::atomic<std::int64_t> NetTransport::_totalPacketsDropped{0};
    std::atomic<std::int64_t> NetTransport::_totalPacketsSent{0};

    const std::shared_ptr<System::Net::IPEndPoint> NetTransport::_playbackSender
        = System::Net::IPEndPoint::Loopback(0);

    NetTransport::DrainEnumerable::iterator::iterator(std::shared_ptr<State> state)
        : _state(std::move(state))
    {
        if (_state && _state->LagWorkerPresent.load(std::memory_order_seq_cst))
        {
            _state->PromoteHeldArrivals();
        }
        Advance();
    }

    ReceivedPacket NetTransport::DrainEnumerable::iterator::operator*() const
    {
        return *_current;
    }

    NetTransport::DrainEnumerable::iterator&
        NetTransport::DrainEnumerable::iterator::operator++()
    {
        Advance();
        return *this;
    }

    void NetTransport::DrainEnumerable::iterator::operator++(int)
    {
        Advance();
    }

    bool NetTransport::DrainEnumerable::iterator::AtEnd() const noexcept
    {
        return !_current.has_value();
    }

    void NetTransport::DrainEnumerable::iterator::Advance()
    {
        if (!_state || !_state->TryDequeueInbox(_current))
        {
            _current.reset();
        }
    }

    NetTransport::DrainEnumerable::DrainEnumerable(std::shared_ptr<State> state) noexcept
        : _state(std::move(state))
    {
    }

    NetTransport::DrainEnumerable::iterator NetTransport::DrainEnumerable::begin() const
    {
        return iterator(_state);
    }

    std::default_sentinel_t NetTransport::DrainEnumerable::end() const noexcept
    {
        return {};
    }

    NetTransport::NetTransport(std::int32_t port)
        : _state(std::make_shared<State>())
    {
        auto socket = std::make_shared<UdpSocket>();
#if defined(_WIN32)
        socket->DisableUdpConnectionResetOnWindows();
#endif
        try
        {
            socket->SetReceiveBufferSize(SocketBufferBytes);
            socket->SetSendBufferSize(SocketBufferBytes);
        }
        catch (const ::MphRead::NativeRuntime::SocketException&)
        {
        }

        socket->SetReceiveTimeout(500);
        socket->Bind(port);

        const std::shared_ptr<State> state = _state;
        state->Socket = socket;
        state->LocalPort = socket->LocalPort();
        state->Running.store(true, std::memory_order_seq_cst);

        std::thread([state]()
        {
            state->ReceiveLoop();
        }).detach();

        if (NetLag::Active())
        {
            state->LagWorkerPresent.store(true, std::memory_order_seq_cst);
            std::thread([state]()
            {
                state->LagLoop();
            }).detach();
        }
    }

    void NetTransport::AnswerPingsImmediately() noexcept
    {
        _state->AutoPong.store(true, std::memory_order_seq_cst);
    }

    std::int32_t NetTransport::LocalPort() const noexcept
    {
        return _state->LocalPort;
    }

    std::int64_t NetTransport::PacketsDropped() const noexcept
    {
        return _state->Dropped.load(std::memory_order_seq_cst);
    }

    NetTransportQueueStats NetTransport::QueueStats() const
    {
        return _state->QueueStats();
    }

    NetTransport::DrainEnumerable NetTransport::Drain()
    {
        return DrainEnumerable(_state);
    }

    void NetTransport::EnqueueForPlayback(
        const std::shared_ptr<std::vector<std::uint8_t>>& data, std::int32_t length)
    {
        (void)_state->TryEnqueueInbox(ReceivedPacket(_playbackSender, data, length));
    }

    void NetTransport::Send(const std::shared_ptr<System::Net::IPEndPoint>& target,
        PacketType type, std::span<const std::uint8_t> payload, std::int64_t extraHoldTicks)
    {
        _state->SendPacket(target, type, payload, extraHoldTicks);
    }

    void NetTransport::Dispose()
    {
        const std::shared_ptr<State> state = _state;
        state->Running.store(false, std::memory_order_seq_cst);

        {
            std::lock_guard lock(state->CancellationLock);
            if (state->CancellationDisposed)
            {
                throw System::ObjectDisposedException(
                    "System.Threading.CancellationTokenSource",
                    "The CancellationTokenSource has been disposed.");
            }
            state->CancellationRequested = true;
        }

        state->Socket->Dispose();

        {
            std::unique_lock lock(state->WorkerExitLock);
            if (!state->WorkerExited)
            {
                (void)state->WorkerExitCondition.wait_for(lock, std::chrono::seconds(1),
                    [&state]() { return state->WorkerExited; });
            }
        }

        {
            std::lock_guard lock(state->CancellationLock);
            state->CancellationDisposed = true;
        }
    }
}
