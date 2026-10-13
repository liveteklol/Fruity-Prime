#include "Mods/Network/Replay/DemoFile.hpp"

#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Streams.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"

#include "Utility/Compress.hpp"

#include <algorithm>
#include <ios>
#include <new>
#include <utility>

namespace
{
    void WriteUInt16LittleEndian(std::span<std::uint8_t> destination, std::uint16_t value)
    {
        destination[0] = static_cast<std::uint8_t>(value);
        destination[1] = static_cast<std::uint8_t>(value >> 8);
    }

    void WriteUInt32LittleEndian(std::span<std::uint8_t> destination, std::uint32_t value)
    {
        destination[0] = static_cast<std::uint8_t>(value);
        destination[1] = static_cast<std::uint8_t>(value >> 8);
        destination[2] = static_cast<std::uint8_t>(value >> 16);
        destination[3] = static_cast<std::uint8_t>(value >> 24);
    }

    std::uint16_t ReadUInt16LittleEndian(std::span<const std::uint8_t> source)
    {
        return static_cast<std::uint16_t>(source[0])
            | static_cast<std::uint16_t>(static_cast<std::uint16_t>(source[1]) << 8);
    }

    std::uint32_t ReadUInt32LittleEndian(std::span<const std::uint8_t> source)
    {
        return static_cast<std::uint32_t>(source[0])
            | (static_cast<std::uint32_t>(source[1]) << 8)
            | (static_cast<std::uint32_t>(source[2]) << 16)
            | (static_cast<std::uint32_t>(source[3]) << 24);
    }
}

namespace
{
    // `Path.GetDirectoryName(path)`, which is what Directory.CreateDirectory
    // is given here.
    [[nodiscard]] std::string DirectoryOf(const std::string& path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }
}

namespace MphRead::Mods::Network
{
    DemoWriter::DemoWriter(const std::string& path)
    {
        NativeRuntime::DirectoryCreateDirectory(DirectoryOf(path));
        _stream = NativeRuntime::FileStreamOpenCreateWriteShareRead(path);
        _stream->Write(DemoFile::Magic);
        _stream->WriteByte(DemoFile::FormatVersion);
        _stream->WriteByte(static_cast<std::uint8_t>(NetConfig::ProtocolVersion));
        _stream->Flush();
        _deflate = NativeRuntime::DeflateStreamCompress(_stream, true);
    }

    void DemoWriter::WriteRecord(std::uint32_t frame, std::span<const std::uint8_t> data)
    {
        if (frame < _lastFrame)
        {
            frame = _lastFrame;
        }
        const std::uint32_t delta = frame - _lastFrame;
        _lastFrame = frame;

        std::size_t at = 0;
        if (delta < DemoFile::LongGap)
        {
            _header[at++] = static_cast<std::uint8_t>(delta);
        }
        else
        {
            _header[at++] = DemoFile::LongGap;
            WriteUInt32LittleEndian(std::span<std::uint8_t>(_header).subspan(at), delta);
            at += 4;
        }
        WriteUInt16LittleEndian(std::span<std::uint8_t>(_header).subspan(at),
            static_cast<std::uint16_t>(data.size()));
        at += 2;

        _deflate->Write(std::span<const std::uint8_t>(_header).first(at));
        _deflate->Write(data);
        if (frame - _lastFlushFrame >= FlushIntervalFrames)
        {
            _lastFlushFrame = frame;
            _deflate->Flush();
            _stream->Flush();
        }
    }

    void DemoWriter::Dispose()
    {
        _deflate->Dispose();
        _stream->Dispose();
    }

    DemoRecord::DemoRecord()
        : Frame(0), Data(nullptr)
    {
    }

    DemoRecord::DemoRecord(std::uint32_t frame,
        std::shared_ptr<std::vector<std::uint8_t>> data)
        : Frame(frame), Data(std::move(data))
    {
    }

    DemoRecord& DemoRecord::operator=(const DemoRecord& other)
    {
        if (this != &other)
        {
            this->~DemoRecord();
            new (this) DemoRecord(other);
        }
        return *this;
    }

    DemoRecord& DemoRecord::operator=(DemoRecord&& other) noexcept
    {
        if (this != &other)
        {
            this->~DemoRecord();
            new (this) DemoRecord(std::move(other));
        }
        return *this;
    }

    std::unique_ptr<DemoReader> DemoReader::Open(const std::string& path)
    {
        std::shared_ptr<::MphRead::NativeRuntime::Stream> stream;
        try
        {
            stream = NativeRuntime::FileStreamOpenReadShareRead(path);
            std::array<std::uint8_t, static_cast<std::size_t>(DemoFile::HeaderSize)> header{};
            if (stream->ReadAtLeast(header) < header.size())
            {
                stream->Dispose();
                return nullptr;
            }
            if (!std::equal(DemoFile::Magic.begin(), DemoFile::Magic.end(), header.begin())
                || header[4] != DemoFile::FormatVersion)
            {
                stream->Dispose();
                return nullptr;
            }
            return std::unique_ptr<DemoReader>(new DemoReader(stream, header[5]));
        }
        catch (const std::ios_base::failure&)
        {
            if (stream != nullptr)
            {
                stream->Dispose();
            }
            return nullptr;
        }
    }

    DemoReader::DemoReader(std::shared_ptr<::MphRead::NativeRuntime::Stream> stream,
        std::uint8_t protocolVersion)
        : _stream(std::move(stream)),
          _deflate(NativeRuntime::DeflateStreamDecompress(_stream, true)),
          _protocolVersion(protocolVersion)
    {
    }

    std::optional<DemoRecord> DemoReader::ReadNext()
    {
        try
        {
            if (!Fill(std::span<std::uint8_t>(_header).first(1)))
            {
                return std::nullopt;
            }
            std::uint32_t delta = _header[0];
            if (delta == DemoFile::LongGap)
            {
                if (!Fill(std::span<std::uint8_t>(_header).first(4)))
                {
                    return std::nullopt;
                }
                delta = ReadUInt32LittleEndian(std::span<const std::uint8_t>(_header).first(4));
            }
            if (!Fill(std::span<std::uint8_t>(_header).first(2)))
            {
                return std::nullopt;
            }
            const std::uint16_t length =
                ReadUInt16LittleEndian(std::span<const std::uint8_t>(_header).first(2));
            auto data = std::make_shared<std::vector<std::uint8_t>>(length);
            if (!Fill(*data))
            {
                return std::nullopt;
            }
            _frame += delta;
            return DemoRecord(_frame, std::move(data));
        }
        catch (const System::IO::InvalidDataException&)
        {
            return std::nullopt;
        }
        catch (const std::ios_base::failure&)
        {
            return std::nullopt;
        }
    }

    bool DemoReader::Fill(std::span<std::uint8_t> destination)
    {
        return _deflate->ReadAtLeast(destination)
            == destination.size();
    }

    std::uint8_t DemoReader::ProtocolVersion() const
    {
        return _protocolVersion;
    }

    void DemoReader::Dispose()
    {
        _deflate->Dispose();
        _stream->Dispose();
    }
}
