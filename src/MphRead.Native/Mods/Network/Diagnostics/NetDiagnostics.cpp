#include "Mods/Network/Diagnostics/NetDiagnostics.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Enums.hpp"
#include "GameState.hpp"
#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Formats/Types.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#endif

using ::MphRead::NativeRuntime::EnvironmentGetVariable;
using ::MphRead::NativeRuntime::HasFlag;
using ::MphRead::NativeRuntime::ManagedListAt;

namespace
{
    [[nodiscard]] std::string NetRoleToString(MphRead::Mods::Network::NetRole value)
    {
        return ::MphRead::Mods::Network::ToString(value);
    }

    [[nodiscard]] std::string BeamTypeToString(MphRead::BeamType value)
    {
        return ::MphRead::ToString(value);
    }
}

namespace MphRead::Mods::Network
{
    bool NetDiagnostics::Enabled()
    {
        if (!_checked)
        {
            _checked = true;
            _enabled = EnvironmentGetVariable("MPHREAD_NET_DEBUG").has_value();
        }
        return _enabled;
    }

    void NetDiagnostics::SetEnabled(bool value) noexcept
    {
        _checked = true;
        _enabled = value;
    }

    void NetDiagnostics::Report(double time)
    {
        if (!Enabled() || !NetSession::Active())
        {
            return;
        }
        if (time - _lastReport < 1.0)
        {
            return;
        }
        _lastReport = time;

        std::string line;
        line += NetPlayerLifecycle::Describe();
        line += " | ";
        line += "[netdbg] role=";
        line += NetRoleToString(NetSession::Role());
        line += " slot=";
        line += ::MphRead::NativeRuntime::ToString(NetSession::LocalSlot());

        std::int32_t active = 0;
        std::int32_t created = 0;
        line += " slots=[";
        for (std::int32_t i = 0; i < Entities::PlayerEntity::MaxPlayers(); i++)
        {
            std::shared_ptr<Entities::PlayerEntity> player
                = ManagedListAt(Entities::PlayerEntity::Players(), i);
            if (player == nullptr)
            {
                line.push_back('-');
                continue;
            }
            created++;
            const bool isActive = HasFlag(player->LoadFlags(), Entities::LoadFlags::Active);
            if (isActive)
            {
                active++;
            }
            line.push_back(isActive
                ? (player->IsBot() ? 'B' : 'A')
                : HasFlag(player->LoadFlags(), Entities::LoadFlags::SlotActive) ? 's' : '.');
        }
        line += "] active=";
        line += ::MphRead::NativeRuntime::ToString(active);
        line += " scoreboard=";
        line += ::MphRead::NativeRuntime::ToString(GameState::ActivePlayers());
        line += " created=";
        line += ::MphRead::NativeRuntime::ToString(created);

        line += " remoteState=[";
        for (std::size_t i = 0; i < NetSession::RemotePlayerSlots; i++)
        {
            line.push_back(NetSession::HasRemoteState(static_cast<std::int32_t>(i)) ? 'y' : 'n');
        }
        line += "] remoteIntent=[";
        for (std::size_t i = 0; i < NetSession::RemotePlayerSlots; i++)
        {
            line.push_back(NetSession::HasRemoteIntent(static_cast<std::int32_t>(i)) ? 'y' : 'n');
        }
        line.push_back(']');

        std::int32_t botRemotes = 0;
        for (std::int32_t i = 0; i < Entities::PlayerEntity::MaxPlayers(); i++)
        {
            std::shared_ptr<Entities::PlayerEntity> player
                = ManagedListAt(Entities::PlayerEntity::Players(), i);
            if (player != nullptr && i != NetSession::LocalSlot() && player->IsBot()
                && HasFlag(player->LoadFlags(), Entities::LoadFlags::Active))
            {
                botRemotes++;
            }
        }
        if (botRemotes > 0)
        {
            line += "  !! ";
            line += ::MphRead::NativeRuntime::ToString(botRemotes);
            line += " remote slot(s) still AI-driven";
        }

        line += " team=[";
        for (std::int32_t i = 0; i < Entities::PlayerEntity::MaxPlayers(); i++)
        {
            std::shared_ptr<Entities::PlayerEntity> player
                = ManagedListAt(Entities::PlayerEntity::Players(), i);
            if (i > 0)
            {
                line.push_back(',');
            }
            line += player == nullptr
                ? "-"
                : ::MphRead::NativeRuntime::ToString(player->TeamIndex());
        }
        line.push_back(']');

        if (!GameState::Teams())
        {
            std::int32_t shared = 0;
            for (std::int32_t i = 0; i < Entities::PlayerEntity::MaxPlayers(); i++)
            {
                std::shared_ptr<Entities::PlayerEntity> a
                    = ManagedListAt(Entities::PlayerEntity::Players(), i);
                if (a == nullptr
                    || !HasFlag(a->LoadFlags(), Entities::LoadFlags::Active))
                {
                    continue;
                }
                for (std::int32_t j = i + 1; j < Entities::PlayerEntity::MaxPlayers(); j++)
                {
                    std::shared_ptr<Entities::PlayerEntity> b
                        = ManagedListAt(Entities::PlayerEntity::Players(), j);
                    if (b != nullptr
                        && HasFlag(b->LoadFlags(), Entities::LoadFlags::Active)
                        && a->TeamIndex() == b->TeamIndex())
                    {
                        shared++;
                    }
                }
            }
            if (shared > 0)
            {
                line += "  !! ";
                line += ::MphRead::NativeRuntime::ToString(shared);
                line += " pair(s) share a team index in a free-for-all: "
                    "bombs and life drain will do nothing between them";
            }
        }

        line += " alt=[";
        for (std::int32_t i = 0; i < Entities::PlayerEntity::MaxPlayers(); i++)
        {
            std::shared_ptr<Entities::PlayerEntity> player
                = ManagedListAt(Entities::PlayerEntity::Players(), i);
            line.push_back(player == nullptr
                    || !HasFlag(player->LoadFlags(), Entities::LoadFlags::Active)
                ? '-'
                : player->IsAltForm() ? 'A'
                : player->IsMorphing() ? 'm'
                : player->IsUnmorphing() ? 'u'
                : 'b');
        }
        line += "] altSaid=[";
        line += NetPlayerBridge::FormSaidByAuthority();
        line.push_back(']');
        line += " shockcoil=";
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().ShockCoilAcquired);
        line.push_back('/');
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().ShockCoilSpawned);
        line += " bomb=";
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombHits);
        line.push_back('/');
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombPlayerChecks);
        line += " bombTeamSkips=";
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombTeamSkips);

        line += " dmg[";
        bool first = true;
        for (std::size_t i = 0; i < NetDamageMetrics::Read().HitsByBeam.size(); i++)
        {
            if (NetDamageMetrics::Read().HitsByBeam[i] == 0)
            {
                continue;
            }
            if (!first)
            {
                line.push_back(' ');
            }
            first = false;
            line += BeamTypeToString(static_cast<MphRead::BeamType>(i));
            line.push_back('=');
            line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().DamageByBeam[i]);
            line.push_back('/');
            line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().HitsByBeam[i]);
        }
        if (NetDamageMetrics::Read().BombDamageHits > 0)
        {
            if (!first)
            {
                line.push_back(' ');
            }
            line += "Bomb=";
            line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombDamageDealt);
            line.push_back('/');
            line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombDamageHits);
        }
        line.push_back(']');

        line += " bombSpawn=";
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombSpawnMade);
        line.push_back('/');
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombSpawnCalls);
        line += " det=";
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombSpawnDetonated);
        line += " stale=";
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombSpawnStaleCount);
        line += " poolEmpty=";
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombSpawnPoolEmpty);
        line += " bombNearest=";
        line += NetDamageMetrics::Read().BombNearest == std::numeric_limits<float>::max()
            ? "n/a"
            : ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombNearest, "0.00");
        line += " bombRadius=";
        line += ::MphRead::NativeRuntime::ToString(NetDamageMetrics::Read().BombRadiusSeen, "0.00");

        if (NetPlayerBridge::PlacementsRefusedCount() > 0)
        {
            line += " placementsRefused=";
            line += ::MphRead::NativeRuntime::ToString(NetPlayerBridge::PlacementsRefusedCount());
        }
        // Not a fault: a respawn this client and the authority placed on
        // different spawn points is the normal case, and the number is
        // how many of them this player was turned to face correctly for.
        if (NetPlayerBridge::SpawnFacingsTurnedCount() > 0)
        {
            line += " spawnFacings=";
            line += ::MphRead::NativeRuntime::ToString(NetPlayerBridge::SpawnFacingsTurnedCount());
            line += " worstSpawnFacing=";
            line += ::MphRead::NativeRuntime::ToString(NetPlayerBridge::WorstSpawnFacingDegrees(), "0.0");
        }
        // Snapshots about a life this player had already left, dropped
        // rather than replayed onto the new one.
        if (NetPlayerBridge::StaleDeathsIgnoredCount() > 0)
        {
            line += " staleDeaths=";
            line += ::MphRead::NativeRuntime::ToString(NetPlayerBridge::StaleDeathsIgnoredCount());
        }

        const std::optional<MatchStatePacket> match = NetSession::ServerMatch();
        if (match.has_value())
        {
            line += " serverMap=";
            if (match->RoomKey.has_value())
            {
                line += *match->RoomKey;
            }
            line += " serverPlayers=";
            line += ::MphRead::NativeRuntime::ToString(match->PlayerCount);
        }
        std::cout << line << '\n';
    }
}
