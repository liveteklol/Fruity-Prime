#include "ClassicMenu.hpp"

#include "MenuData.hpp"

#include "../DebugLog.hpp"
#include "../../Formats/Formats.hpp"
#include "../../Formats/Model.hpp"
#include "../../NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <set>

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
        // keys 170 apart, drawn at 21/32).
        constexpr float TintKeys[3][3] = {{0, 31, 0}, {4, 16, 16}, {19, 9, 6}};
        constexpr int TintKeyFrames = 170;
        constexpr float TintWeight = 21.0F / 32.0F;

        // The game's own two models behind every menu page: the dark channels
        // at alpha 30 and the light running through them, faded on a page
        // change.
        const std::string SlotsModel = "_archives\\frontend2d\\slots_Model.bin";
        const std::string SlotsAnim = "_archives\\frontend2d\\slots_Idle_Anim.bin";
        const std::string LinesModel = "_archives\\frontend2d\\lines_Model.bin";
        const std::string LinesAnim = "_archives\\frontend2d\\lines_Idle_Anim.bin";
        constexpr int GrooveFadeTicks = 10;


        [[nodiscard]] UiVertex Place(const Placement& p, UiVertex v)
        {
            const auto [x, y] = p.ToCanvas(v.X, v.Y);
            v.X = x;
            v.Y = y;
            return v;
        }

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
                  _menu(_file, _strings, _widgets, _font)
            {
                // Options' RUMBLE PAK / SHOW MY STATS values: the game writes
                // its own text over these placeholders -- OFF for the Rumble
                // Pak nobody has, YES for the stats (as the real game shows).
                const auto fill = [this](const char* placeholder, const char* value)
                {
                    const int id = _strings.IndexOf(placeholder);
                    const int text = _strings.IndexOf(value);
                    if (id >= 0) _strings.Fill(id, text >= 0 ? _strings[text] : std::string(value));
                };
                fill("rumb", "OFF");
                fill("priv", "YES");
                _beginGameCall = FindBeginGameCall();
                _menu.OnCall = [this](const MenuAction&, int, int b, int) { return OnCall(b); };
                _menu.OnPageEntered = [this](int page) { OnPageEntered(page); };
                _menu.Enter(TitlePage);
            }

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
            }

            void Press(std::uint16_t keys)
            {
                _showFocus = true;
                _menu.Press(keys);
            }

            void Navigate(int dx, int dy)
            {
                _showFocus = true;
                _menu.Direction(dx, dy);
            }

            // A touch on the touch screen, in DS pixels (y down).
            void Touch(float x, float y)
            {
                _showFocus = false;
                _menu.Touch(x, 192.0F - y);
            }

            std::optional<Request> TakeRequest()
            {
                auto request = _request;
                _request.reset();
                return request;
            }

            const UiTextureCache& Textures() const noexcept { return _textures; }

            // Advance the menus by real time and collect what they show.
            void Advance(double seconds)
            {
                Update(seconds);
                _tris.clear();
                _menu.Collect(_tris);
                AddFocusFrame();
            }

            // One DS screen as the game draws it, 256x192: the top screen is
            // menu space y 0..192, the touch screen -192..0. Every triangle is
            // laid on both and the rasterizer keeps what falls on each, so an
            // item drawn across the two screens is cut where the DS cuts it.
            const UiDrawList& BuildScreen(int screen)
            {
                _draw.Clear();
                const MenuPage* page = _menu.Page();
                if (page == nullptr) return _draw;
                Placement p;
                p.SrcX = 0;
                p.SrcY = screen == 0 ? 192.0F : 0.0F;
                p.Scale = 1;
                DrawBackground(page->Index, screen);
                DrawGrooves(page->Index, p);
                for (const WidgetTri& t : _tris)
                {
                    if (Hidden(t.Item)) continue;
                    _draw.Triangle(t.TextureId, t.WrapS, t.WrapT, Place(p, t.A), Place(p, t.B), Place(p, t.C));
                }
                return _draw;
            }

        private:
            void Tick()
            {
                ++_frame;
                _menu.Tick();
                TickGrooves();
            }

            bool OnCall(int b)
            {
                if (b == _beginGameCall)
                {
                    _request = Request::Adventure;
                    _menu.GoTo(MainMenuPage);
                    return true;
                }
                switch (b)
                {
                case 7: _menu.GoTo(1); return true;
                case 8: _menu.Enter(TitlePage); return true;
                case 9: case 10: case 13: case 15: case 17: case 78: return true;
                case 11: case 12: _menu.GoTo(OptionsPage); return true;
                case 44: _menu.GoTo(MainMenuPage); return true;
                case 46: _menu.GoTo(CreditsPage); return true;
                case 16:
                    // MULTIPLAYER: Fruity Prime's own screens, in place of the
                    // DS's local wireless pages.
                    _request = Request::Multiplayer;
                    return true;
                default:
                    DebugLog::Line("classicmenu", "page " + std::to_string(_menu.Page() ? _menu.Page()->Index : -1)
                        + ": unhandled call " + std::to_string(b));
                    return false;
                }
            }

            void OnPageEntered(int page)
            {
                DebugLog::Line("classicmenu", "page " + std::to_string(page));
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
            }

            int FindBeginGameCall() const
            {
                constexpr std::size_t beginGamePage = 14;
                if (_file.Pages.size() <= beginGamePage) return 14;
                for (const MenuItem& it : _file.Pages[beginGamePage].Items)
                {
                    bool isBeginGame = false;
                    for (const MenuItemState& s : it.States)
                    {
                        if (s.Text.has_value() && _strings[s.Text->StringId] == "BEGIN GAME") isBeginGame = true;
                    }
                    if (!isBeginGame) continue;
                    for (const MenuAction& a : it.Actions)
                    {
                        if (!a.Calls.empty()) return a.Calls.front().second;
                    }
                }
                return 14;
            }

            // The Wi-Fi / wireless signal icons only show when connected.
            bool Hidden(int item) const
            {
                const MenuPage* page = _menu.Page();
                const MenuItemState* s = page->Items.at(static_cast<std::size_t>(item)).GetState(_menu.ItemCode(item));
                if (s == nullptr || s->WidgetIndex < 0) return false;
                const std::string& path = _file.Widgets.at(static_cast<std::size_t>(s->WidgetIndex)).ModelPath;
                return path.rfind("main menu\\wifi", 0) == 0 || path.rfind("main menu\\wireless", 0) == 0;
            }

            // A frame in the menus' orange around the focused item's touch
            // area, pulsing, when its own look has no highlight.
            void AddFocusFrame()
            {
                float x0, y0, x1, y1;
                bool highlighted = false;
                if (!_showFocus || !_menu.TryFocusFrame(x0, y0, x1, y1, highlighted) || highlighted) return;
                const int item = _menu.FocusedItem();
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

            // The menus behind the logo: the game's Samus art (main1 on the
            // top screen, main2 on the touch screen) under the cycling tint.
            void DrawBackground(int page, int screen)
            {
                if (page <= 12) return; // logos and credits are on black
                _draw.Quad(BackgroundTexture(screen), 0, 0, 256, 192, 0, 0, 1, 1, 1, 1, 1, 1);
                if (_tintStart < 0) _tintStart = _frame;
                const long long f = (_frame - _tintStart) % (TintKeyFrames * 3);
                const int k = static_cast<int>(f / TintKeyFrames);
                const float t = static_cast<float>(f % TintKeyFrames) / TintKeyFrames;
                const float* c0 = TintKeys[k];
                const float* c1 = TintKeys[(k + 1) % 3];
                _draw.Quad(-1, 0, 0, 256, 192, 0, 0, 0, 0,
                    (c0[0] + (c1[0] - c0[0]) * t) / 31, (c0[1] + (c1[1] - c0[1]) * t) / 31,
                    (c0[2] + (c1[2] - c0[2]) * t) / 31, TintWeight);
            }

            void TickGrooves()
            {
                if (_menu.Page() == nullptr) return;
                const bool leaving = _menu.PendingPage() != -1;
                if (_grooveCounter < 0) _grooveCounter = leaving ? GrooveFadeTicks : 0;
                else _grooveCounter = leaving ? std::max(0, _grooveCounter - 1) : std::min(GrooveFadeTicks, _grooveCounter + 1);
                _grooveFrame += 2;
            }

            void DrawGrooves(int page, const Placement& p)
            {
                if (_groovesFailed || page <= 12 || page == 49 || page == 50 || page == 65) return;
                _grooveTris.clear();
                try
                {
                    _widgets.Evaluate(_widgets.Instance(SlotsModel, SlotsAnim), 0, true, 30.0F / 31.0F, _grooveTris);
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
                for (const WidgetTri& t : _grooveTris)
                {
                    _draw.Triangle(t.TextureId, t.WrapS, t.WrapT, Place(p, t.A), Place(p, t.B), Place(p, t.C));
                }
            }

            std::string _root;
            MenuFile _file;
            MenuStrings _strings;
            UiTextureCache _textures;
            MenuWidgets _widgets;
            MenuFont _font;
            MenuEngine _menu;
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
            int _beginGameCall = 14;
            std::optional<Request> _request;
        };

        std::unique_ptr<Session> session;
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

        // Textured, vertex-coloured triangles, alpha blended over the target,
        // sampled at pixel centres with the nearest texel (DS pixel art).
        void Rasterize(const UiDrawList& list, const UiTextureCache& textures, int width, int height,
            std::vector<std::uint32_t>& pixels)
        {
            pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0xFF000000U);
            for (const UiBatch& batch : list.Batches)
            {
                const UiTexture* texture = batch.TextureId >= 0 ? &textures[batch.TextureId] : nullptr;
                if (texture != nullptr && (texture->Width <= 0 || texture->Height <= 0)) texture = nullptr;
                for (int v = batch.Start; v + 2 < batch.Start + batch.Count; v += 3)
                {
                    const UiVertex& a = list.Vertices[static_cast<std::size_t>(v)];
                    const UiVertex& b = list.Vertices[static_cast<std::size_t>(v + 1)];
                    const UiVertex& c = list.Vertices[static_cast<std::size_t>(v + 2)];
                    const float area = (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
                    if (std::abs(area) < 1e-6F) continue;
                    const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.X, b.X, c.X}))));
                    const int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({a.X, b.X, c.X}))));
                    const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.Y, b.Y, c.Y}))));
                    const int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({a.Y, b.Y, c.Y}))));
                    if (minX > maxX || minY > maxY) continue;
                    const float inv = 1.0F / area;
                    for (int y = minY; y <= maxY; ++y)
                    {
                        const float py = static_cast<float>(y) + 0.5F;
                        std::uint32_t* row = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
                        for (int x = minX; x <= maxX; ++x)
                        {
                            const float px = static_cast<float>(x) + 0.5F;
                            const float w0 = ((b.X - px) * (c.Y - py) - (b.Y - py) * (c.X - px)) * inv;
                            const float w1 = ((c.X - px) * (a.Y - py) - (c.Y - py) * (a.X - px)) * inv;
                            const float w2 = 1.0F - w0 - w1;
                            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                            float r = a.R * w0 + b.R * w1 + c.R * w2;
                            float g = a.G * w0 + b.G * w1 + c.G * w2;
                            float bl = a.B * w0 + b.B * w1 + c.B * w2;
                            float al = a.A * w0 + b.A * w1 + c.A * w2;
                            if (texture != nullptr)
                            {
                                const float u = a.U * w0 + b.U * w1 + c.U * w2;
                                const float t = a.V * w0 + b.V * w1 + c.V * w2;
                                const int tx = WrapCoord(static_cast<int>(std::floor(u * static_cast<float>(texture->Width))),
                                    texture->Width, batch.WrapS);
                                const int ty = WrapCoord(static_cast<int>(std::floor(t * static_cast<float>(texture->Height))),
                                    texture->Height, batch.WrapT);
                                const std::size_t index = static_cast<std::size_t>(ty) * static_cast<std::size_t>(texture->Width)
                                    + static_cast<std::size_t>(tx);
                                if (index >= texture->Pixels.size()) continue;
                                const ColorRgba& texel = texture->Pixels[index];
                                r *= static_cast<float>(texel.Red) / 255.0F;
                                g *= static_cast<float>(texel.Green) / 255.0F;
                                bl *= static_cast<float>(texel.Blue) / 255.0F;
                                al *= static_cast<float>(texel.Alpha) / 255.0F;
                            }
                            if (al <= 0.0F) continue;
                            al = std::min(al, 1.0F);
                            std::uint32_t& dst = row[x];
                            const float keep = 1.0F - al;
                            const auto mix = [&](float src, int shift)
                            {
                                const float d = static_cast<float>((dst >> shift) & 0xFF);
                                return static_cast<std::uint32_t>(std::clamp(src * 255.0F * al + d * keep, 0.0F, 255.0F) + 0.5F);
                            };
                            dst = mix(r, 0) | (mix(g, 8) << 8) | (mix(bl, 16) << 16) | 0xFF000000U;
                        }
                    }
                }
            }
        }
    }

    bool Facade::RenderScreens(double seconds, std::vector<std::uint32_t>& top, std::vector<std::uint32_t>& bottom)
    {
        if (!Active()) return false;
        try
        {
            session->Advance(seconds);
            Rasterize(session->BuildScreen(0), session->Textures(), ScreenWidth, ScreenHeight, top);
            Rasterize(session->BuildScreen(1), session->Textures(), ScreenWidth, ScreenHeight, bottom);
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

    std::optional<Request> Facade::TakeRequest()
    {
        return session ? session->TakeRequest() : std::nullopt;
    }
}
