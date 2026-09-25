#pragma once

// Demos (DemoFile.cs): every packet a client received, verbatim, stamped with
// the simulation frame it arrived on, so a playback hands each one to the same
// session code on the same frame. The C#'s format, byte for byte:
//
//   "FPDM" | version (2) | protocol      6 bytes, never compressed
//   --- raw deflate ---
//   [frame delta: 1 byte, 0xFF = escape + uint32] [length: uint16] [packet bytes] ...

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace fp::net {

constexpr const char* DemoExtension = ".fpdemo";

class DemoWriter {
public:
    // nullptr when the file cannot be created.
    static std::unique_ptr<DemoWriter> create(const std::filesystem::path& path);
    ~DemoWriter();
    void write(uint32_t frame, std::span<const uint8_t> packet);

private:
    DemoWriter() = default;
    void pack(std::span<const uint8_t> data, int flush);
    struct State;
    std::unique_ptr<State> m;
    uint32_t m_lastFrame = 0, m_lastFlushFrame = 0;
};

struct DemoRecord {
    uint32_t frame = 0;
    std::vector<uint8_t> data; // the packet, its type byte first
};

class DemoReader {
public:
    // nullptr when the file is not a demo this build reads.
    static std::unique_ptr<DemoReader> open(const std::filesystem::path& path);
    ~DemoReader();
    uint8_t protocol() const { return m_protocol; }
    std::optional<DemoRecord> next();

private:
    DemoReader() = default;
    bool fill(uint8_t* dest, size_t count);
    struct State;
    std::unique_ptr<State> m;
    uint8_t m_protocol = 0;
    uint32_t m_frame = 0;
};

} // namespace fp::net
