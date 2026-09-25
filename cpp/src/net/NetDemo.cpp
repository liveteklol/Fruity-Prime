#include "NetDemo.h"

#include "NetProtocol.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fp::net {

namespace {

constexpr uint8_t Magic[4] = {'F', 'P', 'D', 'M'};
constexpr uint8_t FormatVersion = 2;
constexpr uint8_t LongGap = 0xFF;
constexpr uint32_t FlushIntervalFrames = 15;

} // namespace

// ---- writing -------------------------------------------------------------------------

struct DemoWriter::State {
    FILE* file = nullptr;
    mz_stream stream{};
    std::vector<uint8_t> out = std::vector<uint8_t>(64 * 1024);
};

std::unique_ptr<DemoWriter> DemoWriter::create(const std::filesystem::path& path)
{
    std::error_code ignored;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ignored);
    }
#ifdef _WIN32
    FILE* file = _wfopen(path.wstring().c_str(), L"wb");
#else
    FILE* file = std::fopen(path.string().c_str(), "wb");
#endif
    if (file == nullptr) {
        return nullptr;
    }
    std::unique_ptr<DemoWriter> writer(new DemoWriter);
    writer->m = std::make_unique<State>();
    writer->m->file = file;
    const uint8_t header[6] = {Magic[0], Magic[1], Magic[2], Magic[3], FormatVersion, static_cast<uint8_t>(ProtocolVersion)};
    std::fwrite(header, 1, sizeof header, file);
    // Raw deflate (no zlib header), as System.IO.Compression.DeflateStream writes it.
    if (mz_deflateInit2(&writer->m->stream, MZ_BEST_SPEED, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK) {
        std::fclose(file);
        return nullptr;
    }
    return writer;
}

void DemoWriter::pack(std::span<const uint8_t> data, int flush)
{
    mz_stream& s = m->stream;
    s.next_in = data.data();
    s.avail_in = static_cast<unsigned>(data.size());
    do {
        s.next_out = m->out.data();
        s.avail_out = static_cast<unsigned>(m->out.size());
        mz_deflate(&s, flush);
        const size_t produced = m->out.size() - s.avail_out;
        if (produced > 0) {
            std::fwrite(m->out.data(), 1, produced, m->file);
        }
    } while (s.avail_in > 0 || s.avail_out == 0);
}

void DemoWriter::write(uint32_t frame, std::span<const uint8_t> packet)
{
    if (packet.size() > 0xFFFF) {
        return;
    }
    frame = std::max(frame, m_lastFrame);
    const uint32_t delta = frame - m_lastFrame;
    m_lastFrame = frame;
    uint8_t header[7];
    size_t at = 0;
    if (delta < LongGap) {
        header[at++] = static_cast<uint8_t>(delta);
    } else {
        header[at++] = LongGap;
        for (int i = 0; i < 4; i++) {
            header[at++] = static_cast<uint8_t>(delta >> (8 * i));
        }
    }
    header[at++] = static_cast<uint8_t>(packet.size());
    header[at++] = static_cast<uint8_t>(packet.size() >> 8);
    pack({header, at}, MZ_NO_FLUSH);
    pack(packet, MZ_NO_FLUSH);
    // A quarter of a second is what a demo that dies with the game loses.
    if (frame - m_lastFlushFrame >= FlushIntervalFrames) {
        m_lastFlushFrame = frame;
        pack({}, MZ_SYNC_FLUSH);
        std::fflush(m->file);
    }
}

DemoWriter::~DemoWriter()
{
    if (m && m->file) {
        pack({}, MZ_FINISH);
        mz_deflateEnd(&m->stream);
        std::fclose(m->file);
    }
}

// ---- reading -------------------------------------------------------------------------

struct DemoReader::State {
    FILE* file = nullptr;
    mz_stream stream{};
    std::vector<uint8_t> in = std::vector<uint8_t>(64 * 1024);
    bool eof = false, ended = false;
};

std::unique_ptr<DemoReader> DemoReader::open(const std::filesystem::path& path)
{
#ifdef _WIN32
    FILE* file = _wfopen(path.wstring().c_str(), L"rb");
#else
    FILE* file = std::fopen(path.string().c_str(), "rb");
#endif
    if (file == nullptr) {
        return nullptr;
    }
    uint8_t header[6];
    if (std::fread(header, 1, sizeof header, file) != sizeof header || std::memcmp(header, Magic, 4) != 0 || header[4] != FormatVersion) {
        std::fclose(file);
        return nullptr;
    }
    std::unique_ptr<DemoReader> reader(new DemoReader);
    reader->m = std::make_unique<State>();
    reader->m->file = file;
    reader->m_protocol = header[5];
    if (mz_inflateInit2(&reader->m->stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
        std::fclose(file);
        return nullptr;
    }
    return reader;
}

DemoReader::~DemoReader()
{
    if (m && m->file) {
        mz_inflateEnd(&m->stream);
        std::fclose(m->file);
    }
}

bool DemoReader::fill(uint8_t* dest, size_t count)
{
    mz_stream& s = m->stream;
    s.next_out = dest;
    s.avail_out = static_cast<unsigned>(count);
    while (s.avail_out > 0) {
        if (m->ended) {
            return false;
        }
        if (s.avail_in == 0 && !m->eof) {
            const size_t got = std::fread(m->in.data(), 1, m->in.size(), m->file);
            m->eof = got == 0;
            s.next_in = m->in.data();
            s.avail_in = static_cast<unsigned>(got);
        }
        const unsigned before = s.avail_out;
        const int status = mz_inflate(&s, MZ_NO_FLUSH);
        if (status == MZ_STREAM_END) {
            m->ended = true;
        } else if (status != MZ_OK && status != MZ_BUF_ERROR) {
            m->ended = true; // damaged: what was read stands
            return false;
        }
        if (s.avail_out == before && s.avail_in == 0 && m->eof) {
            return false; // a recording cut short: the last record is partial
        }
    }
    return true;
}

std::optional<DemoRecord> DemoReader::next()
{
    uint8_t b[4];
    if (!fill(b, 1)) {
        return std::nullopt;
    }
    uint32_t delta = b[0];
    if (delta == LongGap) {
        if (!fill(b, 4)) {
            return std::nullopt;
        }
        delta = b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24;
    }
    if (!fill(b, 2)) {
        return std::nullopt;
    }
    DemoRecord record;
    record.data.resize(b[0] | b[1] << 8);
    if (!record.data.empty() && !fill(record.data.data(), record.data.size())) {
        return std::nullopt;
    }
    m_frame += delta;
    record.frame = m_frame;
    return record;
}

} // namespace fp::net
