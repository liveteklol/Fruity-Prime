#pragma once

#include "Mods/Network/Domain/ShotKey.hpp"
#include "Mods/Combat/BeamObstacleTrace.hpp"

namespace MphRead::Mods::Network
{
    // One bucket per launch frame/slot; pellets and ricochet children share the
    // wire identity. An ambiguous unmatched claim needs an authority hit.
    class MuzzleObstructionHistory final
    {
    public:
        static constexpr std::size_t Slots = 8;
        // 510 flight steps + 120 rewind + 124 claim age + 144 arbitration < 1024.
        static constexpr std::size_t Depth = 1024;

        void Record(const ShotKey& key, BeamType beam, const Combat::BeamObstacleHit& hit,
            std::uint32_t now) noexcept
        {
            if (!Valid(key, beam)) return;
            auto& entry = _entries[static_cast<std::size_t>(key.ShooterSlot)][key.LaunchFrame % Depth];
            if (!entry.Live || entry.Key != key) entry = Entry{};
            entry.Live = true;
            entry.Key = key;
            entry.Weapons |= 1U << static_cast<unsigned>(beam);
            entry.Position = hit.Collision.Position;
            entry.Plane = hit.Collision.Plane;
            entry.Kind = hit.Kind;
            entry.RecordedAt = now;
        }

        [[nodiscard]] bool Contains(const ShotKey& key, BeamType beam, std::uint32_t now) const noexcept
        {
            if (!Valid(key, beam)) return false;
            const auto& entry = _entries[static_cast<std::size_t>(key.ShooterSlot)][key.LaunchFrame % Depth];
            return entry.Live && entry.Key == key && now - entry.RecordedAt < Depth
                && (entry.Weapons & (1U << static_cast<unsigned>(beam))) != 0;
        }

        void RecordDescendant(const ShotKey& key, BeamType beam, std::uint32_t now) noexcept
        {
            if (!Valid(key, beam)) return;
            auto& entry = _entries[static_cast<std::size_t>(key.ShooterSlot)][key.LaunchFrame % Depth];
            if (entry.Live && entry.Key == key && now - entry.RecordedAt < Depth)
            {
                entry.Weapons |= 1U << static_cast<unsigned>(beam);
                entry.RecordedAt = now;
            }
        }

        void ForgetSlot(std::int32_t slot) noexcept
        {
            if (slot >= 0 && slot < static_cast<std::int32_t>(Slots))
                for (auto& entry : _entries[static_cast<std::size_t>(slot)]) entry.Live = false;
        }

        void Reset() noexcept
        {
            for (std::size_t slot = 0; slot < Slots; ++slot) ForgetSlot(static_cast<std::int32_t>(slot));
        }

    private:
        struct Entry final
        {
            ShotKey Key{};
            OpenTK::Mathematics::Vector3 Position{};
            OpenTK::Mathematics::Vector4 Plane{};
            std::uint32_t Weapons = 0;
            std::uint32_t RecordedAt = 0;
            Combat::BeamObstacleKind Kind = Combat::BeamObstacleKind::Map;
            bool Live = false;
        };

        [[nodiscard]] static constexpr bool Valid(const ShotKey& key, BeamType beam) noexcept
        {
            return key.ShooterSlot >= 0 && key.ShooterSlot < static_cast<std::int32_t>(Slots)
                && key.LaunchFrame != 0 && static_cast<unsigned>(beam) < 32U;
        }

        std::array<std::array<Entry, Depth>, Slots> _entries{};
    };
}
