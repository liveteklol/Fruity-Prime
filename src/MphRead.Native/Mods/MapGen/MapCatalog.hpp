#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace MphRead::Mods::MapGen
{
    // The maps every client gets: the .pk3 packs on FruityPrime's server
    // (index.json beside them lists each with its size, SHA-256 and levels,
    // tools/fp-maps-index.py), downloaded into the map folder's "downloaded"
    // folder when the folder lacks them, in the background as the game
    // starts (CustomRooms::StartGenerating, AndroidMaps::EnsureBuilt).
    class MapCatalog final
    {
    public:
        static constexpr const char* BaseUrl = "https://net.livetek.fr/fruityprime/maps/";

        // Downloads each pack the map folder lacks or holds another copy of,
        // telling progress the pack and how many are done of how many to
        // get. How many arrived. FP_NO_MAP_DOWNLOAD turns it off;
        // FP_MAPS_URL points it at another copy of the server (http allowed).
        static std::int32_t Sync(
            const std::function<void(const std::string& pack, std::int32_t done, std::int32_t total)>& progress = {});

        MapCatalog() = delete;
    };
}
