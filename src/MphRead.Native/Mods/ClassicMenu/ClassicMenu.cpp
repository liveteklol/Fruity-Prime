#include "ClassicMenu.hpp"

#include "Compose.hpp"
#include "MenuData.hpp"
#include "OneScreen.hpp"

#include "../DebugLog.hpp"
#include "../../Formats/Formats.hpp"
#include "../../Formats/Model.hpp"
#include "../../NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <thread>

namespace MphRead::Mods::ClassicMenu
{
    namespace
    {
        // One menu tick is two DS frames (59.8261 Hz each).
        constexpr double TickSeconds = 2.0 * 560190.0 / 33513982.0;
        constexpr int TitlePage = 13;
        constexpr int MainMenuPage = 18;
        constexpr int OptionsPage = 21;
        constexpr int CreditsPage = 6;

        // The menus behind the logo sit on the game's Samus art under a
        // translucent colour cycling green -> teal -> orange (510 ticks, three
        // keys 170 apart): 3D at polygon alpha 20, flat over the top screen
        // and, over the touch screen, from that colour at its top edge to
        // half of it (>> 1) at its bottom.
        constexpr float TintKeys[3][3] = {{0, 31, 0}, {4, 16, 16}, {19, 9, 6}};
        constexpr int TintKeyFrames = 170;
        constexpr float TintAlpha = 20.0F / 31.0F;

        // The DS polygon IDs the game gives the tint and the grooves; items
        // take theirs past the game's 6 bits, one an item.
        constexpr int TintId = 1;
        constexpr int SlotsId = 5;
        constexpr int LinesId = 6;
        [[nodiscard]] constexpr int ItemId(int item) noexcept { return 64 + item; }

        // The game's own two models behind every menu page: the dark channels
        // at alpha 30 and the light running through them, faded on a page
        // change.
        const std::string SlotsModel = "_archives\\frontend2d\\slots_Model.bin";
        const std::string SlotsAnim = "_archives\\frontend2d\\slots_Idle_Anim.bin";
        const std::string LinesModel = "_archives\\frontend2d\\lines_Model.bin";
        const std::string LinesAnim = "_archives\\frontend2d\\lines_Idle_Anim.bin";
        constexpr int GrooveFadeTicks = 10;

        // The canvas is 240 units high (portrait: 256 wide); a unit is drawn
        // as this many pixels at most, the picture scaled up from there.
        constexpr float UnitsHigh = 240;
        constexpr int MaxPixelsPerUnit = 3;

        [[nodiscard]] std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        [[nodiscard]] bool EndsWith(const std::string& text, const std::string& suffix)
        {
            return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        // sourceimages/bg: a 256-colour RGB555 palette, then a 256x192 8-bit image.
        UiTexture LoadBitmap(const std::string& path)
        {
            const std::vector<std::uint8_t> data = NativeRuntime::FileReadAllBytes(path);
            UiTexture texture;
            texture.Width = 256;
            texture.Height = 192;
            texture.Pixels.reserve(256 * 192);
            for (std::size_t i = 0; i < 256 * 192; ++i)
            {
                const std::size_t index = 512 + i < data.size() ? data[512 + i] : 0;
                const std::uint16_t c = static_cast<std::uint16_t>(data.at(index * 2) | (data.at(index * 2 + 1) << 8));
                texture.Pixels.emplace_back(
                    static_cast<std::uint8_t>((c & 31) * 255 / 31),
                    static_cast<std::uint8_t>(((c >> 5) & 31) * 255 / 31),
                    static_cast<std::uint8_t>(((c >> 10) & 31) * 255 / 31),
                    static_cast<std::uint8_t>(255));
            }
            return texture;
        }

        class Session final
        {
        public:
            explicit Session(const std::string& root)
                : _root(root),
                  _file(MenuFile::Parse(NativeRuntime::FileReadAllBytes(
                      Paths::Combine(root, "frontend", "metroidhunters.bin")))),
                  _strings(MenuStrings::Load(root, "en")),
                  _widgets(_file, _textures),
                  _font(_textures),
                  _compose(_file, _strings, _font),
                  _menu(_file, _strings, _widgets, _font),
                  _screen(_file, _strings, _widgets, _font, _compose)
            {
                // Options' RUMBLE PAK / SHOW MY STATS values: the game writes
                // its own text over these placeholders.
                const auto fill = [this](const char* placeholder, const char* value)
                {
                    const int id = _strings.IndexOf(placeholder);
                    const int text = _strings.IndexOf(value);
                    if (id >= 0) _strings.Fill(id, text >= 0 ? _strings[text] : std::string(value));
                };
                fill("rumb", "OFF");
                fill("priv", "YES");
                // Before the first page: the lists must not move under the engine.
                _compose.Build();
                _compose.Attach(&_menu);
                _menu.OnCall = [this](const MenuAction&, int, int b, int) { return OnCall(b); };
                _menu.OnPageEntered = [this](int page) { OnPageEntered(page); };
                _menu.Enter(TitlePage);
            }

            void SetHost(Host* host) { _host = host; _compose.SetHost(host); }

            void Update(double seconds)
            {
                _clock += seconds;
                int ticks = 0;
                while (_clock >= TickSeconds && ticks < 8)
                {
                    _clock -= TickSeconds;
                    Tick();
                    ++ticks;
                }
                if (ticks == 8) _clock = 0;
                _dirty = _dirty || ticks > 0;
            }

            void Press(std::uint16_t keys)
            {
                _showFocus = true;
                if ((keys & KeyB) != 0 && _menu.Page() != nullptr && _compose.Composed(_menu.Page()->Index))
                {
                    _compose.Back();
                    _dirty = true;
                    return;
                }
                _menu.Press(keys);
                _dirty = true;
            }

            void Navigate(int dx, int dy)
            {
                _showFocus = true;
                _menu.Direction(dx, dy);
                _dirty = true;
            }

            // A DS touch (DS pixels on the touch screen, y down).
            void Touch(float x, float y)
            {
                _showFocus = false;
                _menu.Touch(x, 192.0F - y);
                _dirty = true;
            }

            void Pointer(float px, float py, bool press)
            {
                if (_unit <= 0) return;
                float cx = 0, cy = 0;
                const bool on = _screen.ToCombined(px / _unit, py / _unit, cx, cy) && cy >= 186;
                _dirty = true;
                if (press)
                {
                    _showFocus = false;
                    // touches are in text coordinates: Y up from the touch screen's bottom
                    if (on) _menu.Touch(cx, 384.0F - cy);
                    return;
                }
                const int item = on ? ItemAt(cx, 384.0F - cy) : -1;
                if (item == _hover) return;
                _hover = item;
                _compose.Hover(item);
                // an item the ROM gives a look under focus takes it, as the stylus would
                if (item >= 0 && _menu.Page() != nullptr)
                {
                    const MenuItem& it = _menu.Page()->Items[static_cast<std::size_t>(item)];
                    if (it.GetState(static_cast<int>(MenuState::Focused)) != nullptr && _menu.ItemState(item) != MenuState::Focused
                        && _menu.ItemState(item) != MenuState::Hidden && _menu.ItemState(item) != MenuState::Disabled)
                    {
                        _menu.SetState(item, MenuState::Focused);
                    }
                }
            }

            bool Type(const std::string& text, bool backspace, bool enter)
            {
                _dirty = true;
                return _compose.Type(text, backspace, enter);
            }

            [[nodiscard]] bool Listening() const { return _compose.Listening(); }
            void Show(const std::string& what) { _compose.Show(what); _dirty = true; }
            void Visit(const std::string& page)
            {
                const std::map<std::string, int> named{{"servers", _compose.ServersPage}, {"create", _compose.CreatePage},
                    {"lobby", _compose.LobbyPage}, {"room", _compose.RoomPage}, {"manage", _compose.ManagePage},
                    {"pause", _compose.PausePage}, {"end", _compose.EndPage}, {"settings", _compose.SettingsPage},
                    {"credits", _compose.CreditsPage}};
                const auto found = named.find(page);
                const int index = found != named.end() ? found->second : std::atoi(page.c_str());
                if (index >= 0 && static_cast<std::size_t>(index) < _file.Pages.size()) _menu.Enter(index);
                _dirty = true;
            }
            [[nodiscard]] bool OverGame() const { return _menu.Page() != nullptr && _compose.OverGame(_menu.Page()->Index); }

            [[nodiscard]] std::string Describe() const
            {
                const MenuPage* page = _menu.Page();
                if (page == nullptr) return "no page\n";
                std::string out = "page " + std::to_string(page->Index) + " pending " + std::to_string(_menu.PendingPage())
                    + " tick " + std::to_string(_frame) + "\n";
                for (std::size_t i = 0; i < page->Items.size(); ++i)
                {
                    const int item = static_cast<int>(i);
                    out += "item " + std::to_string(i) + " state " + std::to_string(static_cast<int>(_menu.ItemState(item)))
                        + " code " + std::to_string(_menu.ItemCode(item)) + " " + _menu.ItemModelPath(item) + "\n";
                    std::map<std::string, int> looks;
                    float x0 = 1e9F, y0 = 1e9F, x1 = -1e9F, y1 = -1e9F;
                    for (const WidgetTri& t : _tris)
                    {
                        if (t.Item != item) continue;
                        for (const UiVertex* v : {&t.A, &t.B, &t.C})
                        {
                            x0 = std::min(x0, v->X); x1 = std::max(x1, v->X);
                            y0 = std::min(y0, 192.0F - v->Y); y1 = std::max(y1, 192.0F - v->Y);
                        }
                        char look[96];
                        std::snprintf(look, sizeof(look), "tex %d mode %d lights %d rgba %.3f %.3f %.3f %.3f z %.1f", t.TextureId,
                            t.Mode, t.Lights, t.A.R, t.A.G, t.A.B, t.A.A, t.Z);
                        ++looks[look];
                    }
                    if (looks.empty()) continue;
                    char box[96];
                    std::snprintf(box, sizeof(box), "  box %.1f %.1f .. %.1f %.1f\n", x0, y0, x1, y1);
                    out += box;
                    for (const auto& [look, count] : looks) out += "  " + std::to_string(count) + "x " + look + "\n";
                }
                return out;
            }
            [[nodiscard]] bool Dirty() const { return _dirty; }
            void Clean() { _dirty = false; }

            const UiTextureCache& Textures() const noexcept { return _textures; }

            void Advance(double seconds)
            {
                Update(seconds);
                // no tick and no input: the triangles stand
                if (!_dirty && _collected) return;
                _tris.clear();
                _menu.Collect(_tris);
                AddFocusFrame();
                _collected = true;
            }

            // The one screen: W x H units (240 high, or 256 wide in
            // portrait), drawn at `unit` pixels a unit.
            const UiDrawList& BuildCanvas(int width, int height)
            {
                _draw.Clear();
                const MenuPage* page = _menu.Page();
                if (page == nullptr) return _draw;
                const bool portrait = height > width;
                const float w = portrait ? 256.0F : UnitsHigh * static_cast<float>(width) / static_cast<float>(height);
                const float h = portrait ? 256.0F * static_cast<float>(height) / static_cast<float>(width) : UnitsHigh;
                _unit = static_cast<float>(width) / w;
                if (_layoutPage != page->Index || _layoutW != width || _layoutH != height)
                {
                    _screen.Layout(page->Index, w, h);
                    _layoutPage = page->Index; _layoutW = width; _layoutH = height;
                }
                if (!_compose.OverGame(page->Index)) DrawBackdrop(page->Index, _screen.Backdrop(), _screen.Width(), _screen.Height());

                // every item through its group, an item wider than the ROM made it stretched
                std::map<int, std::pair<float, float>> centres;
                {
                    std::map<int, std::array<float, 4>> boxes;
                    for (const WidgetTri& t : _tris)
                    {
                        auto [it, fresh] = boxes.try_emplace(t.Item, std::array<float, 4>{1e9F, 1e9F, -1e9F, -1e9F});
                        for (const UiVertex* v : {&t.A, &t.B, &t.C})
                        {
                            it->second[0] = std::min(it->second[0], v->X); it->second[2] = std::max(it->second[2], v->X);
                            it->second[1] = std::min(it->second[1], 192.0F - v->Y); it->second[3] = std::max(it->second[3], 192.0F - v->Y);
                        }
                    }
                    for (const auto& [item, b] : boxes) centres[item] = {(b[0] + b[2]) / 2, (b[1] + b[3]) / 2};
                }
                for (const WidgetTri& t : _tris)
                {
                    if (Hidden(t.Item)) continue;
                    const auto& c = centres[t.Item];
                    const GroupPlace* g = _screen.PlaceOf(t.Item, c.first, c.second);
                    if (g == nullptr) continue;
                    float pivot = 0, scale = 1;
                    if (const Stretch* s = _compose.StretchOf(page->Index, t.Item)) { pivot = s->Pivot; scale = s->Scale; }
                    const auto place = [&](UiVertex v)
                    {
                        // the DS truncates a vertex to the pixel it falls in
                        const float x = pivot + (std::floor(v.X) - pivot) * scale;
                        const float y = std::floor(192.0F - v.Y);
                        v.X = (g->Ox + x * g->S) * _unit;
                        v.Y = (g->Oy + y * g->S) * _unit;
                        return v;
                    };
                    _draw.Pixel = g->S * _unit;
                    _draw.Triangle(t.TextureId, t.WrapS, t.WrapT, place(t.A), place(t.B), place(t.C), ItemId(t.Item));
                }
                return _draw;
            }

            // One tick at a time, with no clock (-classicframes).
            void Step(int ticks)
            {
                for (int i = 0; i < ticks; ++i) Tick();
                _dirty = true;
                _tris.clear();
                _menu.Collect(_tris);
                AddFocusFrame();
                _collected = true;
            }

            // The two screens as the DS shows them, the top one above.
            const UiDrawList& BuildDs()
            {
                _draw.Clear();
                const MenuPage* page = _menu.Page();
                if (page == nullptr) return _draw;
                _unit = 1;
                if (!_compose.OverGame(page->Index)) DrawBackdrop(page->Index, GroupPlace{}, 256, 384);
                const auto place = [](UiVertex v) { v.X = std::floor(v.X); v.Y = std::floor(192.0F - v.Y); return v; };
                for (const WidgetTri& t : _tris)
                {
                    if (Hidden(t.Item)) continue;
                    _draw.Triangle(t.TextureId, t.WrapS, t.WrapT, place(t.A), place(t.B), place(t.C), ItemId(t.Item));
                }
                return _draw;
            }

        private:
            void Tick()
            {
                ++_frame;
                _menu.Tick();
                _compose.Refresh();
                TickGrooves();
            }

            // The topmost item whose touch area holds (x, y), text coordinates.
            [[nodiscard]] int ItemAt(float x, float y) const
            {
                const MenuPage* page = _menu.Page();
                if (page == nullptr || _menu.PendingPage() != -1) return -1;
                for (int i = static_cast<int>(page->Items.size()) - 1; i >= 0; --i)
                {
                    const MenuState state = _menu.ItemState(i);
                    if (state == MenuState::Hidden || state == MenuState::Disabled) continue;
                    const MenuItem& it = page->Items[static_cast<std::size_t>(i)];
                    for (const MenuAction& a : it.Actions)
                    {
                        float x0, y0, x1, y1;
                        if (MenuEngine::TryRect(a, &it, x0, y0, x1, y1) && x >= x0 && x <= x1 && y >= y0 && y <= y1) return i;
                    }
                }
                return -1;
            }

            bool OnCall(int b)
            {
                _dirty = true;
                if (_compose.Call(b)) return true;
                switch (b)
                {
                case 7: _menu.GoTo(1); return true;
                case 8: _menu.Enter(TitlePage); return true;
                case 9: case 10: case 13: case 15: case 17: case 78: case 76: case 80: case 82: return true;
                case 11: _menu.GoTo(OptionsPage); return true;
                // the game's credits are over: Fruity Prime's
                case 12: _menu.GoTo(_compose.CreditsPage >= 0 ? _compose.CreditsPage : OptionsPage); return true;
                case 44:
                    if (_host != nullptr) _host->SaveSettings();
                    _menu.GoTo(MainMenuPage);
                    return true;
                case 46: _menu.GoTo(CreditsPage); return true;
                // MULTIPLAYER: the DS's own pages, which the single screen has made Fruity Prime's
                case 16: _menu.GoTo(25); return true;
                default:
                    DebugLog::Line("classicmenu", "page " + std::to_string(_menu.Page() ? _menu.Page()->Index : -1)
                        + ": unhandled call " + std::to_string(b));
                    return false;
                }
            }

            void OnPageEntered(int page)
            {
                DebugLog::Line("classicmenu", "page " + std::to_string(page));
                _hover = -1;
                _layoutPage = -1;
                const MenuPage& p = *_menu.Page();
                if (page == OptionsPage)
                {
                    // No Rumble Pak: the rumble row in its greyed look.
                    for (std::size_t i = 0; i < p.Items.size(); ++i)
                    {
                        if (p.Items[i].GetState(static_cast<int>(MenuState::Disabled)) != nullptr)
                        {
                            _menu.SetState(static_cast<int>(i), MenuState::Disabled);
                        }
                    }
                }
                // USA rev 1's logo carries its own "R" item and a "TM" text
                // before it that the game hides wherever the R is.
                for (std::size_t i = 1; i < p.Items.size(); ++i)
                {
                    if (!p.Items[i - 1].IsText()) continue;
                    for (const MenuItemState& s : p.Items[i].States)
                    {
                        if (s.WidgetIndex >= 0 && EndsWith(Lower(_file.Widgets.at(static_cast<std::size_t>(s.WidgetIndex)).ModelPath),
                            "\\toplogor_model.bin"))
                        {
                            _menu.SetState(static_cast<int>(i - 1), MenuState::Hidden);
                            break;
                        }
                    }
                }
                _compose.PageEntered(page);
            }

            // The Wi-Fi / wireless signal icons only show when connected.
            bool Hidden(int item) const
            {
                const MenuPage* page = _menu.Page();
                if (item < 0 || static_cast<std::size_t>(item) >= page->Items.size()) return false;
                // any of its looks: one left on screen as the page goes is still the icon
                for (const MenuItemState& s : page->Items.at(static_cast<std::size_t>(item)).States)
                {
                    if (s.WidgetIndex < 0) continue;
                    const std::string& path = _file.Widgets.at(static_cast<std::size_t>(s.WidgetIndex)).ModelPath;
                    if (path.rfind("main menu\\wifi", 0) == 0 || path.rfind("main menu\\wireless", 0) == 0) return true;
                }
                return false;
            }

            // A frame in the menus' orange around the item in focus, pulsing,
            // when its own look has no highlight: the keyboard's focus, or
            // the pointer over something with no look of its own.
            void AddFocusFrame()
            {
                float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                bool highlighted = false;
                int item = -1;
                if (_showFocus)
                {
                    if (!_menu.TryFocusFrame(x0, y0, x1, y1, highlighted) || highlighted) return;
                    item = _menu.FocusedItem();
                }
                else
                {
                    if (_hover < 0 || _menu.Page() == nullptr) return;
                    const MenuItem& it = _menu.Page()->Items[static_cast<std::size_t>(_hover)];
                    if (it.GetState(static_cast<int>(MenuState::Focused)) != nullptr || it.Actions.empty()) return;
                    if (!MenuEngine::TryRect(it.Actions.front(), &it, x0, y0, x1, y1)) return;
                    // a row with a red bar of its own needs no frame
                    if (_compose.HasRowBar(_menu.Page()->Index, _hover)) return;
                    y0 -= 192; y1 -= 192;
                    item = _hover;
                }
                const float pulse = 0.55F + 0.45F * std::abs(std::sin(static_cast<float>(_frame) * 3.14159265F / 30.0F));
                constexpr float r = 1.0F, g = 20.0F / 31.0F, b = 8.0F / 31.0F, t = 1.5F;
                const auto bar = [&](float ax, float ay, float bx, float by)
                {
                    const UiVertex p0{ax, ay, 0, 0, r, g, b, pulse};
                    const UiVertex p1{bx, ay, 0, 0, r, g, b, pulse};
                    const UiVertex p2{bx, by, 0, 0, r, g, b, pulse};
                    const UiVertex p3{ax, by, 0, 0, r, g, b, pulse};
                    _tris.push_back(WidgetTri{p0, p1, p2, 2000, item, -1});
                    _tris.push_back(WidgetTri{p0, p2, p3, 2000, item, -1});
                };
                x0 -= 2; y0 -= 2; x1 += 2; y1 += 2;
                bar(x0, y1 - t, x1, y1);
                bar(x0, y0, x1, y0 + t);
                bar(x0, y0, x0 + t, y1);
                bar(x1 - t, y0, x1, y1);
            }

            int BackgroundTexture(int which)
            {
                if (_background[which] != -1) return _background[which];
                const std::string name = which == 0 ? "main1" : "main2";
                _background[which] = _textures.GetOrAdd("bg/" + name, [&]
                {
                    return LoadBitmap(Paths::Combine(_root, "sourceimages", "bg", name + ".bin"));
                });
                return _background[which];
            }

            // Behind the menus: the game's Samus art (main1 over main2, one
            // column, cover-fitted on the visor) under the cycling tint, then
            // the grooves. The logos and the credits are on black.
            void DrawBackdrop(int page, const GroupPlace& b, float width, float height)
            {
                if (page <= 12) return;
                const auto quad = [&](int texture, float y0, float y1)
                {
                    _draw.Quad(texture, b.Ox * _unit, (b.Oy + y0 * b.S) * _unit, (b.Ox + 256 * b.S) * _unit, (b.Oy + y1 * b.S) * _unit,
                        0, 0, 1, 1, 1, 1, 1, 1);
                };
                _draw.Backdrop = true;
                quad(BackgroundTexture(0), 0, 192);
                quad(BackgroundTexture(1), 192, 384);
                _draw.Backdrop = false;
                if (_tintStart < 0) _tintStart = _frame;
                const long long f = (_frame - _tintStart) % (TintKeyFrames * 3);
                const int k = static_cast<int>(f / TintKeyFrames);
                const float t = static_cast<float>(f % TintKeyFrames) / TintKeyFrames;
                const float* c0 = TintKeys[k];
                const float* c1 = TintKeys[(k + 1) % 3];
                int top[3];
                for (int i = 0; i < 3; ++i) top[i] = static_cast<int>(c0[i] + (c1[i] - c0[i]) * t);
                const auto colour = [&](float x, float y, int shift)
                {
                    return UiVertex{x, y, 0, 0, static_cast<float>(top[0] >> shift) / 31, static_cast<float>(top[1] >> shift) / 31,
                        static_cast<float>(top[2] >> shift) / 31, TintAlpha};
                };
                _draw.Pixel = b.S * _unit;
                const float x1 = width * _unit;
                for (int screen = 0; screen < 2; ++screen)
                {
                    const float y0 = (b.Oy + static_cast<float>(screen) * 192.0F * b.S) * _unit;
                    const float y1 = (b.Oy + static_cast<float>(screen + 1) * 192.0F * b.S) * _unit;
                    // the canvas above the first screen and below the second takes their edge colours
                    const float top0 = screen == 0 ? std::min(y0, 0.0F) : y0;
                    const float bottom1 = screen == 1 ? std::max(y1, height * _unit) : y1;
                    if (top0 < y0)
                    {
                        _draw.Triangle(-1, UiWrap::Clamp, UiWrap::Clamp, colour(0, top0, 0), colour(x1, top0, 0), colour(x1, y0, 0), TintId);
                        _draw.Triangle(-1, UiWrap::Clamp, UiWrap::Clamp, colour(0, top0, 0), colour(x1, y0, 0), colour(0, y0, 0), TintId);
                    }
                    const int fade = screen;
                    _draw.Triangle(-1, UiWrap::Clamp, UiWrap::Clamp, colour(0, y0, 0), colour(x1, y0, 0), colour(x1, y1, fade), TintId);
                    _draw.Triangle(-1, UiWrap::Clamp, UiWrap::Clamp, colour(0, y0, 0), colour(x1, y1, fade), colour(0, y1, fade), TintId);
                    if (bottom1 > y1)
                    {
                        _draw.Triangle(-1, UiWrap::Clamp, UiWrap::Clamp, colour(0, y1, fade), colour(x1, y1, fade), colour(x1, bottom1, fade), TintId);
                        _draw.Triangle(-1, UiWrap::Clamp, UiWrap::Clamp, colour(0, y1, fade), colour(x1, bottom1, fade), colour(0, bottom1, fade), TintId);
                    }
                }
                DrawGrooves(page, b);
            }

            void TickGrooves()
            {
                if (_menu.Page() == nullptr) return;
                const bool leaving = _menu.PendingPage() != -1;
                if (_grooveCounter < 0) _grooveCounter = leaving ? GrooveFadeTicks : 0;
                else _grooveCounter = leaving ? std::max(0, _grooveCounter - 1) : std::min(GrooveFadeTicks, _grooveCounter + 1);
                _grooveFrame += 2;
            }

            void DrawGrooves(int page, const GroupPlace& b)
            {
                if (_groovesFailed || page == 49 || page == 50 || page == 65) return;
                _grooveTris.clear();
                std::size_t slots = 0;
                try
                {
                    _widgets.Evaluate(_widgets.Instance(SlotsModel, SlotsAnim), 0, true, 30.0F / 31.0F, _grooveTris);
                    slots = _grooveTris.size();
                    const int alpha = _grooveCounter < 0 ? 30 : std::clamp(_grooveCounter * 3, 1, 30);
                    _widgets.Evaluate(_widgets.Instance(LinesModel, LinesAnim), _grooveFrame, true,
                        static_cast<float>(alpha) / 31.0F, _grooveTris);
                }
                catch (const std::exception& ex)
                {
                    _groovesFailed = true;
                    DebugLog::Line("classicmenu", std::string("menu grooves unavailable: ") + ex.what());
                    return;
                }
                const auto place = [&](UiVertex v)
                {
                    v.X = (b.Ox + v.X * b.S) * _unit;
                    v.Y = (b.Oy + (192.0F - v.Y) * b.S) * _unit;
                    return v;
                };
                for (std::size_t i = 0; i < _grooveTris.size(); ++i)
                {
                    const WidgetTri& t = _grooveTris[i];
                    _draw.Triangle(t.TextureId, t.WrapS, t.WrapT, place(t.A), place(t.B), place(t.C), i < slots ? SlotsId : LinesId);
                }
            }

            std::string _root;
            MenuFile _file;
            MenuStrings _strings;
            UiTextureCache _textures;
            MenuWidgets _widgets;
            MenuFont _font;
            Composer _compose;
            MenuEngine _menu;
            OneScreen _screen;
            Host* _host = nullptr;
            UiDrawList _draw;
            std::vector<WidgetTri> _tris;
            std::vector<WidgetTri> _grooveTris;
            double _clock = 0;
            long long _frame = 0;
            long long _tintStart = -1;
            int _background[2]{-1, -1};
            int _grooveCounter = -1;
            int _grooveFrame = 0;
            bool _groovesFailed = false;
            bool _showFocus = false;
            bool _dirty = true;
            bool _collected = false;
            int _hover = -1;
            float _unit = 0;
            int _layoutPage = -1, _layoutW = 0, _layoutH = 0;
        };

        std::unique_ptr<Session> session;
        Host* host = nullptr;
        // the last picture: drawn at last*, shown at shown*
        int lastWidth = 0, lastHeight = 0, shownWidth = 0, shownHeight = 0;
        bool active = false;
        const void* activeOwner = nullptr;
        std::string lastError;
    }

    bool Facade::Active() noexcept
    {
        return active && session != nullptr;
    }

    bool Facade::SetActive(bool value, const void* owner)
    {
        if (!value)
        {
            if (owner == activeOwner)
            {
                active = false;
                activeOwner = nullptr;
            }
            return true;
        }
        try
        {
            // A fresh session each time: the title, as the game starts.
            session = std::make_unique<Session>(Paths::FileSystem());
            session->SetHost(host);
            active = true;
            activeOwner = owner;
            lastError.clear();
            DebugLog::Line("classicmenu", "the DS menus are on");
            return true;
        }
        catch (const std::exception& ex)
        {
            session.reset();
            active = false;
            lastError = ex.what();
            DebugLog::Line("classicmenu", std::string("the DS menus could not load: ") + ex.what());
            return false;
        }
    }

    bool Facade::Owns(const void* owner) noexcept
    {
        return Active() && owner == activeOwner;
    }

    const std::string& Facade::LastError() noexcept
    {
        return lastError;
    }

    void Facade::SetHost(Host* value)
    {
        host = value;
        if (session) session->SetHost(value);
    }

    namespace
    {
        [[nodiscard]] int WrapCoord(int value, int size, UiWrap wrap)
        {
            switch (wrap)
            {
            case UiWrap::Repeat:
            {
                const int m = value % size;
                return m < 0 ? m + size : m;
            }
            case UiWrap::Mirror:
            {
                const int period = size * 2;
                int m = value % period;
                if (m < 0) m += period;
                return m < size ? m : period - 1 - m;
            }
            default:
                return std::clamp(value, 0, size - 1);
            }
        }

        // A pixel of the DS's 3D layer: colour, polygon alpha (0..31) and the
        // ID of the translucent polygon that drew it last (-1: none).
        struct Pixel3d final
        {
            std::uint8_t R = 0, G = 0, B = 0, Alpha = 0;
            std::int32_t Translucent = -1;
        };

        [[nodiscard]] std::uint8_t Byte(float value)
        {
            return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
        }

        // The rows [rowBegin, rowEnd) of the picture: every triangle that
        // crosses them, then the 3D layer over the 2D one. Rows are drawn
        // independently, so bands of them can be drawn at once.
        void RasterizeRows(const UiDrawList& list, const std::vector<const UiTexture*>& textures, int width,
            int rowBegin, int rowEnd, std::uint32_t* pixels, Pixel3d* layer, bool clear)
        {
            for (std::size_t bi = 0; bi < list.Batches.size(); ++bi)
            {
                const UiBatch& batch = list.Batches[bi];
                const UiTexture* texture = textures[bi];
                const int tw = texture != nullptr ? texture->Width : 0;
                const int th = texture != nullptr ? texture->Height : 0;
                // one pixel's colour (interpolated vertex colour, texture coordinates) into its layer
                const auto plot = [&](int x, int y, float r, float g, float bl, float al, float u, float t)
                {
                    if (texture != nullptr)
                    {
                        const int tx = WrapCoord(static_cast<int>(std::floor(u * static_cast<float>(tw))), tw, batch.WrapS);
                        const int ty = WrapCoord(static_cast<int>(std::floor(t * static_cast<float>(th))), th, batch.WrapT);
                        const ColorRgba& texel = texture->Pixels[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tw)
                            + static_cast<std::size_t>(tx)];
                        constexpr float Unit = 1.0F / 255.0F;
                        r *= static_cast<float>(texel.Red) * Unit;
                        g *= static_cast<float>(texel.Green) * Unit;
                        bl *= static_cast<float>(texel.Blue) * Unit;
                        al *= static_cast<float>(texel.Alpha) * Unit;
                    }
                    const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                    if (batch.Backdrop)
                    {
                        if (al <= 0.0F) return;
                        pixels[at] = Byte(r) | (static_cast<std::uint32_t>(Byte(g)) << 8)
                            | (static_cast<std::uint32_t>(Byte(bl)) << 16) | 0xFF000000U;
                        return;
                    }
                    const int alpha = std::min(31, static_cast<int>(al * 31.0F + 0.5F));
                    if (alpha <= 0) return;
                    Pixel3d& dst = layer[at];
                    if (alpha == 31)
                    {
                        dst = Pixel3d{Byte(r), Byte(g), Byte(bl), 31, -1};
                        return;
                    }
                    if (batch.PolyId >= 0 && dst.Translucent == batch.PolyId) return;
                    if (dst.Alpha == 0)
                    {
                        dst = Pixel3d{Byte(r), Byte(g), Byte(bl), static_cast<std::uint8_t>(alpha), batch.PolyId};
                        return;
                    }
                    const float s = static_cast<float>(alpha + 1) / 32.0F;
                    constexpr float Unit = 1.0F / 255.0F;
                    dst.R = Byte(r * s + static_cast<float>(dst.R) * Unit * (1.0F - s));
                    dst.G = Byte(g * s + static_cast<float>(dst.G) * Unit * (1.0F - s));
                    dst.B = Byte(bl * s + static_cast<float>(dst.B) * Unit * (1.0F - s));
                    dst.Alpha = static_cast<std::uint8_t>(std::max<int>(dst.Alpha, alpha));
                    dst.Translucent = batch.PolyId;
                };
                const int pixel = std::max(1, static_cast<int>(std::ceil(batch.Pixel - 1e-3F)));
                for (int v = batch.Start; v + 2 < batch.Start + batch.Count; v += 3)
                {
                    const UiVertex& a = list.Vertices[static_cast<std::size_t>(v)];
                    const UiVertex& b = list.Vertices[static_cast<std::size_t>(v + 1)];
                    const UiVertex& c = list.Vertices[static_cast<std::size_t>(v + 2)];
                    const float fx0 = std::min({a.X, b.X, c.X}), fx1 = std::max({a.X, b.X, c.X});
                    const float fy0 = std::min({a.Y, b.Y, c.Y}), fy1 = std::max({a.Y, b.Y, c.Y});
                    // Thinner than a DS pixel: the DS still draws a pixel a row (a
                    // span is never empty) and a row for a flat one -- a square
                    // squeezed to nothing is a line, as the BEGIN GAME bar's are.
                    const bool thinX = fx1 - fx0 < batch.Pixel - 1e-3F;
                    const bool thinY = fy1 - fy0 < batch.Pixel - 1e-3F;
                    if (thinX || thinY)
                    {
                        // along the long side, between the two vertices furthest apart on it
                        const UiVertex* lo = &a;
                        const UiVertex* hi = &a;
                        for (const UiVertex* p : {&b, &c})
                        {
                            const float k = thinX ? p->Y : p->X;
                            if (k < (thinX ? lo->Y : lo->X)) lo = p;
                            if (k > (thinX ? hi->Y : hi->X)) hi = p;
                        }
                        const float from = thinX ? lo->Y : lo->X;
                        const float span = (thinX ? hi->Y : hi->X) - from;
                        const auto range = [&](float f0, float f1, bool thin, int& i0, int& i1)
                        {
                            i0 = static_cast<int>(std::ceil(f0 - 0.5F));
                            i1 = static_cast<int>(std::floor(f1 - 0.5F));
                            if (thin || i0 > i1)
                            {
                                i0 = static_cast<int>(std::floor(f0));
                                i1 = i0 + pixel - 1;
                            }
                        };
                        int x0, x1, y0, y1;
                        range(fx0, fx1, thinX, x0, x1);
                        range(fy0, fy1, thinY, y0, y1);
                        x0 = std::max(x0, 0); x1 = std::min(x1, width - 1);
                        y0 = std::max(y0, rowBegin); y1 = std::min(y1, rowEnd - 1);
                        for (int y = y0; y <= y1; ++y)
                        {
                            for (int x = x0; x <= x1; ++x)
                            {
                                const float along = (thinX ? static_cast<float>(y) : static_cast<float>(x)) + 0.5F;
                                const float t = span > 1e-6F ? std::clamp((along - from) / span, 0.0F, 1.0F) : 0.0F;
                                const auto mix = [t](float p, float q) { return p + (q - p) * t; };
                                plot(x, y, mix(lo->R, hi->R), mix(lo->G, hi->G), mix(lo->B, hi->B), mix(lo->A, hi->A),
                                    mix(lo->U, hi->U), mix(lo->V, hi->V));
                            }
                        }
                        continue;
                    }
                    const float area = (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
                    if (std::abs(area) < 1e-6F) continue;
                    const int minY = std::max(rowBegin, static_cast<int>(std::floor(fy0)));
                    const int maxY = std::min(rowEnd - 1, static_cast<int>(std::ceil(fy1)));
                    if (minY > maxY) continue;
                    const int minX = std::max(0, static_cast<int>(std::floor(fx0)));
                    const int maxX = std::min(width - 1, static_cast<int>(std::ceil(fx1)));
                    if (minX > maxX) continue;
                    const float inv = 1.0F / area;
                    // the barycentric weights as planes over pixel centres: w = (A x + B y + C) * inv
                    const float a0 = (b.Y - c.Y) * inv, b0 = (c.X - b.X) * inv, c0 = (b.X * c.Y - b.Y * c.X) * inv;
                    const float a1 = (c.Y - a.Y) * inv, b1 = (a.X - c.X) * inv, c1 = (c.X * a.Y - c.Y * a.X) * inv;
                    // an attribute as a plane: c + (a - c) w0 + (b - c) w1
                    struct Plane final { float Dx, Dy, At; };
                    const auto plane = [&](float pa, float pb, float pc)
                    {
                        return Plane{(pa - pc) * a0 + (pb - pc) * a1, (pa - pc) * b0 + (pb - pc) * b1,
                            pc + (pa - pc) * c0 + (pb - pc) * c1};
                    };
                    const Plane pr = plane(a.R, b.R, c.R), pg = plane(a.G, b.G, c.G), pb = plane(a.B, b.B, c.B);
                    const Plane pa = plane(a.A, b.A, c.A), pu = plane(a.U, b.U, c.U), pv = plane(a.V, b.V, c.V);
                    for (int y = minY; y <= maxY; ++y)
                    {
                        const float py = static_cast<float>(y) + 0.5F;
                        // the span of this row inside all three edges (with a pixel to spare: the test below decides)
                        float lo = static_cast<float>(minX), hi = static_cast<float>(maxX) + 1.0F;
                        const auto edge = [&](float ea, float eb, float ec)
                        {
                            const float at = eb * py + ec;
                            if (std::abs(ea) < 1e-12F)
                            {
                                if (at < -1e-6F) hi = lo - 1.0F;
                                return;
                            }
                            const float x = -at / ea;
                            if (ea > 0) lo = std::max(lo, x - 1.0F);
                            else hi = std::min(hi, x + 1.0F);
                        };
                        edge(a0, b0, c0);
                        edge(a1, b1, c1);
                        edge(-a0 - a1, -b0 - b1, 1.0F - c0 - c1);
                        const int x0 = std::max(minX, static_cast<int>(std::floor(lo)));
                        const int x1 = std::min(maxX, static_cast<int>(std::ceil(hi)));
                        for (int x = x0; x <= x1; ++x)
                        {
                            const float px = static_cast<float>(x) + 0.5F;
                            const float w0 = a0 * px + b0 * py + c0;
                            const float w1 = a1 * px + b1 * py + c1;
                            // a hair inside: a pixel on the edge two triangles share is drawn
                            // (twice over is harmless -- a translucent one is not blended
                            // again over its own polygon ID), never dropped by both
                            constexpr float Inside = -1e-4F;
                            if (w0 < Inside || w1 < Inside || 1.0F - w0 - w1 < Inside) continue;
                            plot(x, y, pr.Dx * px + pr.Dy * py + pr.At, pg.Dx * px + pg.Dy * py + pg.At,
                                pb.Dx * px + pb.Dy * py + pb.At, pa.Dx * px + pa.Dy * py + pa.At,
                                pu.Dx * px + pu.Dy * py + pu.At, pv.Dx * px + pv.Dy * py + pv.At);
                        }
                    }
                }
            }
            // the 3D layer over the 2D one, by its own alpha
            const std::size_t begin = static_cast<std::size_t>(rowBegin) * static_cast<std::size_t>(width);
            const std::size_t end = static_cast<std::size_t>(rowEnd) * static_cast<std::size_t>(width);
            for (std::size_t at = begin; at < end; ++at)
            {
                const Pixel3d& p = layer[at];
                if (p.Alpha == 0) continue;
                if (clear)
                {
                    const std::uint32_t a = p.Alpha == 31 ? 255U : static_cast<std::uint32_t>((p.Alpha + 1) * 255 / 32);
                    pixels[at] = p.R | (static_cast<std::uint32_t>(p.G) << 8) | (static_cast<std::uint32_t>(p.B) << 16) | (a << 24);
                    continue;
                }
                const std::uint32_t under = pixels[at];
                if (p.Alpha == 31)
                {
                    pixels[at] = p.R | (static_cast<std::uint32_t>(p.G) << 8) | (static_cast<std::uint32_t>(p.B) << 16) | 0xFF000000U;
                    continue;
                }
                const std::uint32_t eva = static_cast<std::uint32_t>(p.Alpha) + 1;
                const std::uint32_t evb = 32 - eva;
                const auto mix = [&](std::uint32_t over, int shift)
                {
                    return (over * eva + ((under >> shift) & 0xFF) * evb + 16) / 32;
                };
                pixels[at] = mix(p.R, 0) | (mix(p.G, 8) << 8) | (mix(p.B, 16) << 16) | 0xFF000000U;
            }
        }

        // Textured, vertex-coloured triangles sampled at pixel centres with
        // the nearest texel (DS pixel art), composed as the DS composes the
        // menus (GPU3D_Soft, GPU2D ColorBlend5): the backdrop bitmaps are the
        // 2D layer; everything else is drawn into a 3D layer that starts
        // clear, where an opaque pixel is written, a translucent one over
        // nothing is written as it is, and over something is mixed by its
        // alpha (a + 1) / 32 with the larger alpha kept -- never over a
        // translucent pixel of its own polygon ID. The 3D layer then goes
        // over the 2D one by its own alpha. A picture over a match has no 2D
        // layer: the 3D one is the picture, alpha and all. Bands of rows are
        // drawn on as many threads as the machine has, up to eight.
        void Rasterize(const UiDrawList& list, const UiTextureCache& textures, int width, int height,
            std::vector<std::uint32_t>& pixels, bool clear = false)
        {
            const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            pixels.assign(count, clear ? 0x00000000U : 0xFF000000U);
            static std::vector<Pixel3d> layer;
            layer.assign(count, Pixel3d{});
            // looked up here, where an exception can still be answered
            std::vector<const UiTexture*> resolved(list.Batches.size(), nullptr);
            for (std::size_t i = 0; i < list.Batches.size(); ++i)
            {
                const int id = list.Batches[i].TextureId;
                if (id < 0) continue;
                const UiTexture& texture = textures[id];
                if (texture.Width > 0 && texture.Height > 0
                    && texture.Pixels.size() >= static_cast<std::size_t>(texture.Width) * static_cast<std::size_t>(texture.Height))
                {
                    resolved[i] = &texture;
                }
            }
            const unsigned hardware = std::max(1U, std::min(8U, std::thread::hardware_concurrency()));
            const int bands = height >= 96 ? static_cast<int>(hardware) : 1;
            const int rows = (height + bands - 1) / bands;
            std::vector<std::thread> workers;
            for (int band = 1; band < bands; ++band)
            {
                const int begin = band * rows;
                const int end = std::min(height, begin + rows);
                if (begin >= end) break;
                workers.emplace_back(RasterizeRows, std::cref(list), std::cref(resolved), width, begin, end,
                    pixels.data(), layer.data(), clear);
            }
            RasterizeRows(list, resolved, width, 0, std::min(height, rows), pixels.data(), layer.data(), clear);
            for (std::thread& worker : workers) worker.join();
        }
    }

    bool Facade::Render(double seconds, int width, int height, std::vector<std::uint32_t>& pixels, bool* changed)
    {
        if (!Active() || width <= 0 || height <= 0) return false;
        try
        {
            session->Advance(seconds);
            if (changed != nullptr) *changed = true;
            // at most MaxPixelsPerUnit pixels a unit: the shell scales the picture up
            const float units = height > width ? 256.0F : UnitsHigh;
            const float shortSide = static_cast<float>(height > width ? width : height);
            const float per = std::clamp(std::floor(shortSide / units), 1.0F, static_cast<float>(MaxPixelsPerUnit));
            const int w = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * per * units / shortSide)));
            const int h = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * per * units / shortSide)));
            // nothing moved and the size is the same: the last picture stands
            if (!session->Dirty() && w == lastWidth && h == lastHeight && pixels.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h))
            {
                shownWidth = width; shownHeight = height;
                if (changed != nullptr) *changed = false;
                return true;
            }
            Rasterize(session->BuildCanvas(w, h), session->Textures(), w, h, pixels, session->OverGame());
            session->Clean();
            lastWidth = w; lastHeight = h; shownWidth = width; shownHeight = height;
            return true;
        }
        catch (const std::exception& ex)
        {
            lastError = ex.what();
            DebugLog::Line("classicmenu", std::string("the DS menus stopped: ") + ex.what());
            active = false;
            return false;
        }
    }

    bool Facade::RenderDs(int ticks, std::vector<std::uint32_t>& pixels)
    {
        if (!Active()) return false;
        try
        {
            session->Step(ticks);
            Rasterize(session->BuildDs(), session->Textures(), ScreenWidth, ScreenHeight * 2, pixels);
            session->Clean();
            return true;
        }
        catch (const std::exception& ex)
        {
            lastError = ex.what();
            DebugLog::Line("classicmenu", std::string("the DS menus stopped: ") + ex.what());
            active = false;
            return false;
        }
    }

    void Facade::DrawnSize(int& width, int& height)
    {
        width = lastWidth;
        height = lastHeight;
    }

    void Facade::Pointer(float x, float y, bool press)
    {
        if (!Active() || shownWidth <= 0) return;
        // from the shown size to the drawn one
        session->Pointer(x * static_cast<float>(lastWidth) / static_cast<float>(shownWidth),
            y * static_cast<float>(lastHeight) / static_cast<float>(shownHeight), press);
    }

    void Facade::Touch(float x, float y)
    {
        if (Active()) session->Touch(x, y);
    }

    void Facade::Press(std::uint16_t keys)
    {
        if (Active()) session->Press(keys);
    }

    void Facade::Navigate(int dx, int dy)
    {
        if (Active()) session->Navigate(dx, dy);
    }

    bool Facade::Type(const std::string& text, bool backspace, bool enter)
    {
        return Active() && session->Type(text, backspace, enter);
    }

    bool Facade::Listening()
    {
        return Active() && session->Listening();
    }

    void Facade::Show(const std::string& what)
    {
        if (Active()) session->Show(what);
    }

    std::string Facade::Describe()
    {
        return Active() ? session->Describe() : std::string("inactive\n");
    }

    void Facade::Visit(const std::string& page)
    {
        if (Active()) session->Visit(page);
    }

    bool Facade::OverGame()
    {
        return Active() && session->OverGame();
    }
}
