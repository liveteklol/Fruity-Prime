#pragma once

#include "../../NativeRuntime/System/Exceptions.hpp"
#include "BuildVersion.hpp"
#include "SyncHttp.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace MphRead::Mods::Update
{
    using ArgumentNullException = ::System::ArgumentNullException;

    using InvalidOperationException = ::System::InvalidOperationException;

    struct UpdateInfo;

    namespace Detail
    {
        template <typename T>
        class InitOnlyProperty final
        {
        public:
            InitOnlyProperty() = default;

            template <typename U>
                requires std::constructible_from<T, U&&>
            InitOnlyProperty(U&& value)
                : _value(std::forward<U>(value))
            {
            }

            InitOnlyProperty(const InitOnlyProperty&) = default;
            InitOnlyProperty(InitOnlyProperty&&) noexcept(
                std::is_nothrow_move_constructible_v<T>) = default;

            [[nodiscard]] const T& Get() const noexcept
            {
                return _value;
            }

            [[nodiscard]] operator const T&() const noexcept
            {
                return _value;
            }

        private:
            InitOnlyProperty& operator=(const InitOnlyProperty&) = default;
            InitOnlyProperty& operator=(InitOnlyProperty&&) noexcept(
                std::is_nothrow_move_assignable_v<T>) = default;

            T _value{};
            friend struct ::MphRead::Mods::Update::UpdateInfo;
        };
    }

    struct UpdateInfo
    {
        Detail::InitOnlyProperty<std::optional<std::string>> Tag;
        Detail::InitOnlyProperty<std::optional<MphRead::Mods::Update::Version>> Version;
        Detail::InitOnlyProperty<std::optional<std::string>> AssetName;
        Detail::InitOnlyProperty<std::optional<std::string>> AssetUrl;
        Detail::InitOnlyProperty<std::int64_t> AssetSize;
        Detail::InitOnlyProperty<std::optional<std::string>> PageUrl;
        Detail::InitOnlyProperty<std::optional<std::string>> Notes;
    };

    class UpdateCheck final
    {
    public:
        [[nodiscard]] static std::optional<std::string> LastReason();

        [[nodiscard]] static std::optional<UpdateInfo> Latest(
            CancellationToken cancel = nullptr);

        // The latest release as GitHub describes it, or nullopt with
        // LastReason saying why.
        [[nodiscard]] static std::optional<std::string> FetchLatest(
            CancellationToken cancel = nullptr);

        // The dedicated-server package for this machine, out of the latest
        // release, or nullopt.
        [[nodiscard]] static std::optional<UpdateInfo> ServerAsset(
            CancellationToken cancel = nullptr);
        [[nodiscard]] static std::optional<UpdateInfo> ServerAsset(std::string_view json);

        // What a running dedicated server or directory updates to: a release
        // newer than this build, carrying the server package for this machine
        // rather than the player's one. nullopt with LastReason otherwise.
        [[nodiscard]] static std::optional<UpdateInfo> LatestServer(
            CancellationToken cancel = nullptr);

        // Which server package this machine would run, or "" where none is
        // published.
        [[nodiscard]] static std::string ServerRid();
        // The binary inside a server package for this machine.
        [[nodiscard]] static std::string ServerBinaryName();

        [[nodiscard]] static std::optional<UpdateInfo> Parse(
            std::string_view json,
            std::optional<Version> installed = std::nullopt);

        [[nodiscard]] static std::optional<UpdateInfo> Parse(
            std::nullptr_t json,
            std::optional<Version> installed = std::nullopt);

        inline static constexpr std::string_view ReleasesPage =
            "https://github.com/liveteklol/Fruity-Prime/releases";

        [[nodiscard]] static bool IsServerBuild() noexcept;
        [[nodiscard]] static std::string Rid();
        [[nodiscard]] static std::string PackageSuffix();
        [[nodiscard]] static std::string BinaryName();

        UpdateCheck() = delete;
        UpdateCheck(const UpdateCheck&) = delete;
        UpdateCheck& operator=(const UpdateCheck&) = delete;

    private:
        static void SetLastReason(std::optional<std::string> reason);
    };
}
