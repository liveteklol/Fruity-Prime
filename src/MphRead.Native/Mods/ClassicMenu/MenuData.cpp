#include "MenuData.hpp"

#include "../../Formats/Formats.hpp"
#include "../../Formats/Model.hpp"
#include "../../Read.hpp"
#include "../../Strings.hpp"
#include "../../Utility/Extract.hpp"
#include "../../NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace MphRead::Mods::ClassicMenu
{
    namespace
    {
        using OpenTK::Mathematics::Matrix4;

        // Little-endian reads over the file, 20.12 fixed point for Fx.
        class Reader final
        {
        public:
            explicit Reader(const std::vector<std::uint8_t>& data) : _data(data) {}

            [[nodiscard]] std::uint8_t U8(std::uint32_t o) const { return _data.at(o); }
            [[nodiscard]] std::uint16_t U16(std::uint32_t o) const
            {
                return static_cast<std::uint16_t>(U8(o) | (U8(o + 1) << 8));
            }
            [[nodiscard]] std::uint32_t U32(std::uint32_t o) const
            {
                return static_cast<std::uint32_t>(U16(o)) | (static_cast<std::uint32_t>(U16(o + 2)) << 16);
            }
            [[nodiscard]] std::int32_t I32(std::uint32_t o) const { return static_cast<std::int32_t>(U32(o)); }
            [[nodiscard]] float Fx(std::uint32_t o) const { return static_cast<float>(I32(o)) / 4096.0F; }

            [[nodiscard]] std::vector<std::uint32_t> List(std::uint32_t o) const
            {
                std::vector<std::uint32_t> list;
                if (o == 0) return list;
                for (;; o += 4)
                {
                    const std::uint32_t v = U32(o);
                    if (v == 0) return list;
                    list.push_back(v);
                }
            }

            [[nodiscard]] std::string CString(std::uint32_t o) const
            {
                std::string s;
                for (; o < _data.size() && _data[o] != 0; ++o) s.push_back(static_cast<char>(_data[o]));
                return s;
            }

        private:
            const std::vector<std::uint8_t>& _data;
        };

        MenuAction ReadAction(const Reader& r, std::uint32_t o)
        {
            MenuAction a;
            for (std::uint32_t p : r.List(r.U32(o + 16)))
            {
                a.Calls.emplace_back(r.I32(p), r.I32(p + 4));
            }
            a.Kind = r.U16(o);
            a.Field2 = r.U8(o + 2);
            a.Flags = r.U8(o + 3);
            a.Field4 = r.U32(o + 4);
            a.RectX = static_cast<std::int16_t>(r.U16(o + 8));
            a.RectY = static_cast<std::int16_t>(r.U16(o + 10));
            a.RectW = static_cast<std::int16_t>(r.U16(o + 12));
            a.RectH = static_cast<std::int16_t>(r.U16(o + 14));
            a.Item = r.U16(o + 20);
            a.TargetPage = r.U8(o + 22);
            a.Field17 = r.U8(o + 23);
            return a;
        }

        MenuItem ReadItem(const Reader& r, std::uint32_t o)
        {
            MenuItem item;
            for (std::uint32_t so : r.List(r.U32(o + 12)))
            {
                MenuItemState state;
                state.Kind = r.U8(so);
                state.Code = r.U8(so + 1);
                const std::uint32_t value = r.U32(so + 4);
                if (state.Kind == 1)
                {
                    const std::uint32_t word = r.U32(value);
                    const std::uint32_t style = r.U32(value + 4);
                    const std::uint32_t format = r.U32(style + 16);
                    MenuTextStyle text;
                    text.StringId = static_cast<int>(word & 0xFFFF);
                    text.StartColor = r.U32(style);
                    text.EndColor = r.U32(style + 4);
                    text.WrapWidth = r.U16(style + 8);
                    text.Duration = r.Fx(style + 12);
                    text.Size = static_cast<std::uint8_t>(format);
                    text.Align = static_cast<std::uint8_t>(format >> 24);
                    state.Text = text;
                }
                else
                {
                    const int widget = static_cast<int>(value & 0xFFFF);
                    state.WidgetIndex = widget == 0xFFFF ? -1 : widget;
                }
                item.States.push_back(std::move(state));
            }
            for (std::uint32_t lo : r.List(r.U32(o + 8)))
            {
                item.Links.push_back(MenuLink{r.U8(lo), static_cast<MenuState>(r.U8(lo + 1)), r.U16(lo + 2)});
            }
            for (std::uint32_t ao : r.List(r.U32(o + 4)))
            {
                item.Actions.push_back(ReadAction(r, ao));
            }
            item.Index = r.U16(o + 54);
            item.Delay = r.Fx(o + 20);
            item.X = r.Fx(o + 32);
            item.Y = r.Fx(o + 36);
            item.Depth = r.Fx(o + 40);
            item.InitialState = r.U8(o + 52);
            return item;
        }

        [[nodiscard]] std::string FixPath(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
        }

        // The game's text space is a single 5-bit channel per colour byte.
        [[nodiscard]] float Channel(std::uint32_t color, int shift)
        {
            return static_cast<float>((color >> shift) & 0x1F);
        }

        [[nodiscard]] int Sign10(std::uint32_t v)
        {
            return (v & 0x200) != 0 ? static_cast<int>(v | 0xFFFFFC00U) : static_cast<int>(v);
        }

        [[nodiscard]] int Sign16(std::uint32_t v)
        {
            return (v & 0x8000) != 0 ? static_cast<int>(v | 0xFFFF0000U) : static_cast<int>(v);
        }

        constexpr int Stride = 13;
        constexpr float MaterialColor = 2.0F;

        // One mesh's display list as a flat triangle list, reproducing
        // Scene.DoDlist's immediate-mode state machine. Per vertex:
        // pos.xyz, normal.xyz, color.rgba, uv.st, matrix-stack index; color.a
        // MaterialColor means "no colour command yet: the material diffuse".
        std::vector<float> DecodeDisplayList(const Model& model, const Mesh& mesh)
        {
            const Material& material = *model.Materials->at(static_cast<std::size_t>(mesh.MaterialId));
            int textureWidth = 0;
            int textureHeight = 0;
            if (material.TextureId != -1)
            {
                const auto& texture = model.Recolors->at(0)->Textures->at(static_cast<std::size_t>(material.TextureId));
                textureWidth = texture.Width;
                textureHeight = texture.Height;
            }
            const bool texgen = material.TexgenMode == TexgenMode::Normal;
            const auto& list = *model.RenderInstructionLists->at(static_cast<std::size_t>(mesh.DlistId));

            std::vector<float> output;
            std::vector<float> prim;
            int primType = -1;
            float vx = 0, vy = 0, vz = 0;
            float nx = 0, ny = 0, nz = 1;
            float cr = 1, cg = 1, cb = 1, ca = MaterialColor;
            float s = texgen ? 0.5F : 0.0F;
            float t = texgen ? 0.5F : 0.0F;
            float matrixId = 0;

            const auto vertex = [&]
            {
                const float v[Stride]{vx, vy, vz, nx, ny, nz, cr, cg, cb, ca, s, t, matrixId};
                prim.insert(prim.end(), v, v + Stride);
            };
            const auto copy = [&](int index)
            {
                const auto start = prim.begin() + static_cast<std::ptrdiff_t>(index) * Stride;
                output.insert(output.end(), start, start + Stride);
            };
            const auto tri = [&](int a, int b, int c) { copy(a); copy(b); copy(c); };
            const auto endPrimitive = [&]
            {
                const int n = static_cast<int>(prim.size() / Stride);
                switch (primType)
                {
                case 0:
                    for (int i = 0; i + 2 < n; i += 3) tri(i, i + 1, i + 2);
                    break;
                case 1:
                    for (int i = 0; i + 3 < n; i += 4) { tri(i, i + 1, i + 2); tri(i, i + 2, i + 3); }
                    break;
                case 2:
                    for (int i = 2; i < n; i++)
                    {
                        if ((i & 1) == 0) tri(i - 2, i - 1, i);
                        else tri(i - 1, i - 2, i);
                    }
                    break;
                case 3:
                    for (int i = 0; i + 3 < n; i += 2) { tri(i, i + 1, i + 3); tri(i, i + 3, i + 2); }
                    break;
                default:
                    break;
                }
                prim.clear();
                primType = -1;
            };

            for (const auto& instruction : list)
            {
                const RenderInstruction& ins = *instruction;
                const auto arg = [&](std::size_t i) { return ins.Arguments->at(i); };
                switch (ins.Code)
                {
                case InstructionCode::BEGIN_VTXS:
                    if (primType != -1) endPrimitive();
                    primType = static_cast<int>(arg(0));
                    if (primType > 3) throw std::runtime_error("Invalid geometry type");
                    break;
                case InstructionCode::END_VTXS:
                    endPrimitive();
                    break;
                case InstructionCode::COLOR:
                case InstructionCode::DIF_AMB:
                {
                    const std::uint32_t rgb = arg(0);
                    cr = static_cast<float>(rgb & 0x1F) / 31.0F;
                    cg = static_cast<float>((rgb >> 5) & 0x1F) / 31.0F;
                    cb = static_cast<float>((rgb >> 10) & 0x1F) / 31.0F;
                    ca = ins.Code == InstructionCode::COLOR ? 1.0F : 0.0F;
                    break;
                }
                case InstructionCode::NORMAL:
                {
                    const std::uint32_t xyz = arg(0);
                    nx = static_cast<float>(Sign10(xyz & 0x3FF)) / 512.0F;
                    ny = static_cast<float>(Sign10((xyz >> 10) & 0x3FF)) / 512.0F;
                    nz = static_cast<float>(Sign10((xyz >> 20) & 0x3FF)) / 512.0F;
                    break;
                }
                case InstructionCode::TEXCOORD:
                {
                    const std::uint32_t st = arg(0);
                    if (textureWidth > 0 && textureHeight > 0)
                    {
                        s = static_cast<float>(Sign16(st & 0xFFFF)) / 16.0F / static_cast<float>(textureWidth);
                        t = static_cast<float>(Sign16((st >> 16) & 0xFFFF)) / 16.0F / static_cast<float>(textureHeight);
                    }
                    break;
                }
                case InstructionCode::VTX_16:
                {
                    const std::uint32_t xy = arg(0);
                    vx = Fixed::ToFloat(Sign16(xy & 0xFFFF));
                    vy = Fixed::ToFloat(Sign16((xy >> 16) & 0xFFFF));
                    vz = Fixed::ToFloat(Sign16(arg(1) & 0xFFFF));
                    vertex();
                    break;
                }
                case InstructionCode::VTX_10:
                {
                    const std::uint32_t xyz = arg(0);
                    vx = static_cast<float>(Sign10(xyz & 0x3FF)) / 64.0F;
                    vy = static_cast<float>(Sign10((xyz >> 10) & 0x3FF)) / 64.0F;
                    vz = static_cast<float>(Sign10((xyz >> 20) & 0x3FF)) / 64.0F;
                    vertex();
                    break;
                }
                case InstructionCode::VTX_XY:
                {
                    const std::uint32_t xy = arg(0);
                    vx = Fixed::ToFloat(Sign16(xy & 0xFFFF));
                    vy = Fixed::ToFloat(Sign16((xy >> 16) & 0xFFFF));
                    vertex();
                    break;
                }
                case InstructionCode::VTX_XZ:
                {
                    const std::uint32_t xz = arg(0);
                    vx = Fixed::ToFloat(Sign16(xz & 0xFFFF));
                    vz = Fixed::ToFloat(Sign16((xz >> 16) & 0xFFFF));
                    vertex();
                    break;
                }
                case InstructionCode::VTX_YZ:
                {
                    const std::uint32_t yz = arg(0);
                    vy = Fixed::ToFloat(Sign16(yz & 0xFFFF));
                    vz = Fixed::ToFloat(Sign16((yz >> 16) & 0xFFFF));
                    vertex();
                    break;
                }
                case InstructionCode::VTX_DIFF:
                {
                    const std::uint32_t xyz = arg(0);
                    vx += Fixed::ToFloat(Sign10(xyz & 0x3FF));
                    vy += Fixed::ToFloat(Sign10((xyz >> 10) & 0x3FF));
                    vz += Fixed::ToFloat(Sign10((xyz >> 20) & 0x3FF));
                    vertex();
                    break;
                }
                case InstructionCode::MTX_RESTORE:
                    matrixId = static_cast<float>(arg(0));
                    break;
                default:
                    break;
                }
            }
            if (primType != -1) endPrimitive();
            return output;
        }

        // (x, y, z, 1) * m, OpenTK's row-vector convention.
        void Transform(const Matrix4& m, float x, float y, float z, float& ox, float& oy, float& oz)
        {
            ox = x * m.M11 + y * m.M21 + z * m.M31 + m.M41;
            oy = x * m.M12 + y * m.M22 + z * m.M32 + m.M42;
            oz = x * m.M13 + y * m.M23 + z * m.M33 + m.M43;
        }

        [[nodiscard]] int GroupFrame(int frame, int groupFrames, bool loop)
        {
            if (groupFrames <= 1) return 0;
            return loop ? frame % groupFrames : std::min(frame, groupFrames - 1);
        }
    }

    // ---- MenuFile ----

    MenuFile MenuFile::Parse(const std::vector<std::uint8_t>& data)
    {
        if (data.size() < 16 || std::memcmp(data.data(), "MARM", 4) != 0)
        {
            throw std::runtime_error("not a MARM menu file");
        }
        const Reader r(data);
        MenuFile file;
        for (std::uint32_t o : r.List(r.U32(12)))
        {
            std::optional<std::string> model;
            std::optional<std::string> anim;
            for (std::uint32_t f : r.List(r.U32(o + 8)))
            {
                const std::uint32_t nameOffset = r.U32(f + 8);
                std::optional<std::string> name;
                if (nameOffset != 0) name = r.CString(nameOffset);
                if (!model.has_value()) model = name.value_or("");
                else anim = name;
            }
            file.Widgets.push_back(MenuWidget{model.value_or(""), anim});
        }
        for (std::uint32_t o : r.List(r.U32(8)))
        {
            MenuPage page;
            page.Index = static_cast<int>(file.Pages.size());
            for (std::uint32_t io : r.List(r.U32(o + 8))) page.Items.push_back(ReadItem(r, io));
            for (std::uint32_t ao : r.List(r.U32(o + 12))) page.Actions.push_back(ReadAction(r, ao));
            for (std::uint32_t to : r.List(r.U32(o + 16)))
            {
                page.Timers.push_back(MenuTimer{r.Fx(to), ReadAction(r, to + 4)});
            }
            file.Pages.push_back(std::move(page));
        }
        return file;
    }

    // ---- MenuStrings ----

    MenuStrings MenuStrings::Load(const std::string& fileSystemRoot, const std::string& lang)
    {
        const std::vector<std::uint8_t> data = NativeRuntime::FileReadAllBytes(
            Paths::Combine(fileSystemRoot, "frontend", "metroidhunters_text_" + lang + ".bin"));
        const Reader r(data);
        MenuStrings strings;
        for (std::uint32_t o = 0;; o += 4)
        {
            const std::uint32_t entry = r.U32(o);
            if (entry == 0) break;
            const std::uint32_t offset = r.U32(entry);
            const std::uint16_t length = r.U16(entry + 8);
            std::string text;
            for (std::uint32_t i = 0; i < length && offset + i < data.size() && data[offset + i] != 0; ++i)
            {
                text.push_back(static_cast<char>(data[offset + i]));
            }
            strings._strings.push_back(std::move(text));
        }
        return strings;
    }

    const std::string& MenuStrings::operator[](int id) const
    {
        static const std::string empty;
        return id >= 0 && static_cast<std::size_t>(id) < _strings.size() ? _strings[static_cast<std::size_t>(id)] : empty;
    }

    int MenuStrings::IndexOf(const std::string& text) const
    {
        for (std::size_t i = 0; i < _strings.size(); ++i)
        {
            if (_strings[i] == text) return static_cast<int>(i);
        }
        return -1;
    }

    void MenuStrings::Fill(int id, const std::string& text)
    {
        if (id >= 0 && static_cast<std::size_t>(id) < _strings.size()) _strings[static_cast<std::size_t>(id)] = text;
    }

    int MenuStrings::Add(const std::string& text)
    {
        _strings.push_back(text);
        return static_cast<int>(_strings.size()) - 1;
    }

    // ---- textures and the draw list ----

    int UiTextureCache::GetOrAdd(const std::string& key, const std::function<UiTexture()>& create)
    {
        if (const auto found = _byKey.find(key); found != _byKey.end()) return found->second;
        auto texture = std::make_unique<UiTexture>(create());
        texture->Id = static_cast<int>(_byId.size());
        _byId.push_back(std::move(texture));
        _byKey.emplace(key, _byId.back()->Id);
        return _byId.back()->Id;
    }

    void UiDrawList::Triangle(int textureId, UiWrap wrapS, UiWrap wrapT,
        const UiVertex& a, const UiVertex& b, const UiVertex& c)
    {
        if (Batches.empty() || Batches.back().TextureId != textureId
            || Batches.back().WrapS != wrapS || Batches.back().WrapT != wrapT)
        {
            Batches.push_back(UiBatch{textureId, wrapS, wrapT, static_cast<int>(Vertices.size()), 0});
        }
        Vertices.push_back(a);
        Vertices.push_back(b);
        Vertices.push_back(c);
        Batches.back().Count += 3;
    }

    void UiDrawList::Quad(int textureId, float x0, float y0, float x1, float y1,
        float u0, float v0, float u1, float v1, float r, float g, float b, float a)
    {
        const UiVertex p0{x0, y0, u0, v0, r, g, b, a};
        const UiVertex p1{x1, y0, u1, v0, r, g, b, a};
        const UiVertex p2{x1, y1, u1, v1, r, g, b, a};
        const UiVertex p3{x0, y1, u0, v1, r, g, b, a};
        Triangle(textureId, UiWrap::Clamp, UiWrap::Clamp, p0, p1, p2);
        Triangle(textureId, UiWrap::Clamp, UiWrap::Clamp, p0, p2, p3);
    }

    // ---- MenuFont ----

    namespace
    {
        constexpr int MinCharacter = 32;
        constexpr int Cell = 8;
        constexpr int Columns = 16;
    }

    MenuFont::MenuFont(UiTextureCache& textures)
    {
        // The glyph widths and offsets come from the game's code (arm9.bin),
        // read with the rest of its runtime data when a scene is first set
        // up; the front screen may be shown before that ever happens.
        if (const auto& font = Text::Font::Normal(); !font || !font->Widths() || font->Widths()->empty())
        {
            Extract::LoadRuntimeData();
        }
        if (const auto& font = Text::Font::Normal(); font && font->Widths())
        {
            _widths.assign(font->Widths()->begin(), font->Widths()->end());
            if (font->Offsets()) _offsets.assign(font->Offsets()->begin(), font->Offsets()->end());
        }
        const std::shared_ptr<Model> model = Read::ReadModelFile("hudfont", "models/hudfont_Model.bin", std::nullopt);
        const Material& material = *model->Materials->at(0);
        const auto& texture = model->Recolors->at(0)->Textures->at(static_cast<std::size_t>(material.TextureId));
        _atlasWidth = texture.Width;
        _atlasHeight = texture.Height;
        _textureId = textures.GetOrAdd("hudfont", [&]
        {
            UiTexture result;
            result.Width = texture.Width;
            result.Height = texture.Height;
            result.Pixels = model->GetPixels(material.TextureId, material.PaletteId, 0);
            return result;
        });
    }

    int MenuFont::Glyph(const std::string& text, std::size_t& i)
    {
        int ch = static_cast<unsigned char>(text[i]);
        if ((ch & 0x80) != 0 && i + 1 < text.size())
        {
            ch = (static_cast<unsigned char>(text[++i]) & 0x3F) | ((ch & 0x1F) << 6);
        }
        return ch - MinCharacter;
    }

    int MenuFont::Advance(int glyph) const
    {
        return glyph >= 0 && static_cast<std::size_t>(glyph) < _widths.size() ? _widths[static_cast<std::size_t>(glyph)] : Cell;
    }

    float MenuFont::Measure(const std::string& line) const
    {
        float width = 0;
        for (std::size_t i = 0; i < line.size(); ++i) width += static_cast<float>(Advance(Glyph(line, i)));
        return width;
    }

    std::vector<std::string> MenuFont::Lines(const std::string& text, int wrapWidth) const
    {
        std::vector<std::string> lines;
        std::size_t start = 0;
        for (;;)
        {
            const std::size_t end = text.find('\n', start);
            const std::string raw = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (wrapWidth <= 0 || Measure(raw) <= static_cast<float>(wrapWidth))
            {
                lines.push_back(raw);
            }
            else
            {
                std::string current;
                std::size_t wordStart = 0;
                for (;;)
                {
                    const std::size_t space = raw.find(' ', wordStart);
                    const std::string word = raw.substr(wordStart,
                        space == std::string::npos ? std::string::npos : space - wordStart);
                    const std::string candidate = current.empty() ? word : current + " " + word;
                    if (!current.empty() && Measure(candidate) > static_cast<float>(wrapWidth))
                    {
                        lines.push_back(current);
                        current = word;
                    }
                    else
                    {
                        current = candidate;
                    }
                    if (space == std::string::npos) break;
                    wordStart = space + 1;
                }
                lines.push_back(current);
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return lines;
    }

    void MenuFont::Emit(const std::string& text, float x, float y, int align, int wrapWidth, float lineHeight,
        float r, float g, float b, float a, float z, std::vector<WidgetTri>& output) const
    {
        // The anchor is the bottom of the first line, in text coordinates (Y
        // up from the bottom of the touch screen): menu-space y = textY - 192.
        float top = y - 192.0F + lineHeight;
        for (const std::string& line : Lines(text, wrapWidth))
        {
            const float width = Measure(line);
            float penX = align == 1 ? x - width : align == 2 ? x - std::floor(width / 2.0F) : x;
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                const bool space = line[i] == ' ';
                const int glyph = Glyph(line, i);
                if (!space && glyph >= 0 && static_cast<std::size_t>(glyph) < std::max<std::size_t>(_widths.size(), 96))
                {
                    const float offset = static_cast<std::size_t>(glyph) < _offsets.size()
                        ? static_cast<float>(_offsets[static_cast<std::size_t>(glyph)]) : 0.0F;
                    const float gy = top - offset;
                    constexpr float Inset = 0.02F;
                    const float u0 = (static_cast<float>((glyph % Columns) * Cell) + Inset) / static_cast<float>(_atlasWidth);
                    const float v0 = (static_cast<float>((glyph / Columns) * Cell) + Inset) / static_cast<float>(_atlasHeight);
                    const float u1 = u0 + (Cell - 2 * Inset) / static_cast<float>(_atlasWidth);
                    const float v1 = v0 + (Cell - 2 * Inset) / static_cast<float>(_atlasHeight);
                    const UiVertex p0{penX, gy, u0, v0, r, g, b, a};
                    const UiVertex p1{penX + Cell, gy, u1, v0, r, g, b, a};
                    const UiVertex p2{penX + Cell, gy - Cell, u1, v1, r, g, b, a};
                    const UiVertex p3{penX, gy - Cell, u0, v1, r, g, b, a};
                    output.push_back(WidgetTri{p0, p1, p2, z, 0, _textureId});
                    output.push_back(WidgetTri{p0, p2, p3, z, 0, _textureId});
                }
                penX += static_cast<float>(Advance(glyph));
            }
            top -= lineHeight;
        }
    }

    // ---- MenuWidgets ----

    MenuWidgets::MenuWidgets(const MenuFile& file, UiTextureCache& textures) : _file(file), _textures(textures) {}

    std::shared_ptr<ModelInstance> MenuWidgets::Instance(int widgetIndex)
    {
        const MenuWidget& w = _file.Widgets.at(static_cast<std::size_t>(widgetIndex));
        return Instance(w.ModelPath, w.AnimPath);
    }

    std::shared_ptr<ModelInstance> MenuWidgets::Instance(const std::string& modelPath,
        const std::optional<std::string>& animPath)
    {
        const std::string key = modelPath + "|" + animPath.value_or("");
        if (const auto found = _instances.find(key); found != _instances.end()) return found->second;
        std::optional<std::string> anim;
        if (animPath.has_value()) anim = FixPath(*animPath);
        auto model = Read::ReadModelFile(key, FixPath(modelPath), anim);
        auto inst = std::make_shared<ModelInstance>(model);
        _instances.emplace(key, inst);
        return inst;
    }

    int MenuWidgets::FrameCount(int widgetIndex)
    {
        return FrameCount(*Instance(widgetIndex));
    }

    int MenuWidgets::FrameCount(const ModelInstance& inst)
    {
        const AnimationGroups& g = *inst.Model()->AnimationGroups;
        int count = 1;
        if (!g.Node->empty()) count = std::max(count, g.Node->front()->FrameCount);
        if (!g.Material->empty()) count = std::max(count, g.Material->front()->FrameCount);
        if (!g.Texcoord->empty()) count = std::max(count, g.Texcoord->front()->FrameCount);
        if (!g.Texture->empty()) count = std::max(count, g.Texture->front()->FrameCount);
        return count;
    }

    void MenuWidgets::Evaluate(int widgetIndex, int frame, bool loop, float alpha, std::vector<WidgetTri>& output)
    {
        Evaluate(Instance(widgetIndex), frame, loop, alpha, output);
    }

    void MenuWidgets::Evaluate(const std::shared_ptr<ModelInstance>& inst, int frame, bool loop, float alpha,
        std::vector<WidgetTri>& output)
    {
        Model& model = *inst->Model();
        const AnimationGroups& groups = *model.AnimationGroups;
        const std::shared_ptr<AnimationInfo>& info = inst->AnimInfo;
        info->Node->Slot = info->Material->Slot = info->Texture->Slot = info->Texcoord->Slot = 0;
        info->Node->Group = groups.Node->empty() ? nullptr : groups.Node->front();
        info->Material->Group = groups.Material->empty() ? nullptr : groups.Material->front();
        info->Texture->Group = groups.Texture->empty() ? nullptr : groups.Texture->front();
        info->Texcoord->Group = groups.Texcoord->empty() ? nullptr : groups.Texcoord->front();

        (*info->Frame)[0] = GroupFrame(frame, info->Material->Group ? info->Material->Group->FrameCount : 0, loop);
        model.AnimateMaterials(info);
        (*info->Frame)[0] = GroupFrame(frame, info->Texture->Group ? info->Texture->Group->FrameCount : 0, loop);
        model.AnimateTextures(info);
        model.ComputeNodeMatrices(0);
        (*info->Frame)[0] = GroupFrame(frame, info->Node->Group ? info->Node->Group->FrameCount : 0, loop);
        model.AnimateNodes(0, true, OpenTK::Mathematics::IdentityMatrix(), model.Scale, info);
        model.UpdateMatrixStack();
        const int texcoordFrame = GroupFrame(frame, info->Texcoord->Group ? info->Texcoord->Group->FrameCount : 0, loop);

        auto found = _meshes.find(inst.get());
        if (found == _meshes.end())
        {
            std::vector<std::vector<float>> meshes;
            for (const auto& mesh : *model.Meshes) meshes.push_back(DecodeDisplayList(model, *mesh));
            found = _meshes.emplace(inst.get(), std::move(meshes)).first;
        }
        EmitNode(model, texcoordFrame, model.Nodes->empty() ? -1 : 0, found->second, alpha, output, inst);
    }

    void MenuWidgets::EmitNode(Model& model, int texcoordFrame, int index,
        const std::vector<std::vector<float>>& meshes, float alpha, std::vector<WidgetTri>& output,
        const std::shared_ptr<ModelInstance>& inst)
    {
        const std::shared_ptr<AnimationInfo>& info = inst->AnimInfo;
        for (int i = index; i != -1;)
        {
            const Node& node = *model.Nodes->at(static_cast<std::size_t>(i));
            if (node.Enabled)
            {
                const int start = node.MeshId / 2;
                for (int k = 0; k < node.MeshCount; ++k)
                {
                    const std::size_t meshIndex = static_cast<std::size_t>(start + k);
                    const Mesh& mesh = *model.Meshes->at(meshIndex);
                    const std::vector<float>& verts = meshes.at(meshIndex);
                    if (!mesh.Visible) continue;
                    const Material& material = *model.Materials->at(static_cast<std::size_t>(mesh.MaterialId));
                    const float matAlpha = material.CurrentAlpha * alpha;
                    if (matAlpha <= 0) continue;
                    int textureId = -1;
                    if (material.CurrentTextureId != -1)
                    {
                        textureId = TextureFor(model, material.CurrentTextureId, material.CurrentPaletteId);
                    }
                    Matrix4 texMatrix = OpenTK::Mathematics::IdentityMatrix();
                    if (info->Texcoord->Group && info->Texcoord->Group->Animations)
                    {
                        const auto& animations = *info->Texcoord->Group->Animations;
                        if (const auto anim = animations.find(material.Name); anim != animations.end())
                        {
                            texMatrix = model.AnimateTexcoords(info->Texcoord->Group, anim->second, texcoordFrame);
                        }
                    }
                    const auto diffuse = material.CurrentDiffuse;
                    const float scale = model.Scale.X;
                    const std::size_t count = verts.size() / Stride;
                    WidgetTri tri;
                    tri.TextureId = textureId;
                    tri.WrapS = static_cast<UiWrap>(material.XRepeat);
                    tri.WrapT = static_cast<UiWrap>(material.YRepeat);
                    float zSum = 0;
                    for (std::size_t v = 0; v < count; ++v)
                    {
                        const float* p = verts.data() + v * Stride;
                        Matrix4 m = node.Animation;
                        if (model.NodeMatrixIds && !model.NodeMatrixIds->empty())
                        {
                            const auto& s = *model.MatrixStackValues;
                            const std::size_t o = static_cast<std::size_t>(p[12]) * 16;
                            if (o + 15 < s.Length())
                            {
                                m = Matrix4(
                                    {s[o], s[o + 1], s[o + 2], s[o + 3]}, {s[o + 4], s[o + 5], s[o + 6], s[o + 7]},
                                    {s[o + 8], s[o + 9], s[o + 10], s[o + 11]}, {s[o + 12], s[o + 13], s[o + 14], s[o + 15]});
                            }
                        }
                        float x, y, z;
                        Transform(m, p[0], p[1], p[2], x, y, z);
                        float u, w, unused;
                        Transform(texMatrix, p[10], p[11], 0, u, w, unused);
                        const bool useMaterial = p[9] == MaterialColor;
                        UiVertex vertex{x * scale, y * scale, u, w,
                            useMaterial ? diffuse.X : p[6], useMaterial ? diffuse.Y : p[7],
                            useMaterial ? diffuse.Z : p[8], matAlpha};
                        zSum += z * scale;
                        switch (v % 3)
                        {
                        case 0: tri.A = vertex; break;
                        case 1: tri.B = vertex; break;
                        default:
                            tri.C = vertex;
                            tri.Z = zSum / 3.0F;
                            zSum = 0;
                            output.push_back(tri);
                            break;
                        }
                    }
                }
                if (node.ChildIndex != -1)
                {
                    EmitNode(model, texcoordFrame, node.ChildIndex, meshes, alpha, output, inst);
                }
            }
            i = node.NextIndex;
        }
    }

    int MenuWidgets::TextureFor(const Model& model, int textureId, int paletteId)
    {
        const std::string key = model.Name + "|" + std::to_string(textureId) + "|" + std::to_string(paletteId);
        return _textures.GetOrAdd(key, [&]
        {
            const auto& texture = model.Recolors->at(0)->Textures->at(static_cast<std::size_t>(textureId));
            UiTexture result;
            result.Width = texture.Width;
            result.Height = texture.Height;
            result.Pixels = model.GetPixels(textureId, paletteId, 0);
            return result;
        });
    }

    // ---- MenuEngine ----

    MenuEngine::MenuEngine(const MenuFile& file, const MenuStrings& strings, MenuWidgets& widgets, const MenuFont& font)
        : _file(file), _strings(strings), _widgets(widgets), _font(font)
    {
    }

    void MenuEngine::Enter(int page)
    {
        _page = &_file.Pages.at(static_cast<std::size_t>(page));
        ++_generation;
        _pageFrame = 0;
        _pendingPage = -1;
        _exitFrames = 0;
        _focus = -1;
        _items.assign(_page->Items.size(), ItemRuntime{});
        for (std::size_t i = 0; i < _items.size(); ++i) _items[i].Delay = _page->Items[i].Delay;
        if (OnPageEntered) OnPageEntered(page);
        StartItems();
    }

    void MenuEngine::GoTo(int page)
    {
        if (_page == nullptr)
        {
            Enter(page);
            return;
        }
        _pendingPage = page;
        _exitFrames = 0;
        for (std::size_t i = 0; i < _items.size(); ++i) SetState(static_cast<int>(i), MenuState::Hidden);
    }

    void MenuEngine::StartItems()
    {
        for (std::size_t i = 0; i < _items.size(); ++i)
        {
            if (!_items[i].Started && _items[i].Delay <= 0)
            {
                _items[i].Started = true;
                SetState(static_cast<int>(i), static_cast<MenuState>(_page->Items[i].InitialState));
            }
        }
    }

    void MenuEngine::Tick()
    {
        if (_page == nullptr) return;
        _pageFrame++;
        for (std::size_t i = 0; i < _items.size(); ++i)
        {
            ItemRuntime& rt = _items[i];
            if (!rt.Started)
            {
                rt.Delay -= 1;
                continue;
            }
            rt.Frame++;
            if (StateCode::IsTransition(rt.Code) && rt.Frame >= CodeLength(static_cast<int>(i), rt.Code))
            {
                Arrive(static_cast<int>(i));
            }
        }
        if (_pendingPage == -1)
        {
            StartItems();
            const int generation = _generation;
            const std::vector<MenuTimer> timers = _page->Timers;
            for (const MenuTimer& timer : timers)
            {
                if (_generation != generation) break;
                if (_pageFrame == static_cast<int>(std::ceil(timer.Delay))) Run(timer.Action, -1);
            }
        }
        else
        {
            _exitFrames++;
            bool done = true;
            for (const ItemRuntime& rt : _items)
            {
                if (StateCode::IsTransition(rt.Code)) done = false;
            }
            if (done || _exitFrames > 120) Enter(_pendingPage);
        }
    }

    const MenuItemState* MenuEngine::Visual(int item, int code) const
    {
        return _page->Items.at(static_cast<std::size_t>(item)).GetState(code);
    }

    std::string MenuEngine::ItemModelPath(int item) const
    {
        if (_page == nullptr || item < 0 || static_cast<std::size_t>(item) >= _items.size()) return "";
        const MenuItemState* visual = Visual(item, _items[static_cast<std::size_t>(item)].Code);
        return visual != nullptr && visual->WidgetIndex >= 0
            ? _file.Widgets.at(static_cast<std::size_t>(visual->WidgetIndex)).ModelPath : "";
    }

    int MenuEngine::CodeLength(int item, int code) const
    {
        const MenuItemState* s = Visual(item, code);
        if (s == nullptr) return 0;
        if (s->Text.has_value()) return static_cast<int>(std::ceil(s->Text->Duration));
        return s->WidgetIndex < 0 ? 0 : _widgets.FrameCount(s->WidgetIndex);
    }

    void MenuEngine::SetState(int item, MenuState target)
    {
        ItemRuntime& rt = _items.at(static_cast<std::size_t>(item));
        if (!rt.Started)
        {
            rt.Started = true;
            rt.Delay = 0;
        }
        MenuState from = rt.State;
        if (from == target && !StateCode::IsTransition(rt.Code)) return;
        if (target == MenuState::Focused && _focus != item)
        {
            const int old = _focus;
            _focus = item;
            if (old >= 0 && static_cast<std::size_t>(old) < _items.size()
                && _items[static_cast<std::size_t>(old)].State == MenuState::Focused)
            {
                SetState(old, MenuState::Idle);
            }
            from = rt.State;
        }
        else if (from == MenuState::Focused && target != MenuState::Focused && _focus == item)
        {
            _focus = -1;
        }
        int code = StateCode::Transition(from, target);
        if (Visual(item, code) == nullptr && Visual(item, StateCode::Transition(MenuState::Any, target)) != nullptr)
        {
            code = StateCode::Transition(MenuState::Any, target);
        }
        rt.State = target;
        rt.Frame = 0;
        const int generation = _generation;
        Fire(item, StateCode::Transition(from, target));
        Fire(item, StateCode::Transition(MenuState::Any, target));
        Fire(item, StateCode::Transition(from, MenuState::Any));
        if (_generation != generation) return;
        if (Visual(item, code) != nullptr && CodeLength(item, code) > 0)
        {
            _items[static_cast<std::size_t>(item)].Code = code;
        }
        else
        {
            Arrive(item);
        }
    }

    void MenuEngine::Arrive(int item)
    {
        ItemRuntime& rt = _items.at(static_cast<std::size_t>(item));
        const int justPlayed = rt.Code;
        rt.Code = static_cast<int>(rt.State);
        rt.Frame = 0;
        const MenuState arrived = rt.State;
        const int generation = _generation;
        Fire(item, static_cast<int>(arrived));
        if (_generation != generation) return;
        // Kind-0 actions run when their item arrives in state Field2 (or a
        // named FROM->TO transition just finished).
        const std::vector<MenuAction> actions = _page->Items.at(static_cast<std::size_t>(item)).Actions;
        for (const MenuAction& action : actions)
        {
            if (action.Kind == 0 && (action.Field2 == static_cast<int>(arrived) || action.Field2 == justPlayed))
            {
                Run(action, item);
                if (_generation != generation) return;
            }
        }
    }

    void MenuEngine::Fire(int item, int evt)
    {
        const int generation = _generation;
        const std::vector<MenuLink> links = _page->Items.at(static_cast<std::size_t>(item)).Links;
        for (const MenuLink& link : links)
        {
            if (link.Event == evt && static_cast<std::size_t>(link.Target) < _items.size() && link.Target != item)
            {
                SetState(link.Target, link.State);
                if (_generation != generation) return;
            }
        }
    }

    void MenuEngine::Run(const MenuAction& action, int item)
    {
        const MenuAction copy = action;
        const int generation = _generation;
        if (copy.Field17 != 0 && copy.Item < _items.size())
        {
            SetState(copy.Item, StateCode::To(copy.Field17));
            if (_generation != generation) return;
        }
        for (const auto& [a, b] : copy.Calls)
        {
            if (OnCall) (void)OnCall(copy, a, b, item);
            if (_generation != generation) return;
        }
        if (copy.TargetPage != 0xFF && copy.TargetPage < _file.Pages.size()) GoTo(copy.TargetPage);
    }

    bool MenuEngine::TryRect(const MenuAction& a, const MenuItem* it, float& x0, float& y0, float& x1, float& y1)
    {
        x0 = y0 = x1 = y1 = 0;
        if (a.RectW == 0 && a.RectH == 0 && a.RectX == 0 && a.RectY == 0) return false;
        const float ox = it != nullptr ? it->X : 0.0F;
        const float oy = it != nullptr ? it->Y : 0.0F;
        if ((a.Field4 & 1) != 0)
        {
            x0 = static_cast<float>(a.RectX - a.RectW); x1 = static_cast<float>(a.RectX + a.RectW);
            y0 = static_cast<float>(a.RectY - a.RectH); y1 = static_cast<float>(a.RectY + a.RectH);
        }
        else
        {
            x0 = static_cast<float>(std::min(a.RectX, a.RectW)); x1 = static_cast<float>(std::max(a.RectX, a.RectW));
            y0 = static_cast<float>(std::min(a.RectY, a.RectH)); y1 = static_cast<float>(std::max(a.RectY, a.RectH));
        }
        x0 += ox; x1 += ox; y0 += oy; y1 += oy;
        return true;
    }

    int MenuEngine::FocusedItem() const
    {
        for (std::size_t i = 0; i < _items.size(); ++i)
        {
            if (_items[i].State == MenuState::Focused && HasAction(static_cast<int>(i), KeyA)) return static_cast<int>(i);
        }
        return -1;
    }

    bool MenuEngine::HasAction(int item, std::uint16_t key) const
    {
        for (const MenuAction& a : _page->Items.at(static_cast<std::size_t>(item)).Actions)
        {
            if (a.Kind != 0 && (a.Kind & key) != 0) return true;
        }
        return false;
    }

    bool MenuEngine::HasKeyAction(std::uint16_t keys) const
    {
        if (_page == nullptr) return false;
        for (const MenuAction& a : _page->Actions)
        {
            if (a.Kind != 0 && (a.Kind & keys) != 0 && a.Kind != KeyAny) return true;
        }
        const int focused = FocusedItem();
        return focused != -1 && HasAction(focused, keys);
    }

    std::vector<int> MenuEngine::FocusableItems() const
    {
        std::vector<int> result;
        for (std::size_t i = 0; i < _items.size(); ++i)
        {
            if (_items[i].State != MenuState::Hidden && _items[i].State != MenuState::Disabled
                && _page->Items[i].GetState(static_cast<int>(MenuState::Focused)) != nullptr
                && HasAction(static_cast<int>(i), KeyA))
            {
                result.push_back(static_cast<int>(i));
            }
        }
        return result;
    }

    void MenuEngine::Focus(int item)
    {
        const int current = FocusedItem();
        if (current == item) return;
        if (current != -1) SetState(current, MenuState::Idle);
        SetState(item, MenuState::Focused);
    }

    bool MenuEngine::Ready(int item, const MenuAction& action) const
    {
        const ItemRuntime& rt = _items.at(static_cast<std::size_t>(item));
        return (action.Flags & 2) == 0 || (rt.State == MenuState::Focused && !StateCode::IsTransition(rt.Code));
    }

    void MenuEngine::Press(std::uint16_t keys)
    {
        if (_page == nullptr || _pendingPage != -1) return;
        bool matched = false;
        bool alsoPage = false;
        const int focused = FocusedItem();
        if (focused != -1)
        {
            const std::vector<MenuAction> actions = _page->Items.at(static_cast<std::size_t>(focused)).Actions;
            for (const MenuAction& action : actions)
            {
                if (action.Kind == 0 || (action.Kind & keys) == 0) continue;
                matched = true;
                alsoPage = (action.Flags & 1) != 0;
                if (Ready(focused, action)) Run(action, focused);
                break;
            }
        }
        if ((!matched || alsoPage) && _pendingPage == -1 && _page != nullptr)
        {
            const std::vector<MenuAction> actions = _page->Actions;
            for (const MenuAction& action : actions)
            {
                if ((action.Kind & keys) != 0)
                {
                    matched = true;
                    Run(action, -1);
                    break;
                }
            }
        }
        if (!matched && focused == -1 && (keys & KeyA) != 0) FocusFirst();
    }

    void MenuEngine::Direction(int dx, int dy)
    {
        const std::uint16_t key = dx > 0 ? KeyRight : dx < 0 ? KeyLeft : dy > 0 ? KeyUp : KeyDown;
        if (HasKeyAction(key)) Press(key);
        else Navigate(dx, dy);
    }

    std::pair<float, float> MenuEngine::ActionCentre(int item) const
    {
        const MenuItem& it = _page->Items.at(static_cast<std::size_t>(item));
        for (const MenuAction& a : it.Actions)
        {
            float x0, y0, x1, y1;
            if (TryRect(a, &it, x0, y0, x1, y1)) return {(x0 + x1) / 2, (y0 + y1) / 2};
        }
        return {it.X, it.Y};
    }

    void MenuEngine::Navigate(int dx, int dy)
    {
        if (_page == nullptr || _pendingPage != -1) return;
        const int current = FocusedItem();
        if (current == -1)
        {
            FocusFirst();
            return;
        }
        const auto [cx, cy] = ActionCentre(current);
        int best = -1;
        float bestScore = std::numeric_limits<float>::max();
        for (int i : FocusableItems())
        {
            if (i == current) continue;
            const auto [x, y] = ActionCentre(i);
            const float ddx = x - cx;
            const float ddy = y - cy;
            const float along = static_cast<float>(dx) * ddx + static_cast<float>(dy) * ddy;
            if (along <= 0) continue;
            const float across = std::abs(dx != 0 ? ddy : ddx);
            const float score = along + across * 2;
            if (score < bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        if (best != -1) Focus(best);
    }

    void MenuEngine::FocusFirst()
    {
        int first = -1;
        float firstScore = std::numeric_limits<float>::max();
        for (int i : FocusableItems())
        {
            const auto [x, y] = ActionCentre(i);
            const float score = x - y * 2;
            if (score < firstScore)
            {
                firstScore = score;
                first = i;
            }
        }
        if (first != -1) Focus(first);
    }

    bool MenuEngine::TryFocusFrame(float& x0, float& y0, float& x1, float& y1, bool& hasHighlight) const
    {
        x0 = y0 = x1 = y1 = 0;
        hasHighlight = false;
        const int focused = FocusedItem();
        if (focused == -1) return false;
        const MenuItem& it = _page->Items.at(static_cast<std::size_t>(focused));
        if (const MenuItemState* look = it.GetState(static_cast<int>(MenuState::Focused)); look && look->WidgetIndex >= 0)
        {
            std::string path = _file.Widgets.at(static_cast<std::size_t>(look->WidgetIndex)).ModelPath;
            std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            hasHighlight = path.find("highlight") != std::string::npos;
        }
        for (const MenuAction& a : it.Actions)
        {
            if (TryRect(a, &it, x0, y0, x1, y1))
            {
                y0 -= 192;
                y1 -= 192;
                return true;
            }
        }
        return false;
    }

    void MenuEngine::Touch(float x, float y)
    {
        if (_page == nullptr || _pendingPage != -1) return;
        for (int i = static_cast<int>(_items.size()) - 1; i >= 0; --i)
        {
            const MenuState state = _items[static_cast<std::size_t>(i)].State;
            if (state == MenuState::Hidden || state == MenuState::Disabled) continue;
            const MenuItem& it = _page->Items[static_cast<std::size_t>(i)];
            for (const MenuAction& a : it.Actions)
            {
                float x0, y0, x1, y1;
                if (!TryRect(a, &it, x0, y0, x1, y1)) continue;
                if (x >= x0 && x <= x1 && y >= y0 && y <= y1)
                {
                    const MenuAction action = a;
                    const bool focusable = it.GetState(static_cast<int>(MenuState::Focused)) != nullptr && HasAction(i, KeyA);
                    const bool ready = Ready(i, action);
                    const int generation = _generation;
                    if (focusable) Focus(i);
                    if (!ready || _generation != generation) return;
                    Run(action, i);
                    return;
                }
            }
        }
        const std::vector<MenuAction> actions = _page->Actions;
        for (const MenuAction& a : actions)
        {
            float x0, y0, x1, y1;
            if (TryRect(a, nullptr, x0, y0, x1, y1) && x >= x0 && x <= x1 && y >= y0 && y <= y1)
            {
                Run(a, -1);
                return;
            }
        }
    }

    void MenuEngine::Collect(std::vector<WidgetTri>& output)
    {
        if (_page == nullptr) return;
        const std::size_t start = output.size();
        for (std::size_t i = 0; i < _items.size(); ++i)
        {
            const ItemRuntime& rt = _items[i];
            if (!rt.Started) continue;
            const MenuItem& item = _page->Items[i];
            const MenuItemState* visual = Visual(static_cast<int>(i), rt.Code);
            if (visual == nullptr) continue;
            const bool steady = !StateCode::IsTransition(rt.Code);
            const std::size_t first = output.size();
            if (visual->Text.has_value())
            {
                EmitText(item, *visual->Text, rt.Frame, steady, output);
            }
            else if (visual->WidgetIndex >= 0)
            {
                const std::size_t before = output.size();
                _widgets.Evaluate(visual->WidgetIndex, rt.Frame, steady, 1.0F, output);
                if (item.X != 0 || item.Y != 0)
                {
                    for (std::size_t t = before; t < output.size(); ++t)
                    {
                        WidgetTri& tri = output[t];
                        tri.A.X += item.X; tri.B.X += item.X; tri.C.X += item.X;
                        tri.A.Y += item.Y; tri.B.Y += item.Y; tri.C.Y += item.Y;
                    }
                }
            }
            for (std::size_t t = first; t < output.size(); ++t) output[t].Item = static_cast<int>(i);
        }
        // Far to near; ties keep item order (the DS draws in list order).
        std::stable_sort(output.begin() + static_cast<std::ptrdiff_t>(start), output.end(),
            [](const WidgetTri& p, const WidgetTri& q) { return p.Z < q.Z; });
    }

    void MenuEngine::EmitText(const MenuItem& item, const MenuTextStyle& style, int frame, bool steady,
        std::vector<WidgetTri>& output) const
    {
        float t = 0;
        if (style.Duration > 0)
        {
            const float f = static_cast<float>(frame) / style.Duration;
            t = steady ? 1 - std::abs(std::fmod(f, 2.0F) - 1) : std::min(1.0F, f);
        }
        const auto lerp = [t](float s, float e) { return (s + (e - s) * t) / 31.0F; };
        const float a = lerp(Channel(style.StartColor, 24), Channel(style.EndColor, 24));
        if (a <= 0) return;
        _font.Emit(_strings[style.StringId], item.X, item.Y, style.Align, style.WrapWidth,
            style.Size == 0 ? 8.0F : static_cast<float>(style.Size),
            lerp(Channel(style.StartColor, 0), Channel(style.EndColor, 0)),
            lerp(Channel(style.StartColor, 8), Channel(style.EndColor, 8)),
            lerp(Channel(style.StartColor, 16), Channel(style.EndColor, 16)),
            a, 1000.0F + item.Depth, output);
    }

    // ---- MenuLayout ----

    namespace
    {
        constexpr float TopY0 = 0, TopY1 = 192, BottomY0 = -192, BottomY1 = 0;

        LayoutRegion Region(float x0, float y0, float x1, float y1, float ax, float ay, float px, float py,
            float ox, float oy, float scale)
        {
            LayoutRegion r;
            r.X0 = x0; r.Y0 = y0; r.X1 = x1; r.Y1 = y1;
            r.AnchorX = ax; r.AnchorY = ay; r.PivotX = px; r.PivotY = py;
            r.OffsetX = ox; r.OffsetY = oy; r.Scale = scale;
            return r;
        }
    }

    MenuLayout MenuLayout::For(const MenuFile& file, const MenuPage& page)
    {
        if (page.Index == 0) return Stacked();
        if (page.Index <= 5) return Overlay();
        if (page.Index >= 14 && page.Index <= 17) return MainFamily(false);
        for (const MenuItem& item : page.Items)
        {
            for (const MenuItemState& s : item.States)
            {
                if (s.WidgetIndex < 0) continue;
                std::string path = file.Widgets.at(static_cast<std::size_t>(s.WidgetIndex)).ModelPath;
                std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const std::string suffix = "toplogo_model.bin";
                if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
                {
                    return MainFamily(page.Index == 24);
                }
            }
        }
        return SideBySide();
    }

    MenuLayout MenuLayout::SideBySide()
    {
        MenuLayout layout;
        layout.Kind = LayoutKind::SideBySide;
        layout.Regions = {
            Region(0, TopY0, 256, TopY1, 0.5F, 0.5F, 1, 0.5F, -4, 0, 0.82F),
            Region(0, BottomY0, 256, BottomY1, 0.5F, 0.5F, 0, 0.5F, 4, 0, 0.82F),
        };
        return layout;
    }

    MenuLayout MenuLayout::Stacked()
    {
        MenuLayout layout;
        layout.Kind = LayoutKind::Stacked;
        layout.Regions = {
            Region(0, TopY0, 256, TopY1, 0.5F, 0.5F, 0.5F, 1, 0, 0, 0.62F),
            Region(0, BottomY0, 256, BottomY1, 0.5F, 0.5F, 0.5F, 0, 0, 0, 0.62F),
        };
        return layout;
    }

    MenuLayout MenuLayout::Overlay()
    {
        MenuLayout layout;
        layout.Kind = LayoutKind::Overlay;
        layout.Regions = {
            Region(0, TopY0, 256, TopY1, 0.5F, 0.5F, 0.5F, 0.5F, 0, 0, 1.2F),
            Region(0, BottomY0, 256, BottomY1, 0.5F, 0.5F, 0.5F, 0.5F, 0, 0, 1.2F),
        };
        return layout;
    }

    // Logo across the top, the touch screen's controls in the middle, the top
    // screen's description band at the bottom, the footer row pinned to the
    // canvas corners.
    MenuLayout MenuLayout::MainFamily(bool actionsAboveRight)
    {
        MenuLayout layout;
        layout.Kind = LayoutKind::MainFamily;
        layout.Regions = {
            Region(0, -192, 64, -160, 0, 1, 0, 1, 6, -4, 0.9F),
            Region(176, -192, 256, -160, 1, 1, 1, 1, -6, -4, 0.9F),
            actionsAboveRight
                ? Region(64, -192, 176, -160, 1, 1, 1, 1, -6 - 13 * 0.9F, -4 - 40 * 0.9F, 0.9F)
                : Region(64, -192, 176, -160, 0.5F, 1, 0.5F, 1, 0, -4, 0.9F),
            Region(256, -160, 332, 0, 1, 0, 1, 0, -8, 64, 0.76F),
            Region(0, -160, 256, 0, 0.5F, 0, 0.5F, 0, 0, 64, 0.76F),
            Region(0, 60, 256, 192, 0.5F, 0, 0.5F, 0, 0, 0, 0.5F),
            Region(0, 0, 256, 60, 0.5F, 1, 0.5F, 1, 0, -2, 0.86F),
        };
        return layout;
    }

    std::vector<Placement> MenuLayout::Resolve(float width, float height) const
    {
        const float unit = height / VirtualHeight;
        std::vector<Placement> result;
        result.reserve(Regions.size());
        for (const LayoutRegion& r : Regions)
        {
            Placement p;
            p.Scale = r.Scale * unit;
            p.SrcX = r.X0 + r.PivotX * (r.X1 - r.X0);
            p.SrcY = r.Y1 - r.PivotY * (r.Y1 - r.Y0);
            p.DstX = r.AnchorX * width + r.OffsetX * unit;
            p.DstY = r.AnchorY * height + r.OffsetY * unit;
            result.push_back(p);
        }
        return result;
    }

    int MenuLayout::RegionOf(float x, float y) const
    {
        for (std::size_t i = 0; i < Regions.size(); ++i)
        {
            if (Regions[i].Contains(x, y)) return static_cast<int>(i);
        }
        return -1;
    }
}
