#include "SpectatorMode.hpp"

#include "../Entities/Players/PlayerEntity.hpp"
#include "../GameState.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../Formats/Types.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Session/SessionRoster.hpp"
#include "Mods/SpectatorVisibilityPolicy.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::TestFlag;

namespace
{
    using MphRead::Entities::LoadFlags;
    using MphRead::Entities::PlayerEntity;
    using MphRead::Mods::SpectatorSlotSnapshot;
    using MphRead::Mods::SpectatorTargetIdentity;
    using MphRead::Mods::SpectatorTargetPolicy;
    namespace Network = MphRead::Mods::Network;

    void EnsureHudReady(PlayerEntity& player)
    {
        if (!player.HudReady())
        {
            player.SetUpHud();
        }
    }

    void ApplyLocalSpectatorRequest(PlayerEntity& player, bool spectating)
    {
        if (Network::NetSession::IsClient() && !Network::NetSession::IsAuthority())
        {
            // The local camera request is immediate, but the PlayerEntity bit
            // remains the last state accepted by the authority.
            player.ModClearSpectatorInput();
            return;
        }
        player.ModSetSpectating(spectating);
    }

    [[nodiscard]] bool HasPlayer(std::int32_t slot) noexcept
    {
        const auto& players = PlayerEntity::Players();
        return slot >= 0 && static_cast<std::size_t>(slot) < players.size()
            && ManagedAt(players, slot) != nullptr;
    }

    [[nodiscard]] SpectatorSlotSnapshot CaptureSlot(std::int32_t slot, bool networked)
    {
        SpectatorSlotSnapshot snapshot{};
        const auto& players = PlayerEntity::Players();
        if (slot < 0 || static_cast<std::size_t>(slot) >= players.size())
        {
            return snapshot;
        }

        const std::shared_ptr<PlayerEntity> player = ManagedAt(players, slot);
        if (player == nullptr)
        {
            return snapshot;
        }

        snapshot.Exists = true;
        snapshot.Active = TestFlag(player->LoadFlags(), LoadFlags::Active);
        snapshot.Spawned = TestFlag(player->LoadFlags(), LoadFlags::Spawned);
        snapshot.Spectating = player->ModIsSpectating();
        snapshot.Health = player->Health();
        if (networked)
        {
            snapshot.RosterOccupied = Network::SessionSlots().IsOccupied(slot);
            snapshot.Generation = Network::NetPlayerLifecycle::Generation(slot);
            snapshot.LifeId = Network::NetPlayerLifecycle::Get(slot);
        }
        return snapshot;
    }

    [[nodiscard]] std::array<SpectatorSlotSnapshot, PlayerEntity::SlotCapacity> CaptureSlots(bool networked)
    {
        std::array<SpectatorSlotSnapshot, PlayerEntity::SlotCapacity> snapshots{};
        const auto& players = PlayerEntity::Players();
        const std::size_t count = std::min(players.size(), snapshots.size());
        for (std::size_t slot = 0; slot < count; ++slot)
        {
            snapshots[slot] = CaptureSlot(static_cast<std::int32_t>(slot), networked);
        }
        return snapshots;
    }

    [[nodiscard]] std::optional<SpectatorTargetIdentity> CaptureEligibleTarget(std::int32_t slot)
    {
        const bool networked = Network::NetSession::Active();
        const SpectatorSlotSnapshot snapshot = CaptureSlot(slot, networked);
        if (!SpectatorTargetPolicy::IsEligible(snapshot, slot,
            Network::NetHooks::LocalSlot(), networked))
        {
            return std::nullopt;
        }
        return SpectatorTargetIdentity{slot, snapshot.Generation, snapshot.LifeId};
    }

    [[nodiscard]] bool IdentityIsCurrent(const SpectatorTargetIdentity& identity)
    {
        const bool networked = Network::NetSession::Active();
        const SpectatorSlotSnapshot snapshot = CaptureSlot(identity.Slot, networked);
        return SpectatorTargetPolicy::Matches(identity, snapshot, identity.Slot,
            Network::NetHooks::LocalSlot(), networked);
    }

    [[nodiscard]] std::int32_t FindSafeMainSlot() noexcept
    {
        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        if (HasPlayer(localSlot))
        {
            return localSlot;
        }

        const auto& players = PlayerEntity::Players();
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(players.size()); ++slot)
        {
            if (HasPlayer(slot))
            {
                return slot;
            }
        }
        return -1;
    }

    [[nodiscard]] std::int32_t SelectionOrigin() noexcept
    {
        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        return localSlot >= 0 ? localSlot : PlayerEntity::MainPlayerIndex();
    }
}

namespace MphRead::Mods
{
    bool SpectatorMode::_isSpectating = false;
    bool SpectatorMode::_freeCamera = false;
    std::optional<SpectatorTargetIdentity> SpectatorMode::_target{};
    bool SpectatorMode::_targetChanged = false;
    std::optional<SpectatorCameraRequest> SpectatorMode::_cameraRequest{};
    bool SpectatorMode::_showScoreboard = false;

    bool SpectatorMode::IsSpectating() noexcept
    {
        return _isSpectating;
    }

    bool SpectatorMode::ShouldHideLocalPlayerModel(std::int32_t slot) noexcept
    {
        return SpectatorVisibilityPolicy::ShouldHideLocalPlayerModel(
            _isSpectating, slot, Network::NetHooks::LocalSlot());
    }

    bool SpectatorMode::FreeCamera() noexcept
    {
        return _freeCamera;
    }

    bool SpectatorMode::CanSpectate()
    {
        return GameState::Multiplayer();
    }

    bool SpectatorMode::CanUsePlayerCamera() noexcept
    {
        const std::int32_t mainSlot = PlayerEntity::MainPlayerIndex();
        const bool targetIsCurrent = _target.has_value()
            && _target->Slot == mainSlot && IdentityIsCurrent(*_target);
        return SpectatorCameraPolicy::CanFollowPlayer(
            _isSpectating, HasPlayer(mainSlot), _target.has_value(), targetIsCurrent);
    }

    bool SpectatorMode::CanBorrowLocalCameraPose() noexcept
    {
        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        return SpectatorCameraPolicy::CanBorrowLocalPose(_isSpectating,
            _target.has_value(), PlayerEntity::MainPlayerIndex() == localSlot,
            localSlot >= 0 && HasPlayer(localSlot));
    }

    void SpectatorMode::Start(bool watchSomeone)
    {
        if (_isSpectating || !CanSpectate())
        {
            return;
        }

        const std::int32_t origin = SelectionOrigin();
        _isSpectating = true;
        _target.reset();
        _targetChanged = false;

        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        if (HasPlayer(localSlot))
        {
            ApplyLocalSpectatorRequest(*ManagedAt(PlayerEntity::Players(), localSlot), true);
        }

        if (watchSomeone)
        {
            const std::int32_t next = FindNextActiveSlot(origin);
            if (next >= 0 && Switch(next, true))
            {
                return;
            }
        }

        const std::int32_t safeMain = FindSafeMainSlot();
        if (safeMain >= 0)
        {
            PlayerEntity::SetMainPlayerIndex(safeMain);
        }
        RequestCamera(true, false);
    }

    void SpectatorMode::Update()
    {
        if (!_isSpectating || !_target.has_value())
        {
            return;
        }

        if (!_targetChanged && IdentityIsCurrent(*_target))
        {
            if (PlayerEntity::MainPlayerIndex() != _target->Slot)
            {
                PlayerEntity::SetMainPlayerIndex(_target->Slot);
                if (_cameraRequest.has_value() && !_cameraRequest->FreeCamera)
                {
                    _cameraRequest->Target = _target;
                }
                else if (!_freeCamera)
                {
                    RequestCamera(false, false, _target);
                }
            }
            return;
        }

        const SpectatorTargetIdentity expired = *_target;
        _target.reset();
        _targetChanged = false;

        const std::int32_t next = FindNextActiveSlot(expired.Slot);
        if (next >= 0 && Switch(next, false))
        {
            return;
        }

        const std::int32_t safeMain = FindSafeMainSlot();
        if (safeMain >= 0)
        {
            PlayerEntity::SetMainPlayerIndex(safeMain);
        }
        if (!_freeCamera)
        {
            // The camera pose was cached by Scene while the target was still
            // valid. Never recover it from the departed entity here.
            RequestCamera(true, true);
        }
        else if (_cameraRequest.has_value() && !_cameraRequest->FreeCamera)
        {
            // Cancel a queued follow request tied to the expired identity.
            _cameraRequest.reset();
        }
    }

    void SpectatorMode::NotifyTargetChanged(std::int32_t slot) noexcept
    {
        if (_target.has_value() && _target->Slot == slot)
        {
            _targetChanged = true;
        }
    }

    void SpectatorMode::CycleNext()
    {
        if (!_isSpectating)
        {
            return;
        }
        Update();
        const std::int32_t origin = _target.has_value() ? _target->Slot : SelectionOrigin();
        const std::int32_t next = FindNextActiveSlot(origin);
        if (next >= 0)
        {
            static_cast<void>(Switch(next, true));
        }
    }

    void SpectatorMode::CyclePrevious()
    {
        if (!_isSpectating)
        {
            return;
        }
        Update();
        const std::int32_t origin = _target.has_value() ? _target->Slot : SelectionOrigin();
        const std::int32_t previous = FindPreviousActiveSlot(origin);
        if (previous >= 0)
        {
            static_cast<void>(Switch(previous, true));
        }
    }

    void SpectatorMode::ToggleView()
    {
        if (!_isSpectating)
        {
            return;
        }
        if (_freeCamera)
        {
            if (_target.has_value() && IdentityIsCurrent(*_target)
                && Switch(_target->Slot, true))
            {
                return;
            }
            CycleNext();
            return;
        }
        RequestCamera(true, false);
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

    void SpectatorMode::RequestCamera(bool freeCamera, bool preserveLastPose,
        std::optional<SpectatorTargetIdentity> target) noexcept
    {
        _cameraRequest = SpectatorCameraRequest{freeCamera, preserveLastPose, target};
    }

    std::optional<SpectatorCameraRequest> SpectatorMode::TakeCameraRequest() noexcept
    {
        std::optional<SpectatorCameraRequest> request = _cameraRequest;
        _cameraRequest.reset();
        if (request.has_value() && !request->FreeCamera && _isSpectating
            && (!request->Target.has_value() || !IdentityIsCurrent(*request->Target)))
        {
            RequestCamera(true, true);
            request = _cameraRequest;
            _cameraRequest.reset();
        }
        return request;
    }

    bool SpectatorMode::Switch(std::int32_t slot, bool requestPlayerCamera)
    {
        const std::optional<SpectatorTargetIdentity> identity = CaptureEligibleTarget(slot);
        if (!identity.has_value())
        {
            return false;
        }

        const std::shared_ptr<PlayerEntity> target = ManagedAt(PlayerEntity::Players(), slot);
        if (target == nullptr || target->CameraInfo() == nullptr)
        {
            return false;
        }

        EnsureHudReady(*target);
        PlayerEntity::SetMainPlayerIndex(slot);
        target->CameraInfo()->NodeRef = target->NodeRef;
        _target = identity;
        _targetChanged = false;

        if (requestPlayerCamera || (_cameraRequest.has_value() && !_cameraRequest->FreeCamera))
        {
            RequestCamera(false, false, identity);
        }
        return true;
    }

    void SpectatorMode::Rejoin()
    {
        if (!_isSpectating)
        {
            return;
        }

        const std::int32_t localSlot = Network::NetHooks::LocalSlot();
        const auto& players = PlayerEntity::Players();
        PlayerEntity* localPlayer = nullptr;
        if (localSlot >= 0 && static_cast<std::size_t>(localSlot) < players.size())
        {
            const std::shared_ptr<PlayerEntity> player = ManagedAt(players, localSlot);
            if (player != nullptr)
            {
                localPlayer = player.get();
                EnsureHudReady(*localPlayer);
                ApplyLocalSpectatorRequest(*localPlayer, false);
                PlayerEntity::SetMainPlayerIndex(localSlot);
            }
        }
        if (localPlayer == nullptr)
        {
            const std::int32_t safeMain = FindSafeMainSlot();
            if (safeMain >= 0)
            {
                PlayerEntity::SetMainPlayerIndex(safeMain);
            }
        }

        _isSpectating = false;
        _target.reset();
        _targetChanged = false;
        _showScoreboard = false;
        RequestCamera(false, false);

        if (localPlayer != nullptr
            && static_cast<std::size_t>(localSlot) < GameState::Points().size())
        {
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
        _target.reset();
        _targetChanged = false;
        _showScoreboard = false;
        _cameraRequest.reset();
    }

    std::int32_t SpectatorMode::FindPreviousActiveSlot(std::int32_t fromSlot)
    {
        const auto snapshots = CaptureSlots(Network::NetSession::Active());
        return SpectatorTargetPolicy::Find(snapshots, fromSlot,
            Network::NetHooks::LocalSlot(), Network::NetSession::Active(), false);
    }

    std::int32_t SpectatorMode::FindNextActiveSlot(std::int32_t fromSlot)
    {
        const auto snapshots = CaptureSlots(Network::NetSession::Active());
        return SpectatorTargetPolicy::Find(snapshots, fromSlot,
            Network::NetHooks::LocalSlot(), Network::NetSession::Active(), true);
    }
}
