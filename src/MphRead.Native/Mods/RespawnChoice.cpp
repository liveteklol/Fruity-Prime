#include "RespawnChoice.hpp"

#include "../GameState.hpp"
#include "EndScreen.hpp"
#include "Launcher/Portable/LaunchPlan.hpp"
#include "Launcher/Portable/LauncherPrefs.hpp"

#include "../Entities/Players/PlayerEntity.hpp"
#include "../Formats/Types.hpp"
#include "Mods/Network/Replay/DemoClip.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"
#include "../NativeRuntime/System/Managed.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

using ::MphRead::NativeRuntime::RequireReference;

namespace
{
}

namespace MphRead::Mods
{
    std::optional<MphRead::Hunter> RespawnChoice::_hunter{};
    std::optional<std::int32_t> RespawnChoice::_color{};

    MphRead::Hunter RespawnChoice::Hunter()
    {
        if (_hunter.has_value())
        {
            return _hunter.value();
        }
        std::shared_ptr<Entities::PlayerEntity> player = Entities::PlayerEntity::Main();
        if (player != nullptr)
        {
            return player->Hunter();
        }
        return MphRead::Hunter::Samus;
    }

    std::int32_t RespawnChoice::Color()
    {
        if (_color.has_value())
        {
            return _color.value();
        }

        std::shared_ptr<Entities::PlayerEntity> firstMain = Entities::PlayerEntity::Main();
        if (firstMain != nullptr)
        {
            std::shared_ptr<Entities::PlayerEntity> secondMain = Entities::PlayerEntity::Main();
            if (RequireReference(secondMain).SlotIndex() >= 0)
            {
                std::shared_ptr<Entities::PlayerEntity> thirdMain = Entities::PlayerEntity::Main();
                const std::int32_t slot = RequireReference(thirdMain).SlotIndex();
                return Network::PlayerColors::ChoiceFor(slot);
            }
        }
        return Launcher::LauncherPrefs::LastColor();
    }

    void RespawnChoice::Reset()
    {
        _hunter.reset();
        _color.reset();
        EndScreen::ClearReady();
        Network::DemoClip::Purge();
    }

    void RespawnChoice::Request(MphRead::Hunter hunter, std::int32_t color)
    {
        _hunter = Launcher::Hunters::Resolve(hunter);
        _color = Network::PlayerColors::Clamp(color);
    }

    void RespawnChoice::ApplyOnSpawn(const std::shared_ptr<Entities::PlayerEntity>& player)
    {
        if (player != Entities::PlayerEntity::Main())
        {
            return;
        }
        if (!GameState::Multiplayer())
        {
            return;
        }
        if (!_hunter.has_value() && !_color.has_value())
        {
            return;
        }

        MphRead::Hunter hunter;
        if (_hunter.has_value())
        {
            hunter = _hunter.value();
        }
        else
        {
            hunter = RequireReference(player).Hunter();
        }

        std::int32_t color;
        if (_color.has_value())
        {
            color = _color.value();
        }
        else
        {
            color = Color();
        }

        _hunter.reset();
        _color.reset();

        if (hunter != RequireReference(player).Hunter())
        {
            RequireReference(player).ModSetHunter(hunter);
            RequireReference(player).Initialize();
        }
        if (RequireReference(player).SlotIndex() >= 0
            && RequireReference(player).SlotIndex()
                < static_cast<std::int32_t>(Network::PlayerColors::SlotCount()))
        {
            const std::int32_t slot = RequireReference(player).SlotIndex();
            Network::PlayerColors::SetChoice(slot, color);
        }
        if (Network::NetSession::Active())
        {
            Network::NetSession::SetLocalHunter(hunter);
            Network::NetSession::SetLocalColor(color);
            Network::NetSession::SendIdentify();
        }
        Network::PlayerColors::Resolve();
    }
}
