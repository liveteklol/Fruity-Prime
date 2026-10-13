#pragma once

#include "NativeRuntime/System/Net.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    enum class PacketType : std::uint8_t;
}

namespace MphRead::Mods::Network
{
    struct NetTransportInboxStats final
    {
        std::size_t Depth = 0;
        std::size_t Capacity = 0;
        std::int64_t ExistingSupersededDrops = 0;
        std::int64_t IncomingSupersededDrops = 0;
        std::int64_t CriticalOverflowDrops = 0;
    };

    struct NetTransportFaultQueueStats final
    {
        std::int32_t Depth = 0;
        std::int32_t Capacity = 0;
        std::int64_t LossDrops = 0;
        std::int64_t CapacityDrops = 0;
    };

    struct NetTransportQueueStats final
    {
        NetTransportInboxStats Inbox{};
        NetTransportFaultQueueStats HeldInput{};
        NetTransportFaultQueueStats HeldOutput{};
    };

    struct ReceivedPacket
    {
        const std::shared_ptr<System::Net::IPEndPoint> Sender{};
        const std::shared_ptr<std::vector<std::uint8_t>> Data{};
        const std::int32_t Length = 0;
        const std::int64_t ArrivedAt = 0;

        ReceivedPacket() noexcept = default;
        ReceivedPacket(std::shared_ptr<System::Net::IPEndPoint> sender,
            std::shared_ptr<std::vector<std::uint8_t>> data, std::int32_t length,
            std::int64_t arrivedAt = 0) noexcept;
        ReceivedPacket(const ReceivedPacket&) noexcept = default;
        ReceivedPacket(ReceivedPacket&& other) noexcept;
        ReceivedPacket& operator=(const ReceivedPacket& other) noexcept;
        ReceivedPacket& operator=(ReceivedPacket&& other) noexcept;
        ~ReceivedPacket() = default;

        [[nodiscard]] PacketType Type() const;
        [[nodiscard]] std::span<const std::uint8_t> Payload() const;
    };

    class NetTransport final
    {
    private:
        struct State;

    public:
        class DrainEnumerable final
        {
        public:
            class iterator final
            {
            public:
                using value_type = ReceivedPacket;
                using difference_type = std::ptrdiff_t;

                iterator() noexcept = default;
                [[nodiscard]] ReceivedPacket operator*() const;
                iterator& operator++();
                void operator++(int);

                [[nodiscard]] bool AtEnd() const noexcept;
                friend bool operator==(const iterator& left, std::default_sentinel_t) noexcept
                {
                    return left.AtEnd();
                }
                friend bool operator!=(const iterator& left, std::default_sentinel_t) noexcept
                {
                    return !left.AtEnd();
                }

            private:
                friend class DrainEnumerable;
                explicit iterator(std::shared_ptr<State> state);
                void Advance();

                std::shared_ptr<State> _state{};
                std::optional<ReceivedPacket> _current{};
            };

            [[nodiscard]] iterator begin() const;
            [[nodiscard]] std::default_sentinel_t end() const noexcept;

        private:
            friend class NetTransport;
            explicit DrainEnumerable(std::shared_ptr<State> state) noexcept;
            std::shared_ptr<State> _state;
        };

        explicit NetTransport(std::int32_t port);
        ~NetTransport() = default;

        NetTransport(const NetTransport&) = delete;
        NetTransport(NetTransport&&) = delete;
        NetTransport& operator=(const NetTransport&) = delete;
        NetTransport& operator=(NetTransport&&) = delete;

        void AnswerPingsImmediately() noexcept;

        [[nodiscard]] std::int32_t LocalPort() const noexcept;
        [[nodiscard]] std::int64_t PacketsDropped() const noexcept;
        [[nodiscard]] NetTransportQueueStats QueueStats() const;

        [[nodiscard]] static std::int64_t TotalPacketsDropped() noexcept
        {
            return _totalPacketsDropped.load(std::memory_order_seq_cst);
        }
        [[nodiscard]] static std::int64_t TotalPacketsSent() noexcept
        {
            return _totalPacketsSent.load(std::memory_order_seq_cst);
        }

        [[nodiscard]] DrainEnumerable Drain();

        void EnqueueForPlayback(const std::shared_ptr<std::vector<std::uint8_t>>& data,
            std::int32_t length);

        void Send(const std::shared_ptr<System::Net::IPEndPoint>& target,
            PacketType type, std::span<const std::uint8_t> payload,
            std::int64_t extraHoldTicks = 0);

        void Dispose();

    private:
        static constexpr std::int32_t MaxQueuedPackets = 2048;
        static constexpr std::int32_t SocketBufferBytes = 1 << 20;

        static std::atomic<std::int64_t> _totalPacketsDropped;
        static std::atomic<std::int64_t> _totalPacketsSent;
        static const std::shared_ptr<System::Net::IPEndPoint> _playbackSender;
        std::shared_ptr<State> _state;
    };
}
