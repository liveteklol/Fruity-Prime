#include "SpectatorMode.hpp"

#include "../Entities/Players/PlayerEntity.hpp"
#include "../GameState.hpp"
#include "../Scene.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../Formats/Types.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::MphRead::TestFlag;

namespace
{
    using MphRead::Entities::LoadFlags;
    using MphRead::Entities::PlayerEntity;

    void EnsureHudReady(PlayerEntity& player)
    {
        if (!player.HudReady())
        {
            player.SetUpHud();
        }
    }
}


namespace MphRead::Mods
{
    bool SpectatorMode::_isSpectating = false;
    bool SpectatorMode::_freeCamera = false;
    std::optional<bool> SpectatorMode::_cameraRequest{};
    bool SpectatorMode::_showScoreboard = false;

    bool SpectatorMode::IsSpectating() noexcept
    {
        return _isSpectating;
    }

    bool SpectatorMode::FreeCamera() noexcept
    {
        return _freeCamera;
    }

    bool SpectatorMode::CanSpectate()
    {
        return GameState::Multiplayer();
    }

    void SpectatorMode::Start(bool watchSomeone)
    {
        if (_isSpectating || !CanSpectate())
        {
            return;
        }
        const std::int32_t next = FindNextActiveSlot(PlayerEntity::MainPlayerIndex());
        if (watchSomeone && next == -1)
        {
            return;
        }
        _isSpectating = true;

        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        if (localSlot >= 0
            && static_cast<std::size_t>(localSlot) < PlayerEntity::Players().size())
        {
            RequireReference(ManagedAt(PlayerEntity::Players(), localSlot))
                .ModSetSpectating(true);
        }
        if (watchSomeone)
        {
            Switch(next);
            return;
        }
        _cameraRequest = true;
    }

    void SpectatorMode::CycleNext()
    {
        if (!_isSpectating)
        {
            return;
        }
        const std::int32_t next = FindNextActiveSlot(PlayerEntity::MainPlayerIndex());
        if (next == -1)
        {
            return;
        }
        if (_freeCamera)
        {
            _cameraRequest = false;
        }
        Switch(next);
    }

    void SpectatorMode::CyclePrevious()
    {
        if (!_isSpectating)
        {
            return;
        }
        const std::int32_t previous = FindPreviousActiveSlot(PlayerEntity::MainPlayerIndex());
        if (previous == -1)
        {
            return;
        }
        if (_freeCamera)
        {
            _cameraRequest = false;
        }
        Switch(previous);
    }

    void SpectatorMode::ToggleView()
    {
        if (!_isSpectating)
        {
            return;
        }
        if (_freeCamera)
        {
            CycleNext();
            return;
        }
        _cameraRequest = true;
    }

    bool SpectatorMode::ShowScoreboard() noexcept
    {
        return _showScoreboard;
    }

    void SpectatorMode::NoteScoreboard(bool down) noexcept
    {
        _showScoreboard = down && _isSpectating;
    }

    void SpectatorMode::NoteFreeCamera(bool on) noexcept
    {
        _freeCamera = on;
    }

    std::optional<bool> SpectatorMode::TakeCameraRequest() noexcept
    {
        const std::optional<bool> request = _cameraRequest;
        _cameraRequest.reset();
        return request;
    }

    void SpectatorMode::Switch(std::int32_t slot)
    {
        const std::shared_ptr<PlayerEntity> target
            = ManagedAt(PlayerEntity::Players(), slot);
        PlayerEntity& targetRef = RequireReference(target);
        EnsureHudReady(targetRef);
        PlayerEntity::SetMainPlayerIndex(slot);
        Entities::CameraInfo& cameraInfo = RequireReference(targetRef.CameraInfo());
        const Formats::Culling::NodeRef nodeRef = targetRef.NodeRef;
        cameraInfo.NodeRef = nodeRef;
    }

    void SpectatorMode::Rejoin()
    {
        if (!_isSpectating)
        {
            return;
        }
        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        PlayerEntity::SetMainPlayerIndex(localSlot);
        PlayerEntity* localPlayer = nullptr;
        const auto& players = PlayerEntity::Players();
        if (localSlot >= 0 && static_cast<std::size_t>(localSlot) < players.size())
        {
            localPlayer = std::addressof(RequireReference(ManagedAt(players, localSlot)));
            EnsureHudReady(*localPlayer);
        }
        _isSpectating = false;
        _showScoreboard = false;
        _cameraRequest = false;

        if (localPlayer != nullptr
            && static_cast<std::size_t>(localSlot) < GameState::Points().size())
        {
            localPlayer->ModSetSpectating(false);
            std::int32_t& pointTarget = ManagedAt(GameState::Points(), localSlot);
            const std::int32_t pointValue = ManagedAt(GameState::Points(), localSlot);
            pointTarget = std::min<std::int32_t>(0, pointValue);
            ManagedAt(GameState::Kills(), localSlot) = 0;
            ManagedAt(GameState::Deaths(), localSlot) = 0;
        }
    }

    void SpectatorMode::Reset() noexcept
    {
        _isSpectating = false;
        _freeCamera = false;
        _showScoreboard = false;
        _cameraRequest.reset();
    }

    std::int32_t SpectatorMode::FindPreviousActiveSlot(std::int32_t fromSlot)
    {
        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        const auto& players = PlayerEntity::Players();
        for (std::int32_t offset = 1;
            offset <= static_cast<std::int32_t>(players.size()); ++offset)
        {
            const std::int32_t index
                = UncheckedAdd(
                    UncheckedSubtract(fromSlot, offset),
                    static_cast<std::int32_t>(players.size()))
                % static_cast<std::int32_t>(players.size());
            if (index == localSlot)
            {
                continue;
            }
            const std::shared_ptr<PlayerEntity> candidate
                = ManagedAt(players, index);
            PlayerEntity& candidateRef = RequireReference(candidate);
            if (TestFlag(candidateRef.LoadFlags(), LoadFlags::Active)
                && TestFlag(candidateRef.LoadFlags(), LoadFlags::Spawned)
                && candidateRef.Health() > 0)
            {
                return index;
            }
        }
        return -1;
    }

    std::int32_t SpectatorMode::FindNextActiveSlot(std::int32_t fromSlot)
    {
        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        const auto& players = PlayerEntity::Players();
        for (std::int32_t offset = 1;
            offset <= static_cast<std::int32_t>(players.size()); ++offset)
        {
            const std::int32_t index
                = UncheckedAdd(fromSlot, offset)
                % static_cast<std::int32_t>(players.size());
            if (index == localSlot)
            {
                continue;
            }
            const std::shared_ptr<PlayerEntity> candidate
                = ManagedAt(players, index);
            PlayerEntity& candidateRef = RequireReference(candidate);
            if (TestFlag(candidateRef.LoadFlags(), LoadFlags::Active)
                && TestFlag(candidateRef.LoadFlags(), LoadFlags::Spawned)
                && candidateRef.Health() > 0)
            {
                return index;
            }
        }
        return -1;
    }
}
