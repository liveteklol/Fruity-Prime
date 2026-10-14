#include "Mods/Network/Replay/DemoRecorder.hpp"
#include "NativeRuntime/System/DateTime.hpp"

#include "Mods/Network/Replay/DemoClip.hpp"
#include "Mods/Network/Replay/DemoFile.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "NativeRuntime/System/Stopwatch.hpp"
#include "Read.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <ios>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    std::unique_ptr<DemoWriter> DemoRecorder::_writer{};
    DemoFrameClock DemoRecorder::_frameClock{};
    std::optional<std::string> DemoRecorder::_currentPath{};

    bool DemoRecorder::IsRecording() noexcept
    {
        return _writer != nullptr;
    }

    std::optional<std::string> DemoRecorder::CurrentPath()
    {
        return _currentPath;
    }

    bool DemoRecorder::Start()
    {
        if (IsRecording() || !NetSession::Active() || DemoPlayback::IsActive())
        {
            return false;
        }

        const std::optional<MatchStatePacket> serverMatch = NetSession::ServerMatch();
        const std::string room = SanitizeFileName(
            serverMatch.has_value() && serverMatch->RoomKey.has_value()
                ? serverMatch->RoomKey.value()
                : std::string("match"));

        const std::string timestamp = ::MphRead::NativeRuntime::DateTimeToString(
            ::MphRead::NativeRuntime::DateTimeNow(), "yyyy-MM-dd_HH-mm-ss");

        const std::string fileName
            = room + "_" + timestamp + std::string(DemoFile::Extension);
        const std::string& exportPath = Paths::Export();
        const std::string path = Paths::Combine(exportPath, "_demos", fileName);

        try
        {
            _writer = std::make_unique<DemoWriter>(path);
        }
        catch (const std::ios_base::failure& ex)
        {
            std::cout << "[demo] could not start recording: " << ex.what() << '\n';
            _writer.reset();
            return false;
        }

        _currentPath = path;
        _frameClock.Start(::MphRead::NativeRuntime::StopwatchGetTimestamp(),
            ::MphRead::NativeRuntime::StopwatchFrequency());
        return true;
    }

    void DemoRecorder::Stop()
    {
        if (_writer != nullptr)
        {
            _writer->Dispose();
        }
        _writer.reset();
        _currentPath.reset();
        _frameClock.Reset();
    }

    void DemoRecorder::Record(ReceivedPacket packet)
    {
        std::span<const std::uint8_t> data{};
        if (packet.Data == nullptr)
        {
            if (packet.Length != 0)
            {
                throw std::out_of_range(
                    "Specified argument was out of the range of valid values.");
            }
        }
        else
        {
            if (packet.Length < 0
                || static_cast<std::size_t>(packet.Length) > packet.Data->size())
            {
                throw std::out_of_range(
                    "Specified argument was out of the range of valid values.");
            }
            data = std::span<const std::uint8_t>(*packet.Data)
                .first(static_cast<std::size_t>(packet.Length));
        }

        DemoClip::Add(data);
        if (_writer == nullptr)
        {
            return;
        }

        _writer->WriteRecord(RecordFrame(data, _frameClock.FrameAt(packet.ArrivedAt)), data);
    }

    void DemoRecorder::RecordOwnIntent(std::int32_t slot, const IntentPacket& intent)
    {
        if (_writer == nullptr && !DemoClip::Active())
        {
            return;
        }

        const std::vector<std::uint8_t> buffer = OwnIntentRecord(slot, intent);
        const std::span<const std::uint8_t> data(buffer);
        DemoClip::Add(data);
        if (_writer != nullptr)
        {
            _writer->WriteRecord(Frame(), data);
        }
    }

    std::vector<std::uint8_t> DemoRecorder::OwnIntentRecord(std::int32_t slot, const IntentPacket& intent)
    {
        // The whole wire form, not FullSize: past it are the shot events and
        // the bombs, and a copy fires nothing its intent does not carry.
        std::vector<std::uint8_t> buffer(2 + intent.WireSize());
        buffer[0] = static_cast<std::uint8_t>(PacketType::SlotIntent);
        buffer[1] = static_cast<std::uint8_t>(slot);
        intent.Write(std::span<std::uint8_t>(buffer).subspan(2));
        return buffer;
    }

    void DemoRecorder::RecordOwnSnapshot(std::span<const std::uint8_t> payload)
    {
        if (_writer == nullptr && !DemoClip::Active())
        {
            return;
        }

        std::vector<std::uint8_t> buffer(1 + payload.size());
        buffer[0] = static_cast<std::uint8_t>(PacketType::Snapshot);
        std::copy(payload.begin(), payload.end(), buffer.begin() + 1);

        const std::span<const std::uint8_t> data(buffer);
        DemoClip::Add(data);
        if (_writer != nullptr)
        {
            _writer->WriteRecord(RecordFrame(data, Frame()), data);
        }
    }

    std::uint32_t DemoRecorder::Frame()
    {
        return _frameClock.FrameAt(::MphRead::NativeRuntime::StopwatchGetTimestamp());
    }

    std::uint32_t DemoRecorder::RecordFrame(
        std::span<const std::uint8_t> packet, std::uint32_t fallbackFrame)
    {
        if (packet.size() < static_cast<std::size_t>(SnapshotHeader::Size + 1)
            || packet[0] != static_cast<std::uint8_t>(PacketType::Snapshot))
        {
            return fallbackFrame;
        }

        const SnapshotHeader snapshot = SnapshotHeader::Read(packet.subspan(1));
        if (!_frameClock.HasAuthorityFrame())
        {
            _frameClock.AnchorAuthorityFrame(snapshot.Frame, fallbackFrame);
        }
        return _frameClock.FrameAtAuthority(snapshot.Frame, fallbackFrame);
    }

    std::string DemoRecorder::SanitizeFileName(std::string name)
    {
#if defined(_WIN32)
        std::replace(name.begin(), name.end(), '"', '_');
        std::replace(name.begin(), name.end(), '<', '_');
        std::replace(name.begin(), name.end(), '>', '_');
        std::replace(name.begin(), name.end(), '|', '_');
        std::replace(name.begin(), name.end(), '\0', '_');
        for (unsigned int value = 1; value < 32; ++value)
        {
            std::replace(name.begin(), name.end(), static_cast<char>(value), '_');
        }
        std::replace(name.begin(), name.end(), ':', '_');
        std::replace(name.begin(), name.end(), '*', '_');
        std::replace(name.begin(), name.end(), '?', '_');
        std::replace(name.begin(), name.end(), '\\', '_');
        std::replace(name.begin(), name.end(), '/', '_');
#else
        std::replace(name.begin(), name.end(), '\0', '_');
        std::replace(name.begin(), name.end(), '/', '_');
#endif
        return name;
    }
}
