#include "Mods/Network/Replay/DemoClip.hpp"

#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/DateTime.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "Read.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include <algorithm>
#include <cstddef>
#include <ios>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace MphRead::Mods::Network
{
    namespace
    {
        constexpr std::int32_t ClipLengths[]{ 5, 10, 15 };
    }

    std::int32_t DemoClip::_seconds = 10;
    std::deque<DemoRecord> DemoClip::_records{};
    std::int64_t DemoClip::_bytes = 0;

    std::span<const std::int32_t> DemoClip::Lengths() noexcept
    {
        return ClipLengths;
    }

    std::int32_t DemoClip::Seconds() noexcept
    {
        return _seconds;
    }

    void DemoClip::Seconds(std::int32_t value) noexcept
    {
        _seconds = value;
    }

    bool DemoClip::Active()
    {
        return _seconds > 0
            && NetSession::Active()
            && !DemoPlayback::IsActive();
    }

    double DemoClip::Held()
    {
        if (_records.empty())
        {
            return 0.0;
        }

        std::uint32_t first = 0;
        for (DemoRecord record : _records)
        {
            first = record.Frame;
            break;
        }

        const std::uint32_t age = NetSession::NetFrame() - first;
        return std::max(0.0, static_cast<double>(age) / 60.0);
    }

    void DemoClip::Add(std::span<const std::uint8_t> data)
    {
        if (!Active()
            || data.empty()
            || data.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
        {
            return;
        }

        // C# evaluates constructor arguments from left to right: capture the
        // frame before materializing data.ToArray().
        const std::uint32_t frame = NetSession::NetFrame();
        auto copy = std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.end());
        _records.emplace_back(frame, std::move(copy));
        _bytes += static_cast<std::int64_t>(data.size());
        Trim();
    }

    void DemoClip::Trim()
    {
        // C# evaluates Seconds * 60 as unchecked Int32 arithmetic, casts that
        // wrapped bit pattern to UInt32, then adds Slack as unchecked UInt32.
        // Doing the multiplication in UInt32 preserves the same modulo-2^32
        // result without invoking signed-overflow UB in C++.
        const std::uint32_t window
            = static_cast<std::uint32_t>(_seconds) * std::uint32_t{60} + Slack;
        const std::uint32_t now = NetSession::NetFrame();

        while (!_records.empty())
        {
            DemoRecord oldest = _records.front();
            const bool tooOld = now >= oldest.Frame && now - oldest.Frame > window;
            if (!tooOld && _bytes <= MaxBytes)
            {
                break;
            }

            _bytes -= static_cast<std::int64_t>(oldest.Data->size());
            _records.pop_front();
        }
    }

    void DemoClip::Purge()
    {
        _records.clear();
        _bytes = 0;
    }

    std::optional<std::string> DemoClip::Save()
    {
        if (_records.empty())
        {
            return std::nullopt;
        }

        const std::optional<std::string> roomKey = (NetSession::ServerMatch().has_value()
            ? std::optional<std::string>(NetSession::ServerMatch()->RoomKey)
            : std::nullopt);
        std::string room = roomKey.has_value() ? *roomKey : "match";

        for (char bad : NativeRuntime::PathGetInvalidFileNameChars())
        {
            std::replace(room.begin(), room.end(), bad, '_');
        }
        std::replace(room.begin(), room.end(), ' ', '_');

        const std::string stamp = room + "_clip_"
            + NativeRuntime::DateTimeToString(NativeRuntime::DateTimeNow(), "yyyy-MM-dd_HH-mm-ss");

        // Keep C# argument evaluation order explicit: Paths.Export is read
        // before the third Paths.Combine argument is built.
        const std::string& exportPath = Paths::Export();
        const std::string fileName = stamp + std::string(DemoFile::Extension);
        std::string path = Paths::Combine(exportPath, "_demos", fileName);

        for (std::int32_t i = 2; NativeRuntime::FileExists(path) && i < 1000; ++i)
        {
            const std::string& nextExportPath = Paths::Export();
            const std::string nextFileName
                = stamp + "_" + ::MphRead::NativeRuntime::ToString(i)
                + std::string(DemoFile::Extension);
            path = Paths::Combine(nextExportPath, "_demos", nextFileName);
        }

        try
        {
            DemoWriter writer(path);
            try
            {
                std::uint32_t start = 0;
                bool first = true;
                for (DemoRecord record : _records)
                {
                    if (first)
                    {
                        start = record.Frame;
                        first = false;
                    }

                    writer.WriteRecord(
                        record.Frame >= start ? record.Frame - start : std::uint32_t{0},
                        std::span<const std::uint8_t>(*record.Data));
                }
            }
            catch (...)
            {
                // C# using-declaration disposal runs during exceptional exit.
                // If Dispose itself throws, that exception replaces the active
                // one, matching finally semantics.
                writer.Dispose();
                throw;
            }
            writer.Dispose();
        }
        catch (const std::ios_base::failure& ex)
        {
            NativeRuntime::ConsoleWriteLine(
                std::string("[demo] could not save the clip: ") + ex.what());
            return std::nullopt;
        }

        return path;
    }
}
