#include "Mods/Network/Testing/Lobby/NetLobbyTest.hpp"

#include "Mods/Network/Server/Core/DedicatedServer.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Bots/ServerBots.hpp"
#include "Mods/Network/Replay/DemoFile.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Server/Lobby/LobbyRules.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Testing/Pickups/NetItemStateTest.hpp"
#include "Mods/Network/Transport/NetLag.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Session/NetMatchSync.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Discovery/NetStatus.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include "Mods/Multiplayer/TeamGameplayTest.hpp"
#include "Mods/Multiplayer/TeamLayout.hpp"
#include "GameState.hpp"
#include "Metadata/Rooms.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Stopwatch.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Multiplayer::MatchWorldProfile;
    using Multiplayer::ResourceSpawnProfile;
    using Multiplayer::TeamLayout;
    using Multiplayer::TeamRules;

    namespace
    {
        [[nodiscard]] std::int64_t ElapsedMilliseconds(std::int64_t start)
        {
            return Runtime::StopwatchGetElapsedTicks(start) / 10000;
        }

        // Array.IndexOf(array, value, 0, count).
        [[nodiscard]] std::int32_t IndexOf(const std::vector<std::uint8_t>& values, std::uint8_t value,
            std::int32_t count)
        {
            for (std::int32_t i = 0; i < count; i++)
            {
                if (values.at(static_cast<std::size_t>(i)) == value)
                {
                    return i;
                }
            }
            return -1;
        }

        [[nodiscard]] bool NoneReady(const RosterPacket& roster)
        {
            for (std::int32_t i = 0; i < roster.Count; i++)
            {
                if ((*roster.LobbyReady)[static_cast<std::size_t>(i)])
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool TeamsStartWith(const RosterPacket& roster, std::initializer_list<std::int8_t> expected)
        {
            return std::equal(expected.begin(), expected.end(), roster.Teams->begin());
        }

        [[nodiscard]] std::int32_t CountTeam(const RosterPacket& roster, std::int32_t take, std::int8_t team)
        {
            return static_cast<std::int32_t>(std::count(roster.Teams->begin(), roster.Teams->begin() + take, team));
        }
    }

    class NetLobbyTest::Client final
    {
    public:
        std::shared_ptr<NetTransport> Transport = std::make_shared<NetTransport>(0);
        const std::uint32_t Id;
        const std::shared_ptr<System::Net::IPEndPoint> Server;
        std::int32_t Slot = -1;
        std::optional<SessionStatePacket> State{};
        RosterPacket Roster = RosterPacket::Create();
        MatchStatePacket Match{};
        bool Authority = false;
        bool Refused = false;
        std::vector<ChatPacket> Chats{};
        std::unordered_map<std::uint32_t, LobbyCommandResultPacket> Results{};

        Client(std::int32_t port, std::uint32_t id, Runtime::Guid token = {})
            : Id(id), Server(System::Net::IPEndPoint::Loopback(port))
        {
            Transport->AnswerPingsImmediately();
            Hello(token);
        }

        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        // `using` / Rig.Dispose: a client disposed explicitly is not disposed twice.
        ~Client()
        {
            try
            {
                Dispose();
            }
            catch (...)
            {
            }
        }

        void Hello(Runtime::Guid token = {})
        {
            HelloPacket hello{};
            hello.Protocol = NetConfig::ProtocolVersion;
            hello.Slot = Slot < 0 ? HelloPacket::AnySlot : static_cast<std::uint8_t>(Slot);
            hello.ClientId = Id;
            hello.OwnerToken = token;
            // One secret per client, kept across Rebind like a real process's.
            hello.Secret.fill(static_cast<std::uint8_t>(Id | 1U));
            std::array<std::uint8_t, HelloPacket::Size> bytes{};
            hello.Write(bytes);
            Send(PacketType::Hello, bytes);
        }

        void Identify(std::uint8_t hunter = 0)
        {
            std::array<std::uint8_t, 8> bytes{};
            bytes[0] = hunter;
            NetText::Write(std::span(bytes).subspan(2), "Test" + std::to_string(Id));
            Send(PacketType::Identify, bytes);
        }

        void Send(PacketType type, std::span<const std::uint8_t> bytes)
        {
            Transport->Send(Server, type, bytes);
        }

        LobbyCommandPacket Command(LobbyCommandType type, bool ready = false,
            std::optional<SessionStatePacket> config = std::nullopt,
            std::uint8_t target = 255, std::int8_t team = -1, std::optional<std::uint16_t> revision = std::nullopt)
        {
            LobbyCommandPacket packet{};
            packet.CommandId = ++_command;
            packet.ExpectedRevision = revision.has_value() ? *revision : State.value().Revision;
            packet.Type = type;
            packet.Ready = ready;
            packet.Configuration = config.has_value() ? *config : State.value();
            packet.TargetSlot = target;
            packet.TeamIndex = team;
            Resend(packet);
            return packet;
        }

        void Resend(const LobbyCommandPacket& command)
        {
            std::vector<std::uint8_t> bytes(LobbyCommandPacket::Size);
            command.Write(bytes);
            Send(PacketType::LobbyCommand, bytes);
        }

        void Loaded(std::optional<std::uint16_t> id = std::nullopt)
        {
            std::array<std::uint8_t, 2> bytes{};
            MatchLoadedPacket{id.has_value() ? *id : State.value().MatchId}.Write(bytes);
            Send(PacketType::MatchLoaded, bytes);
        }

        void ReadyResults()
        {
            IntentPacket intent{};
            intent.Frame = ++_frame;
            intent.Buttons = IntentButtons::ReadyState;
            intent.MatchId = State.value().MatchId;
            intent.AuthorityEpoch = State->AuthorityEpoch;
            const std::int32_t index = IndexOf(*Roster.Slots, static_cast<std::uint8_t>(Slot),
                static_cast<std::int32_t>(Roster.Slots->size()));
            intent.SlotGeneration = Roster.Generations->at(static_cast<std::size_t>(index));
            std::vector<std::uint8_t> bytes(IntentPacket::FullSize);
            intent.Write(bytes);
            Send(PacketType::Intent, bytes);
        }

        void EndMatch()
        {
            std::array<std::uint8_t, 10> bytes{};
            Runtime::WriteUInt16LittleEndian(bytes, State.value().MatchId);
            Runtime::WriteUInt64LittleEndian(std::span(bytes).subspan(2), State->AuthorityEpoch);
            Send(PacketType::MatchEnd, bytes);
        }

        void ProposeMap(const std::string& roomKey)
        {
            VotePacket vote{};
            vote.Kind = VotePacket::KindPropose;
            vote.RoomKey = roomKey;
            std::array<std::uint8_t, VotePacket::Size> bytes{};
            vote.Write(bytes);
            Send(PacketType::Vote, bytes);
        }

        void Drain()
        {
            for (const ReceivedPacket packet : Transport->Drain())
            {
                const std::span<const std::uint8_t> payload = packet.Payload();
                if (packet.Type() == PacketType::Welcome)
                {
                    Slot = payload[0];
                }
                if (packet.Type() == PacketType::Authority)
                {
                    Authority = true;
                }
                if (packet.Type() == PacketType::Refused)
                {
                    Refused = true;
                }
                if (packet.Type() == PacketType::Chat && payload.size() == static_cast<std::size_t>(ChatPacket::Size))
                {
                    Chats.push_back(ChatPacket::Read(payload));
                }
                SessionStatePacket state{};
                if (packet.Type() == PacketType::SessionState && SessionStatePacket::TryRead(payload, state)
                    && (!State.has_value() || state.Revision == State->Revision
                        || SessionStatePacket::IsNewer(state.Revision, State->Revision)))
                {
                    State = state;
                }
                RosterPacket roster{};
                if (packet.Type() == PacketType::Roster && RosterPacket::TryRead(payload, roster)
                    && (roster.Revision == Roster.Revision || NetLifecycleTracker::Newer(roster.Revision, Roster.Revision)))
                {
                    Roster = roster;
                }
                LobbyCommandResultPacket result{};
                if (packet.Type() == PacketType::LobbyCommandResult && LobbyCommandResultPacket::TryRead(payload, result))
                {
                    Results[result.CommandId] = result;
                }
                if (packet.Type() == PacketType::MatchState && payload.size() == static_cast<std::size_t>(MatchStatePacket::Size))
                {
                    Match = MatchStatePacket::Read(payload);
                }
            }
        }

        void Rebind()
        {
            Transport->Dispose();
            Transport = std::make_shared<NetTransport>(0);
            Transport->AnswerPingsImmediately();
            Hello();
        }

        void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;
            Send(PacketType::Bye, {});
            Transport->Dispose();
        }

    private:
        std::uint32_t _command = 0;
        std::uint32_t _frame = 0;
        bool _disposed = false;
    };

    class NetLobbyTest::Rig final
    {
    private:
        struct ThreadState final
        {
            explicit ThreadState(std::shared_ptr<DedicatedServer> server)
                : Server(std::move(server))
            {
            }

            std::shared_ptr<DedicatedServer> Server;
            std::mutex Mutex{};
            std::condition_variable Finished{};
            std::exception_ptr Error{};
            bool Done = false;
        };

    public:
        std::shared_ptr<DedicatedServer> Server;
        std::vector<std::unique_ptr<Client>> Clients{};
        std::int32_t _expectedBotCount = 0;

        explicit Rig(ServerSessionPolicy policy = ServerSessionPolicy::Lobby, Runtime::Guid token = {},
            float timeLimit = 0)
        {
            Server = std::make_shared<DedicatedServer>(0, 8,
                MapRotation::SingleMatch(Rooms()[0], GameMode::Battle,
                    policy == ServerSessionPolicy::Continuous ? 1 : timeLimit, 0));
            Server->SessionPolicy(policy);
            Server->OwnerToken(token);
            Server->RunsTheMatch(false);
            _threadState = std::make_shared<ThreadState>(Server);
            const std::shared_ptr<ThreadState> state = _threadState;
            _thread = std::thread([state]()
            {
                try
                {
                    state->Server->Run();
                }
                catch (...)
                {
                    const std::scoped_lock lock(state->Mutex);
                    state->Error = std::current_exception();
                }
                {
                    const std::scoped_lock lock(state->Mutex);
                    state->Done = true;
                }
                state->Finished.notify_all();
            });
            try
            {
                Wait([this]() { return Server->Listening(); }, "server listening");
            }
            catch (...)
            {
                Server->Stop();
                try
                {
                    JoinWithin(5000);
                }
                catch (...)
                {
                    if (_thread.joinable())
                    {
                        _thread.detach();
                    }
                }
                throw;
            }
        }

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        ~Rig()
        {
            try
            {
                Dispose();
            }
            catch (...)
            {
                Server->Stop();
                try
                {
                    JoinWithin(5000);
                }
                catch (...)
                {
                    if (_thread.joinable())
                    {
                        _thread.detach();
                    }
                }
            }
        }

        Client& Add(std::uint32_t id, Runtime::Guid token = {})
        {
            Clients.push_back(std::make_unique<Client>(Server->BoundPort(), id, token));
            Client& client = *Clients.back();
            Wait([&client]() { return client.Slot >= 0 && client.State.has_value(); }, "client admitted");
            client.Identify();
            Stable();
            return client;
        }

        void ConfigureBots(std::int32_t count, std::int32_t level = ServerBots::DefaultLevel)
        {
            _expectedBotCount = count;
            Server->Bots(count, level);
            if (!Clients.empty())
            {
                Wait([this]() { return BotRosterMatches(); }, "bot slot roster converges");
                Stable();
            }
        }

        void Remove(const Client& client)
        {
            std::erase_if(Clients, [&client](const std::unique_ptr<Client>& c) { return c.get() == &client; });
        }

        void Stable()
        {
            Wait([this]()
            {
                if (Clients.empty())
                {
                    return false;
                }
                if (!BotRosterMatches())
                {
                    return false;
                }
                const auto revision = [](const Client& c)
                {
                    return c.State.has_value() ? std::optional<std::uint16_t>(c.State->Revision) : std::nullopt;
                };
                for (const std::unique_ptr<Client>& c : Clients)
                {
                    if (revision(*c) != revision(*Clients[0])
                        || !c->State.has_value() || c->Roster.SessionRevision != c->State->Revision
                        || c->Roster.Count != Clients.size() + static_cast<std::size_t>(_expectedBotCount))
                    {
                        return false;
                    }
                    for (std::int32_t i = 0; i < c->Roster.Count; i++)
                    {
                        const std::uint8_t slot = (*c->Roster.Slots)[static_cast<std::size_t>(i)];
                        const Client* single = nullptr;
                        for (const std::unique_ptr<Client>& p : Clients)
                        {
                            if (p->Slot == slot)
                            {
                                if (single != nullptr)
                                {
                                    throw System::InvalidOperationException("Sequence contains more than one matching element");
                                }
                                single = p.get();
                            }
                        }
                        if (single == nullptr)
                        {
                            const std::size_t firstBotSlot = ServerSlotIdentity::SlotCapacity
                                - static_cast<std::size_t>(_expectedBotCount);
                            if (static_cast<std::size_t>(slot) >= firstBotSlot
                                && (*c->Roster.Names)[static_cast<std::size_t>(i)].value_or("").starts_with("Bot "))
                            {
                                continue;
                            }
                            throw System::InvalidOperationException("Sequence contains no matching element");
                        }
                        if ((*c->Roster.Names)[static_cast<std::size_t>(i)] != "Test" + std::to_string(single->Id))
                        {
                            return false;
                        }
                    }
                }
                return true;
            }, "roster and state converge");
        }

        void Wait(const std::function<bool()>& condition, std::string_view label, std::int32_t ms = 4000)
        {
            const std::int64_t clock = Runtime::StopwatchGetTimestamp();
            do
            {
                for (const std::unique_ptr<Client>& client : Clients)
                {
                    client->Drain();
                }
                std::exception_ptr error;
                {
                    const std::scoped_lock lock(_threadState->Mutex);
                    error = _threadState->Error;
                }
                if (error != nullptr)
                {
                    std::rethrow_exception(error);
                }
                if (condition())
                {
                    Check(true, label);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (ElapsedMilliseconds(clock) < ms);
            throw System::InvalidOperationException("Timed out: " + std::string(label));
        }

        LobbyCommandResultPacket Expect(Client& client, const LobbyCommandPacket& command, LobbyResultCode expected)
        {
            client.Resend(command);
            Wait([&]() { return client.Results.contains(command.CommandId); }, "command answered");
            const LobbyCommandResultPacket first = client.Results.at(command.CommandId);
            client.Results.erase(command.CommandId);
            client.Resend(command);
            Wait([&]() { return client.Results.contains(command.CommandId); }, "duplicate answered");
            const LobbyCommandResultPacket result = client.Results.at(command.CommandId);
            Check(first.CurrentRevision == result.CurrentRevision && first.ResultCode == result.ResultCode,
                "duplicate is idempotent");
            Check(result.ResultCode == expected, ToString(command.Type) + ": expected " + ToString(expected)
                + ", got " + ToString(result.ResultCode) + ": " + result.Reason);
            Stable();
            return result;
        }

        void ReadyAll()
        {
            for (const std::unique_ptr<Client>& client : Clients)
            {
                Stable();
                static_cast<void>(Expect(*client, client->Command(LobbyCommandType::SetReady, true), LobbyResultCode::Ok));
            }
        }

        void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;
            for (const std::unique_ptr<Client>& client : Clients)
            {
                client->Dispose();
            }
            Server->Stop();
            JoinWithin(5000);
        }

    private:
        [[nodiscard]] bool BotRosterMatches() const
        {
            if (Clients.empty())
            {
                return false;
            }
            for (const std::unique_ptr<Client>& client : Clients)
            {
                if (!client->State.has_value()
                    || client->Roster.SessionRevision != client->State->Revision
                    || client->Roster.Count != Clients.size() + static_cast<std::size_t>(_expectedBotCount))
                {
                    return false;
                }
                std::array<bool, ServerSlotIdentity::SlotCapacity> seen{};
                const std::size_t firstBotSlot = seen.size()
                    - static_cast<std::size_t>(_expectedBotCount);
                for (std::int32_t i = 0; i < client->Roster.Count; ++i)
                {
                    const std::size_t at = static_cast<std::size_t>(i);
                    const std::size_t slot = (*client->Roster.Slots)[at];
                    if (slot >= seen.size() || seen[slot])
                    {
                        return false;
                    }
                    seen[slot] = true;
                    const bool isBot = (*client->Roster.Names)[at].value_or("").starts_with("Bot ");
                    if (isBot != (slot >= firstBotSlot))
                    {
                        return false;
                    }
                    if (!isBot && std::none_of(Clients.begin(), Clients.end(), [slot](const auto& peer)
                        {
                            return peer->Slot == static_cast<std::int32_t>(slot);
                        }))
                    {
                        return false;
                    }
                }
                for (std::size_t slot = firstBotSlot; slot < seen.size(); ++slot)
                {
                    if (!seen[slot])
                    {
                        return false;
                    }
                }
                for (const std::unique_ptr<Client>& peer : Clients)
                {
                    if (peer->Slot < 0 || static_cast<std::size_t>(peer->Slot) >= seen.size()
                        || !seen[static_cast<std::size_t>(peer->Slot)])
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        void JoinWithin(std::int32_t milliseconds)
        {
            if (!_thread.joinable())
            {
                return;
            }
            std::unique_lock lock(_threadState->Mutex);
            const bool finished = _threadState->Finished.wait_for(lock,
                std::chrono::milliseconds(milliseconds), [this]() { return _threadState->Done; });
            lock.unlock();
            if (finished)
            {
                _thread.join();
            }
            else
            {
                // Thread.IsBackground: a timed-out test thread must not hold
                // process exit, and its shared state keeps the server alive.
                _thread.detach();
            }
        }

        std::thread _thread{};
        std::shared_ptr<ThreadState> _threadState{};
        bool _disposed = false;
    };

    void NetLobbyTest::Check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            throw System::InvalidOperationException(message);
        }
        _checks++;
    }

    std::int32_t NetLobbyTest::Run()
    {
        const auto cleanup = []()
        {
            NetSession::Stop();
            static_cast<void>(NetLag::Configure("0"));
            static_cast<void>(NetLag::ConfigureLoss("0"));
            ServerBots::Configure(0, ServerBots::DefaultLevel,
                static_cast<std::int32_t>(ServerSlotIdentity::SlotCapacity));
        };
        std::int32_t result = 0;
        try
        {
            NetItemStateTest::Run();
            ProtocolChecks();
            DemoProtocolCheck();
            LayoutChecks();
            ClientStateChecks();
            Scenario();
            TeamScenario();
            CustomScenario();
            FourTeamScenario();
            ContinuousScenario();
            MapVoteScenario();
            ClientSessionScenario();
            Multiplayer::TeamGameplayTest::Run([](bool condition, std::string_view message) { Check(condition, message); });
            BotSlotScenario();
            Runtime::ConsoleWriteLine("[netlobbytest] PASS: " + std::to_string(_checks)
                + " assertions; protocol, UDP lifecycle, two rounds, owner migration, teams, rebind, bot slot churn and continuous rotation.");
            result = 0;
        }
        catch (const std::exception& ex)
        {
            Runtime::ConsoleErrorWriteLine("[netlobbytest] FAIL after " + std::to_string(_checks) + " assertions: " + ex.what());
            result = 1;
        }
        cleanup();
        return result;
    }

    void NetLobbyTest::ProtocolChecks()
    {
        Check(NetConfig::ProtocolVersion >= 19 && static_cast<std::uint8_t>(PacketType::SessionState) == 36
            && static_cast<std::uint8_t>(PacketType::MapOffer) == 32 && static_cast<std::uint8_t>(PacketType::MapDone) == 35,
            "combined protocol and non-overlapping map/lobby IDs");
        SessionStatePacket state{};
        state.Phase = SessionPhase::Starting;
        state.Policy = ServerSessionPolicy::Lobby;
        state.OwnerSlot = 7;
        state.MaxPlayers = 8;
        state.Revision = 65535;
        state.MatchId = 19;
        state.RuleFlags = SessionRules::RequireReady | SessionRules::AllowJoinInProgress | SessionRules::LockTeams;
        state.WorldProfile = MatchWorldProfile::Resolve(8);
        state.ExpectedParticipants = 255;
        state.LoadedParticipants = 3;
        state.Match.RoomKey = std::string(40, 'X');
        state.Match.Mode = GameMode::BattleTeams;
        state.Match.Format = MatchFormat::FourVsFour;
        state.Match.TimeLimitSeconds = 600;
        state.Match.PointGoal = 20;
        state.Match.FriendlyFire = true;
        state.Match.AffinityWeapons = true;
        state.Match.ShadowFreeze = true;
        state.Match.HideOpponentHealth = true;
        state.Match.Instagib = true;
        std::vector<std::uint8_t> data(SessionStatePacket::Size);
        state.Write(data);
        SessionStatePacket read{};
        Check(SessionStatePacket::TryRead(data, read) && read.Match == state.Match
            && read.Revision == state.Revision && read.LoadedParticipants == 3, "session round trip/max room/revision");
        SessionStatePacket discard{};
        for (std::size_t length = 0; length < data.size(); length++)
        {
            Check(!SessionStatePacket::TryRead(std::span(data).first(length), discard), "truncated session");
        }
        for (const std::size_t offset : {0U, 1U, 8U, 9U})
        {
            const std::uint8_t saved = data[offset];
            data[offset] = 254;
            Check(!SessionStatePacket::TryRead(data, discard), "invalid enum");
            data[offset] = saved;
        }
        const std::uint8_t savedExpected = data[16];
        const std::uint8_t savedLoaded = data[17];
        data[16] = 1;
        data[17] = 2;
        Check(!SessionStatePacket::TryRead(data, discard), "loaded participant must belong to frozen barrier");
        data[16] = savedExpected;
        data[17] = savedLoaded;
        const std::uint8_t savedMax = data[7];
        const std::uint8_t savedOwner = data[6];
        data[7] = 2;
        data[6] = 0;
        Check(!SessionStatePacket::TryRead(data, discard), "participant mask bounded by server slots");
        data[7] = savedMax;
        data[6] = savedOwner;
        Check(SessionStatePacket::IsNewer(0, 65535) && !SessionStatePacket::IsNewer(65535, 0), "revision wrap ordering");
        // Enum.GetValues<LobbyCommandType>().
        for (const LobbyCommandType type : {LobbyCommandType::SetReady, LobbyCommandType::SetTeam,
            LobbyCommandType::UpdateMatch, LobbyCommandType::StartMatch, LobbyCommandType::KickPlayer,
            LobbyCommandType::TransferOwner})
        {
            LobbyCommandPacket command{};
            command.CommandId = 42;
            command.ExpectedRevision = 17;
            command.Type = type;
            command.TargetSlot = 7;
            command.TeamIndex = -1;
            command.Ready = true;
            command.Configuration = state;
            std::vector<std::uint8_t> bytes(LobbyCommandPacket::Size);
            command.Write(bytes);
            LobbyCommandPacket decoded{};
            Check(LobbyCommandPacket::TryRead(bytes, decoded) && decoded.Type == type
                && decoded.CommandId == 42 && decoded.TeamIndex == -1 && decoded.Ready, "command round trip");
            for (std::size_t length = 0; length < bytes.size(); length++)
            {
                Check(!LobbyCommandPacket::TryRead(std::span(bytes).first(length), decoded), "truncated command");
            }
        }
        // Enum.GetValues<LobbyResultCode>().
        for (std::uint8_t value = static_cast<std::uint8_t>(LobbyResultCode::Ok);
            value <= static_cast<std::uint8_t>(LobbyResultCode::MapUnavailable); value++)
        {
            const auto code = static_cast<LobbyResultCode>(value);
            LobbyCommandResultPacket result{};
            result.CommandId = 42;
            result.ResultCode = code;
            result.CurrentRevision = 65535;
            result.Reason = "A reason";
            std::vector<std::uint8_t> bytes(LobbyCommandResultPacket::Size);
            result.Write(bytes);
            LobbyCommandResultPacket decoded{};
            Check(LobbyCommandResultPacket::TryRead(bytes, decoded) && decoded.ResultCode == code
                && decoded.Reason == result.Reason && decoded.CurrentRevision == 65535, "result round trip");
            for (std::size_t length = 0; length < bytes.size(); length++)
            {
                Check(!LobbyCommandResultPacket::TryRead(std::span(bytes).first(length), decoded), "truncated result");
            }
        }
        std::vector<std::uint8_t> loadedBytes(MatchLoadedPacket::Size);
        MatchLoadedPacket{65535}.Write(loadedBytes);
        MatchLoadedPacket loaded{};
        Check(MatchLoadedPacket::TryRead(loadedBytes, loaded) && loaded.MatchId == 65535, "loaded round trip");
        Check(!MatchLoadedPacket::TryRead(std::span(loadedBytes).first(1), loaded), "truncated loaded");
        std::vector<std::uint8_t> failedBytes(MatchLoadFailedPacket::Size);
        MatchLoadFailedPacket{17, "missing map"}.Write(failedBytes);
        MatchLoadFailedPacket failed{};
        Check(MatchLoadFailedPacket::TryRead(failedBytes, failed) && failed.MatchId == 17 && failed.Reason == "missing map",
            "failed round trip");
        for (std::size_t length = 0; length < failedBytes.size(); length++)
        {
            Check(!MatchLoadFailedPacket::TryRead(std::span(failedBytes).first(length), failed), "truncated failure");
        }
        RosterPacket roster = RosterPacket::Create();
        roster.Count = 8;
        roster.Revision = 123;
        for (std::int32_t i = 0; i < 8; i++)
        {
            const auto index = static_cast<std::size_t>(i);
            (*roster.Slots)[index] = static_cast<std::uint8_t>(i);
            (*roster.Teams)[index] = static_cast<std::int8_t>(i % 5 - 1);
            (*roster.LobbyReady)[index] = i % 2 == 0;
            (*roster.Names)[index] = "Player" + std::to_string(i);
        }
        std::vector<std::uint8_t> rosterBytes(RosterPacket::Size);
        roster.Write(rosterBytes);
        RosterPacket rr{};
        Check(RosterPacket::TryRead(rosterBytes, rr) && *rr.Teams == *roster.Teams
            && *rr.LobbyReady == *roster.LobbyReady && rr.Revision == 123, "roster team/ready/revision round trip");
        for (std::size_t length = 0; length < rosterBytes.size(); length++)
        {
            Check(!RosterPacket::TryRead(std::span(rosterBytes).first(length), rr), "truncated roster");
        }
        HostRequestPacket host{};
        host.Protocol = NetConfig::ProtocolVersion;
        host.MaxPlayers = 8;
        host.RoomKey = "room";
        host.ServerName = "test";
        host.Policy = ServerSessionPolicy::Lobby;
        host.RequireReady = true;
        host.AllowJoinInProgress = true;
        host.Format = MatchFormat::FourVsFour;
        std::vector<std::uint8_t> hostBytes(static_cast<std::size_t>(host.Length()));
        host.Write(hostBytes);
        const HostRequestPacket hr = HostRequestPacket::Read(hostBytes);
        Check(hr.Policy == host.Policy && hr.Format == host.Format && hr.RequireReady && hr.AllowJoinInProgress,
            "host options appended without rotation");
        HostReplyPacket reply{};
        reply.Started = true;
        reply.Port = 123;
        reply.OwnerToken = Runtime::Guid::NewGuid();
        std::vector<std::uint8_t> replyBytes(HostReplyPacket::Size);
        reply.Write(replyBytes);
        Check(HostReplyPacket::Read(replyBytes).OwnerToken == reply.OwnerToken, "owner token round trip");
    }

    void NetLobbyTest::DemoProtocolCheck()
    {
        const std::string path = Runtime::PathCombine(Runtime::PathGetTempPath(),
            "team-protocol-" + Runtime::Guid::NewGuid().ToString("N") + std::string(DemoFile::Extension));
        const auto cleanup = [&path]()
        {
            DemoPlayback::Stop();
            Runtime::FileDelete(path);
        };
        try
        {
            {
                DemoWriter writer(path);
                writer.Dispose();
            }
            std::vector<std::uint8_t> bytes = Runtime::FileReadAllBytes(path);
            bytes[5] = static_cast<std::uint8_t>(NetConfig::ProtocolVersion - 1);
            Runtime::FileWriteAllBytes(path, bytes);
            Check(!DemoPlayback::Join(path), "incompatible demo is rejected");
            Check(!DemoPlayback::IsActive(), "incompatible demo does not activate playback");
            Check(DemoPlayback::LastError() == std::optional<std::string>(
                "This demo uses an incompatible network protocol."),
                "incompatible demo returns its stable user-facing error");
            Check(Runtime::FileOpenExclusiveLength(path) >= DemoFile::HeaderSize, "rejected demo releases its file handle");
        }
        catch (...)
        {
            cleanup();
            throw;
        }
        cleanup();
    }

    void NetLobbyTest::LayoutChecks()
    {
        MatchDefinition match{};
        match.RoomKey = Rooms()[0];
        match.Mode = GameMode::BattleTeams;
        const std::pair<MatchFormat, TeamLayout> presets[] = {
            {MatchFormat::OneVsOne, TeamLayout(2, 1, 1)}, {MatchFormat::TwoVsTwo, TeamLayout(2, 2, 2)},
            {MatchFormat::ThreeVsThree, TeamLayout(2, 3, 3)}, {MatchFormat::FourVsFour, TeamLayout(2, 4, 4)},
            {MatchFormat::TwoVsTwoVsTwoVsTwo, TeamLayout(4, 2, 2, 2, 2)}};
        std::string reason;
        for (const auto& [format, layout] : presets)
        {
            MatchDefinition preset = match;
            preset.Format = format;
            Check(LobbyRules::ResolveTeamLayout(preset) == layout, "resolve " + ToString(format));
            Check(LobbyRules::ValidateDefinition(preset, reason) == LobbyResultCode::Ok, "validate " + ToString(format));
        }
        for (const TeamLayout& layout : {TeamLayout(2, 1, 2), TeamLayout(2, 4, 2), TeamLayout(2, 2, 3),
            TeamLayout(3, 1, 1, 1), TeamLayout(3, 1, 2, 2), TeamLayout(3, 2, 2, 2), TeamLayout(4, 1, 1, 2, 4)})
        {
            MatchDefinition custom = match;
            custom.Format = MatchFormat::Custom;
            custom.CustomTeams = layout;
            Check(LobbyRules::ValidateDefinition(custom, reason) == LobbyResultCode::Ok, "valid custom " + layout.ToString());
            SessionStatePacket state{};
            state.MaxPlayers = 8;
            state.Match = custom;
            state.WorldProfile = MatchWorldProfile::Resolve(layout.TotalPlayers());
            std::vector<std::uint8_t> bytes(SessionStatePacket::Size);
            state.Write(bytes);
            SessionStatePacket read{};
            Check(SessionStatePacket::TryRead(bytes, read) && read.Match == custom && read.WorldProfile == state.WorldProfile,
                "custom/world wire roundtrip");
            std::array<std::int32_t, 4> counts{};
            for (std::int32_t player = 0; player < layout.TotalPlayers(); player++)
            {
                const std::int32_t team = TeamRules::ChooseTeam(layout, counts);
                Check(team >= 0 && counts[static_cast<std::size_t>(team)] < layout.Capacity(team),
                    "normalized assignment stays within capacity");
                for (std::int32_t candidate = 0; candidate < layout.TeamCount; candidate++)
                {
                    if (counts[static_cast<std::size_t>(candidate)] < layout.Capacity(candidate))
                    {
                        Check(counts[static_cast<std::size_t>(team)] * layout.Capacity(candidate)
                            <= counts[static_cast<std::size_t>(candidate)] * layout.Capacity(team), "lowest normalized occupancy");
                    }
                }
                counts[static_cast<std::size_t>(team)]++;
            }
            Check(TeamRules::ChooseTeam(layout, counts) == -1, "full layout refuses admission");
            for (std::int32_t team = 0; team < layout.TeamCount; team++)
            {
                Check(counts[static_cast<std::size_t>(team)] == layout.Capacity(team), "fills exact asymmetric layout");
            }
        }
        for (const TeamLayout& invalid : {TeamLayout(2, 0, 2), TeamLayout(5, 1, 1, 1, 1), TeamLayout(3, 4, 4, 1),
            TeamLayout(4, 2, 2), TeamLayout(2, 2, 2, 1)})
        {
            MatchDefinition custom = match;
            custom.Format = MatchFormat::Custom;
            custom.CustomTeams = invalid;
            Check(LobbyRules::ValidateDefinition(custom, reason) == LobbyResultCode::InvalidConfiguration, "reject invalid layout");
        }
        MatchDefinition capture = match;
        capture.Mode = GameMode::Capture;
        capture.Format = MatchFormat::TwoVsTwoVsTwoVsTwo;
        Check(LobbyRules::ValidateDefinition(capture, reason) == LobbyResultCode::InvalidConfiguration, "capture rejects four teams");
        MatchDefinition primeHunter = match;
        primeHunter.Mode = GameMode::PrimeHunter;
        primeHunter.Format = MatchFormat::OneVsOne;
        Check(LobbyRules::ValidateDefinition(primeHunter, reason) == LobbyResultCode::InvalidConfiguration, "prime hunter stays FFA");
        RosterPacket single = RosterPacket::Create();
        single.Count = 1;
        MatchDefinition battle = match;
        battle.Mode = GameMode::Battle;
        battle.Format = MatchFormat::FreeForAll;
        Check(LobbyRules::Validate(battle, single, false, reason) == LobbyResultCode::NotEnoughPlayers, "explicit FFA minimum two");
        for (std::int32_t players = 2; players <= 8; players++)
        {
            const MatchWorldProfile world = MatchWorldProfile::Resolve(players);
            Check(world.IsValid() && world.EntityLayerPlayers == std::min(players, 4), "native entity layer bounded 2/3/4");
            Check(world.Resources == (players == 2 ? ResourceSpawnProfile::Low
                : players <= 4 ? ResourceSpawnProfile::Standard : ResourceSpawnProfile::High), "resource tier");
        }
    }

    void NetLobbyTest::ClientStateChecks()
    {
        NetSession::StartPlayback();
        Check(!NetSession::SessionTimedOut(), "playback has no network timeout");
        SessionStatePacket state{};
        state.AuthorityEpoch = 1;
        state.Policy = ServerSessionPolicy::Lobby;
        state.Phase = SessionPhase::Lobby;
        state.Revision = 65535;
        state.MatchId = 4;
        state.MaxPlayers = 8;
        state.OwnerSlot = 255;
        state.Match.RoomKey = Rooms()[0];
        state.Match.Mode = GameMode::Battle;
        NetSession::ApplySessionState(state);
        state.Revision = 0;
        state.Phase = SessionPhase::Starting;
        state.MatchId++;
        NetSession::ApplySessionState(state);
        Check(NetSession::IsStarting() && NetSession::ServerSession().has_value() && NetSession::ServerSession()->MatchId == 5,
            "client accepts session revision wrap");
        state.Revision = 65535;
        state.Phase = SessionPhase::Lobby;
        state.MatchId--;
        NetSession::ApplySessionState(state);
        Check(NetSession::IsStarting() && NetSession::ServerSession().has_value() && NetSession::ServerSession()->MatchId == 5,
            "delayed state cannot roll back a new match");
        NetMatchSync::Apply();
        Check(GameState::MatchTime() == -1 && GameState::PointGoal() == 0,
            "unlimited match uses the finite hidden-clock sentinel and no point goal");
        NetSession::Stop();
    }

    // Metadata.RoomMetadata in insertion order, which is RoomList's.
    std::vector<std::string> NetLobbyTest::Rooms()
    {
        std::vector<std::string> rooms;
        for (const auto& room : Metadata::RoomList)
        {
            if (Runtime::RequireReference(room).Multiplayer)
            {
                rooms.push_back(room->Name);
                if (rooms.size() == 2)
                {
                    break;
                }
            }
        }
        return rooms;
    }

    void NetLobbyTest::Scenario()
    {
        const Runtime::Guid token = Runtime::Guid::NewGuid();
        Rig rig(ServerSessionPolicy::Lobby, token);
        Client& b = rig.Add(2);
        Check(b.State.value().OwnerSlot == 255, "first arrival cannot steal hosted ownership");
        Client& a = rig.Add(1, token);
        Check(a.State.value().OwnerSlot == a.Slot, "creator claims owner token");
        const std::shared_ptr<NetTransport> originalA = a.Transport;
        const std::shared_ptr<NetTransport> originalB = b.Transport;
        const std::int32_t slotA = a.Slot;
        const std::int32_t slotB = b.Slot;
        static_cast<void>(rig.Expect(b, b.Command(LobbyCommandType::StartMatch), LobbyResultCode::NotOwner));
        static_cast<void>(rig.Expect(a, a.Command(LobbyCommandType::StartMatch), LobbyResultCode::PlayersNotReady));
        static_cast<void>(rig.Expect(a, a.Command(LobbyCommandType::SetReady, true, std::nullopt, 255, -1, 0),
            LobbyResultCode::StaleRevision));
        rig.ReadyAll();
        SessionStatePacket config = a.State.value();
        config.Match.RoomKey = Rooms()[1];
        config.Match.TimeLimitSeconds = 2;
        config.Match.HideOpponentHealth = true;
        static_cast<void>(rig.Expect(a, a.Command(LobbyCommandType::UpdateMatch, false, config), LobbyResultCode::Ok));
        Check(NoneReady(a.Roster), "configuration clears ready");
        Check(a.State->Match.HideOpponentHealth && b.State.value().Match.HideOpponentHealth,
            "owner hidden-health rule synchronizes to both UDP clients");
        SessionStatePacket forbidden = b.State.value();
        forbidden.Match.HideOpponentHealth = false;
        forbidden.RuleFlags &= ~SessionRules::HideOpponentHealth;
        static_cast<void>(rig.Expect(b, b.Command(LobbyCommandType::UpdateMatch, false, forbidden), LobbyResultCode::NotOwner));
        Check(a.State->Match.HideOpponentHealth, "non-owner cannot expose hidden health");
        const ServerStatus lobbyStatus = NetStatus::Query("127.0.0.1", rig.Server->BoundPort(), false);
        Check(lobbyStatus.Online && lobbyStatus.Phase == SessionPhase::Lobby && lobbyStatus.TimeRemaining == 2,
            "browser status clock stays at the full time limit in the lobby");
        rig.ReadyAll();
        const LobbyCommandPacket start = a.Command(LobbyCommandType::StartMatch);
        static_cast<void>(rig.Expect(a, start, LobbyResultCode::Ok));
        Check(a.State->Phase == SessionPhase::Starting, "start enters barrier");
        a.Loaded(static_cast<std::uint16_t>(a.State->MatchId - 1));
        a.Loaded();
        rig.Wait([&a]() { return a.State->LoadedParticipants == (1 << a.Slot); }, "one participant loaded");
        Check(a.State->Phase == SessionPhase::Starting, "one loaded cannot release barrier");
        const ServerStatus loadingStatus = NetStatus::Query("127.0.0.1", rig.Server->BoundPort(), false);
        Check(loadingStatus.Phase == SessionPhase::Starting && loadingStatus.TimeRemaining == 2,
            "browser status clock stays frozen through the load barrier");
        Client& late = rig.Add(3);
        Check((late.State.value().ExpectedParticipants & (1 << late.Slot)) == 0, "late join excluded from barrier");
        b.Loaded();
        rig.Wait([&a]() { return a.State->Phase == SessionPhase::InMatch; }, "barrier released");
        b.EndMatch();
        rig.Stable();
        Check(!a.Authority && !b.Authority, "lobby owner and guests never receive client authority");
        rig.Wait([&a]() { return a.State->Phase == SessionPhase::PostMatch; }, "results entered");
        for (const std::unique_ptr<Client>& client : rig.Clients)
        {
            client->ReadyResults();
        }
        rig.Wait([&a]() { return a.State->Phase == SessionPhase::Lobby; }, "results return to lobby", 18000);
        rig.Stable();
        Check(originalA == a.Transport && originalB == b.Transport && a.Slot == slotA && b.Slot == slotB,
            "same UDP transports and slots across rounds");
        Check(NoneReady(a.Roster), "return clears lobby ready");
        const std::uint16_t firstMatch = a.State->MatchId;
        rig.ReadyAll();
        static_cast<void>(rig.Expect(a, a.Command(LobbyCommandType::StartMatch), LobbyResultCode::Ok));
        for (const std::unique_ptr<Client>& client : rig.Clients)
        {
            client->Loaded();
        }
        rig.Wait([&a]() { return a.State->Phase == SessionPhase::InMatch; }, "second round starts");
        Check(a.State->MatchId != firstMatch, "new match id on same map");
        a.Dispose();
        rig.Remove(a);
        rig.Wait([&b]() { return b.State.value().OwnerSlot == b.Slot; }, "oldest peer becomes owner");
        b.Rebind();
        rig.Stable();
        Check(b.Slot == slotB && b.State->OwnerSlot == slotB, "owner rebind keeps identity and slot");
    }

    void NetLobbyTest::TeamScenario()
    {
        Rig rig{};
        Client& owner = rig.Add(10);
        Client& other = rig.Add(11);
        SessionStatePacket config = owner.State.value();
        config.Match.Mode = GameMode::BattleTeams;
        config.Match.Format = MatchFormat::TwoVsTwo;
        config.RuleFlags &= ~SessionRules::RequireReady;
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::UpdateMatch, false, config), LobbyResultCode::Ok));
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::StartMatch), LobbyResultCode::NotEnoughPlayers));
        static_cast<void>(rig.Add(12));
        static_cast<void>(rig.Add(13));
        Check(TeamsStartWith(owner.Roster, {0, 1, 0, 1}), "deterministic 2v2 assignment");
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::SetTeam, false, std::nullopt,
            static_cast<std::uint8_t>(other.Slot), 0), LobbyResultCode::TeamFull));
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::StartMatch), LobbyResultCode::Ok));
        Client& leaving = *rig.Clients.back();
        leaving.Dispose();
        rig.Remove(leaving);
        rig.Wait([&owner]() { return owner.State->Phase == SessionPhase::Lobby; },
            "disconnect invalidates exact team format during load");
        config = owner.State.value();
        config.Match.Format = MatchFormat::FourVsFour;
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::UpdateMatch, false, config), LobbyResultCode::Ok));
        for (std::uint32_t id = 14; rig.Clients.size() < 8; id++)
        {
            static_cast<void>(rig.Add(id));
        }
        Check(CountTeam(owner.Roster, 8, 0) == 4 && CountTeam(owner.Roster, 8, 1) == 4, "4v4 assignment");
        config = owner.State.value();
        config.Match.Format = MatchFormat::TwoVsTwoVsTwoVsTwo;
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::UpdateMatch, false, config), LobbyResultCode::Ok));
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::StartMatch), LobbyResultCode::Ok));
        rig.Wait([&owner]() { return owner.State->Phase == SessionPhase::InMatch; }, "load timeout releases barrier", 18000);
    }

    void NetLobbyTest::CustomScenario()
    {
        Rig rig{};
        Client& owner = rig.Add(50);
        Client& other = rig.Add(51);
        SessionStatePacket config = owner.State.value();
        config.Match.Mode = GameMode::BattleTeams;
        config.Match.Format = MatchFormat::Custom;
        config.Match.CustomTeams = TeamLayout(2, 4, 2);
        config.RuleFlags &= ~SessionRules::RequireReady;
        const auto setTeam = [](Client& client, std::int32_t target, std::int8_t team)
        {
            return client.Command(LobbyCommandType::SetTeam, false, std::nullopt, static_cast<std::uint8_t>(target), team);
        };
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::UpdateMatch, false, config), LobbyResultCode::Ok));
        static_cast<void>(rig.Expect(other, setTeam(other, owner.Slot, 1), LobbyResultCode::NotOwner));
        static_cast<void>(rig.Expect(other, setTeam(other, other.Slot, 0), LobbyResultCode::Ok));
        static_cast<void>(rig.Expect(other, other.Command(LobbyCommandType::SetReady, true), LobbyResultCode::Ok));
        static_cast<void>(rig.Expect(owner, setTeam(owner, other.Slot, -1), LobbyResultCode::Ok));
        Check(!(*owner.Roster.LobbyReady).at(static_cast<std::size_t>(other.Slot)), "team move clears target ready");
        config = owner.State.value();
        config.RuleFlags |= SessionRules::LockTeams;
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::UpdateMatch, false, config), LobbyResultCode::Ok));
        static_cast<void>(rig.Expect(other, setTeam(other, other.Slot, 0), LobbyResultCode::NotOwner));
        static_cast<void>(rig.Expect(owner, setTeam(owner, other.Slot, -1), LobbyResultCode::Ok));
        static_cast<void>(rig.Expect(owner, setTeam(owner, other.Slot, 3), LobbyResultCode::InvalidTeam));
        for (std::uint32_t id = 52; rig.Clients.size() < 6; id++)
        {
            static_cast<void>(rig.Add(id));
        }
        Check(CountTeam(owner.Roster, 6, 0) == 4 && CountTeam(owner.Roster, 6, 1) == 2, "custom 4v2 fills asymmetrically");
        config = owner.State.value();
        config.Match.CustomTeams = TeamLayout(2, 2, 2);
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::UpdateMatch, false, config),
            LobbyResultCode::InvalidConfiguration));
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::StartMatch), LobbyResultCode::Ok));
        Check(owner.State->WorldProfile == MatchWorldProfile::Resolve(6), "world profile frozen with exact layout");
        Client* leaving = rig.Clients.back().get();
        leaving->Dispose();
        rig.Remove(*leaving);
        rig.Wait([&owner]() { return owner.State->Phase == SessionPhase::Lobby; }, "4v2 disconnect cancels load barrier");
        Check(NoneReady(owner.Roster), "cancelled barrier clears readiness");
        static_cast<void>(rig.Add(60));
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::StartMatch), LobbyResultCode::Ok));
        for (const std::unique_ptr<Client>& client : rig.Clients)
        {
            client->Loaded();
        }
        rig.Wait([&owner]() { return owner.State->Phase == SessionPhase::InMatch; }, "4v2 starts after load");
        leaving = nullptr;
        for (const std::unique_ptr<Client>& c : rig.Clients)
        {
            if (c.get() != &owner && (*owner.Roster.Teams).at(static_cast<std::size_t>(c->Slot)) == 0)
            {
                leaving = c.get();
                break;
            }
        }
        if (leaving == nullptr)
        {
            throw System::InvalidOperationException("Sequence contains no matching element");
        }
        leaving->Dispose();
        rig.Remove(*leaving);
        rig.Stable();
        Client& late = rig.Add(61);
        const std::int32_t lateIndex = IndexOf(*owner.Roster.Slots, static_cast<std::uint8_t>(late.Slot), owner.Roster.Count);
        Check((*owner.Roster.Teams).at(static_cast<std::size_t>(lateIndex)) == 0, "JIP fills only A vacancy");
        Check(late.State.value().WorldProfile == MatchWorldProfile::Resolve(6), "JIP retains frozen world");
        Client overflow(rig.Server->BoundPort(), 62);
        rig.Wait([&overflow]()
        {
            overflow.Drain();
            return overflow.Refused;
        }, "full custom layout rejects late join below physical player cap");
        Check(overflow.Slot < 0, "overflow never activated");
    }

    void NetLobbyTest::FourTeamScenario()
    {
        Rig rig{};
        Client& owner = rig.Add(70);
        SessionStatePacket config = owner.State.value();
        config.Match.Mode = GameMode::BattleTeams;
        config.Match.Format = MatchFormat::TwoVsTwoVsTwoVsTwo;
        config.RuleFlags &= ~SessionRules::RequireReady;
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::UpdateMatch, false, config), LobbyResultCode::Ok));
        for (std::int32_t slot = 1; slot < 8; slot++)
        {
            Client& added = rig.Add(static_cast<std::uint32_t>(70 + slot));
            if (slot >= 4 && slot <= 6)
            {
                static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::SetTeam, false, std::nullopt,
                    static_cast<std::uint8_t>(added.Slot), static_cast<std::int8_t>(7 - slot)), LobbyResultCode::Ok));
            }
        }
        Check(TeamsStartWith(owner.Roster, {0, 1, 2, 3, 3, 2, 1, 0}), "non-parity four-team roster");
        for (const std::unique_ptr<Client>& client : rig.Clients)
        {
            client->Chats.clear();
        }
        std::vector<std::uint8_t> chat(ChatPacket::Size);
        ChatPacket packet{};
        packet.Kind = ChatPacket::KindTeam;
        packet.Text = "A only";
        packet.Name = "untrusted";
        packet.Write(chat);
        owner.Send(PacketType::Chat, chat);
        const auto saidAOnly = [](const Client& client)
        {
            return std::any_of(client.Chats.begin(), client.Chats.end(),
                [](const ChatPacket& c) { return c.Text == "A only"; });
        };
        rig.Wait([&]() { return saidAOnly(*rig.Clients[7]); }, "team chat reaches non-parity ally");
        bool excluded = true;
        for (std::size_t i = 1; i < 7; i++)
        {
            excluded = excluded && !saidAOnly(*rig.Clients[i]);
        }
        Check(excluded, "team chat excluded opposing teams");
        Client& rebound = *rig.Clients[5];
        const std::uint16_t beforeRebind = owner.State.value().Revision;
        rebound.Rebind();
        rig.Wait([&]() { return owner.State.value().Revision != beforeRebind; }, "rebind advances roster revision");
        rig.Stable();
        Check((*owner.Roster.Teams)[5] == 2, "reconnect preserves explicit team");
        static_cast<void>(rig.Expect(owner, owner.Command(LobbyCommandType::StartMatch), LobbyResultCode::Ok));
        for (const std::unique_ptr<Client>& client : rig.Clients)
        {
            client->Loaded();
        }
        rig.Wait([&owner]() { return owner.State->Phase == SessionPhase::InMatch; }, "four-team barrier starts");
    }

    void NetLobbyTest::ContinuousScenario()
    {
        Rig rig(ServerSessionPolicy::Continuous);
        Client& client = rig.Add(20);
        Check(client.State.value().Phase == SessionPhase::InMatch, "continuous starts in match");
        const std::uint16_t match = client.State->MatchId;
        client.EndMatch();
        Check(!client.Authority, "continuous creator stays a client");
        rig.Wait([&client]() { return client.State->Phase == SessionPhase::PostMatch; }, "continuous results");
        client.ReadyResults();
        rig.Wait([&]() { return client.State->Phase == SessionPhase::InMatch && client.State->MatchId != match; },
            "continuous rotates automatically", 18000);
    }

    void NetLobbyTest::MapVoteScenario()
    {
        Rig rig(ServerSessionPolicy::Continuous);
        Client& client = rig.Add(21);
        rig.Wait([&client]() { return client.Match.RoomKey.has_value(); }, "initial map-vote match state");
        const std::vector<std::string> rooms = Rooms();
        Check(rooms.size() == 2 && client.Match.RoomKey.value() == rooms[0],
            "single-map rotation starts on the expected map");
        const std::uint16_t previousMatchId = client.Match.MatchId;
        client.ProposeMap(rooms[1]);
        rig.Wait([&client, &rooms, previousMatchId]()
        {
            return client.Match.MatchId != previousMatchId && client.Match.RoomKey == rooms[1];
        }, "accepted map vote changes the continuous match");
    }

    void NetLobbyTest::ClientSessionScenario()
    {
        static_cast<void>(NetLag::Configure("80:20"));
        Rig rig(ServerSessionPolicy::Lobby, {}, 2);
        Check(NetLaunch::Connect("127.0.0.1", rig.Server->BoundPort(), "RealClient", Hunter::Samus),
            "NetSession connects to an idle lobby");
        const std::int32_t port = NetSession::ConnectionPort();
        const std::int32_t slot = NetSession::LocalSlot();
        const std::uint32_t clientId = NetSession::ClientId;
        const auto pumpUntil = [&rig](const std::function<bool()>& condition, std::string_view message,
            std::int32_t timeout = 5000)
        {
            rig.Wait([&condition]()
            {
                NetSession::Pump();
                return condition();
            }, message, timeout);
        };
        pumpUntil([slot]()
        {
            return NetSession::LocalIsLobbyOwner()
                && GameState::Nicknames()[static_cast<std::size_t>(slot)] == "RealClient"
                && NetSession::LobbyRoster().SessionRevision == NetSession::SessionRevision();
        }, "real client owns a consistent lobby");
        Check(NetSession::IsInLobby() && !NetSession::ShouldLoadMatch(), "connection does not require a running match");
        static_cast<void>(NetLag::ConfigureLoss("100"));
        Check(NetSession::SendLobbyCommand(LobbyCommandType::SetReady, 255, -1, true), "enqueue ready");
        const std::int64_t loss = Runtime::StopwatchGetTimestamp();
        while (ElapsedMilliseconds(loss) < 400)
        {
            NetSession::Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(NetSession::LobbyCommandPending(), "lost command remains pending");
        static_cast<void>(NetLag::ConfigureLoss("0"));
        const auto readyReceived = [slot]()
        {
            return !NetSession::LobbyCommandPending() && SessionSlots().LobbyReady(slot);
        };
        const auto consistent = []() { return NetSession::LobbyRoster().SessionRevision == NetSession::SessionRevision(); };
        pumpUntil(readyReceived, "retransmission recovers lost ready");
        pumpUntil(consistent, "ready state converged");
        Check(NetSession::SendLobbyCommand(LobbyCommandType::StartMatch), "real client starts");
        pumpUntil([]() { return NetSession::IsStarting() && !NetSession::LobbyCommandPending(); }, "real client load barrier");
        Check(NetSession::FreezeGameplay(), "gameplay frozen before loaded");
        Check(NetSession::IsStarting() && NetSession::ConnectionPort() == port, "lobby connection survives the load barrier");
        NetSession::MarkMatchLoaded();
        pumpUntil([]() { return NetSession::IsPlaying(); }, "real load ack starts match");
        Check(!NetSession::FreezeGameplay(), "gameplay released after barrier");
        NetSession::SendMatchEnd();
        pumpUntil([]() { return NetSession::IsPostMatch(); }, "real client results");
        std::uint32_t frame = 100;
        pumpUntil([&frame]()
        {
            IntentPacket intent{};
            intent.Frame = frame++;
            intent.Buttons = IntentButtons::ReadyState;
            NetSession::SendIntent(intent);
            return NetSession::IsInLobby();
        }, "real client returns to lobby", 18000);
        NetSession::ResetMatchState();
        Check(NetSession::Active() && NetSession::ConnectionPort() == port && NetSession::LocalSlot() == slot
            && NetSession::ClientId == clientId && NetSession::LocalIsLobbyOwner(),
            "real client socket/slot/id/owner survive match teardown");
        pumpUntil(consistent, "next lobby consistent");
        Check(NetSession::SendLobbyCommand(LobbyCommandType::SetReady, 255, -1, true), "ready for second real-client match");
        pumpUntil(readyReceived, "second ready received");
        Check(NetSession::SendLobbyCommand(LobbyCommandType::StartMatch), "second real-client start");
        pumpUntil([]() { return NetSession::IsStarting(); }, "second real-client load barrier");
        NetSession::MarkMatchLoaded();
        pumpUntil([]() { return NetSession::IsPlaying(); }, "second real-client round");
        Check(NetSession::ConnectionPort() == port && NetSession::LocalSlot() == slot, "same client UDP session in second match");
        NetSession::Stop();
        static_cast<void>(NetLag::Configure("0"));
    }

    void NetLobbyTest::BotSlotScenario()
    {
        Rig rig{};
        rig.ConfigureBots(4);
        Client& a = rig.Add(80);
        Client& b = rig.Add(81);
        Client& c = rig.Add(82);
        Client& d = rig.Add(83);
        Check(a.Slot == 0 && b.Slot == 1 && c.Slot == 2 && d.Slot == 3,
            "humans occupy the open slots beside four reserved bot slots");

        const std::int32_t oldBotIndex = IndexOf(*a.Roster.Slots, 4, a.Roster.Count);
        Check(oldBotIndex >= 0, "bot initially owns slot 4");
        const std::uint16_t oldBotGeneration = a.Roster.Generations->at(static_cast<std::size_t>(oldBotIndex));
        rig.ConfigureBots(2);
        Client& e = rig.Add(84);
        Client& f = rig.Add(85);
        Check(e.Slot == 4 && f.Slot == 5,
            "humans can join bot-vacated slots without colliding with remaining bots");
        const std::int32_t humanIndex = IndexOf(*a.Roster.Slots, 4, a.Roster.Count);
        Check(humanIndex >= 0, "human roster includes the reused bot slot");
        const std::uint16_t humanGeneration = a.Roster.Generations->at(static_cast<std::size_t>(humanIndex));
        Check(humanGeneration != oldBotGeneration,
            "bot-to-human reuse advances the slot generation");

        const std::uint16_t revisionBeforeConflict = a.State->Revision;
        rig.Server->Bots(4, ServerBots::DefaultLevel);
        const std::int64_t conflictWait = Runtime::StopwatchGetTimestamp();
        while (ElapsedMilliseconds(conflictWait) < 250)
        {
            for (const std::unique_ptr<Client>& client : rig.Clients)
            {
                client->Drain();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Check(a.State->Revision == revisionBeforeConflict && a.Slot == 0 && b.Slot == 1
            && c.Slot == 2 && d.Slot == 3 && e.Slot == 4 && f.Slot == 5,
            "bot expansion is rejected while humans occupy the requested slots");
        rig.Stable();

        rig.Remove(e);
        rig.Remove(f);
        rig.Stable();
        rig.ConfigureBots(4);
        const std::int32_t rejoinedBotIndex = IndexOf(*a.Roster.Slots, 4, a.Roster.Count);
        Check(rejoinedBotIndex >= 0
            && (*a.Roster.Names)[static_cast<std::size_t>(rejoinedBotIndex)].value_or("").starts_with("Bot ")
            && a.Roster.Generations->at(static_cast<std::size_t>(rejoinedBotIndex))
                != humanGeneration,
            "bot can reclaim a human-vacated slot under a new generation");
        Check(a.Roster.Count == 8, "four humans and four bots occupy distinct roster slots");
    }
}
