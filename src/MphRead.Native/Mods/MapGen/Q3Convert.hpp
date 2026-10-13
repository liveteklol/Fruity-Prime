#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::MapGen
{
    class MapDefinition;
    class Q3Bsp;

    class Q3Convert final
    {
    public:
        // Quake units to one of this game's: a Quake player stands 56 units
        // tall and a hunter 1.6, so at 35 a level is the size it was built
        // for -- its stairs, doorways and jumps -- and a run at 320 units a
        // second is 9.1 here, against a hunter's 7.2.
        static constexpr float UnitsPerUnit = 35.0F;

        // Bumped whenever a translated level comes out differently, so the
        // rooms already built from one are built again (CustomRooms).
        static constexpr std::int32_t TranslatorVersion = 29;

        // A Quake 3 level (mapName, one of Q3Bsp::ListMaps) inside a .pk3 or
        // .zip, read as it is: a room whose geometry, art, pickups, starts,
        // jump pads and teleporters come out of the level every time it is
        // built. Nothing is written; the room is named after the level.
        [[nodiscard]] static std::shared_ptr<MapDefinition> Translate(
            const std::string& archive,
            const std::string& mapName);

        [[nodiscard]] static float AutoScale(float widestExtent);
        [[nodiscard]] static float WidestExtent(Q3Bsp* bsp);

        Q3Convert() = delete;
        Q3Convert(const Q3Convert&) = delete;
        Q3Convert& operator=(const Q3Convert&) = delete;

    private:
        static void Bounds(
            Q3Bsp* bsp,
            std::shared_ptr<std::vector<float>>& min,
            std::shared_ptr<std::vector<float>>& max,
            bool sky);

        [[nodiscard]] static std::int32_t ScaleFactor(
            const std::vector<float>* min,
            const std::vector<float>* max,
            float unit);

        [[nodiscard]] static std::string Title(const std::string& message);

        static void AddSpawns(
            MapDefinition* definition,
            Q3Bsp* bsp,
            float unit);

        [[nodiscard]] static float Round(float value) noexcept;

        [[nodiscard]] static std::shared_ptr<std::vector<float>> ParseVector(
            const std::string& value);
    };
}
