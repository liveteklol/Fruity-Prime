#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MphRead::Mods
{
    class ThumbnailGenerator
    {
    public:
        ThumbnailGenerator() = delete;
        ThumbnailGenerator(const ThumbnailGenerator&) = delete;
        ThumbnailGenerator& operator=(const ThumbnailGenerator&) = delete;

        static constexpr std::int32_t ThumbnailWidth = 1600;
        static constexpr std::int32_t ThumbnailHeight = 900;

        static std::string CacheDirectory();
        static std::string PathFor(const std::string& roomKey);
        static bool Exists(const std::string& roomKey);
        static std::vector<std::string> MultiplayerRooms();
        static bool FirstHuntAvailable();
        static std::vector<std::string> MissingThumbnails();
        // A Quake 3 level's preview is the levelshot its .pk3 carries
        // (levelshots/<map>.jpg or .tga), cropped to the launcher's shape;
        // only a level without one is rendered, once it is built, and one
        // that renders nothing is not tried again until it is rebuilt.
        static void EnsureCustomPreviews(std::function<void(const std::string&)> report = {});
        // Whether EnsureCustomPreviews is at work (the launcher shows it).
        [[nodiscard]] static bool RefreshingPreviews() noexcept;
        // The view through each portal of a Quake 3 level, rendered from its
        // camera, and the room built again to show them.
        static void EnsurePortalViews(std::function<void(const std::string&)> report = {});
        static constexpr std::int32_t PortalViewSize = 256;
        static void EnsureCacheDirectory();
    };
}
