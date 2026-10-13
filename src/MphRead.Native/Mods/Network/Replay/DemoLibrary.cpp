#include "Mods/Network/Replay/DemoLibrary.hpp"

#include "NativeRuntime/System/Exceptions.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/DateTime.hpp"
#include "NativeRuntime/System/IO.hpp"

#include "Mods/Network/Replay/DemoFile.hpp"
#include "Read.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <iostream>
#include <system_error>
#include "NativeRuntime/System/Sort.hpp"

using ::MphRead::NativeRuntime::ManagedSort;
namespace
{
    constexpr std::int64_t TicksPerSecond = 10'000'000;
    constexpr std::int64_t TicksPerMinute = TicksPerSecond * 60;
    constexpr std::int64_t TicksPerHour = TicksPerMinute * 60;
    constexpr std::int64_t TicksPerDay = TicksPerHour * 24;

    [[nodiscard]] constexpr bool IsLeapYear(std::int32_t year) noexcept
    {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    [[nodiscard]] bool ParseDigits(std::string_view text, std::size_t offset,
        std::size_t count, std::int32_t& value) noexcept
    {
        value = 0;
        if (offset > text.size() || count > text.size() - offset)
        {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            const char ch = text[offset + i];
            if (ch < '0' || ch > '9')
            {
                return false;
            }
            value = value * 10 + static_cast<std::int32_t>(ch - '0');
        }
        return true;
    }

    [[nodiscard]] bool TryParseInvariantTimestamp(
        std::string_view stamp, ::MphRead::NativeRuntime::ManagedDateTime& parsed) noexcept
    {
        // Exact C# format: yyyy-MM-dd_HH-mm-ss, DateTimeStyles.None.
        if (stamp.size() != 19
            || stamp[4] != '-' || stamp[7] != '-' || stamp[10] != '_'
            || stamp[13] != '-' || stamp[16] != '-')
        {
            return false;
        }

        std::int32_t year = 0;
        std::int32_t month = 0;
        std::int32_t day = 0;
        std::int32_t hour = 0;
        std::int32_t minute = 0;
        std::int32_t second = 0;
        if (!ParseDigits(stamp, 0, 4, year)
            || !ParseDigits(stamp, 5, 2, month)
            || !ParseDigits(stamp, 8, 2, day)
            || !ParseDigits(stamp, 11, 2, hour)
            || !ParseDigits(stamp, 14, 2, minute)
            || !ParseDigits(stamp, 17, 2, second))
        {
            return false;
        }
        if (year < 1 || year > 9999 || month < 1 || month > 12
            || hour > 23 || minute > 59 || second > 59)
        {
            return false;
        }

        static constexpr std::array<std::int32_t, 13> DaysToMonth365 = {
            0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
        };
        static constexpr std::array<std::int32_t, 13> DaysToMonth366 = {
            0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366
        };
        const auto& days = IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;
        const std::int32_t daysInMonth = days[static_cast<std::size_t>(month)]
            - days[static_cast<std::size_t>(month - 1)];
        if (day < 1 || day > daysInMonth)
        {
            return false;
        }

        const std::int64_t y = static_cast<std::int64_t>(year - 1);
        const std::int64_t totalDays = y * 365 + y / 4 - y / 100 + y / 400
            + days[static_cast<std::size_t>(month - 1)] + day - 1;
        const std::int64_t ticks = totalDays * TicksPerDay
            + static_cast<std::int64_t>(hour) * TicksPerHour
            + static_cast<std::int64_t>(minute) * TicksPerMinute
            + static_cast<std::int64_t>(second) * TicksPerSecond;
        parsed = ::MphRead::NativeRuntime::ManagedDateTime(ticks);
        return true;
    }

    void WriteListFailure(const std::exception& ex)
    {
        // The C# catch handler evaluates Directory again while constructing
        // the message; if that property now throws, that exception propagates.
        const std::string directory = MphRead::Mods::Network::DemoLibrary::Directory();
        std::cout << "[demo] could not list " << directory << ": " << ex.what() << '\n';
    }
}

namespace MphRead::Mods::Network
{
    DemoRecording::DemoRecording(std::string path, std::string room,
        ::MphRead::NativeRuntime::ManagedDateTime recorded, std::int64_t bytes)
        : _path(std::move(path)),
          _room(std::move(room)),
          _recorded(recorded),
          _bytes(bytes)
    {
    }

    const std::string& DemoRecording::Path() const noexcept
    {
        return _path;
    }

    const std::string& DemoRecording::Room() const noexcept
    {
        return _room;
    }

    ::MphRead::NativeRuntime::ManagedDateTime DemoRecording::Recorded() const noexcept
    {
        return _recorded;
    }

    std::int64_t DemoRecording::Bytes() const noexcept
    {
        return _bytes;
    }

    std::string DemoRecording::FileName() const
    {
        return NativeRuntime::PathGetFileName(_path);
    }

    std::string DemoLibrary::Directory()
    {
        return NativeRuntime::PathGetFullPath(Paths::Combine(Paths::Export(), "_demos"));
    }

    std::shared_ptr<const std::vector<DemoRecording>> DemoLibrary::List()
    {
        auto found = std::make_shared<std::vector<DemoRecording>>();
        try
        {
            const std::string directory = Directory();
            if (!NativeRuntime::DirectoryExists(std::string(directory)))
            {
                return found;
            }

            for (const std::string& path : NativeRuntime::DirectoryEnumerateFilesWithSuffix(
                directory, std::string(DemoFile::Extension)))
            {
                const NativeRuntime::FileInfo info = NativeRuntime::CreateFileInfo(path);
                auto [room, stamp] = ReadName(info.Name);

                // Null-coalescing in C#: a parsed file-name timestamp avoids
                // evaluating FileInfo.LastWriteTime entirely.
                const ::MphRead::NativeRuntime::ManagedDateTime recorded = stamp.has_value()
                    ? *stamp
                    : ::MphRead::NativeRuntime::ManagedDateTime{info.LastWriteTimeTicks, 2};
                found->emplace_back(path, std::move(room), recorded, info.Length);
            }
        }
        catch (const ::System::IO::IOException& ex)
        {
            WriteListFailure(ex);
        }
        catch (const ::System::UnauthorizedAccessException& ex)
        {
            WriteListFailure(ex);
        }

        ManagedSort(*found, [](const DemoRecording& a, const DemoRecording& b)
        {
            const auto left = b.Recorded().Ticks;
            const auto right = a.Recorded().Ticks;
            return left < right ? -1 : (left > right ? 1 : 0);
        });
        return found;
    }

    std::pair<std::string, std::optional<::MphRead::NativeRuntime::ManagedDateTime>>
        DemoLibrary::ReadName(const std::string& fileName)
    {
        std::string name = NativeRuntime::PathGetFileNameWithoutExtension(fileName);
        constexpr std::size_t stampLength = 19;
        if (name.size() < stampLength + 2
            || name[name.size() - (stampLength + 1)] != '_')
        {
            return {std::move(name), std::nullopt};
        }

        const std::string_view stamp(name.data() + name.size() - stampLength, stampLength);
        ::MphRead::NativeRuntime::ManagedDateTime parsed;
        if (!TryParseInvariantTimestamp(stamp, parsed))
        {
            return {std::move(name), std::nullopt};
        }
        name.resize(name.size() - (stampLength + 1));
        return {std::move(name), parsed};
    }

    std::string DemoLibrary::Describe(const DemoRecording& demo)
    {
        return NativeRuntime::DateTimeToString((demo.Recorded()), ("d MMM yyyy, HH:mm"))
            + " — " + Size(demo.Bytes());
    }

    std::string DemoLibrary::Size(std::int64_t bytes)
    {
        if (bytes >= 1024 * 1024)
        {
            const float megabytes = static_cast<float>(bytes) / (1024.0F * 1024.0F);
            return ::MphRead::NativeRuntime::ToStringInvariant(megabytes, "0.0") + " MB";
        }
        if (bytes >= 1024)
        {
            return ::MphRead::NativeRuntime::ToString(bytes / 1024) + " KB";
        }
        return ::MphRead::NativeRuntime::ToString(bytes) + " bytes";
    }
}
