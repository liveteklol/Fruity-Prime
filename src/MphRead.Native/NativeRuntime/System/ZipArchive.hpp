#pragma once

// System.IO.Compression.ZipArchive: reading one (stored, deflate and deflate64
// entries, ZIP64 included) and creating one. Entry names are UTF-8, as .NET
// reads and writes them when no encoding is given.

#include "Streams.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MphRead::NativeRuntime
{
    enum class ZipArchiveMode : std::int32_t
    {
        Read = 0,
        Create = 1,
    };

    class ZipArchive;

    class ZipArchiveEntry final
    {
    public:
        // ZipArchiveEntry.FullName / Name.
        [[nodiscard]] const std::string& FullName() const noexcept { return _fullName; }
        [[nodiscard]] std::string Name() const;
        // Length and CompressedLength: what the central directory says.
        [[nodiscard]] std::int64_t Length() const noexcept { return static_cast<std::int64_t>(_length); }
        [[nodiscard]] std::int64_t CompressedLength() const noexcept
        {
            return static_cast<std::int64_t>(_compressedLength);
        }
        [[nodiscard]] std::uint32_t Crc32() const noexcept { return _crc; }

        // Read mode: the whole entry, decompressed. Create mode: nothing.
        [[nodiscard]] std::vector<std::uint8_t> ReadAllBytes() const;
        // entry.Open(): a stream over the contents (Read), or one to write
        // them to (Create; once per entry).
        [[nodiscard]] std::shared_ptr<Stream> Open();
        // entry.LastWriteTime = value, before Open in Create mode.
        void LastWriteTime(std::chrono::system_clock::time_point value);

    private:
        friend class ZipArchive;
        class WriteStream;

        ZipArchive* _archive = nullptr;
        std::string _fullName;
        std::uint16_t _flags = 0;
        std::uint16_t _method = 0;
        std::uint16_t _dosTime = 0;
        std::uint16_t _dosDate = 0;
        std::uint32_t _crc = 0;
        std::uint64_t _compressedLength = 0;
        std::uint64_t _length = 0;
        std::uint64_t _localOffset = 0;
        CompressionLevel _level = CompressionLevel::Optimal;
        bool _opened = false;
        bool _written = false;
    };

    class ZipArchive final
    {
    public:
        // new ZipArchive(stream, mode, leaveOpen). Read needs a seekable
        // stream; InvalidDataException when it is not a zip.
        ZipArchive(std::shared_ptr<Stream> stream, ZipArchiveMode mode, bool leaveOpen = false);
        ZipArchive(const ZipArchive&) = delete;
        ZipArchive& operator=(const ZipArchive&) = delete;
        ~ZipArchive();

        // ZipFile.OpenRead(path).
        [[nodiscard]] static std::shared_ptr<ZipArchive> OpenRead(const std::string& path);
        // ZipFile.Open(path, ZipArchiveMode.Create).
        [[nodiscard]] static std::shared_ptr<ZipArchive> OpenCreate(const std::string& path);

        // ZipArchive.Entries (Read mode), in central-directory order.
        [[nodiscard]] const std::vector<std::shared_ptr<ZipArchiveEntry>>& Entries() const noexcept
        {
            return _entries;
        }
        // ZipArchive.GetEntry(name): ordinal; null when absent.
        [[nodiscard]] std::shared_ptr<ZipArchiveEntry> GetEntry(std::string_view name) const;
        // ZipArchive.CreateEntry(name, level) (Create mode).
        [[nodiscard]] std::shared_ptr<ZipArchiveEntry> CreateEntry(
            std::string_view name, CompressionLevel level = CompressionLevel::Optimal);
        // Writes the central directory (Create) and closes the stream unless
        // leaveOpen. A second call does nothing.
        void Dispose();

        // By index, for callers that walk the list.
        [[nodiscard]] std::size_t Count() const noexcept { return _entries.size(); }
        [[nodiscard]] const std::string& FullName(std::size_t index) const;
        [[nodiscard]] std::vector<std::uint8_t> Read(std::size_t index) const;

    private:
        friend class ZipArchiveEntry;

        void ReadCentralDirectory();
        [[nodiscard]] std::vector<std::uint8_t> ReadEntry(const ZipArchiveEntry& entry) const;
        void WriteEntry(ZipArchiveEntry& entry, std::span<const std::uint8_t> contents);
        void FinishPendingEntry();

        std::shared_ptr<Stream> _stream;
        ZipArchiveMode _mode;
        bool _leaveOpen;
        bool _disposed = false;
        std::vector<std::shared_ptr<ZipArchiveEntry>> _entries;
        std::shared_ptr<ZipArchiveEntry> _pending;
        // A large archive (a game's texture pak) is not read whole: its
        // directory is read from its end, each entry from the file when it is
        // asked for, one at a time.
        bool _streamed = false;
        mutable std::mutex _readLock;
    };

    // ZipFile.ExtractToDirectory(archive, destination, overwriteFiles).
    void ZipFileExtractToDirectory(const std::string& archivePath, const std::string& destination,
        bool overwriteFiles);

    // Raw deflate64 (method 9), which zlib does not read: `expectedLength`
    // bytes of output at most.
    [[nodiscard]] std::vector<std::uint8_t> InflateDeflate64(
        std::span<const std::uint8_t> input, std::size_t expectedLength);
}
