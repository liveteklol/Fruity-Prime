#pragma once

#include "UpdateCheck.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace MphRead::Mods::Update
{
    class DesktopUpdate final
    {
    public:
        inline static constexpr std::string_view ApplyFlag = "applyupdate";
        inline static constexpr std::string_view RelaunchSeparator = "--relaunch";

        [[nodiscard]] static std::string StagedBuildPath();
        [[nodiscard]] static std::optional<std::string> LastError();
        [[nodiscard]] static bool Supported();

        [[nodiscard]] static bool Stage(
            UpdateInfo update,
            const std::function<void(float)>& progress = {},
            CancellationToken cancel = nullptr);

        [[nodiscard]] static bool Launch(
            std::optional<std::span<const std::string>> relaunchArgs = std::nullopt);

        [[nodiscard]] static std::int32_t Apply(
            const std::string& target,
            std::int32_t waitFor,
            std::optional<std::span<const std::string>> relaunchArgs = std::nullopt);

        static void Clean();

        DesktopUpdate() = delete;
        DesktopUpdate(const DesktopUpdate&) = delete;
        DesktopUpdate& operator=(const DesktopUpdate&) = delete;

    private:
        [[nodiscard]] static std::string Staging();
        [[nodiscard]] static std::string StagedBuild();
        // Where the binary sits inside an unpacked package, relative to its root:
        // "" at the top, "bin" for the Windows and Linux installs, the .app bundle
        // on macOS. Nothing when the package holds no binary.
        [[nodiscard]] static std::optional<std::string> BinaryDirectory(const std::string& root);
        static void SetLastError(std::optional<std::string> value);
        static void WaitForExit(std::int32_t pid);
        static void Copy(const std::string& source, const std::string& target);
        static void CopyWithRetries(const std::string& from, const std::string& to);
        static void MakeExecutable(const std::string& path);
    };
}
