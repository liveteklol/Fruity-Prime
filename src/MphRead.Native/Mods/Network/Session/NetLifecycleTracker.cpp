#include "Mods/Network/Session/NetLifecycleTracker.hpp"

#include "NativeRuntime/System/Enum.hpp"

#include <array>
#include <bit>

namespace MphRead::Mods::Network
{
    std::string ToString(NetworkPlayerState value)
    {
        static constexpr std::array<::MphRead::NativeRuntime::EnumNameEntry, 5> Names = {{
            {0, "Empty"}, {1, "WaitingToSpawn"}, {2, "Alive"}, {3, "Dead"}, {4, "Spectating"}}};
        return ::MphRead::NativeRuntime::ManagedEnumToString(value, Names.data(), Names.size(), false);
    }

    std::string ToString(LifecycleRejection value)
    {
        static constexpr std::array<::MphRead::NativeRuntime::EnumNameEntry, 5> Names = {{
            {0, "None"}, {1, "WrongGeneration"}, {2, "OldLife"}, {3, "InvalidResurrection"},
            {4, "InvalidState"}}};
        return ::MphRead::NativeRuntime::ManagedEnumToString(value, Names.data(), Names.size(), false);
    }

    std::uint16_t NetLifecycleTracker::Next(std::uint16_t value) noexcept
    {
        return value == 0xFFFFU ? static_cast<std::uint16_t>(1) : static_cast<std::uint16_t>(value + 1);
    }

    bool NetLifecycleTracker::Newer(std::uint16_t value, std::uint16_t previous) noexcept
    {
        return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value - previous)) > 0;
    }

    bool NetLifecycleTracker::Newer(std::uint64_t value, std::uint64_t previous) noexcept
    {
        return value > previous;
    }

    void NetLifecycleTracker::SetOccupant(std::uint16_t generation)
    {
        _generation = generation;
        ResetLife();
    }

    void NetLifecycleTracker::ResetLife()
    {
        _lifeId = 0;
        _state = _generation == 0 ? NetworkPlayerState::Empty : NetworkPlayerState::WaitingToSpawn;
        _dead = false;
    }

    std::uint16_t NetLifecycleTracker::BeginLife()
    {
        _lifeId = Next(_lifeId);
        _state = NetworkPlayerState::Alive;
        _dead = false;
        return _lifeId;
    }

    LifecycleRejection NetLifecycleTracker::Accept(std::uint16_t generation, std::uint16_t life,
        NetworkPlayerState state, bool& newLife)
    {
        newLife = false;
        if (generation == 0 || generation != _generation)
        {
            return LifecycleRejection::WrongGeneration;
        }
        if (life != _lifeId && (life == 0 || (_lifeId != 0 && !Newer(life, _lifeId))))
        {
            return LifecycleRejection::OldLife;
        }
        if (life == 0 && state != NetworkPlayerState::WaitingToSpawn
            && state != NetworkPlayerState::Spectating)
        {
            return LifecycleRejection::InvalidState;
        }
        if (life == _lifeId && _dead && state == NetworkPlayerState::Alive)
        {
            return LifecycleRejection::InvalidResurrection;
        }
        newLife = life != _lifeId;
        if (newLife)
        {
            _lifeId = life;
            _dead = false;
        }
        // Waiting/spectator packets cannot erase the death tombstone.
        _dead = _dead || state == NetworkPlayerState::Dead;
        _state = state;
        return LifecycleRejection::None;
    }
}
