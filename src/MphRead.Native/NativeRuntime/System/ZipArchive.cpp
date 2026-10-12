#include "ZipArchive.hpp"

#include "Encoding.hpp"
#include "Exceptions.hpp"
#include "IO.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <limits>

namespace MphRead::NativeRuntime
{
    namespace
    {
        constexpr std::uint32_t LocalHeaderSignature = 0x04034B50U;
        constexpr std::uint32_t CentralHeaderSignature = 0x02014B50U;
        constexpr std::uint32_t EndOfCentralDirectorySignature = 0x06054B50U;
        constexpr std::uint32_t Zip64EndSignature = 0x06064B50U;
        constexpr std::uint32_t Zip64LocatorSignature = 0x07064B50U;
        constexpr std::uint16_t MethodStored = 0;
        constexpr std::uint16_t MethodDeflate = 8;
        constexpr std::uint16_t MethodDeflate64 = 9;
        constexpr std::uint16_t FlagUtf8 = 0x0800U;
        constexpr std::uint64_t Zip32Max = 0xFFFFFFFFULL;

        [[noreturn]] void Corrupt(std::string_view message)
        {
            throw System::IO::InvalidDataException(std::string(message));
        }

        [[nodiscard]] std::uint16_t U16(std::span<const std::uint8_t> bytes, std::size_t at)
        {
            if (at > bytes.size() || bytes.size() - at < 2)
            {
                Corrupt("Central Directory corrupt.");
            }
            return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
        }

        [[nodiscard]] std::uint32_t U32(std::span<const std::uint8_t> bytes, std::size_t at)
        {
            return static_cast<std::uint32_t>(U16(bytes, at)) | (static_cast<std::uint32_t>(U16(bytes, at + 2)) << 16);
        }

        [[nodiscard]] std::uint64_t U64(std::span<const std::uint8_t> bytes, std::size_t at)
        {
            return static_cast<std::uint64_t>(U32(bytes, at)) | (static_cast<std::uint64_t>(U32(bytes, at + 4)) << 32);
        }

        void Put16(std::vector<std::uint8_t>& out, std::uint16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
        }

        void Put32(std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            Put16(out, static_cast<std::uint16_t>(value));
            Put16(out, static_cast<std::uint16_t>(value >> 16));
        }

        void Put64(std::vector<std::uint8_t>& out, std::uint64_t value)
        {
            Put32(out, static_cast<std::uint32_t>(value));
            Put32(out, static_cast<std::uint32_t>(value >> 32));
        }

        [[nodiscard]] std::uint32_t Clip32(std::uint64_t value) noexcept
        {
            return value >= Zip32Max ? static_cast<std::uint32_t>(Zip32Max) : static_cast<std::uint32_t>(value);
        }

        // ZipHelper.DateTimeToDosTime: local time, years held to what the
        // format can say.
        void DosTime(std::chrono::system_clock::time_point value, std::uint16_t& time, std::uint16_t& date)
        {
            const std::time_t raw = std::chrono::system_clock::to_time_t(value);
            std::tm local{};
#if defined(_WIN32)
            localtime_s(&local, &raw);
#else
            localtime_r(&raw, &local);
#endif
            const int year = std::clamp(local.tm_year + 1900, 1980, 2107);
            date = static_cast<std::uint16_t>(((year - 1980) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
            time = static_cast<std::uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
        }

        // ZipArchiveEntry: CompressionLevel is recorded in bits 1 and 2.
        [[nodiscard]] std::uint16_t LevelFlags(CompressionLevel level) noexcept
        {
            switch (level)
            {
            case CompressionLevel::SmallestSize:
                return 0x0002U;
            case CompressionLevel::Fastest:
                return 0x0006U;
            default:
                return 0;
            }
        }

        [[nodiscard]] std::vector<std::uint8_t> ReadAll(Stream& stream)
        {
            if (stream.CanSeek())
            {
                (void)stream.Seek(0, SeekOrigin::Begin);
            }
            return stream.ReadToEnd();
        }
    }

    // ==== Deflate64 ===================================================================

    namespace
    {
        struct HuffmanNode final
        {
            std::array<std::int32_t, 2> Child{ -1, -1 };
            std::int32_t Symbol = -1;
        };

        class BitReader final
        {
        public:
            explicit BitReader(std::span<const std::uint8_t> bytes) noexcept : _bytes(bytes) {}

            [[nodiscard]] std::uint32_t Bits(std::int32_t count)
            {
                std::uint32_t value = 0;
                for (std::int32_t i = 0; i < count; ++i)
                {
                    if (_bit >= _bytes.size() * 8U)
                    {
                        Corrupt("Block length does not match with its complement.");
                    }
                    value |= static_cast<std::uint32_t>((_bytes[_bit >> 3] >> (_bit & 7U)) & 1U) << i;
                    ++_bit;
                }
                return value;
            }

            void AlignByte() noexcept { _bit = (_bit + 7U) & ~std::size_t(7U); }
            [[nodiscard]] std::size_t Byte() const noexcept { return _bit >> 3; }
            void Byte(std::size_t value) noexcept { _bit = value << 3; }

        private:
            std::span<const std::uint8_t> _bytes;
            std::size_t _bit = 0;
        };

        [[nodiscard]] std::vector<HuffmanNode> BuildTree(const std::vector<std::uint8_t>& lengths)
        {
            std::array<std::int32_t, 16> counts{};
            for (const std::uint8_t length : lengths)
            {
                if (length > 15)
                {
                    Corrupt("Failed to construct a huffman tree using the length array.");
                }
                if (length != 0)
                {
                    ++counts[length];
                }
            }
            std::array<std::int32_t, 16> next{};
            std::int32_t code = 0;
            for (std::int32_t bits = 1; bits <= 15; ++bits)
            {
                code = (code + counts[static_cast<std::size_t>(bits - 1)]) << 1;
                next[static_cast<std::size_t>(bits)] = code;
            }
            std::vector<HuffmanNode> nodes(1);
            for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol)
            {
                const std::int32_t length = lengths[symbol];
                if (length == 0)
                {
                    continue;
                }
                const std::int32_t assigned = next[static_cast<std::size_t>(length)]++;
                std::int32_t node = 0;
                for (std::int32_t bit = length - 1; bit >= 0; --bit)
                {
                    const std::size_t side = static_cast<std::size_t>((assigned >> bit) & 1);
                    if (nodes[static_cast<std::size_t>(node)].Child[side] < 0)
                    {
                        nodes[static_cast<std::size_t>(node)].Child[side] = static_cast<std::int32_t>(nodes.size());
                        nodes.emplace_back();
                    }
                    node = nodes[static_cast<std::size_t>(node)].Child[side];
                }
                nodes[static_cast<std::size_t>(node)].Symbol = static_cast<std::int32_t>(symbol);
            }
            return nodes;
        }

        [[nodiscard]] std::int32_t Decode(BitReader& reader, const std::vector<HuffmanNode>& tree)
        {
            std::int32_t node = 0;
            for (std::int32_t depth = 0; depth <= 15; ++depth)
            {
                if (tree[static_cast<std::size_t>(node)].Symbol >= 0)
                {
                    return tree[static_cast<std::size_t>(node)].Symbol;
                }
                node = tree[static_cast<std::size_t>(node)].Child[reader.Bits(1)];
                if (node < 0)
                {
                    break;
                }
            }
            Corrupt("Failed to construct a huffman tree using the length array.");
        }

        // One compressed block; true when the output is full.
        [[nodiscard]] bool InflateCodes(BitReader& reader, std::vector<std::uint8_t>& output,
            const std::vector<HuffmanNode>& literals, const std::vector<HuffmanNode>& distances, std::size_t limit)
        {
            static constexpr std::array<std::int32_t, 29> LengthBase{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19,
                23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 3 };
            static constexpr std::array<std::int32_t, 29> LengthExtra{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2,
                2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 16 };
            static constexpr std::array<std::int32_t, 32> DistanceBase{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49,
                65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385,
                24577, 32769, 49153 };
            static constexpr std::array<std::int32_t, 32> DistanceExtra{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5,
                5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14 };
            while (true)
            {
                const std::int32_t symbol = Decode(reader, literals);
                if (symbol < 256)
                {
                    output.push_back(static_cast<std::uint8_t>(symbol));
                    if (output.size() >= limit)
                    {
                        return true;
                    }
                    continue;
                }
                if (symbol == 256)
                {
                    return false;
                }
                if (symbol > 285)
                {
                    Corrupt("Found invalid data while decoding.");
                }
                const auto index = static_cast<std::size_t>(symbol - 257);
                std::int32_t length = LengthBase[index];
                if (LengthExtra[index] != 0)
                {
                    length += static_cast<std::int32_t>(reader.Bits(LengthExtra[index]));
                }
                const std::int32_t distanceSymbol = Decode(reader, distances);
                if (distanceSymbol < 0 || distanceSymbol >= 32)
                {
                    Corrupt("Found invalid data while decoding.");
                }
                const auto d = static_cast<std::size_t>(distanceSymbol);
                std::int32_t distance = DistanceBase[d];
                if (DistanceExtra[d] != 0)
                {
                    distance += static_cast<std::int32_t>(reader.Bits(DistanceExtra[d]));
                }
                if (static_cast<std::size_t>(distance) > output.size())
                {
                    Corrupt("Found invalid data while decoding.");
                }
                for (std::int32_t i = 0; i < length; ++i)
                {
                    output.push_back(output[output.size() - static_cast<std::size_t>(distance)]);
                    if (output.size() >= limit)
                    {
                        return true;
                    }
                }
            }
        }
    }

    std::vector<std::uint8_t> InflateDeflate64(std::span<const std::uint8_t> input, std::size_t expectedLength)
    {
        std::vector<std::uint8_t> output;
        if (expectedLength == 0)
        {
            return output;
        }
        BitReader reader(input);
        bool last = false;
        while (!last)
        {
            last = reader.Bits(1) != 0;
            const std::uint32_t type = reader.Bits(2);
            if (type == 0)
            {
                reader.AlignByte();
                std::size_t at = reader.Byte();
                const std::uint16_t length = U16(input, at);
                if (static_cast<std::uint16_t>(~length) != U16(input, at + 2))
                {
                    Corrupt("Block length does not match with its complement.");
                }
                at += 4;
                const std::size_t count = std::min<std::size_t>(length, expectedLength - output.size());
                if (count > input.size() - std::min(at, input.size()))
                {
                    Corrupt("Block length does not match with its complement.");
                }
                output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(at),
                    input.begin() + static_cast<std::ptrdiff_t>(at + count));
                if (output.size() >= expectedLength)
                {
                    return output;
                }
                reader.Byte(at + length);
            }
            else if (type == 1)
            {
                std::vector<std::uint8_t> literal(288, 8);
                std::fill(literal.begin() + 144, literal.begin() + 256, 9);
                std::fill(literal.begin() + 256, literal.begin() + 280, 7);
                if (InflateCodes(reader, output, BuildTree(literal), BuildTree(std::vector<std::uint8_t>(32, 5)),
                        expectedLength))
                {
                    return output;
                }
            }
            else if (type == 2)
            {
                const auto literalCount = static_cast<std::int32_t>(reader.Bits(5)) + 257;
                const auto distanceCount = static_cast<std::int32_t>(reader.Bits(5)) + 1;
                const auto codeCount = static_cast<std::int32_t>(reader.Bits(4)) + 4;
                static constexpr std::array<std::size_t, 19> Order{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3,
                    13, 2, 14, 1, 15 };
                std::vector<std::uint8_t> codeLengths(19, 0);
                for (std::int32_t i = 0; i < codeCount; ++i)
                {
                    codeLengths[Order[static_cast<std::size_t>(i)]] = static_cast<std::uint8_t>(reader.Bits(3));
                }
                const std::vector<HuffmanNode> codeTree = BuildTree(codeLengths);
                std::vector<std::uint8_t> lengths;
                while (static_cast<std::int32_t>(lengths.size()) < literalCount + distanceCount)
                {
                    const std::int32_t symbol = Decode(reader, codeTree);
                    if (symbol <= 15)
                    {
                        lengths.push_back(static_cast<std::uint8_t>(symbol));
                        continue;
                    }
                    std::uint8_t value = 0;
                    std::int32_t repeat = 0;
                    if (symbol == 16)
                    {
                        if (lengths.empty())
                        {
                            Corrupt("Found invalid data while decoding.");
                        }
                        value = lengths.back();
                        repeat = static_cast<std::int32_t>(reader.Bits(2)) + 3;
                    }
                    else if (symbol == 17)
                    {
                        repeat = static_cast<std::int32_t>(reader.Bits(3)) + 3;
                    }
                    else
                    {
                        repeat = static_cast<std::int32_t>(reader.Bits(7)) + 11;
                    }
                    lengths.insert(lengths.end(), static_cast<std::size_t>(repeat), value);
                }
                if (static_cast<std::int32_t>(lengths.size()) != literalCount + distanceCount)
                {
                    Corrupt("Found invalid data while decoding.");
                }
                const std::vector<std::uint8_t> literal(lengths.begin(), lengths.begin() + literalCount);
                const std::vector<std::uint8_t> distance(lengths.begin() + literalCount, lengths.end());
                if (InflateCodes(reader, output, BuildTree(literal), BuildTree(distance), expectedLength))
                {
                    return output;
                }
            }
            else
            {
                Corrupt("Unknown block type. Stream might be corrupted.");
            }
        }
        return output;
    }

    // ==== ZipArchiveEntry ============================================================

    std::string ZipArchiveEntry::Name() const
    {
        const std::size_t slash = _fullName.find_last_of("/\\");
        return slash == std::string::npos ? _fullName : _fullName.substr(slash + 1);
    }

    std::vector<std::uint8_t> ZipArchiveEntry::ReadAllBytes() const
    {
        if (_archive == nullptr || _archive->_mode != ZipArchiveMode::Read)
        {
            throw System::NotSupportedException("Reading from the entry is not supported in Create mode.");
        }
        return _archive->ReadEntry(*this);
    }

    class ZipArchiveEntry::WriteStream final : public Stream
    {
    public:
        explicit WriteStream(std::shared_ptr<ZipArchiveEntry> entry) : _entry(std::move(entry)) {}
        ~WriteStream() override
        {
            try
            {
                Dispose();
            }
            catch (...)
            {
            }
        }

        [[nodiscard]] bool CanRead() const noexcept override { return false; }
        [[nodiscard]] bool CanWrite() const noexcept override { return !_closed; }
        [[nodiscard]] bool CanSeek() const noexcept override { return false; }
        [[nodiscard]] std::size_t Read(std::span<std::uint8_t>) override
        {
            throw System::NotSupportedException("Stream does not support reading.");
        }
        void Write(std::span<const std::uint8_t> buffer) override
        {
            if (_closed)
            {
                throw System::ObjectDisposedException("Stream");
            }
            _contents.insert(_contents.end(), buffer.begin(), buffer.end());
        }
        void Dispose() override
        {
            if (_closed)
            {
                return;
            }
            _closed = true;
            _entry->_archive->WriteEntry(*_entry, _contents);
        }

    private:
        std::shared_ptr<ZipArchiveEntry> _entry;
        std::vector<std::uint8_t> _contents;
        bool _closed = false;
    };

    std::shared_ptr<Stream> ZipArchiveEntry::Open()
    {
        if (_archive == nullptr || _archive->_disposed)
        {
            throw System::ObjectDisposedException("ZipArchive");
        }
        if (_archive->_mode == ZipArchiveMode::Read)
        {
            return std::make_shared<MemoryStream>(ReadAllBytes(), false);
        }
        if (_opened)
        {
            throw System::IO::IOException("Entries in create mode may only be written to once, and only one entry may be held open at a time.");
        }
        _opened = true;
        for (const auto& entry : _archive->_entries)
        {
            if (entry.get() == this)
            {
                return std::make_shared<WriteStream>(entry);
            }
        }
        throw System::InvalidOperationException();
    }

    void ZipArchiveEntry::LastWriteTime(std::chrono::system_clock::time_point value)
    {
        if (_opened)
        {
            throw System::IO::IOException("Cannot modify entry in Create mode after entry has been opened for writing.");
        }
        DosTime(value, _dosTime, _dosDate);
    }

    // ==== ZipArchive =================================================================

    ZipArchive::ZipArchive(std::shared_ptr<Stream> stream, ZipArchiveMode mode, bool leaveOpen)
        : _stream(std::move(stream)), _mode(mode), _leaveOpen(leaveOpen)
    {
        if (_stream == nullptr)
        {
            throw System::ArgumentNullException("stream");
        }
        if (mode == ZipArchiveMode::Read)
        {
            ReadCentralDirectory();
        }
        else if (!_stream->CanWrite())
        {
            throw System::ArgumentException("Cannot use create mode on a non-writable stream.");
        }
    }

    ZipArchive::~ZipArchive()
    {
        try
        {
            Dispose();
        }
        catch (...)
        {
        }
    }

    std::shared_ptr<ZipArchive> ZipArchive::OpenRead(const std::string& path)
    {
        return std::make_shared<ZipArchive>(
            std::make_shared<FileStream>(path, FileMode::Open, FileAccess::Read, FileShare::Read), ZipArchiveMode::Read);
    }

    std::shared_ptr<ZipArchive> ZipArchive::OpenCreate(const std::string& path)
    {
        return std::make_shared<ZipArchive>(
            std::make_shared<FileStream>(path, FileMode::CreateNew, FileAccess::Write, FileShare::None),
            ZipArchiveMode::Create);
    }

    void ZipArchive::ReadCentralDirectory()
    {
        // Up to this size an archive is read whole and the file let go, as
        // ZipFile.OpenRead's callers have always had it; past it, a game's
        // 450 MB pak opened for one level would be read whole each time.
        constexpr std::int64_t WholeAtMost = 32LL * 1024 * 1024;
        std::vector<std::uint8_t> bytes;
        // Where bytes[0] lies in the archive, and the archive's length.
        std::uint64_t base = 0;
        std::uint64_t total = 0;
        if (_stream->CanSeek() && _stream->Length() > WholeAtMost)
        {
            _streamed = true;
            total = static_cast<std::uint64_t>(_stream->Length());
            // The end: the end record, its comment of up to 64 KB, and the
            // Zip64 locator before it.
            const std::uint64_t tail = std::min<std::uint64_t>(total, 22 + 0xFFFFU + 20);
            std::vector<std::uint8_t> end(static_cast<std::size_t>(tail));
            (void)_stream->Seek(static_cast<std::int64_t>(total - tail), SeekOrigin::Begin);
            _stream->ReadExactly(end);
            const std::span<const std::uint8_t> view(end);
            std::size_t at = end.size() - 22;
            while (U32(view, at) != EndOfCentralDirectorySignature)
            {
                if (at == 0)
                {
                    Corrupt("End of Central Directory record could not be found.");
                }
                --at;
            }
            std::uint64_t offset = U32(view, at + 16);
            if (offset == Zip32Max && at >= 20 && U32(view, at - 20) == Zip64LocatorSignature)
            {
                offset = std::min<std::uint64_t>(offset, U64(view, at - 12));
            }
            if (offset >= total)
            {
                Corrupt("Central Directory corrupt.");
            }
            // From the directory (or the Zip64 end record, which comes
            // after it) to the end of the file.
            base = offset;
            bytes.resize(static_cast<std::size_t>(total - base));
            (void)_stream->Seek(static_cast<std::int64_t>(base), SeekOrigin::Begin);
            _stream->ReadExactly(bytes);
        }
        else
        {
            bytes = ReadAll(*_stream);
            total = bytes.size();
        }
        const std::span<const std::uint8_t> archive(bytes);
        if (bytes.size() < 22)
        {
            Corrupt("End of Central Directory record could not be found.");
        }
        // The end record is last, after a comment of up to 64 KB.
        std::size_t end = bytes.size() - 22;
        const std::size_t floor = bytes.size() > 22 + 0xFFFFU ? bytes.size() - 22 - 0xFFFFU : 0;
        while (U32(archive, end) != EndOfCentralDirectorySignature)
        {
            if (end == floor)
            {
                Corrupt("End of Central Directory record could not be found.");
            }
            --end;
        }
        if (U16(archive, end + 4) != U16(archive, end + 6) || U16(archive, end + 8) != U16(archive, end + 10))
        {
            Corrupt("Split or spanned archives are not supported.");
        }
        std::uint64_t count = U16(archive, end + 10);
        std::uint64_t offset = U32(archive, end + 16);
        if ((count == 0xFFFFU || offset == Zip32Max) && end >= 20 && U32(archive, end - 20) == Zip64LocatorSignature)
        {
            const std::uint64_t zip64 = U64(archive, end - 12);
            if (zip64 < base || zip64 - base > bytes.size()
                || U32(archive, static_cast<std::size_t>(zip64 - base)) != Zip64EndSignature)
            {
                Corrupt("Zip 64 End of Central Directory Record not where indicated.");
            }
            count = U64(archive, static_cast<std::size_t>(zip64 - base) + 32);
            offset = U64(archive, static_cast<std::size_t>(zip64 - base) + 48);
        }
        if (offset < base || offset - base > bytes.size())
        {
            Corrupt("Central Directory corrupt.");
        }

        std::size_t at = static_cast<std::size_t>(offset - base);
        while (at + 4 <= bytes.size() && U32(archive, at) == CentralHeaderSignature)
        {
            auto entry = std::make_shared<ZipArchiveEntry>();
            entry->_archive = this;
            entry->_flags = U16(archive, at + 8);
            entry->_method = U16(archive, at + 10);
            entry->_dosTime = U16(archive, at + 12);
            entry->_dosDate = U16(archive, at + 14);
            entry->_crc = U32(archive, at + 16);
            const std::uint32_t compressed = U32(archive, at + 20);
            const std::uint32_t length = U32(archive, at + 24);
            const std::uint16_t nameLength = U16(archive, at + 28);
            const std::uint16_t extraLength = U16(archive, at + 30);
            const std::uint16_t commentLength = U16(archive, at + 32);
            const std::uint32_t local = U32(archive, at + 42);
            const std::size_t variable = std::size_t{ nameLength } + extraLength + commentLength;
            if (bytes.size() - at < 46 + variable)
            {
                Corrupt("Central Directory corrupt.");
            }
            entry->_fullName = Utf8GetString(archive.subspan(at + 46, nameLength));
            entry->_compressedLength = compressed;
            entry->_length = length;
            entry->_localOffset = local;

            // The ZIP64 extra field holds, in order, whichever of the three
            // the 32-bit fields could not.
            std::span<const std::uint8_t> extra = archive.subspan(at + 46 + nameLength, extraLength);
            while (extra.size() >= 4)
            {
                const std::uint16_t tag = U16(extra, 0);
                const std::uint16_t size = U16(extra, 2);
                if (size > extra.size() - 4)
                {
                    break;
                }
                if (tag == 0x0001U)
                {
                    std::size_t field = 4;
                    const auto take = [&](std::uint64_t& target)
                    {
                        if (field + 8 <= 4U + size)
                        {
                            target = U64(extra, field);
                            field += 8;
                        }
                    };
                    if (length == Zip32Max)
                    {
                        take(entry->_length);
                    }
                    if (compressed == Zip32Max)
                    {
                        take(entry->_compressedLength);
                    }
                    if (local == Zip32Max)
                    {
                        take(entry->_localOffset);
                    }
                    break;
                }
                extra = extra.subspan(4U + size);
            }
            _entries.push_back(std::move(entry));
            at += 46 + variable;
        }
        if (_entries.size() != count)
        {
            Corrupt("Number of entries expected in End Of Central Directory does not correspond to number of entries in Central Directory.");
        }
        // Read mode keeps the bytes: entries are read from them later. A
        // streamed archive keeps its file instead.
        if (!_streamed)
        {
            _stream = std::make_shared<MemoryStream>(std::move(bytes), false);
        }
        (void)total;
    }

    std::vector<std::uint8_t> ZipArchive::ReadEntry(const ZipArchiveEntry& entry) const
    {
        if (_disposed)
        {
            throw System::ObjectDisposedException("ZipArchive");
        }
        if (_streamed)
        {
            // The local header, then the entry's bytes after it, from the file.
            std::vector<std::uint8_t> compressed;
            {
                const std::lock_guard<std::mutex> guard(_readLock);
                const std::int64_t length = _stream->Length();
                if (entry._localOffset + 30 > static_cast<std::uint64_t>(length))
                {
                    Corrupt("A local file header is corrupt.");
                }
                std::array<std::uint8_t, 30> header{};
                (void)_stream->Seek(static_cast<std::int64_t>(entry._localOffset), SeekOrigin::Begin);
                _stream->ReadExactly(header);
                const std::span<const std::uint8_t> view(header);
                if (U32(view, 0) != LocalHeaderSignature)
                {
                    Corrupt("A local file header is corrupt.");
                }
                const std::uint64_t data = entry._localOffset + 30 + U16(view, 26) + U16(view, 28);
                if (data > static_cast<std::uint64_t>(length)
                    || entry._compressedLength > static_cast<std::uint64_t>(length) - data)
                {
                    Corrupt("A local file header is corrupt.");
                }
                compressed.resize(static_cast<std::size_t>(entry._compressedLength));
                (void)_stream->Seek(static_cast<std::int64_t>(data), SeekOrigin::Begin);
                _stream->ReadExactly(compressed);
            }
            switch (entry._method)
            {
            case MethodStored:
                return compressed;
            case MethodDeflate:
                return InflateBytes(compressed);
            case MethodDeflate64:
                return InflateDeflate64(compressed, static_cast<std::size_t>(entry._length));
            default:
                Corrupt("The archive entry was compressed using an unsupported compression method.");
            }
        }
        const std::vector<std::uint8_t>& bytes = static_cast<const MemoryStream&>(*_stream).Buffer();
        const std::span<const std::uint8_t> archive(bytes);
        if (entry._localOffset > bytes.size() || U32(archive, static_cast<std::size_t>(entry._localOffset)) != LocalHeaderSignature)
        {
            Corrupt("A local file header is corrupt.");
        }
        const auto local = static_cast<std::size_t>(entry._localOffset);
        const std::size_t data = local + 30 + U16(archive, local + 26) + U16(archive, local + 28);
        if (data > bytes.size() || entry._compressedLength > bytes.size() - data)
        {
            Corrupt("A local file header is corrupt.");
        }
        const std::span<const std::uint8_t> compressed = archive.subspan(data, static_cast<std::size_t>(entry._compressedLength));
        switch (entry._method)
        {
        case MethodStored:
            return std::vector<std::uint8_t>(compressed.begin(), compressed.end());
        case MethodDeflate:
            return InflateBytes(compressed);
        case MethodDeflate64:
            return InflateDeflate64(compressed, static_cast<std::size_t>(entry._length));
        default:
            Corrupt("The archive entry was compressed using an unsupported compression method.");
        }
    }

    std::shared_ptr<ZipArchiveEntry> ZipArchive::GetEntry(std::string_view name) const
    {
        for (const auto& entry : _entries)
        {
            if (entry->_fullName == name)
            {
                return entry;
            }
        }
        return nullptr;
    }

    const std::string& ZipArchive::FullName(std::size_t index) const
    {
        return _entries.at(index)->_fullName;
    }

    std::vector<std::uint8_t> ZipArchive::Read(std::size_t index) const
    {
        return ReadEntry(*_entries.at(index));
    }

    std::shared_ptr<ZipArchiveEntry> ZipArchive::CreateEntry(std::string_view name, CompressionLevel level)
    {
        if (_mode != ZipArchiveMode::Create)
        {
            throw System::NotSupportedException("Cannot create entries on an archive opened in read mode.");
        }
        if (name.empty())
        {
            throw System::ArgumentException("String cannot be empty. (Parameter 'entryName')");
        }
        if (name.size() > 0xFFFFU)
        {
            throw System::ArgumentException("Entry names cannot require more than 2^16 bits.");
        }
        // Only one entry holds the stream at a time: the last one created
        // and never opened is written empty now.
        FinishPendingEntry();
        auto entry = std::make_shared<ZipArchiveEntry>();
        entry->_archive = this;
        entry->_fullName = std::string(name);
        entry->_level = level;
        const bool ascii = std::all_of(name.begin(), name.end(),
            [](char ch) { return static_cast<unsigned char>(ch) >= 0x20U && static_cast<unsigned char>(ch) <= 0x7EU; });
        entry->_flags = static_cast<std::uint16_t>(LevelFlags(level) | (ascii ? 0U : FlagUtf8));
        entry->_method = MethodDeflate;
        DosTime(std::chrono::system_clock::now(), entry->_dosTime, entry->_dosDate);
        _entries.push_back(entry);
        _pending = entry;
        return entry;
    }

    void ZipArchive::FinishPendingEntry()
    {
        if (_pending != nullptr && !_pending->_written)
        {
            if (_pending->_opened)
            {
                throw System::IO::IOException("Entries cannot be created while previously created entries are still open.");
            }
            _pending->_opened = true;
            WriteEntry(*_pending, {});
        }
        _pending = nullptr;
    }

    void ZipArchive::WriteEntry(ZipArchiveEntry& entry, std::span<const std::uint8_t> contents)
    {
        std::vector<std::uint8_t> compressed;
        if (contents.empty() || entry._level == CompressionLevel::NoCompression)
        {
            // An entry never written is stored empty, as .NET stores it.
            entry._method = MethodStored;
            compressed.assign(contents.begin(), contents.end());
        }
        else
        {
            compressed = DeflateBytes(contents, entry._level);
        }
        entry._crc = NativeRuntime::Crc32(contents);
        entry._length = contents.size();
        entry._compressedLength = compressed.size();
        entry._localOffset = static_cast<std::uint64_t>(_stream->CanSeek() ? _stream->Position() : 0);
        const bool zip64 = entry._length >= Zip32Max || entry._compressedLength >= Zip32Max;

        std::vector<std::uint8_t> header;
        Put32(header, LocalHeaderSignature);
        Put16(header, zip64 ? 45 : 20);
        Put16(header, entry._flags);
        Put16(header, entry._method);
        Put16(header, entry._dosTime);
        Put16(header, entry._dosDate);
        Put32(header, entry._crc);
        Put32(header, Clip32(entry._compressedLength));
        Put32(header, Clip32(entry._length));
        Put16(header, static_cast<std::uint16_t>(entry._fullName.size()));
        Put16(header, zip64 ? 20 : 0);
        header.insert(header.end(), entry._fullName.begin(), entry._fullName.end());
        if (zip64)
        {
            Put16(header, 0x0001U);
            Put16(header, 16);
            Put64(header, entry._length);
            Put64(header, entry._compressedLength);
        }
        _stream->Write(header);
        _stream->Write(compressed);
        entry._written = true;
        if (_pending.get() == &entry)
        {
            _pending = nullptr;
        }
    }

    void ZipArchive::Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        if (_mode == ZipArchiveMode::Create)
        {
            FinishPendingEntry();
            for (const auto& entry : _entries)
            {
                if (!entry->_written)
                {
                    throw System::IO::IOException("A ZIP entry stream was not closed.");
                }
            }
            const auto centralOffset = static_cast<std::uint64_t>(_stream->Position());
            std::vector<std::uint8_t> central;
            for (const auto& entry : _entries)
            {
                const bool sizes64 = entry->_length >= Zip32Max || entry->_compressedLength >= Zip32Max;
                const bool offset64 = entry->_localOffset >= Zip32Max;
                const std::uint16_t extra = static_cast<std::uint16_t>(
                    (sizes64 || offset64 ? 4 : 0) + (sizes64 ? 16 : 0) + (offset64 ? 8 : 0));
                Put32(central, CentralHeaderSignature);
#if defined(_WIN32)
                Put16(central, (sizes64 || offset64) ? 45 : 20);
#else
                Put16(central, static_cast<std::uint16_t>(0x0300U | ((sizes64 || offset64) ? 45 : 20)));
#endif
                Put16(central, (sizes64 || offset64) ? 45 : 20);
                Put16(central, entry->_flags);
                Put16(central, entry->_method);
                Put16(central, entry->_dosTime);
                Put16(central, entry->_dosDate);
                Put32(central, entry->_crc);
                Put32(central, sizes64 ? static_cast<std::uint32_t>(Zip32Max) : static_cast<std::uint32_t>(entry->_compressedLength));
                Put32(central, sizes64 ? static_cast<std::uint32_t>(Zip32Max) : static_cast<std::uint32_t>(entry->_length));
                Put16(central, static_cast<std::uint16_t>(entry->_fullName.size()));
                Put16(central, extra);
                Put16(central, 0);
                Put16(central, 0);
                Put16(central, 0);
#if defined(_WIN32)
                Put32(central, 0);
#else
                // -rw-r--r--, as .NET records on Unix.
                Put32(central, 0x81A40000U);
#endif
                Put32(central, Clip32(entry->_localOffset));
                central.insert(central.end(), entry->_fullName.begin(), entry->_fullName.end());
                if (extra != 0)
                {
                    Put16(central, 0x0001U);
                    Put16(central, static_cast<std::uint16_t>(extra - 4));
                    if (sizes64)
                    {
                        Put64(central, entry->_length);
                        Put64(central, entry->_compressedLength);
                    }
                    if (offset64)
                    {
                        Put64(central, entry->_localOffset);
                    }
                }
            }
            const std::uint64_t centralSize = central.size();
            const std::uint64_t count = _entries.size();
            if (centralOffset >= Zip32Max || centralSize >= Zip32Max || count >= 0xFFFFU)
            {
                const std::uint64_t zip64 = centralOffset + centralSize;
                Put32(central, Zip64EndSignature);
                Put64(central, 44);
                Put16(central, 45);
                Put16(central, 45);
                Put32(central, 0);
                Put32(central, 0);
                Put64(central, count);
                Put64(central, count);
                Put64(central, centralSize);
                Put64(central, centralOffset);
                Put32(central, Zip64LocatorSignature);
                Put32(central, 0);
                Put64(central, zip64);
                Put32(central, 1);
            }
            Put32(central, EndOfCentralDirectorySignature);
            Put16(central, 0);
            Put16(central, 0);
            Put16(central, static_cast<std::uint16_t>(std::min<std::uint64_t>(count, 0xFFFFU)));
            Put16(central, static_cast<std::uint16_t>(std::min<std::uint64_t>(count, 0xFFFFU)));
            Put32(central, Clip32(centralSize));
            Put32(central, Clip32(centralOffset));
            Put16(central, 0);
            _stream->Write(central);
            _stream->Flush();
        }
        if (!_leaveOpen)
        {
            _stream->Dispose();
        }
    }

    void ZipFileExtractToDirectory(const std::string& archivePath, const std::string& destination, bool overwriteFiles)
    {
        const std::shared_ptr<ZipArchive> archive = ZipArchive::OpenRead(archivePath);
        const std::filesystem::path root = std::filesystem::weakly_canonical(PathFromUtf8(PathGetFullPath(destination)));
        std::filesystem::create_directories(root);
        for (const auto& entry : archive->Entries())
        {
            const std::filesystem::path target = std::filesystem::weakly_canonical(root / PathFromUtf8(entry->FullName()));
            const std::string rootText = PathToUtf8(root);
            const std::string targetText = PathToUtf8(target);
            if (targetText.compare(0, rootText.size(), rootText) != 0)
            {
                throw System::IO::IOException(
                    "Extracting Zip entry would have resulted in a file outside the specified destination directory.");
            }
            if (entry->Name().empty())
            {
                if (entry->Length() != 0)
                {
                    throw System::IO::IOException("Zip entry name ends in directory separator character but contains data.");
                }
                std::filesystem::create_directories(target);
                continue;
            }
            std::filesystem::create_directories(target.parent_path());
            if (!overwriteFiles && std::filesystem::exists(target))
            {
                throw System::IO::IOException("The file '" + targetText + "' already exists.");
            }
            FileWriteAllBytes(targetText, entry->ReadAllBytes());
        }
    }
}
