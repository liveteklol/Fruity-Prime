#include "Mods/Network/Replay/DemoInfo.hpp"

#include "Mods/Network/Replay/DemoFile.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <ios>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::FileExists;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedSubtract;

namespace MphRead::Mods::Network
{
    namespace
    {
        template <typename TValue>
        class OrderedPacketDictionary final
        {
        public:
            struct Entry final
            {
                PacketType Key;
                TValue Value;
            };

            bool TryGetValue(PacketType key, TValue& value) const
            {
                for (const Entry& entry : _entries)
                {
                    if (entry.Key == key)
                    {
                        value = entry.Value;
                        return true;
                    }
                }

                value = TValue{};
                return false;
            }

            void Set(PacketType key, TValue value)
            {
                for (Entry& entry : _entries)
                {
                    if (entry.Key == key)
                    {
                        entry.Value = value;
                        return;
                    }
                }

                _entries.push_back(Entry{key, value});
            }

            [[nodiscard]] bool ContainsKey(PacketType key) const
            {
                for (const Entry& entry : _entries)
                {
                    if (entry.Key == key)
                    {
                        return true;
                    }
                }
                return false;
            }

            [[nodiscard]] TValue At(PacketType key) const
            {
                for (const Entry& entry : _entries)
                {
                    if (entry.Key == key)
                    {
                        return entry.Value;
                    }
                }
                throw std::out_of_range("PacketType key was not present.");
            }

            [[nodiscard]] const std::vector<Entry>& Entries() const noexcept
            {
                return _entries;
            }

        private:
            std::vector<Entry> _entries;
        };

        [[nodiscard]] std::int64_t FileLength(const std::string& path)
        {
            const std::uintmax_t length
                = std::filesystem::file_size(MphRead::NativeRuntime::PathFromUtf8(path));
            if (length > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max()))
            {
                throw std::overflow_error("File length does not fit in System.Int64.");
            }
            return static_cast<std::int64_t>(length);
        }

        [[nodiscard]] std::string PacketTypeToString(PacketType type)
        {
            switch (type)
            {
            case PacketType::Hello:
                return "Hello";
            case PacketType::Welcome:
                return "Welcome";
            case PacketType::Intent:
                return "Intent";
            case PacketType::Snapshot:
                return "Snapshot";
            case PacketType::Bye:
                return "Bye";
            case PacketType::Ping:
                return "Ping";
            case PacketType::Pong:
                return "Pong";
            case PacketType::MatchState:
                return "MatchState";
            case PacketType::MapChange:
                return "MapChange";
            case PacketType::Roster:
                return "Roster";
            case PacketType::Identify:
                return "Identify";
            case PacketType::Authority:
                return "Authority";
            case PacketType::SlotIntent:
                return "SlotIntent";
            case PacketType::StatusQuery:
                return "StatusQuery";
            case PacketType::StatusReply:
                return "StatusReply";
            case PacketType::MatchEnd:
                return "MatchEnd";
            case PacketType::MasterHeartbeat:
                return "MasterHeartbeat";
            case PacketType::MasterQuery:
                return "MasterQuery";
            case PacketType::MasterList:
                return "MasterList";
            case PacketType::HostRequest:
                return "HostRequest";
            case PacketType::HostReply:
                return "HostReply";
            case PacketType::Refused:
                return "Refused";
            case PacketType::Chat:
                return "Chat";
            case PacketType::Vote:
                return "Vote";
            case PacketType::VoteState:
                return "VoteState";
            }

            return std::to_string(static_cast<std::uint32_t>(type));
        }

        [[nodiscard]] std::string AlignLeft(const std::string& value, std::size_t width)
        {
            if (value.size() >= width)
            {
                return value;
            }
            return value + std::string(width - value.size(), ' ');
        }

        [[nodiscard]] std::string AlignRight(const std::string& value, std::size_t width)
        {
            if (value.size() >= width)
            {
                return value;
            }
            return std::string(width - value.size(), ' ') + value;
        }

        void WriteLine(const std::string& value)
        {
            std::cout << value << '\n';
            if (!std::cout)
            {
                throw std::ios_base::failure("Console.WriteLine failed.");
            }
        }
    }

    std::int32_t DemoInfo::Print(const std::string& path, bool replay)
    {
        if (!FileExists(path))
        {
            WriteLine("[demo] no such file: " + path);
            return 1;
        }

        std::unique_ptr<DemoReader> reader = DemoReader::Open(path);
        if (reader == nullptr)
        {
            WriteLine("[demo] \"" + path + "\" is not a demo this build can read "
                + "(bad magic, or not format version "
                + std::to_string(static_cast<std::uint32_t>(DemoFile::FormatVersion)) + ")");
            return 1;
        }

        std::int32_t result = 0;
        try
        {
            OrderedPacketDictionary<std::int32_t> counts;
            OrderedPacketDictionary<std::int64_t> bytes;
            std::int64_t records = 0;
            std::int64_t payload = 0;
            std::uint32_t firstFrame = 0;
            std::uint32_t lastFrame = 0;
            std::uint32_t biggestGap = 0;
            std::uint32_t previousFrame = 0;
            bool first = true;

            while (true)
            {
                std::optional<DemoRecord> next = reader->ReadNext();
                if (!next.has_value())
                {
                    break;
                }

                DemoRecord record = *next;
                records = UncheckedAdd(records, 1);
                const std::int32_t dataLength
                    = static_cast<std::int32_t>(record.Data->size());
                payload = UncheckedAdd(payload, dataLength);

                if (first)
                {
                    firstFrame = record.Frame;
                    previousFrame = record.Frame;
                    first = false;
                }

                biggestGap = std::max(biggestGap, record.Frame - previousFrame);
                previousFrame = record.Frame;
                lastFrame = record.Frame;

                if (dataLength > 0)
                {
                    const PacketType type = static_cast<PacketType>((*record.Data)[0]);

                    std::int32_t count = 0;
                    counts.TryGetValue(type, count);
                    counts.Set(type, UncheckedAdd(count, 1));

                    std::int64_t size = 0;
                    bytes.TryGetValue(type, size);
                    bytes.Set(type, UncheckedAdd(size, dataLength));
                }
            }

            const std::int64_t onDisk = FileLength(path);
            const std::uint32_t frames
                = records == 0 ? std::uint32_t{0} : lastFrame - firstFrame + std::uint32_t{1};
            const double seconds = static_cast<double>(frames) / 60.0;

            WriteLine("[demo] " + path);

            const std::uint8_t protocolForDisplay = reader->ProtocolVersion();
            const std::int32_t buildProtocolForDisplay = NetConfig::ProtocolVersion;
            const std::uint8_t protocolForComparison = reader->ProtocolVersion();
            const std::int32_t buildProtocolForComparison = NetConfig::ProtocolVersion;
            std::string protocolLine = "  protocol "
                + std::to_string(static_cast<std::uint32_t>(protocolForDisplay))
                + " (this build: " + std::to_string(buildProtocolForDisplay) + ")";
            if (protocolForComparison != buildProtocolForComparison)
            {
                protocolLine += "  -- MISMATCH";
            }
            WriteLine(protocolLine);

            const std::string secondsText = ::MphRead::NativeRuntime::ToString(seconds, "0.0");
            WriteLine("  " + std::to_string(records) + " record(s) over frames "
                + std::to_string(firstFrame) + "-" + std::to_string(lastFrame)
                + " (" + secondsText + " s at 60 fps)");

            const double denominator = std::max(seconds, 0.001);
            const double ratio = payload > 0
                ? static_cast<double>(payload) / static_cast<double>(onDisk)
                : 0.0;
            const std::string onDiskKiBText
                = ::MphRead::NativeRuntime::ToString(static_cast<double>(onDisk) / 1024.0, "0.0");
            const std::string payloadKiBText
                = ::MphRead::NativeRuntime::ToString(static_cast<double>(payload) / 1024.0, "0.0");
            const std::string ratioText = ::MphRead::NativeRuntime::ToString(ratio, "0.00");
            const std::string diskRateText
                = ::MphRead::NativeRuntime::ToString(static_cast<double>(onDisk) / denominator / 1024.0, "0.0");
            WriteLine("  " + onDiskKiBText + " KiB on disk, "
                + payloadKiBText + " KiB of packets -- " + ratioText + "x, "
                + diskRateText + " KiB/s");
            WriteLine("  longest gap between records: " + std::to_string(biggestGap)
                + " frame(s)");

            for (const OrderedPacketDictionary<std::int32_t>::Entry& entry : counts.Entries())
            {
                const std::string typeText = PacketTypeToString(entry.Key);
                const std::string countText = std::to_string(entry.Value);
                const double rate = static_cast<double>(entry.Value) / denominator;
                const std::string rateText = ::MphRead::NativeRuntime::ToString(rate, "0.0");
                const std::int64_t byteCount = bytes.At(entry.Key);
                const std::string byteText
                    = ::MphRead::NativeRuntime::ToString(static_cast<double>(byteCount) / 1024.0, "0.0");
                WriteLine("  " + AlignLeft(typeText, 14) + " "
                    + AlignRight(countText, 7) + " ("
                    + AlignRight(rateText, 6) + "/s, " + byteText + " KiB)");
            }

            if (!counts.ContainsKey(PacketType::Snapshot))
            {
                WriteLine("  NO SNAPSHOTS -- nothing in this file ever places a player, "
                    "so it will play back as an empty room.");
                result = 1;
            }
            else
            {
                result = replay ? Replay(path) : 0;
            }
        }
        catch (...)
        {
            // The C# using declaration disposes after every exceptional exit.
            // A Dispose exception replaces the active exception, as in finally.
            reader->Dispose();
            throw;
        }

        reader->Dispose();
        return result;
    }

    std::int32_t DemoInfo::Replay(const std::string& path)
    {
        WriteLine("  --- replayed through DemoPlayback ---");

        std::optional<std::uint32_t> firstSnapshotFrame;
        std::optional<std::uint32_t> lastSnapshotFrame;
        std::unique_ptr<DemoReader> scan = DemoReader::Open(path);
        if (scan != nullptr)
        {
            while (true)
            {
                const std::optional<DemoRecord> record = scan->ReadNext();
                if (!record.has_value())
                {
                    break;
                }
                if (record->Data == nullptr || record->Data->empty()
                    || static_cast<PacketType>((*record->Data)[0]) != PacketType::Snapshot)
                {
                    continue;
                }
                if (!firstSnapshotFrame.has_value())
                {
                    firstSnapshotFrame = record->Frame;
                }
                lastSnapshotFrame = record->Frame;
            }
            scan->Dispose();
        }

        if (!DemoPlayback::Join(path))
        {
            const std::optional<std::string> lastError = DemoPlayback::LastError();
            WriteLine("  replay failed: " + lastError.value_or(std::string{}));
            return 1;
        }

        std::int64_t previousSnapshots = NetSession::SnapshotsReceived();
        std::int64_t previousIntents = NetSession::IntentsReceived();
        std::int64_t frames = 0;
        std::int64_t framesWithSnapshot = 0;
        std::int64_t framesWithSeveral = 0;
        std::int64_t gap = 0;
        std::int64_t worstGap = 0;
        std::int64_t intents = 0;
        std::int64_t snapshotWindowFrames = 0;

        while (!DemoPlayback::AtEnd() && frames < 60 * 60 * 30)
        {
            const std::int64_t frame = frames;
            DemoPlayback::PumpFrame();
            NetSession::Update(static_cast<double>(frames) / 60.0);
            frames = UncheckedAdd(frames, 1);

            const std::int64_t snapshots
                = UncheckedSubtract(NetSession::SnapshotsReceived(), previousSnapshots);
            previousSnapshots = NetSession::SnapshotsReceived();

            intents = UncheckedAdd(intents,
                UncheckedSubtract(NetSession::IntentsReceived(), previousIntents));
            previousIntents = NetSession::IntentsReceived();

            if (!firstSnapshotFrame.has_value() || !lastSnapshotFrame.has_value()
                || frame < static_cast<std::int64_t>(firstSnapshotFrame.value())
                || frame > static_cast<std::int64_t>(lastSnapshotFrame.value()))
            {
                continue;
            }
            snapshotWindowFrames = UncheckedAdd(snapshotWindowFrames, 1);

            if (snapshots == 0)
            {
                gap = UncheckedAdd(gap, 1);
                worstGap = std::max(worstGap, gap);
                continue;
            }

            gap = 0;
            framesWithSnapshot = UncheckedAdd(framesWithSnapshot, 1);
            if (snapshots > 1)
            {
                framesWithSeveral = UncheckedAdd(framesWithSeveral, 1);
            }
        }

        DemoPlayback::Stop();
        NetSession::Stop();

        const double percent = snapshotWindowFrames == 0
            ? 0.0
            : 100.0 * static_cast<double>(framesWithSnapshot)
                / static_cast<double>(snapshotWindowFrames);
        WriteLine("  " + std::to_string(frames) + " frame(s) replayed, "
            + std::to_string(intents) + " slot intent(s) applied");
        WriteLine("  " + std::to_string(framesWithSnapshot) + "/"
            + std::to_string(snapshotWindowFrames) + " active snapshot frame(s) covered ("
            + ::MphRead::NativeRuntime::ToString(percent, "0.0") + "%), "
            + std::to_string(framesWithSeveral) + " got more than one");
        WriteLine("  longest run of frames with no snapshot in that window: "
            + std::to_string(worstGap));

        return !firstSnapshotFrame.has_value() || !lastSnapshotFrame.has_value()
            || framesWithSeveral > frames / 20 || worstGap > 10 ? 1 : 0;
    }
}
