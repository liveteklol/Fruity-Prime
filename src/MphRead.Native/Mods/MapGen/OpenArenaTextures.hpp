#pragma once

#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::MapGen
{
    // OpenArena's art for the Quake 3 textures a level leans on without
    // carrying them, fetched from net.livetek.fr a file at a time
    // (tools/openarena-pack.py builds what is published there) and kept, so
    // that a level reads as Quake drew it wherever OpenArena has a picture
    // of it. Where it has none, or nothing can be reached, the cartridge's
    // own art stands in as before (MphTextures).
    class OpenArenaTextures final
    {
    public:
        static constexpr const char* BaseUrl = "https://net.livetek.fr/fruityprime/openarena/";

        OpenArenaTextures() = delete;

        // Fetches, of what the shaders `shaders` draw with, what the level's
        // own archive `level` lacks and has not been fetched before, and
        // OpenArena's shader scripts. Quiet, and harmless, when the server
        // cannot be reached.
        static void Fetch(const std::string& level, const std::vector<std::string>& shaders);

        // The archive of everything fetched so far, read after a level's own;
        // none before anything has been.
        [[nodiscard]] static std::optional<std::string> Pack();

        // "" until the server's index has been seen, then what of it: a
        // translated level built without it is built again once it is there.
        [[nodiscard]] static std::string Stamp();

    private:
        [[nodiscard]] static std::string Directory();
    };
}
