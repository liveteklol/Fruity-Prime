#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Gameplay/Player/NetHudHealth.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Platform/AppPaths.hpp"
#include "NativeRuntime/System/DateTime.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Culling.hpp"
#include "Scene.hpp"
#include "GameState.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Formats/Types.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Encoding.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Runtime.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
#else
#error Unsupported platform for AppContext.BaseDirectory parity.
#endif

#if !defined(_WIN32)
#include <langinfo.h>
#include <locale.h>
#endif

using ::MphRead::NativeRuntime::AppContextBaseDirectory;
using ::MphRead::NativeRuntime::DecodeUtf8Scalar;
using ::MphRead::NativeRuntime::EnvironmentGetVariable;
using ::MphRead::NativeRuntime::HasFlag;
using ::MphRead::NativeRuntime::PathFromUtf8;
using ::MphRead::NativeRuntime::Utf16Length;
using ::MphRead::NativeRuntime::Utf8Scalar;

namespace
{
    template <typename TInt>
    [[nodiscard]] std::string IntegerText(TInt value)
    {
        return ::MphRead::NativeRuntime::ToString(value);
    }

    [[nodiscard]] std::string AlignLeft(std::string text, std::size_t width)
    {
        const std::size_t length = Utf16Length(text);
        if (length < width)
        {
            text.append(width - length, ' ');
        }
        return text;
    }

    [[nodiscard]] std::string SafeClientName(std::string_view clientName)
    {
        std::string safe;
        for (std::size_t index = 0; index < clientName.size();)
        {
            const Utf8Scalar unit = DecodeUtf8Scalar(clientName, index);
            if (!unit.Valid())
            {
                safe.push_back('_');
            }
            else if (unit.Value > 0xFFFFU)
            {
                safe += "__";
            }
            else if (::MphRead::NativeRuntime::CharIsLetterOrDigit(static_cast<char16_t>(unit.Value)))
            {
                safe.append(clientName.substr(index, unit.Length));
            }
            else
            {
                safe.push_back('_');
            }
            index += unit.Length;
        }
        return safe;
    }

    [[nodiscard]] std::string NullableText(const std::optional<std::string>& value)
    {
        return value.has_value() ? *value : std::string();
    }

    [[nodiscard]] std::shared_ptr<MphRead::Entities::PlayerEntity> PlayerAt(
        std::int32_t slot)
    {
        const auto& players = MphRead::Entities::PlayerEntity::Players();
        if (slot < 0 || static_cast<std::size_t>(slot) >= players.size())
        {
            throw MphRead::SceneDetail::IndexOutOfRangeException();
        }
        return players[static_cast<std::size_t>(slot)];
    }
}

namespace MphRead::Mods::Network
{
    std::unique_ptr<std::ofstream> NetLog::_writer{};
    double NetLog::_lastWrite = 0.0;
    bool NetLog::_failed = false;
    const double NetLog::Interval = NetLog::ReadInterval();
    bool NetLog::_enabled = false;

    bool NetLog::Enabled() noexcept
    {
        return _enabled;
    }

    double NetLog::ReadInterval()
    {
        const std::optional<std::string> value = EnvironmentGetVariable("MPHREAD_NETLOG_INTERVAL");
        double parsed = 0.0;
        if (value.has_value()
            && ::MphRead::NativeRuntime::DoubleTryParseInvariant(*value, parsed)
            && parsed > 0.0 && parsed <= 10.0)
        {
            return parsed;
        }
        return 1.0;
    }

    void NetLog::Open(const std::string& clientName)
    {
        if (_failed || _writer != nullptr)
        {
            return;
        }
        try
        {
            const std::string safe = SafeClientName(clientName);
            const std::filesystem::path baseDirectory = PathFromUtf8(Platform::AppPaths::UserDataDirectory());
            const std::string fileName = "netlog-" + safe + ".txt";
            const std::u8string utf8FileName(
                reinterpret_cast<const char8_t*>(fileName.data()), fileName.size());
            const std::filesystem::path path
                = baseDirectory / std::filesystem::path(utf8FileName);
            auto writer = std::make_unique<std::ofstream>();
            writer->exceptions(std::ios::failbit | std::ios::badbit);
            // Emptied once here, then written in append mode: a server's log
            // is emptied daily from outside (tools/netlog-clean.sh), and a
            // writer without O_APPEND would carry on at its old offset and
            // leave a sparse file as long as before.
            writer->open(path, std::ios::out | std::ios::trunc);
            writer->close();
            writer->open(path, std::ios::out | std::ios::app);
            _writer = std::move(writer);
            _enabled = true;
            Line("=== MphRead net log for \"" + clientName + "\" ===");
            Line("started " + ::MphRead::NativeRuntime::DateTimeToString(::MphRead::NativeRuntime::DateTimeNow(), "yyyy-MM-dd HH:mm:ss"));
        }
        catch (const std::ios_base::failure& ex)
        {
            _failed = true;
            std::cout << "[netlog] could not open log: " << ex.what() << std::endl;
        }
    }

    void NetLog::Close()
    {
        if (_writer != nullptr)
        {
            _writer->close();
        }
        _writer.reset();
        _enabled = false;
        _lastWrite = 0.0;
    }

    void NetLog::CollisionRange(
        std::int32_t slot, const std::string& label,
        OpenTK::Mathematics::Vector3 prev, OpenTK::Mathematics::Vector3 current)
    {
        const OpenTK::Mathematics::Vector3 vector = current - prev;
        const float delta = std::sqrt(
            vector.X * vector.X + vector.Y * vector.Y + vector.Z * vector.Z);
        std::string message = "collision ";
        message += label;
        message += " slot=";
        message += ::MphRead::NativeRuntime::ToString(slot);
        message += " prev=(";
        message += ::MphRead::NativeRuntime::ToString(prev.X, "0.00");
        message += ",";
        message += ::MphRead::NativeRuntime::ToString(prev.Y, "0.00");
        message += ",";
        message += ::MphRead::NativeRuntime::ToString(prev.Z, "0.00");
        message += ") cur=(";
        message += ::MphRead::NativeRuntime::ToString(current.X, "0.00");
        message += ",";
        message += ::MphRead::NativeRuntime::ToString(current.Y, "0.00");
        message += ",";
        message += ::MphRead::NativeRuntime::ToString(current.Z, "0.00");
        message += ") delta=";
        message += ::MphRead::NativeRuntime::ToString(delta, "0.00");
        Event(message);
    }

    void NetLog::Event(const std::string& message)
    {
        Line("[" + ::MphRead::NativeRuntime::DateTimeToString(::MphRead::NativeRuntime::DateTimeNow(), "HH:mm:ss.fff") + "] EVENT  " + message);
    }

    void NetLog::Snapshot(double time)
    {
        SnapshotInternal(time, nullptr);
    }

    void NetLog::Snapshot(double time, MphRead::Scene& scene)
    {
        SnapshotInternal(time, &scene);
    }

    void NetLog::SnapshotInternal(double time, MphRead::Scene* scene)
    {
        if (_writer == nullptr || time - _lastWrite < Interval)
        {
            return;
        }
        _lastWrite = time;

        std::string state;
        state += "[";
        state += ::MphRead::NativeRuntime::DateTimeToString(::MphRead::NativeRuntime::DateTimeNow(), "HH:mm:ss.fff");
        state += "] STATE  role=";
        state += ToString(NetSession::Role());
        state += " slot=";
        state += ::MphRead::NativeRuntime::ToString(NetSession::LocalSlot());
        state += " authority=";
        state += NetSession::IsAuthority() ? "True" : "False";
        state += " main=";
        state += ::MphRead::NativeRuntime::ToString(Entities::PlayerEntity::MainPlayerIndex());
        state += " mode=";
        state += ::MphRead::ToString(GameState::Mode());
        state += " matchTime=";
        state += ::MphRead::NativeRuntime::ToString(GameState::MatchTime(), "0.0");
        state += " matchState=";
        state += ::MphRead::ToString(GameState::MatchState());
        state += " goal=";
        state += ::MphRead::NativeRuntime::ToString(GameState::PointGoal());
        state += " ";

        const std::optional<MatchStatePacket> match = NetSession::ServerMatch();
        if (match.has_value())
        {
            state += "serverTime=";
            state += ::MphRead::NativeRuntime::ToString(match->TimeRemaining, "0.0");
            state += " serverMap=";
            state += NullableText(match->RoomKey);
            state += " serverPeers=";
            state += ::MphRead::NativeRuntime::ToString(match->PlayerCount);
            state += " ";
        }
        Line(state);
        HitReg();

        for (std::int32_t slot = 0;
            slot < Entities::PlayerEntity::MaxPlayers(); slot++)
        {
            const std::shared_ptr<Entities::PlayerEntity> player = PlayerAt(slot);
            if (player == nullptr)
            {
                Line("           slot " + ::MphRead::NativeRuntime::ToString(slot) + ": (no entity)");
                continue;
            }
            const bool active = HasFlag(
                player->LoadFlags(), Entities::LoadFlags::Active);
            const bool occupied = SessionSlots().IsOccupied(slot);
            if (!active && !occupied)
            {
                continue;
            }

            std::string line;
            line += "           slot ";
            line += ::MphRead::NativeRuntime::ToString(slot);
            line += ": name=";
            line += AlignLeft(GameState::Nicknames()[slot], 10);
            line += " occupied=";
            line += occupied ? "y" : "n";
            line += " active=";
            line += active ? "y" : "n";
            line += " spawned=";
            line += HasFlag(player->LoadFlags(), Entities::LoadFlags::Spawned) ? "y" : "n";
            line += " bot=";
            line += player->IsBot() ? "y" : "n";
            line += " hp=";
            line += AlignLeft(::MphRead::NativeRuntime::ToString(player->Health()), 3);
            line += " score=";
            line += ::MphRead::NativeRuntime::ToString(GameState::Points()[slot]);
            line += "/";
            line += ::MphRead::NativeRuntime::ToString(GameState::TeamPoints()[slot]);
            line += "p ";
            line += ::MphRead::NativeRuntime::ToString(GameState::Kills()[slot]);
            line += "k";
            line += ::MphRead::NativeRuntime::ToString(GameState::Deaths()[slot]);
            line += "d respawnTimer=";
            line += AlignLeft(::MphRead::NativeRuntime::ToString(player->RespawnTimer()), 5);
            line += " pos=(";
            line += ::MphRead::NativeRuntime::ToString(player->Position.X, "0.00");
            line += ",";
            line += ::MphRead::NativeRuntime::ToString(player->Position.Y, "0.00");
            line += ",";
            line += ::MphRead::NativeRuntime::ToString(player->Position.Z, "0.00");
            line += ") form=";
            line += AlignLeft(player->ModFormState(), 24);
            line += " nodeRef=";
            line += DescribeNodeRef(*player);
            line += " ";
            const HudHealthSample health = NetHudHealth::Sample(*player);
            line += "health slot=" + IntegerText(player->SlotIndex())
                + " generation=" + IntegerText(NetPlayerLifecycle::Generation(player->SlotIndex()))
                + " life=" + IntegerText(NetPlayerLifecycle::Get(player->SlotIndex()))
                + " authorityHP=" + (health.Authoritative ? IntegerText(health.Health) : std::string("unknown"))
                + " entityHP=" + IntegerText(player->Health())
                + " hpFrame=" + IntegerText(health.SnapshotFrame)
                + " snapshotAge=" + IntegerText(NetSession::SnapshotAge())
                + " " + NetHitPrediction::HealthDetails(player->SlotIndex()) + " ";
            if (NetSession::HasRemoteState(slot))
            {
                const PlayerState& remote = NetSession::RemoteState(static_cast<std::size_t>(slot));
                line += "lastDamageEvent=" + IntegerText(remote.DamageEventId)
                    + " lastAttacker=" + IntegerText(remote.AttackerSlot) + " ";
            }
            line += " inScene=";
            line += InScene(scene, *player) ? "y" : "n";
            line += " stateValid=";
            line += NetSession::HasRemoteState(slot) ? "y" : "n";
            line += " intentValid=";
            line += NetSession::HasRemoteIntent(slot) ? "y" : "n";
            Line(line);
        }
    }

    void NetLog::HitReg()
    {
        namespace Runtime = ::MphRead::NativeRuntime;
        std::string line = "           hitreg ";
        std::uint32_t readFrame = 0;
        std::uint8_t readSub = 0;
        if (NetSmoothing::AckPoint(readFrame, readSub))
        {
            line += "read=" + IntegerText(readFrame) + "+" + Runtime::ToString(readSub / 256.0, "0.00") + " ";
            line += "buffer=" + IntegerText(NetSmoothing::Delay()) + "f ";
            line += "newestSnap=" + IntegerText(NetSession::LastSnapshotFrame()) + " ";
            line += "starved=" + IntegerText(NetSmoothing::Starved()) + " snaps=" + IntegerText(NetSmoothing::Snaps()) + " ";
        }
        else
        {
            line += "read=off ack=" + IntegerText(NetSession::AppliedSnapshotFrame()) + " ";
        }
        if (NetUnlagged::ShotsCompensated() > 0)
        {
            line += "rewound=" + IntegerText(NetUnlagged::ShotsCompensated()) + " ";
            line += "meanRewind=" + Runtime::ToString(static_cast<double>(NetUnlagged::FramesRewound())
                / static_cast<double>(NetUnlagged::ShotsCompensated()), "0.0") + "f ";
            line += "worst=" + IntegerText(NetUnlagged::WorstRewind()) + "f ";
            line += "ceiling=" + IntegerText(NetUnlagged::MaxRewindFrames()) + "f ";
            line += "clamped=" + IntegerText(NetUnlagged::ShotsClamped()) + " ";
            line += "historyMiss=" + IntegerText(NetUnlagged::HistoryMisses()) + " ";
        }
        if (NetHitClaims::Declared() > 0)
        {
            line += "claims=" + IntegerText(NetHitClaims::Declared()) + " applied=" + IntegerText(NetHitClaims::Applied()) + " ";
            line += "dup=" + IntegerText(NetHitClaims::Duplicate()) + " ";
            line += "voidShooter=" + IntegerText(NetHitClaims::RefusedDeadShooter()) + " ";
            line += "voidVictim=" + IntegerText(NetHitClaims::RefusedDeadVictim()) + " ";
            line += "refused=" + IntegerText(NetHitClaims::RefusedOther()) + " ";
            line += "unanswered=" + IntegerText(NetHitClaims::Unanswered()) + " ";
        }
        if (NetHitClaims::Received() > 0)
        {
            line += "claimsIn=" + IntegerText(NetHitClaims::Received()) + " ";
            line += "rescued=" + IntegerText(NetHitClaims::AppliedHere()) + " ";
            line += "(" + IntegerText(NetHitClaims::RescuedKills()) + "k "
                + IntegerText(NetHitClaims::RescuedHeadshots()) + "hs) ";
            line += "dupIn=" + IntegerText(NetHitClaims::DuplicateHere()) + " ";
            line += "voidShooterIn=" + IntegerText(NetHitClaims::VoidedDeadShooter()) + " ";
            line += "refusedIn=" + IntegerText(NetHitClaims::RefusedHere()) + " ";
        }
        line += "predicted=" + IntegerText(NetHitPrediction::Predicted()) + " ";
        line += "confirmed=" + IntegerText(NetHitPrediction::Confirmed()) + " ";
        line += "denied=" + IntegerText(NetHitPrediction::Denied()) + " ";
        line += "unpredicted=" + IntegerText(NetHitPrediction::Unpredicted());
        line += " unpredMoving=" + IntegerText(NetHitPrediction::UnpredictedMoving());
        line += " unpredStill=" + IntegerText(NetHitPrediction::UnpredictedStill());
        line += " firedMoving=" + IntegerText(NetDamageMetrics::Read().FiredMoving);
        line += " firedStill=" + IntegerText(NetDamageMetrics::Read().FiredStill);
        Line(line);
    }

    bool NetLog::InScene(MphRead::Scene* scene, Entities::PlayerEntity& player)
    {
        if (scene == nullptr)
        {
            return false;
        }
        auto enumerator = scene->Entities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            const std::shared_ptr<Entities::EntityBase> entity = enumerator.Current();
            if (entity.get() == &player)
            {
                return true;
            }
        }
        return false;
    }

    std::string NetLog::DescribeNodeRef(Entities::PlayerEntity& player)
    {
        try
        {
            const Formats::Culling::NodeRef nodeRef = player.NodeRef;
            if (nodeRef == Formats::Culling::NodeRef::None)
            {
                return "none";
            }
            std::string result = nodeRef.RoomName.HasValue() ? *nodeRef.RoomName : "?";
            result += ":";
            result += ::MphRead::NativeRuntime::ToString(nodeRef.PartIndex);
            result += "/";
            result += ::MphRead::NativeRuntime::ToString(nodeRef.NodeIndex);
            return result;
        }
        catch (...)
        {
            return "?";
        }
    }

    void NetLog::Line(const std::string& text)
    {
        try
        {
            if (_writer != nullptr)
            {
                *_writer << text << std::endl;
            }
        }
        catch (const std::ios_base::failure&)
        {
        }
    }
}
