#pragma once

#include "NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/DateTime.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MphRead::Mods::Network
{

    namespace Detail
    {
        using DemoLibraryIOException = ::System::IO::IOException;

        using DemoLibraryUnauthorizedAccessException = ::System::UnauthorizedAccessException;

        // Pair-local runtime closure for the System.IO and current-culture
        // services used by DemoLibrary.cs. These operations intentionally keep
        // enumeration and FileInfo metadata lazy so the C# evaluation order,
        // exception filtering, and partial-list behavior remain observable.
        struct DemoLibraryFileEnumeratorHandle;
        struct DemoLibraryFileInfoHandle;

        [[nodiscard]] std::string DemoLibraryGetFullPath(const std::string& path);
        [[nodiscard]] std::string DemoLibraryGetFileName(const std::string& path);
        [[nodiscard]] std::string DemoLibraryGetFileNameWithoutExtension(const std::string& path);
        [[nodiscard]] bool DemoLibraryDirectoryExists(const std::string& path);

        [[nodiscard]] std::shared_ptr<DemoLibraryFileEnumeratorHandle>
            DemoLibraryEnumerateFiles(const std::string& directory, const std::string& pattern);
        [[nodiscard]] bool DemoLibraryFileEnumeratorMoveNext(
            const std::shared_ptr<DemoLibraryFileEnumeratorHandle>& enumerator);
        [[nodiscard]] std::string DemoLibraryFileEnumeratorCurrent(
            const std::shared_ptr<DemoLibraryFileEnumeratorHandle>& enumerator);
        void DemoLibraryFileEnumeratorDispose(
            const std::shared_ptr<DemoLibraryFileEnumeratorHandle>& enumerator);

        [[nodiscard]] std::shared_ptr<DemoLibraryFileInfoHandle>
            DemoLibraryCreateFileInfo(const std::string& path);
        [[nodiscard]] std::string DemoLibraryFileInfoName(
            const std::shared_ptr<DemoLibraryFileInfoHandle>& info);
        [[nodiscard]] ::MphRead::NativeRuntime::ManagedDateTime DemoLibraryFileInfoLastWriteTime(
            const std::shared_ptr<DemoLibraryFileInfoHandle>& info);
        [[nodiscard]] std::int64_t DemoLibraryFileInfoLength(
            const std::shared_ptr<DemoLibraryFileInfoHandle>& info);

        [[nodiscard]] std::string DemoLibraryFormatCurrentCultureDateTime(
            ::MphRead::NativeRuntime::ManagedDateTime value, std::string_view format);
        [[nodiscard]] std::string DemoLibraryFormatCurrentCultureInt64(std::int64_t value);
    }

    // C# internal readonly struct DemoRecording. This remains a value type in
    // Native code; copying a DemoRecording copies its complete value and there
    // are no mutating property setters.
    struct DemoRecording final
    {
        DemoRecording() = default;
        DemoRecording(std::string path, std::string room,
            ::MphRead::NativeRuntime::ManagedDateTime recorded, std::int64_t bytes);

        [[nodiscard]] const std::string& Path() const noexcept;
        [[nodiscard]] const std::string& Room() const noexcept;
        [[nodiscard]] ::MphRead::NativeRuntime::ManagedDateTime Recorded() const noexcept;
        [[nodiscard]] std::int64_t Bytes() const noexcept;
        [[nodiscard]] std::string FileName() const;

    private:
        std::string _path{};
        std::string _room{};
        ::MphRead::NativeRuntime::ManagedDateTime _recorded{};
        std::int64_t _bytes = 0;
    };

    // C# internal static class DemoLibrary.
    class DemoLibrary final
    {
    public:
        DemoLibrary() = delete;
        DemoLibrary(const DemoLibrary&) = delete;
        DemoLibrary& operator=(const DemoLibrary&) = delete;

        [[nodiscard]] static std::string Directory();
        [[nodiscard]] static std::shared_ptr<const std::vector<DemoRecording>> List();
        [[nodiscard]] static std::string Describe(const DemoRecording& demo);

    private:
        [[nodiscard]] static std::pair<std::string, std::optional<::MphRead::NativeRuntime::ManagedDateTime>>
            ReadName(const std::string& fileName);
        [[nodiscard]] static std::string Size(std::int64_t bytes);
    };
}
