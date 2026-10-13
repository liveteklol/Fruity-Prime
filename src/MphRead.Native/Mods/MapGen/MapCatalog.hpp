#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::MapGen
{
    // The maps FruityPrime's server offers: the .pk3 packs it holds (index.json
    // beside them lists each with its size, SHA-256 and levels,
    // tools/fp-maps-index.py), downloaded into the map folder's "downloaded"
    // folder. Never on their own at start: when the player asks (the Download
    // maps page, -mapsync) or when a server plays a level this device lacks
    // (EnsureRoom).
    class MapCatalog final
    {
    public:
        static constexpr const char* BaseUrl = "https://net.livetek.fr/fruityprime/maps/";

        // Downloads each pack the map folder lacks or holds another copy of,
        // telling progress the pack and how many are done of how many to
        // get. How many arrived. Only for -mapsync: nothing calls it at start.
        // FP_MAPS_URL points it at another copy of the server (http allowed).
        static std::int32_t Sync(
            const std::function<void(const std::string& pack, std::int32_t done, std::int32_t total)>& progress = {});

        // A server plays this room: whether this device can load it afterwards.
        // Known already (the cartridge's, or one the folder holds), it does
        // nothing; otherwise the pack with that level is fetched, listed and
        // the room added to the game's lists, so no restart is needed. Waits
        // for the download. False when no pack on the server has the level or
        // the pack did not arrive.
        [[nodiscard]] static bool EnsureRoom(const std::string& roomKey);
        // EnsureRoom's download alone, for a thread that must not touch the
        // game's lists: the pack holding this level is on disk afterwards.
        // Listing it is EnsureRoom's, on the thread that loads rooms.
        [[nodiscard]] static bool FetchForRoom(const std::string& roomKey);

        // One pack on the server, as the launcher's Download maps page shows
        // it: its levels, each level's picture (a file name beside the
        // packs, or none) and whether the map folder has it already.
        struct Pack
        {
            std::string File;
            std::int64_t Size = 0;
            std::string Sha256;
            std::vector<std::string> Levels;
            std::vector<std::pair<std::string, std::string>> Previews;
            bool Installed = false;
        };
        // The server's packs; nothing when it cannot be reached.
        [[nodiscard]] static std::optional<std::vector<Pack>> List();
        // Fetches one pack into the map folder's "downloaded" folder, checked
        // against its size and SHA-256. Whether it arrived.
        [[nodiscard]] static bool Download(const Pack& pack);
        // A level's picture, fetched once into a cache folder: its path, or
        // nothing.
        [[nodiscard]] static std::optional<std::string> Preview(const std::string& picture);

        MapCatalog() = delete;
    };
}
