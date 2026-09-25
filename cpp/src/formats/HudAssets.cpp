#include "HudAssets.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace fp {

namespace {

template <typename T>
T readAt(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("HUD file truncated");
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::vector<uint8_t> readGameFile(const std::filesystem::path& root, const std::string& file)
{
    return readFile(resolveCaseInsensitive(root, file));
}

} // namespace

uint32_t rgb15ToRgba(uint16_t value, uint8_t alpha)
{
    auto channel = [](uint32_t v) { return static_cast<uint32_t>(std::lround((v & 0x1F) / 31.0f * 255.0f)); };
    return channel(value) | channel(value >> 5) << 8 | channel(value >> 10) << 16 | static_cast<uint32_t>(alpha) << 24;
}

uint8_t HudObject::index(int frame, int x, int y) const
{
    const int tilesX = width / 8;
    const size_t i = static_cast<size_t>(frame) * width * height + (y / 8) * tilesX * 64 + (x / 8) * 64 + (y % 8) * 8 + x % 8;
    return i < indices.size() ? indices[i] : 0;
}

HudObject loadHudObject(const std::filesystem::path& root, const std::string& file)
{
    const std::vector<uint8_t> bytes = readGameFile(root, file);
    // UiObjectHeader
    const auto paramSize = readAt<int32_t>(bytes, 8);
    const auto attrSize = readAt<int32_t>(bytes, 12);
    const auto charSize = readAt<int32_t>(bytes, 16);
    const auto palSize = readAt<int32_t>(bytes, 20);
    size_t offset = 24;
    HudObject obj;
    // UiAnimParams: ImageIndex, Delay, then 14 bytes the game does not use here.
    for (int i = 0; i < paramSize / 16; i++) {
        const uint8_t image = bytes.at(offset + i * 16);
        const uint8_t delay = bytes.at(offset + i * 16 + 1);
        obj.animFrames.insert(obj.animFrames.end(), delay, image);
    }
    offset += paramSize;
    // The first OAM entry's shape and size are the sprite's dimensions.
    const auto attr0 = readAt<uint16_t>(bytes, offset);
    const auto attr1 = readAt<uint16_t>(bytes, offset + 2);
    static constexpr int dims[3][4][2] = {
        {{1, 1}, {2, 2}, {4, 4}, {8, 8}},
        {{2, 1}, {4, 1}, {4, 2}, {8, 4}},
        {{1, 2}, {1, 4}, {2, 4}, {4, 8}},
    };
    const int shape = std::min(attr0 >> 14, 2);
    const int size = attr1 >> 14;
    obj.width = dims[shape][size][0] * 8;
    obj.height = dims[shape][size][1] * 8;
    offset += attrSize;
    obj.indices.reserve(charSize * 2);
    for (int i = 0; i < charSize; i++) {
        const uint8_t data = bytes.at(offset + i);
        obj.indices.push_back(data & 0xF);
        obj.indices.push_back(data >> 4);
    }
    offset += charSize;
    for (int i = 0; i < palSize / 2; i++) {
        obj.palette.push_back(rgb15ToRgba(readAt<uint16_t>(bytes, offset + i * 2)));
    }
    return obj;
}

Image loadCharMap(const std::filesystem::path& root, const std::string& file, int startX, int startY, int tilesX,
    int tilesY, const std::vector<uint16_t>* paletteOverride, std::vector<uint16_t>* paletteOut)
{
    const std::vector<uint8_t> bytes = readGameFile(root, file);
    // UiPartHeader
    const auto charSize = readAt<int32_t>(bytes, 4);
    const auto palSize = readAt<int32_t>(bytes, 8);
    size_t offset = 12;
    const size_t charOffset = offset;
    offset += charSize;
    std::vector<uint16_t> palette(palSize / 2);
    for (size_t i = 0; i < palette.size(); i++) {
        palette[i] = readAt<uint16_t>(bytes, offset + i * 2);
    }
    offset += palSize;
    // ScrDatInfo
    const auto charsX = readAt<uint16_t>(bytes, offset);
    const auto charsY = readAt<uint16_t>(bytes, offset + 2);
    const auto scrSize = readAt<int32_t>(bytes, offset + 4);
    offset += 8;
    std::vector<uint16_t> screen(scrSize / 2);
    for (size_t i = 0; i < screen.size(); i++) {
        screen[i] = readAt<uint16_t>(bytes, offset + i * 2);
    }
    if (paletteOverride) {
        std::vector<uint16_t> replaced{palette.at(0), palette.at(1)};
        replaced.insert(replaced.end(), paletteOverride->begin() + 1, paletteOverride->end());
        palette = std::move(replaced);
    }
    if (paletteOut) {
        *paletteOut = palette;
    }
    if (tilesX == 0) {
        tilesX = charsX;
    }
    if (tilesY == 0) {
        tilesY = charsY;
    }
    Image image;
    image.width = tilesX * 8;
    image.height = tilesY * 8;
    image.rgba.assign(static_cast<size_t>(image.width) * image.height, 0);
    image.opaque = false;
    const int rowStride = charsX > 32 ? charsX / 2 : charsX;
    for (int cy = 0; cy < tilesY; cy++) {
        const int icy = cy + startY;
        for (int cx = 0; cx < tilesX; cx++) {
            const int icx = cx + startX;
            // Maps wider than 32 tiles keep their right half 0x400 entries on.
            const size_t idx = icy * rowStride + (icx / 32 == 1 ? 0x400 + (icx - 32) : icx);
            if (idx >= screen.size()) {
                continue;
            }
            const uint16_t entry = screen[idx];
            const int charId = entry & 0x3FF;
            const bool flipH = entry & 0x400;
            const bool flipV = entry & 0x800;
            for (int py = 0; py < 8; py++) {
                const int iy = flipV ? 7 - py : py;
                for (int px = 0; px < 8; px++) {
                    const int ix = flipH ? 7 - px : px;
                    const size_t byte = charOffset + charId * 32 + iy * 4 + ix / 2;
                    if (byte >= charOffset + charSize) {
                        continue;
                    }
                    const uint8_t data = bytes[byte];
                    const int index = ix % 2 == 0 ? data & 0xF : data >> 4;
                    if (index != 0 && index < static_cast<int>(palette.size())) {
                        image.rgba[(cy * 8 + py) * image.width + cx * 8 + px] = rgb15ToRgba(palette[index]);
                    }
                }
            }
        }
    }
    return image;
}

int HudFont::glyph(int ch) const
{
    const int index = ch - minCharacter;
    const int count = std::min<int>(widths.size(), offsets.size());
    if (index >= 0 && index < count) {
        return index;
    }
    const int fallback = '?' - minCharacter;
    return fallback >= 0 && fallback < count ? fallback : 0;
}

HudFont loadFont(const std::filesystem::path& root)
{
    // Extract._romData: FontWidths, FontOffsets, FontCharData in arm9.bin.
    struct Offsets {
        const char* version;
        uint32_t widths, offsets, chars;
    };
    static constexpr Offsets table[] = {
        {"AMHE0", 0xBF9B0, 0xBFB90, 0xC0270},
        {"AMHE1", 0xC020C, 0xC03EC, 0xC0ACC},
        {"AMHP0", 0xC022C, 0xC040C, 0xC0AEC},
        {"AMHP1", 0xC02AC, 0xC048C, 0xC0B6C},
        {"AMHJ0", 0xC1754, 0xC1934, 0xC2014},
        {"AMHJ1", 0xC1714, 0xC18F4, 0xC1FD4},
        {"AMHK0", 0xBD580, 0xBD760, 0xB9560},
        {"A76E0", 0x95C68, 0x95A88, 0x96348},
        {"NTRJ0", 0x1FC07C, 0x1FC25C, 0x1FC93C},
    };
    std::vector<uint8_t> arm9;
    try {
        arm9 = readGameFile(root, "_bin/arm9.bin");
    } catch (const std::exception&) {
        return {};
    }
    const std::string folder = root.filename().string();
    auto build = [&](const Offsets& o) {
        HudFont font;
        if (o.chars + 0x4000 > arm9.size() || o.widths + 480 > arm9.size() || o.offsets + 480 > arm9.size()) {
            return font;
        }
        for (int i = 0; i < 480; i++) {
            font.widths.push_back(arm9[o.widths + i]);
            font.offsets.push_back(static_cast<int8_t>(arm9[o.offsets + i]));
        }
        for (int i = 0; i < 0x4000; i++) {
            font.indices.push_back(arm9[o.chars + i] & 0xF);
            font.indices.push_back(arm9[o.chars + i] >> 4);
        }
        return font;
    };
    for (const Offsets& o : table) {
        if (folder.find(o.version) != std::string::npos) {
            return build(o);
        }
    }
    // Unknown folder name: the release whose table reads like a font -- digits
    // all the same width, and every glyph narrower than its 8 pixels.
    for (const Offsets& o : table) {
        HudFont font = build(o);
        if (!font.valid()) {
            continue;
        }
        const int zero = font.widths['0' - 32];
        bool plausible = zero > 0 && zero <= 9;
        for (int d = 1; d < 10 && plausible; d++) {
            plausible = font.widths['0' + d - 32] == zero;
        }
        for (int i = 0; i < 96 && plausible; i++) {
            plausible = font.widths[i] <= 9;
        }
        if (plausible) {
            return font;
        }
    }
    return {};
}

std::string HudStrings::hudMessage(int id)
{
    if (id >= 1 && id <= 11) {
        return message('H', id, "HudMsgsCommon.bin");
    }
    if (id >= 101 && id <= 122) {
        return message('H', id, "HudMessagesSP.bin");
    }
    if (id >= 201 && id <= 257) {
        return message('H', id, "HudMessagesMP.bin");
    }
    if (id >= 301 && id <= 305) {
        return message('W', id - 300, "HudMessagesMP.bin");
    }
    return " ";
}

std::string HudStrings::message(char type, int id, const std::string& table)
{
    auto it = std::find_if(m_tables.begin(), m_tables.end(), [&](const auto& t) { return t.first == table; });
    if (it == m_tables.end()) {
        std::vector<std::pair<std::string, std::string>> entries;
        try {
            const std::vector<uint8_t> bytes = readGameFile(m_root, "stringTables/" + table);
            const auto count = readAt<uint32_t>(bytes, 0);
            for (uint32_t i = 0; i < count; i++) {
                const size_t e = 4 + i * 12;
                // RawStringTableEntry: the id's four characters are stored reversed.
                std::string entryId(reinterpret_cast<const char*>(bytes.data() + e), 4);
                std::reverse(entryId.begin(), entryId.end());
                const auto offset = readAt<uint32_t>(bytes, e + 4);
                const auto length = readAt<uint16_t>(bytes, e + 8);
                if (offset >= bytes.size()) {
                    continue;
                }
                std::string value;
                for (size_t j = 0; j < length && offset + j < bytes.size() && bytes[offset + j] != 0; j++) {
                    if (bytes[offset + j] != '$') {
                        value.push_back(static_cast<char>(bytes[offset + j]));
                    }
                }
                if (std::count(value.begin(), value.end(), '\\') == 1) {
                    value = value.substr(0, value.find('\\'));
                }
                entries.emplace_back(entryId, value);
            }
        } catch (const std::exception&) {
        }
        m_tables.emplace_back(table, std::move(entries));
        it = m_tables.end() - 1;
    }
    char wanted[8];
    std::snprintf(wanted, sizeof(wanted), "%c%03d", type, id);
    for (const auto& [entryId, value] : it->second) {
        if (entryId == wanted) {
            return value;
        }
    }
    return " ";
}

} // namespace fp
