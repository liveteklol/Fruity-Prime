#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MphRead
{
    enum class GameMode : std::uint8_t;

    namespace Mods::Network
    {
        class MapRotation;

        class RotationEntry final
        {
        public:
            RotationEntry();

            const std::string RoomKey;
            const GameMode Mode;
            const float TimeLimit;
            const std::int32_t PointGoal;

            [[nodiscard]] std::string ToString() const;

            RotationEntry(const RotationEntry&) = delete;
            RotationEntry& operator=(const RotationEntry&) = delete;
            RotationEntry(RotationEntry&&) = delete;
            RotationEntry& operator=(RotationEntry&&) = delete;

        private:
            RotationEntry(std::string roomKey, GameMode mode, float timeLimit,
                std::int32_t pointGoal);

            friend class MapRotation;
        };

        class MapRotation final
        {
        public:
            MapRotation() = default;

            [[nodiscard]] const std::vector<std::shared_ptr<const RotationEntry>>& Entries() const;
            [[nodiscard]] std::shared_ptr<const RotationEntry> Current() const;
            [[nodiscard]] std::shared_ptr<const RotationEntry> Next() const;
            [[nodiscard]] std::int32_t Index() const;

            [[nodiscard]] static std::shared_ptr<MapRotation> SingleMatch(
                const std::string& roomKey, GameMode mode, float timeLimit,
                std::int32_t pointGoal);

            [[nodiscard]] static std::shared_ptr<MapRotation> FromList(
                const std::vector<std::pair<std::string, GameMode>>& maps, float timeLimit, std::int32_t pointGoal);
            static void WriteList(const std::string& path,
                const std::vector<std::pair<std::string, GameMode>>& maps, float timeLimit, std::int32_t pointGoal);

            void PlayNext(const std::string& roomKey, GameMode mode);
            void ClearPending();
            [[nodiscard]] std::shared_ptr<const RotationEntry> Advance();

            [[nodiscard]] static std::shared_ptr<MapRotation> Load(const std::string& path);
            static void WriteDefault(const std::string& path);
            [[nodiscard]] static std::shared_ptr<MapRotation> LoadOrCreate(const std::string& path);

            MapRotation(const MapRotation&) = delete;
            MapRotation& operator=(const MapRotation&) = delete;
            MapRotation(MapRotation&&) = delete;
            MapRotation& operator=(MapRotation&&) = delete;

        private:
            std::vector<std::shared_ptr<const RotationEntry>> _entries{};
            std::int32_t _index = 0;

            static const std::shared_ptr<const RotationEntry> _fallback;

            std::shared_ptr<const RotationEntry> _pending{};
            std::shared_ptr<const RotationEntry> _override{};
        };
    }
}
