#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    enum class NetworkPlayerState : std::uint8_t
    {
        Empty, WaitingToSpawn, Alive, Dead, Spectating
    };

    enum class LifecycleRejection : std::uint8_t
    {
        None, WrongGeneration, OldLife, InvalidResurrection, InvalidState
    };

    // NetworkPlayerState.ToString() / LifecycleRejection.ToString().
    [[nodiscard]] std::string ToString(NetworkPlayerState value);
    [[nodiscard]] std::string ToString(LifecycleRejection value);

    // Protocol invariants, independent of rendering and the game clock.
    class NetLifecycleTracker final
    {
    public:
        [[nodiscard]] std::uint16_t Generation() const noexcept { return _generation; }
        [[nodiscard]] std::uint16_t LifeId() const noexcept { return _lifeId; }
        [[nodiscard]] NetworkPlayerState State() const noexcept { return _state; }

        // Serial-number arithmetic: zero is reserved for an unassigned identity.
        [[nodiscard]] static std::uint16_t Next(std::uint16_t value) noexcept;
        [[nodiscard]] static bool Newer(std::uint16_t value, std::uint16_t previous) noexcept;
        [[nodiscard]] static bool Newer(std::uint64_t value, std::uint64_t previous) noexcept;
        [[nodiscard]] static bool Newer(std::uint32_t value, std::uint32_t previous) noexcept;

        void SetOccupant(std::uint16_t generation);
        void ResetLife();
        std::uint16_t BeginLife();
        [[nodiscard]] LifecycleRejection Accept(std::uint16_t generation, std::uint16_t life,
            NetworkPlayerState state, bool& newLife);

    private:
        std::uint16_t _generation = 0;
        std::uint16_t _lifeId = 0;
        NetworkPlayerState _state = NetworkPlayerState::Empty;
        bool _dead = false;
    };
}
