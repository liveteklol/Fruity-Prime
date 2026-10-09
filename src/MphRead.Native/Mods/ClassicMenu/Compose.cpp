#include "Compose.hpp"

#include "../DebugLog.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace MphRead::Mods::ClassicMenu
{
    namespace
    {
        // The ROM's pages this works from.
        constexpr int TitlePage = 13;
        constexpr int MainMenuPage = 18;
        constexpr int MoviesPage = 19;
        constexpr int OptionsPage = 21;
        constexpr int FilePage = 24;
        constexpr int MultiplayerPage = 25;
        constexpr int CreateJoinPage = 26;
        constexpr int ModePage = 27;
        constexpr int FirstModeOptions = 28;   // 28..34
        constexpr int HunterPage = 35;
        constexpr int HunterJoinPage = 36;
        constexpr int ResultsPage = 47;
        constexpr int WifiPage = 52;
        constexpr int ServerListPage = 56;
        constexpr int FriendSearchPage = 57;
        constexpr int WifiLobbyPage = 58;
        constexpr int SingleCardLobby = 44;
        constexpr int KeyboardPage = 62;
        constexpr int CreditsFirst = 6;

        // The ROM's text colours, 5-bit.
        constexpr std::uint32_t Orange = Rgb5(31, 20, 8);
        constexpr std::uint32_t White = Rgb5(31, 31, 31);
        constexpr std::uint32_t Heading = Rgb5(31, 22, 13);
        constexpr std::uint32_t ListText = Rgb5(29, 15, 1);
        constexpr std::uint32_t Black = Rgb5(0, 0, 0);
        constexpr std::uint32_t Pale = Rgb5(31, 27, 21);
        constexpr std::uint32_t Dim = Rgb5(14, 17, 16);
        constexpr std::uint32_t Good = Rgb5(8, 31, 8);
        constexpr std::uint32_t Fair = Rgb5(31, 25, 4);
        constexpr std::uint32_t Bad = Rgb5(31, 8, 8);

        // Game modes in the DS's order, and the options page each opens.
        const char* const ModeNames[7] = {"battle", "survival", "bounty", "defender", "prime hunter", "capture", "nodes"};
        const char* const HunterNames[7] = {"samus", "kanden", "spire", "trace", "noxus", "sylux", "weavel"};
        const std::uint32_t HunterColours[7] = {Rgb5(31, 18, 6), Rgb5(23, 27, 8), Rgb5(31, 22, 5), Rgb5(31, 8, 7),
            Rgb5(16, 18, 31), Rgb5(12, 21, 31), Rgb5(12, 26, 18)};
        const char* const BotNames[7] = {"SAMBOT", "KANBOT", "SPIBOT", "TRABOT", "NOXBOT", "SYBOT", "WEABOT"};
        const char* const Skills[4] = {"easy", "norm", "hard", "max"};
        const char* const TeamNames[4] = {"red", "blue", "green", "gold"};
        const std::uint32_t TeamColours[4] = {Rgb5(31, 9, 7), Rgb5(10, 18, 31), Rgb5(8, 31, 8), Rgb5(31, 25, 4)};
        // The options each mode's page shows, in its rows.
        const std::vector<std::vector<std::string>> ModeOptions[7] = {
            {{"5", "7", "10", "15", "20", "25"}, {"5", "7", "10", "15", "20"}, {"off", "on"}},
            {{"1", "3", "5", "7", "10"}, {"5", "10", "20"}, {"off", "on"}},
            {{"3", "5", "7", "10"}, {"10", "20"}, {"off", "on"}, {"off", "on"}},
            {{"1", "3", "5"}, {"10", "20"}, {"off", "on"}},
            {{"1", "3", "5"}, {"10", "20"}},
            {{"3", "5", "7"}, {"10", "20"}, {"off", "on"}},
            {{"50", "100", "150"}, {"10", "20"}, {"off", "on"}}};
        // battle, survival, bounty, defender, prime hunter, capture, nodes -> pages 28..34
        constexpr int ModePages[7] = {28, 30, 32, 31, 29, 34, 33};
        const char* const Sections[7] = {"display", "audio", "keyboard", "gamepad", "stylus", "profile", "online"};

        constexpr int CallBase = 5000;
        constexpr int SettingsRows = 7;
        constexpr int ServerRows = 9;
        constexpr int RoomSlots = 24;

        [[nodiscard]] std::string BaseName(std::string path)
        {
            const std::size_t slash = path.find_last_of("\\/");
            if (slash != std::string::npos) path = path.substr(slash + 1);
            std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const std::string suffix = "_model.bin";
            if (path.size() > suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                path.resize(path.size() - suffix.size());
            }
            return path;
        }

        [[nodiscard]] std::string Upper(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return s;
        }

        [[nodiscard]] std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        void Recolour(MenuItem& it, std::uint32_t colour)
        {
            for (MenuItemState& s : it.States)
            {
                if (!s.Text.has_value()) continue;
                s.Text->StartColor = (s.Text->StartColor & 0xFF000000U) | (colour & 0x00FFFFFFU);
                s.Text->EndColor = (s.Text->EndColor & 0xFF000000U) | (colour & 0x00FFFFFFU);
            }
        }

        [[nodiscard]] std::uint32_t PingColour(int ping)
        {
            return ping < 0 ? Dim : ping < 60 ? Good : ping < 120 ? Fair : Bad;
        }
    }

    Composer::Composer(MenuFile& file, MenuStrings& strings, const MenuFont& font)
        : _file(file), _strings(strings), _font(font)
    {
    }

    // ---------------------------------------------------------------- finding

    int Composer::FindModel(int page, const std::string& name) const
    {
        if (page < 0 || static_cast<std::size_t>(page) >= _rom.size()) return -1;
        const std::string want = Lower(name);
        const MenuPage& p = _rom[static_cast<std::size_t>(page)];
        for (std::size_t i = 0; i < p.Items.size(); ++i)
        {
            for (const MenuItemState& s : p.Items[i].States)
            {
                if (s.WidgetIndex >= 0 && BaseName(_file.Widgets.at(static_cast<std::size_t>(s.WidgetIndex)).ModelPath) == want)
                {
                    return static_cast<int>(i);
                }
            }
        }
        return -1;
    }

    int Composer::FindText(int page, const std::string& words) const
    {
        if (page < 0 || static_cast<std::size_t>(page) >= _rom.size()) return -1;
        const MenuPage& p = _rom[static_cast<std::size_t>(page)];
        for (std::size_t i = 0; i < p.Items.size(); ++i)
        {
            for (const MenuItemState& s : p.Items[i].States)
            {
                if (s.Text.has_value() && _strings[s.Text->StringId] == words) return static_cast<int>(i);
            }
        }
        return -1;
    }

    float Composer::RestTop(int, int) const { return 0; }

    // ---------------------------------------------------------------- building

    int Composer::NewPage(Family family)
    {
        MenuPage page;
        page.Index = static_cast<int>(_file.Pages.size());
        _file.Pages.push_back(std::move(page));
        _family[_file.Pages.back().Index] = family;
        return _file.Pages.back().Index;
    }

    // A copy of a ROM item, moved by (dx, dy) on the canvas (y down). It
    // keeps its looks and its animations; what it did on its own page (its
    // links to other items, its touches) is not copied.
    int Composer::Clone(int dst, int srcPage, int srcItem, float dx, float dy)
    {
        if (srcItem < 0 || static_cast<std::size_t>(srcPage) >= _rom.size()
            || static_cast<std::size_t>(srcItem) >= _rom[static_cast<std::size_t>(srcPage)].Items.size())
        {
            return -1;
        }
        MenuItem it = _rom[static_cast<std::size_t>(srcPage)].Items[static_cast<std::size_t>(srcItem)];
        MenuPage& page = _file.Pages.at(static_cast<std::size_t>(dst));
        it.Index = static_cast<int>(page.Items.size());
        it.X += dx;
        it.Y -= dy;
        it.Links.clear();
        it.Actions.clear();
        page.Items.push_back(std::move(it));
        return static_cast<int>(page.Items.size()) - 1;
    }

    int Composer::CloneModel(int dst, int srcPage, const std::string& model, float dx, float dy)
    {
        const int item = FindModel(srcPage, model);
        if (item < 0) DebugLog::Line("classicmenu", "no " + model + " on page " + std::to_string(srcPage));
        return Clone(dst, srcPage, item, dx, dy);
    }

    // A text item in the ROM's font: a copy of a ROM text item (its size and
    // how it fades in and out) with new words and colour. x is the anchor
    // for the alignment, cy the middle of the first line, on the canvas.
    int Composer::AddText(int dst, const std::string& words, std::uint32_t colour, float x, float cy, int align,
        int refPage, int refItem, int wrap)
    {
        const int id = _strings.Add(words);
        MenuItem it;
        if (refItem >= 0 && static_cast<std::size_t>(refPage) < _rom.size()
            && static_cast<std::size_t>(refItem) < _rom[static_cast<std::size_t>(refPage)].Items.size())
        {
            it = _rom[static_cast<std::size_t>(refPage)].Items[static_cast<std::size_t>(refItem)];
        }
        else
        {
            MenuItemState s;
            s.Code = static_cast<int>(MenuState::Idle);
            s.Kind = 1;
            s.Text = MenuTextStyle{};
            it.States.push_back(s);
            it.InitialState = static_cast<std::uint8_t>(MenuState::Idle);
        }
        int size = 8;
        for (MenuItemState& s : it.States)
        {
            if (!s.Text.has_value()) continue;
            s.Text->StringId = id;
            s.Text->Align = static_cast<std::uint8_t>(align);
            s.Text->WrapWidth = static_cast<std::uint16_t>(wrap);
            if (s.Text->Size != 0) size = s.Text->Size;
        }
        Recolour(it, colour);
        // a text copied from one the ROM only shows under focus (a
        // description) is shown here from the start
        it.InitialState = static_cast<std::uint8_t>(MenuState::Idle);
        // Where the font puts a line drawn at text y = 0: the anchor is the
        // bottom of the first line, Y up, so moving it up moves the text up.
        std::vector<WidgetTri> probe;
        _font.Emit("0", 0, 0, 0, 0, static_cast<float>(size), 1, 1, 1, 1, 0, probe);
        float lo = 1e9F, hi = -1e9F;
        for (const WidgetTri& t : probe)
        {
            for (const UiVertex* v : {&t.A, &t.B, &t.C}) { lo = std::min(lo, v->Y); hi = std::max(hi, v->Y); }
        }
        const float centre0 = probe.empty() ? 380.0F : 192.0F - (lo + hi) / 2.0F;
        MenuPage& page = _file.Pages.at(static_cast<std::size_t>(dst));
        it.Index = static_cast<int>(page.Items.size());
        it.X = x;
        it.Y = centre0 - cy;
        it.Links.clear();
        it.Actions.clear();
        page.Items.push_back(std::move(it));
        return static_cast<int>(page.Items.size()) - 1;
    }

    int Composer::AddLive(int dst, std::function<std::string()> words, std::uint32_t colour, float x, float cy, int align,
        int refPage, int refItem, std::function<std::uint32_t()> liveColour)
    {
        const int item = AddText(dst, " ", colour, x, cy, align, refPage, refItem);
        const MenuItem& it = _file.Pages[static_cast<std::size_t>(dst)].Items[static_cast<std::size_t>(item)];
        int id = -1;
        for (const MenuItemState& s : it.States) if (s.Text.has_value()) id = s.Text->StringId;
        _texts.push_back(Text{dst, item, id, std::move(words), std::move(liveColour)});
        return item;
    }

    // A touch zone with nothing to draw: an item with no looks, on top of
    // everything (touches are tried from the last item back).
    int Composer::Hot(int dst, float x0, float y0, float x1, float y1, std::function<void()> act)
    {
        MenuAction a;
        a.Kind = 0;
        a.RectX = static_cast<std::int16_t>(std::lround(x0));
        a.RectW = static_cast<std::int16_t>(std::lround(x1));
        // touches are in text coordinates: Y up from the touch screen's bottom
        a.RectY = static_cast<std::int16_t>(std::lround(384 - y1));
        a.RectH = static_cast<std::int16_t>(std::lround(384 - y0));
        a.Calls.emplace_back(0, Register(std::move(act)));
        MenuItem it;
        MenuPage& page = _file.Pages.at(static_cast<std::size_t>(dst));
        it.Index = static_cast<int>(page.Items.size());
        it.InitialState = static_cast<std::uint8_t>(MenuState::Idle);
        it.Actions.push_back(a);
        page.Items.push_back(std::move(it));
        return static_cast<int>(page.Items.size()) - 1;
    }

    int Composer::Register(std::function<void()> act)
    {
        _calls.push_back(std::move(act));
        return CallBase + static_cast<int>(_calls.size()) - 1;
    }

    void Composer::ShowWhen(int page, int item, std::function<bool()> when)
    {
        if (item < 0) return;
        _shown.push_back(Shown{page, item, std::move(when), -1});
    }

    void Composer::SetStretch(int page, int item, float pivot, float scale)
    {
        if (item >= 0) _stretch[{page, item}] = Stretch{pivot, scale};
    }

    void Composer::Move(int page, int item, float dx, float dy)
    {
        if (item < 0) return;
        MenuItem& it = _file.Pages.at(static_cast<std::size_t>(page)).Items.at(static_cast<std::size_t>(item));
        it.X += dx;
        it.Y -= dy;
    }

    // A ROM item a page no longer has: never shown, never touched.
    void Composer::Remove(int page, int item)
    {
        if (item < 0) return;
        MenuItem& it = _file.Pages.at(static_cast<std::size_t>(page)).Items.at(static_cast<std::size_t>(item));
        it.InitialState = static_cast<std::uint8_t>(MenuState::Hidden);
        it.States.clear();
        it.Actions.clear();
        it.Links.clear();
    }

    // New words for a ROM text item, in a string of its own (the ROM's
    // string may be shown elsewhere).
    void Composer::Retext(int page, int item, const std::string& words)
    {
        if (item < 0) return;
        const int id = _strings.Add(words);
        for (MenuItemState& s : _file.Pages.at(static_cast<std::size_t>(page)).Items.at(static_cast<std::size_t>(item)).States)
        {
            if (s.Text.has_value()) s.Text->StringId = id;
        }
    }

    // The touch screen's dark panel, its heading and the back arrow, as
    // every menu page has them; wider for the pages that need it.
    void Composer::TouchFrame(int dst, const std::string& heading, float width)
    {
        const int drop = CloneModel(dst, MainMenuPage, "blackdrop", 0, 0);
        if (width != 256) SetStretch(dst, drop, 0, width / 256.0F);
        CloneModel(dst, MainMenuPage, "backicon", 0, 0);
        AddText(dst, heading, Heading, width / 2, 207, 2, MultiplayerPage, FindText(MultiplayerPage, "CHOOSE MULTIPLAYER TYPE"));
        Hot(dst, 4, 348, 36, 380, [this] { Back(); });
    }

    void Composer::TopLogo(int dst, std::function<std::string()> band)
    {
        CloneModel(dst, MainMenuPage, "toplogo", 0, 0);
        CloneModel(dst, MainMenuPage, "toplogor", 0, 0);
        CloneModel(dst, MainMenuPage, "topdrop", 0, 0);
        const int ref = FindText(MainMenuPage, "MISSION FILE 79109:\njourney to the TETRA GALAXY to uncover the source of a mysterious message.");
        const int item = AddLive(dst, std::move(band), White, 128, 160, 2, MainMenuPage, ref);
        for (MenuItemState& s : _file.Pages[static_cast<std::size_t>(dst)].Items[static_cast<std::size_t>(item)].States)
        {
            if (s.Text.has_value()) s.Text->WrapWidth = 230;
        }
    }

    // The top screen of the match pages: settings, arena, mode, players.
    void Composer::TopStatus(int dst)
    {
        const MenuPage& p = _rom[FirstModeOptions];
        for (std::size_t i = 0; i < p.Items.size(); ++i)
        {
            bool top = false;
            for (const MenuItemState& s : p.Items[i].States)
            {
                if (s.WidgetIndex >= 0)
                {
                    const std::string m = BaseName(_file.Widgets.at(static_cast<std::size_t>(s.WidgetIndex)).ModelPath);
                    top = m == "topstatus" || m == "topsettings";
                }
                // the panel's words sit above the seam: text y > 192
                else if (s.Text.has_value()) top = p.Items[i].Y > 200;
            }
            if (top) Clone(dst, FirstModeOptions, static_cast<int>(i), 0, 0);
        }
    }

    int Composer::OkButton(int dst, std::function<void()> act, float x)
    {
        const int ok = FindModel(FirstModeOptions, "ok");
        const int item = Clone(dst, FirstModeOptions, ok, x - 199, 0);
        Hot(dst, x - 2, 350, x + 28, 380, std::move(act));
        return item;
    }

    // The controls page's list buttons, two across: each lights up under
    // the pointer as the DS's does when chosen.
    void Composer::ListButtons(int dst, const std::vector<std::pair<std::function<std::string()>, std::function<void()>>>& entries,
        float top, std::vector<int>* boxes)
    {
        const int off = FindModel(23, "selectaoff");
        const int ref = FindText(23, "stylus mode\nright");
        for (std::size_t k = 0; k < entries.size(); ++k)
        {
            const float col = static_cast<float>(k % 2), row = static_cast<float>(k / 2);
            const float dx = 4 + col * 116, dy = top - 224 + row * 32;
            const int box = Clone(dst, 23, off, dx, dy);
            const int label = AddLive(dst, entries[k].first, ListText, 62 + dx, 237 + dy, 2, 23, ref);
            const int hot = Hot(dst, 8 + dx, 224 + dy, 116 + dx, 250 + dy, entries[k].second);
            if (boxes != nullptr) { boxes->push_back(box); boxes->push_back(label); boxes->push_back(hot); }
            _rows[dst].push_back({hot, {8 + dx, 224 + dy}});
        }
    }

    // ---------------------------------------------------------------- the pages

    void Composer::Build()
    {
        _rom = _file.Pages;
        _vars = {{"bots", 3}, {"skill", 1}, {"teams", 0}, {"arena", 0}, {"mode", 0}, {"hunter", 0}, {"suit", 0},
            {"page", 0}, {"rotation", 0}, {"hoston", 0}, {"hosting", 0}};
        for (int p = 0; p < static_cast<int>(_file.Pages.size()); ++p)
        {
            _family[p] = p <= 12 ? Family::Stacked
                : p == TitlePage ? Family::Title
                : p >= 62 && p <= 64 ? Family::Keys
                : (p >= 18 && p <= 25) || p == 40 || p == 41 || (p >= 51 && p <= 54) ? Family::Logo
                : Family::Split;
        }
        BuildServers();
        BuildCreate();
        BuildSettings();
        BuildLobby();
        BuildRoom();
        BuildManage();
        BuildPause();
        BuildEnd();
        BuildCredits();
        PatchRom();
        BuildMultiplayer();
        DebugLog::Line("classicmenu", "single screen: " + std::to_string(_file.Pages.size()) + " pages");
    }

    // The server browser: page 56's game panel idea, as one ROM-font line a
    // server so dozens fit, nine at a time, page 57's arrows to turn the list.
    void Composer::BuildServers()
    {
        const int p = ServersPage = NewPage(Family::Wide);
        TopLogo(p, [this] { return _host ? _host->ServerNote() : std::string("every server, with its arena, mode, players and ping."); });
        TouchFrame(p, "SERVERS", 400);
        const int boxRef = FindModel(FirstModeOptions, "box_type");
        const int labelRef = FindText(FirstModeOptions, "ARENA");
        const auto box = [&](float x0, float x1, float y)
        {
            const int b = Clone(p, FirstModeOptions, boxRef, 0, y - 235);
            // box_type is 106..226 at scale 1: stretch it over x0..x1
            Move(p, b, x0 - 106, 0);
            SetStretch(p, b, x0, (x1 - x0) / 120.0F);
            return b;
        };
        box(6, 96, 220);
        AddLive(p, [this] { return _host ? Upper(_host->PlayerName()) : std::string("PLAYER"); }, White, 51, 228, 2, FirstModeOptions, labelRef);
        Hot(p, 6, 220, 96, 236, [this]
        {
            OpenKeyboard("your name", _host ? _host->PlayerName() : "", [this](const std::string& s) { if (_host) _host->SetPlayerName(s); });
        });
        box(100, 330, 220);
        AddLive(p, [this]
        {
            const std::string a = _host ? _host->Address() : "";
            return a.empty() ? std::string("host:port, or pick a row") : a;
        }, Pale, 215, 228, 2, FirstModeOptions, labelRef);
        Hot(p, 100, 220, 330, 236, [this]
        {
            OpenKeyboard("server address", _host ? _host->Address() : "", [this](const std::string& s) { if (_host) _host->SetAddress(s); });
        });
        box(334, 394, 220);
        AddText(p, "REFRESH", Orange, 364, 228, 2, FirstModeOptions, labelRef);
        Hot(p, 334, 220, 394, 236, [this] { if (_host) _host->RefreshServers(); });

        const struct { float X; int Align; const char* Name; } cols[] = {
            {10, 0, "SERVER"}, {150, 0, "ARENA"}, {250, 0, "MODE"}, {346, 1, "PLAYERS"}, {394, 1, "PING"}};
        for (const auto& c : cols) AddText(p, c.Name, Orange, c.X, 244, c.Align, FirstModeOptions, labelRef);
        const int line = CloneModel(p, ResultsPage, "teamdivide", 4, 249 - 303);
        SetStretch(p, line, 4, 392.0F / 256.0F);

        const int barRef = FindModel(CreateJoinPage, "redbar");
        const int bar = Clone(p, CreateJoinPage, barRef, 0, 0);
        _rowBar[p] = bar;
        for (int r = 0; r < ServerRows; ++r)
        {
            const float y = 253 + static_cast<float>(r) * 9;
            const auto row = [this, r] { return _host ? _host->Servers() : std::vector<ServerRow>{}; };
            const auto at = [this, r](const std::vector<ServerRow>& all) -> const ServerRow*
            {
                const std::size_t i = static_cast<std::size_t>(_vars["page"] * ServerRows + r);
                return i < all.size() ? &all[i] : nullptr;
            };
            const auto fit = [](std::string s, std::size_t n) { return s.size() > n ? s.substr(0, n - 1) + "." : s; };
            const int name = AddLive(p, [row, at, fit] { const auto all = row(); const ServerRow* s = at(all); return s ? fit(Upper(s->Name), 22) : std::string(" "); },
                White, 10, y + 3, 0, FirstModeOptions, labelRef);
            AddLive(p, [row, at, fit] { const auto all = row(); const ServerRow* s = at(all); return s ? fit(Lower(s->Arena), 16) : std::string(" "); },
                Orange, 150, y + 3, 0, FirstModeOptions, labelRef);
            AddLive(p, [row, at, fit] { const auto all = row(); const ServerRow* s = at(all); return s ? fit(Lower(s->Mode), 14) : std::string(" "); },
                Pale, 250, y + 3, 0, FirstModeOptions, labelRef);
            AddLive(p, [row, at] { const auto all = row(); const ServerRow* s = at(all); return s ? s->Players : std::string(" "); },
                White, 346, y + 3, 1, FirstModeOptions, labelRef);
            AddLive(p, [row, at] { const auto all = row(); const ServerRow* s = at(all); return s ? (s->Ping < 0 ? std::string("...") : std::to_string(s->Ping)) : std::string(" "); },
                White, 394, y + 3, 1, FirstModeOptions, labelRef,
                [row, at] { const auto all = row(); const ServerRow* s = at(all); return s ? PingColour(s->Ping) : Dim; });
            (void)name;
            const int hot = Hot(p, 4, y - 2, 396, y + 9, [this, r]
            {
                const int index = _vars["page"] * ServerRows + r;
                if (!_host || static_cast<std::size_t>(index) >= _host->Servers().size()) return;
                _vars["join"] = index;
                _vars["flow"] = 3;   // join
                GoTo(HunterJoinPage);
            });
            _rows[p].push_back({hot, {4, y - 2}});
        }
        const int up = CloneModel(p, FriendSearchPage, "uparrow", 346 - 105, 332 - 332);
        const int down = CloneModel(p, FriendSearchPage, "downarrow", 374 - 136, 332 - 333);
        (void)up; (void)down;
        Hot(p, 340, 330, 366, 348, [this] { _vars["page"] = std::max(0, _vars["page"] - 1); });
        Hot(p, 368, 330, 394, 348, [this]
        {
            const int count = _host ? static_cast<int>(_host->Servers().size()) : 0;
            if ((_vars["page"] + 1) * ServerRows < count) ++_vars["page"];
        });
        AddLive(p, [this]
        {
            const int count = _host ? static_cast<int>(_host->Servers().size()) : 0;
            if (count == 0) return std::string("no servers yet");
            const int first = _vars["page"] * ServerRows + 1;
            return "servers " + std::to_string(first) + "-" + std::to_string(std::min(count, first + ServerRows - 1)) + " of " + std::to_string(count);
        }, Pale, 10, 341, 0, FirstModeOptions, labelRef);
        const int create = CloneModel(p, ServerListPage, "creategame", 258 - 8, 356 - 321);
        SetStretch(p, create, 258, 1.0F);
        AddText(p, "CREATE SERVER", Black, 326, 364, 2, ServerListPage, FindText(ServerListPage, "create game"));
        Hot(p, 258, 356, 394, 372, [this] { GoTo(CreatePage); });
    }

    // Creating a server: the options page's rows, the top screen's panels.
    void Composer::BuildCreate()
    {
        const int p = CreatePage = NewPage(Family::Logo);
        TopLogo(p, [this] { return _host ? _host->CreateNote() : std::string(" "); });
        TouchFrame(p, "CREATE SERVER");
        const int arrowsRef = FindModel(FirstModeOptions, "arrows_type");
        const int boxRef = FindModel(FirstModeOptions, "box_type");
        const int labelRef = FindText(FirstModeOptions, "ARENA");
        struct RowDef { const char* Label; std::function<std::string()> Value; std::function<void(int)> Step; };
        _vars["cname"] = 0;
        std::vector<RowDef> rows = {
            {"name", [this] { return _vars.count("named") ? _keyboardText : (_host ? Upper(_host->PlayerName()) + "'S GAME" : std::string("MY GAME")); },
                [this](int) { OpenKeyboard("server name", _host ? Upper(_host->PlayerName()) + "'S GAME" : "", [this](const std::string& s)
                    { _vars["named"] = 1; _keyboardText = s; }); }},
            {"game type", [this] { return std::string(ModeNames[_vars["mode"]]); }, [this](int d) { _vars["mode"] = (_vars["mode"] + d + 7) % 7; }},
            {"your hunter", [this] { return std::string(HunterNames[_vars["hunter"]]); }, [this](int d) { _vars["hunter"] = (_vars["hunter"] + d + 7) % 7; }},
            {"rotation", [this] { const auto r = _host ? _host->Rotations() : std::vector<std::string>{"all arenas"};
                return r.empty() ? std::string("-") : Lower(r[static_cast<std::size_t>(_vars["rotation"]) % r.size()]); },
                [this](int d) { const int n = _host ? std::max<int>(1, static_cast<int>(_host->Rotations().size())) : 1; _vars["rotation"] = (_vars["rotation"] + d + n) % n; }},
            {"host on", [this] { const auto r = _host ? _host->HostChoices() : std::vector<std::string>{"this machine"};
                return r.empty() ? std::string("-") : Lower(r[static_cast<std::size_t>(_vars["hoston"]) % r.size()]); },
                [this](int d) { const int n = _host ? std::max<int>(1, static_cast<int>(_host->HostChoices().size())) : 1; _vars["hoston"] = (_vars["hoston"] + d + n) % n; }},
            {"hosting", [this] { const auto r = _host ? _host->Kinds() : std::vector<std::string>{"hosted"};
                return r.empty() ? std::string("-") : Lower(r[static_cast<std::size_t>(_vars["hosting"]) % r.size()]); },
                [this](int d) { const int n = _host ? std::max<int>(1, static_cast<int>(_host->Kinds().size())) : 1; _vars["hosting"] = (_vars["hosting"] + d + n) % n; }}};
        for (std::size_t k = 0; k < rows.size(); ++k)
        {
            const float y = 222 + static_cast<float>(k) * 20;
            Clone(p, FirstModeOptions, arrowsRef, 0, y - 236);
            Clone(p, FirstModeOptions, boxRef, 0, y - 235);
            AddText(p, rows[k].Label, Orange, 86, y + 7, 1, FirstModeOptions, labelRef);
            AddLive(p, rows[k].Value, White, 166, y + 7, 2, FirstModeOptions, labelRef);
            const auto step = rows[k].Step;
            Hot(p, 91, y - 2, 106, y + 16, [step] { step(-1); });
            Hot(p, 226, y - 2, 241, y + 16, [step] { step(1); });
            Hot(p, 106, y - 1, 226, y + 15, [step] { step(1); });
        }
        OkButton(p, [this]
        {
            if (!_host) return;
            CreateChoice c;
            c.Name = _vars.count("named") ? _keyboardText : Upper(_host->PlayerName()) + "'S GAME";
            c.Mode = _vars["mode"];
            c.Hunter = _vars["hunter"];
            c.Rotation = _vars["rotation"];
            c.Host = _vars["hoston"];
            c.Kind = _vars["hosting"];
            _host->CreateServer(c);
        });
    }

    // One page for every section of the settings: up to seven rows in the
    // options page's arrows and boxes, L and R to turn the page, the
    // section's name between arrows at the top.
    void Composer::BuildSettings()
    {
        const int p = SettingsPage = NewPage(Family::Wide);
        TopLogo(p, [this]
        {
            return _section == "display" ? std::string("how the game is drawn: window, renderer, view and the hud.")
                : _section == "audio" ? std::string("change the balance of sound effects and music, and the language.")
                : _section == "keyboard" ? std::string("tap a key's box, then press the key or button you want.")
                : _section == "gamepad" ? std::string("how a controller aims and which button does what.")
                : _section == "stylus" ? std::string("a pen tablet as the ds's touch screen.")
                : _section == "profile" ? std::string("your name, your hunter and your suit.")
                : std::string("where fruity prime looks for servers, and whether it checks for updates.");
        });
        TouchFrame(p, " ", 400);
        const int headRef = FindText(MultiplayerPage, "CHOOSE MULTIPLAYER TYPE");
        AddLive(p, [this] { return Upper(_section) + (_listening ? "  -  PRESS A KEY" : ""); }, Heading, 200, 207, 2, MultiplayerPage, headRef);
        const int optArrows = FindModel(FirstModeOptions, "arrows_option");
        const auto section = [this](int d)
        {
            int i = 0;
            for (int k = 0; k < 7; ++k) if (_section == Sections[k]) i = k;
            _section = Sections[(i + d + 7) % 7];
            _settingsPage = 0;
        };
        // arrows_option is 178..241 x 269..283: around the heading, 105..295
        const int head = Clone(p, FirstModeOptions, optArrows, 105 - 178, 200 - 269);
        SetStretch(p, head, 105, 190.0F / 63.0F);
        Hot(p, 100, 196, 125, 218, [section] { section(-1); });
        Hot(p, 275, 196, 300, 218, [section] { section(1); });

        const int arrowsRef = FindModel(FirstModeOptions, "arrows_type");
        const int boxRef = FindModel(FirstModeOptions, "box_type");
        const int labelRef = FindText(FirstModeOptions, "ARENA");
        const auto rowAt = [this](int r) -> std::optional<SettingRow>
        {
            if (!_host) return std::nullopt;
            const auto rows = _host->Settings(_section);
            const std::size_t i = static_cast<std::size_t>(_settingsPage * SettingsRows + r);
            if (i >= rows.size()) return std::nullopt;
            return rows[i];
        };
        for (int r = 0; r < SettingsRows; ++r)
        {
            const float y = 222 + static_cast<float>(r) * 16;
            // arrows_type 91..241 at scale 1, moved to 195..345
            const int arrows = Clone(p, FirstModeOptions, arrowsRef, 104, y - 236);
            const int box = Clone(p, FirstModeOptions, boxRef, 104, y - 235);
            AddLive(p, [rowAt, r] { const auto s = rowAt(r); return s ? Lower(s->Label) : std::string(" "); }, Orange, 190, y + 7, 1, FirstModeOptions, labelRef);
            AddLive(p, [rowAt, r]
            {
                const auto s = rowAt(r);
                if (!s) return std::string(" ");
                std::string v = s->Value;
                if (v.size() > 22) v = v.substr(0, 21) + ".";
                return v.empty() ? std::string("-") : v;
            }, White, 270, y + 7, 2, FirstModeOptions, labelRef);
            ShowWhen(p, arrows, [rowAt, r] { const auto s = rowAt(r); return s && s->Steps; });
            ShowWhen(p, box, [rowAt, r] { return rowAt(r).has_value(); });
            const auto id = [rowAt, r] { const auto s = rowAt(r); return s ? s->Id : std::string(); };
            Hot(p, 195, y - 2, 210, y + 14, [this, id] { if (_host && !id().empty()) _host->StepSetting(_section, id(), -1); });
            Hot(p, 330, y - 2, 345, y + 14, [this, id] { if (_host && !id().empty()) _host->StepSetting(_section, id(), 1); });
            Hot(p, 210, y - 1, 330, y + 14, [this, rowAt, r]
            {
                const auto s = rowAt(r);
                if (!s || !_host) return;
                if (s->Steps) { _host->StepSetting(_section, s->Id, 1); return; }
                if (s->Text)
                {
                    const std::string section = _section, id = s->Id;
                    OpenKeyboard(Lower(s->Label), s->Value, [this, section, id](const std::string& text)
                    {
                        if (_host) _host->SetSettingText(section, id, text);
                    });
                    return;
                }
                _host->ClickSetting(_section, s->Id);
            });
        }
        CloneModel(p, FriendSearchPage, "uparrow", 346 - 105, 332 - 332);
        CloneModel(p, FriendSearchPage, "downarrow", 374 - 136, 332 - 333);
        Hot(p, 340, 330, 366, 348, [this] { _settingsPage = std::max(0, _settingsPage - 1); });
        Hot(p, 368, 330, 394, 348, [this]
        {
            const int count = _host ? static_cast<int>(_host->Settings(_section).size()) : 0;
            if ((_settingsPage + 1) * SettingsRows < count) ++_settingsPage;
        });
        AddLive(p, [this]
        {
            const int count = _host ? static_cast<int>(_host->Settings(_section).size()) : 0;
            const int pages = std::max(1, (count + SettingsRows - 1) / SettingsRows);
            return "page " + std::to_string(_settingsPage + 1) + " of " + std::to_string(pages);
        }, Pale, 10, 341, 0, FirstModeOptions, labelRef);
        OkButton(p, [this] { if (_host) _host->SaveSettings(); Back(); }, 360);
    }

    // The room on this machine (offline): you and up to sixteen bots, their
    // skill and the teams on the options page's arrows, every slot on one
    // screen in two columns, START.
    void Composer::BuildLobby()
    {
        const int p = LobbyPage = NewPage(Family::Wide);
        TopLogo(p, [] { return std::string("you can wait for more players or add bots."); });
        TouchFrame(p, " ", 400);
        const int headRef = FindText(MultiplayerPage, "CHOOSE MULTIPLAYER TYPE");
        AddLive(p, [this]
        {
            const auto arenas = _host ? _host->Arenas() : std::vector<std::string>{};
            const std::string arena = arenas.empty() ? std::string("arena") : arenas[static_cast<std::size_t>(_vars["arena"]) % arenas.size()];
            return Upper(ModeName(_vars["mode"])) + " - " + Upper(arena) + " - " + std::to_string(1 + _vars["bots"]) + " / " + std::to_string(1 + (_host ? _host->MaxBots() : 16));
        }, Heading, 200, 207, 2, MultiplayerPage, headRef);
        const int labelRef = FindText(FirstModeOptions, "ARENA");
        const int arrowsRef = FindModel(FirstModeOptions, "arrows_option");
        const int boxRef = FindModel(FirstModeOptions, "box_arrows");
        struct Ctl { const char* Var; const char* Label; float X; float W; std::function<int()> Count; std::function<std::string(int)> Name; };
        const std::vector<Ctl> ctls = {
            {"bots", "bots", 44, 44, [this] { return 1 + (_host ? _host->MaxBots() : 16); }, [](int v) { return std::to_string(v); }},
            {"skill", "bot skill", 176, 44, [] { return 4; }, [](int v) { return std::string(Skills[v % 4]); }},
            {"teams", "teams", 300, 60, [] { return 3; }, [](int v) { return std::string(v == 0 ? "off" : v == 1 ? "2 teams" : "4 teams"); }}};
        for (const Ctl& c : ctls)
        {
            const float y = 215;
            const int a = Clone(p, FirstModeOptions, arrowsRef, c.X - 178, y - 269);
            SetStretch(p, a, c.X, (c.W + 30) / 63.0F);
            const int b = Clone(p, FirstModeOptions, boxRef, c.X + 15 - 194, y - 1 - 268);
            SetStretch(p, b, c.X + 15, c.W / 32.0F);
            AddText(p, c.Label, Orange, c.X - 3, y + 7, 1, FirstModeOptions, labelRef);
            const std::string var = c.Var;
            const auto name = c.Name;
            AddLive(p, [this, var, name] { return name(_vars[var]); }, White, c.X + 15 + c.W / 2, y + 7, 2, FirstModeOptions, labelRef);
            const auto count = c.Count;
            Hot(p, c.X, y - 2, c.X + 15, y + 16, [this, var, count] { _vars[var] = (_vars[var] - 1 + count()) % count(); });
            Hot(p, c.X + c.W + 15, y - 2, c.X + c.W + 30, y + 16, [this, var, count] { _vars[var] = (_vars[var] + 1) % count(); });
        }
        const struct { float Dx; int Align; const char* Name; } cols[] = {{10, 1, "#"}, {14, 0, "PLAYER"}, {80, 0, "HUNTER"}, {127, 0, "TEAM"}, {192, 1, "STATE"}};
        for (float x0 : {4.0F, 204.0F})
        {
            for (const auto& c : cols) AddText(p, c.Name, Orange, x0 + c.Dx, 237, c.Align, FirstModeOptions, labelRef);
        }
        const int line = CloneModel(p, ResultsPage, "teamdivide", 4, 242 - 303);
        SetStretch(p, line, 4, 392.0F / 256.0F);
        for (int slot = 0; slot < RoomSlots; ++slot)
        {
            const float x0 = slot < 12 ? 4.0F : 204.0F;
            const float cy = 246 + static_cast<float>(slot % 12) * 8 + 3;
            const auto row = [this, slot]() -> std::optional<PlayerRow>
            {
                const auto rows = OfflineRows();
                if (static_cast<std::size_t>(slot) < rows.size()) return rows[static_cast<std::size_t>(slot)];
                return std::nullopt;
            };
            AddText(p, std::to_string(slot + 1), Pale, x0 + 10, cy, 1, FirstModeOptions, labelRef);
            AddLive(p, [row] { const auto r = row(); return r ? r->Name : std::string("open"); }, White, x0 + 14, cy, 0, FirstModeOptions, labelRef,
                [row] { const auto r = row(); return !r ? Dim : r->You ? Orange : White; });
            AddLive(p, [row] { const auto r = row(); return r ? r->Hunter : std::string("-"); }, White, x0 + 80, cy, 0, FirstModeOptions, labelRef,
                [row] { const auto r = row(); if (!r) return Dim; for (int h = 0; h < 7; ++h) if (r->Hunter == HunterNames[h]) return HunterColours[h]; return White; });
            AddLive(p, [row] { const auto r = row(); return r ? r->Team : std::string("-"); }, White, x0 + 127, cy, 0, FirstModeOptions, labelRef,
                [row] { const auto r = row(); if (!r) return Dim; for (int t = 0; t < 4; ++t) if (r->Team == TeamNames[t]) return TeamColours[t]; return Dim; });
            AddLive(p, [row] { const auto r = row(); return r ? r->State : std::string("-"); }, Pale, x0 + 192, cy, 1, FirstModeOptions, labelRef,
                [row] { const auto r = row(); return !r ? Dim : r->State == "host" ? Orange : Pale; });
        }
        const int pill = CloneModel(p, SingleCardLobby, "startgame", 93 - 135, 0);
        (void)pill;
        AddText(p, "START", Pale, 141, 365, 2, SingleCardLobby, FindText(SingleCardLobby, "LOCK PLAYERS"));
        Hot(p, 93, 352, 205, 378, [this]
        {
            if (!_host) return;
            OfflineMatch m;
            m.Mode = _vars["mode"];
            m.Arena = _vars["arena"];
            m.Hunter = _vars["hunter"];
            m.Suit = _vars["suit"];
            m.Bots = _vars["bots"];
            m.Skill = _vars["skill"];
            m.Teams = _vars["teams"];
            _host->StartOffline(m);
        });
    }

    // A network room: what LobbyModel says, in the same columns.
    void Composer::BuildRoom()
    {
        const int p = RoomPage = NewPage(Family::Wide);
        TopLogo(p, [this] { return _host ? Lower(_host->LobbyStatus()) : std::string(" "); });
        TouchFrame(p, " ", 400);
        const int headRef = FindText(MultiplayerPage, "CHOOSE MULTIPLAYER TYPE");
        AddLive(p, [this] { return _host ? Upper(_host->LobbyTitle()) : std::string("LOBBY"); }, Heading, 200, 207, 2, MultiplayerPage, headRef);
        const int labelRef = FindText(FirstModeOptions, "ARENA");
        const int arrowsRef = FindModel(FirstModeOptions, "arrows_option");
        const int boxRef = FindModel(FirstModeOptions, "box_arrows");
        // your own hunter and suit, which a room lets you change
        const struct { const char* Var; const char* Label; float X; } mine[] = {{"hunter", "your hunter", 90}, {"suit", "suit", 260}};
        for (const auto& c : mine)
        {
            const float y = 215, w = 60;
            const int a = Clone(p, FirstModeOptions, arrowsRef, c.X - 178, y - 269);
            SetStretch(p, a, c.X, (w + 30) / 63.0F);
            const int b = Clone(p, FirstModeOptions, boxRef, c.X + 15 - 194, y - 1 - 268);
            SetStretch(p, b, c.X + 15, w / 32.0F);
            AddText(p, c.Label, Orange, c.X - 3, y + 7, 1, FirstModeOptions, labelRef);
            const std::string var = c.Var;
            AddLive(p, [this, var] { return var == "hunter" ? std::string(HunterNames[_vars["hunter"] % 7]) : std::to_string(_vars["suit"] + 1); },
                White, c.X + 15 + w / 2, y + 7, 2, FirstModeOptions, labelRef);
            const auto step = [this, var](int d)
            {
                const int n = var == "hunter" ? 7 : 4;
                _vars[var] = (_vars[var] + d + n) % n;
                if (!_host) return;
                if (var == "hunter") _host->LobbySetHunter(_vars["hunter"]);
                else _host->LobbySetSuit(_vars["suit"]);
            };
            Hot(p, c.X, y - 2, c.X + 15, y + 16, [step] { step(-1); });
            Hot(p, c.X + w + 15, y - 2, c.X + w + 30, y + 16, [step] { step(1); });
        }
        const struct { float Dx; int Align; const char* Name; } cols[] = {{10, 1, "#"}, {14, 0, "PLAYER"}, {80, 0, "HUNTER"}, {127, 0, "TEAM"}, {192, 1, "STATE"}};
        for (float x0 : {4.0F, 204.0F})
        {
            for (const auto& c : cols) AddText(p, c.Name, Orange, x0 + c.Dx, 237, c.Align, FirstModeOptions, labelRef);
        }
        const int line = CloneModel(p, ResultsPage, "teamdivide", 4, 242 - 303);
        SetStretch(p, line, 4, 392.0F / 256.0F);
        const int bar = Clone(p, CreateJoinPage, FindModel(CreateJoinPage, "redbar"), 0, 0);
        _rowBar[p] = bar;
        for (int slot = 0; slot < RoomSlots; ++slot)
        {
            const float x0 = slot < 12 ? 4.0F : 204.0F;
            const float y = 246 + static_cast<float>(slot % 12) * 8;
            const float cy = y + 3;
            const auto row = [this, slot]() -> std::optional<PlayerRow>
            {
                const auto rows = RoomRows();
                if (static_cast<std::size_t>(slot) < rows.size()) return rows[static_cast<std::size_t>(slot)];
                return std::nullopt;
            };
            AddText(p, std::to_string(slot + 1), Pale, x0 + 10, cy, 1, FirstModeOptions, labelRef);
            AddLive(p, [row] { const auto r = row(); return r ? r->Name : std::string("open"); }, White, x0 + 14, cy, 0, FirstModeOptions, labelRef,
                [row] { const auto r = row(); return !r ? Dim : r->You ? Orange : White; });
            AddLive(p, [row] { const auto r = row(); return r ? r->Hunter : std::string("-"); }, White, x0 + 80, cy, 0, FirstModeOptions, labelRef,
                [row] { const auto r = row(); if (!r) return Dim; for (int h = 0; h < 7; ++h) if (r->Hunter == HunterNames[h]) return HunterColours[h]; return White; });
            AddLive(p, [row] { const auto r = row(); return r ? r->Team : std::string("-"); }, White, x0 + 127, cy, 0, FirstModeOptions, labelRef,
                [row] { const auto r = row(); if (!r) return Dim; for (int t = 0; t < 4; ++t) if (r->Team == TeamNames[t]) return TeamColours[t]; return Dim; });
            AddLive(p, [row] { const auto r = row(); return r ? r->State : std::string("-"); }, Pale, x0 + 192, cy, 1, FirstModeOptions, labelRef,
                [row] { const auto r = row(); return !r ? Dim : r->State == "ready" ? Good : r->State == "host" ? Orange : Pale; });
            const int hot = Hot(p, x0, y - 1, x0 + 194, y + 7, [this, slot]
            {
                const auto rows = RoomRows();
                if (!_host || !_host->LobbyOwner() || static_cast<std::size_t>(slot) >= rows.size() || rows[static_cast<std::size_t>(slot)].You) return;
                _vars["target"] = slot;
                GoTo(ManagePage);
            });
            _rows[p].push_back({hot, {x0, y - 1}});
        }
        const int chat = CloneModel(p, WifiLobbyPage, "chat", -5, 0);
        (void)chat;
        AddText(p, "chat", Orange, 227, 380, 2, 38, FindText(38, "add bot"));
        Hot(p, 212, 349, 242, 379, [this]
        {
            OpenKeyboard("chat here", "", [this](const std::string& s) { if (_host && !s.empty()) _host->LobbyChat(s); });
        });
        CloneModel(p, SingleCardLobby, "startgame", 93 - 135, 0);
        AddLive(p, [this] { return _host && _host->LobbyOwner() ? std::string("START") : std::string("READY"); }, Pale, 141, 365, 2,
            SingleCardLobby, FindText(SingleCardLobby, "LOCK PLAYERS"));
        Hot(p, 93, 352, 205, 378, [this]
        {
            if (!_host) return;
            if (_host->LobbyOwner()) _host->LobbyStart();
            else _host->LobbyReady();
        });
    }

    // Master's owner actions, on the controls page's list buttons.
    void Composer::BuildManage()
    {
        const int p = ManagePage = NewPage(Family::Logo);
        TopLogo(p, [this]
        {
            const auto rows = RoomRows();
            const std::size_t t = static_cast<std::size_t>(_vars["target"]);
            return t < rows.size() ? rows[t].Name + "  -  " + rows[t].Hunter + "  -  team " + rows[t].Team : std::string(" ");
        });
        TouchFrame(p, "MANAGE PLAYER");
        std::vector<std::pair<std::function<std::string()>, std::function<void()>>> entries;
        const char* names[4] = {"MOVE TO RED", "MOVE TO BLUE", "TRANSFER HOST", "KICK"};
        for (int a = 0; a < 4; ++a)
        {
            const std::string n = names[a];
            entries.emplace_back([n] { return n; }, [this, a]
            {
                if (_host) _host->LobbyAdmin(_vars["target"], a);
                Back();
            });
        }
        ListButtons(p, entries, 236);
    }

    // The pause menu over the match: Second Hunt builds its own from the
    // controls page's list buttons, and so does this.
    void Composer::BuildPause()
    {
        const int p = PausePage = NewPage(Family::Overlay);
        TouchFrame(p, "PAUSED");
        const auto entries = [this]
        {
            std::vector<std::pair<std::string, std::function<void()>>> e;
            const PauseInfo info = _host ? _host->Pause() : PauseInfo{};
            e.emplace_back("RESUME", [this] { if (_host) _host->Resume(); });
            if (info.Vote)
            {
                e.emplace_back("ACCEPT VOTE", [this] { if (_host) _host->AnswerVote(true); });
                e.emplace_back("DENY VOTE", [this] { if (_host) _host->AnswerVote(false); });
            }
            if (info.Spectating) e.emplace_back("REJOIN MATCH", [this] { if (_host) _host->Rejoin(); });
            else if (info.CanSpectate) e.emplace_back("SPECTATE", [this] { if (_host) _host->Spectate(); });
            e.emplace_back("OPTIONS", [this] { _settingsReturn = PausePage; _section = "display"; GoTo(SettingsPage); });
            e.emplace_back("LEAVE MATCH", [this] { if (_host) _host->LeaveMatch(); });
            e.emplace_back("QUIT", [this] { if (_host) _host->QuitFromMatch(); });
            return e;
        };
        std::vector<std::pair<std::function<std::string()>, std::function<void()>>> slots;
        std::vector<int> boxes;
        for (int k = 0; k < 8; ++k)
        {
            slots.emplace_back([entries, k]
            {
                const auto e = entries();
                return static_cast<std::size_t>(k) < e.size() ? e[static_cast<std::size_t>(k)].first : std::string(" ");
            }, [entries, k]
            {
                const auto e = entries();
                if (static_cast<std::size_t>(k) < e.size()) e[static_cast<std::size_t>(k)].second();
            });
        }
        ListButtons(p, slots, 222, &boxes);
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            const int k = static_cast<int>(i / 3);
            ShowWhen(p, boxes[i], [entries, k] { return static_cast<std::size_t>(k) < entries().size(); });
        }
    }

    // The results panel over the engine's own scoreboard: the arena vote
    // (page 55's arena box), the hunter and suit to come back as, READY.
    void Composer::BuildEnd()
    {
        const int p = EndPage = NewPage(Family::Overlay);
        TouchFrame(p, "RESULTS");
        const int labelRef = FindText(FirstModeOptions, "ARENA");
        const int arrowsRef = FindModel(FirstModeOptions, "arrows_type");
        const int boxRef = FindModel(FirstModeOptions, "box_type");
        AddText(p, "VOTE FOR AN ARENA", Pale, 128, 222, 2, FirstModeOptions, labelRef);
        Clone(p, FirstModeOptions, arrowsRef, 0, 232 - 236);
        Clone(p, FirstModeOptions, boxRef, 0, 232 - 235);
        AddLive(p, [this]
        {
            const EndInfo e = _host ? _host->End() : EndInfo{};
            if (e.Arenas.empty()) return std::string("the rotation decides");
            const std::size_t i = static_cast<std::size_t>(_vars["vote"]) % e.Arenas.size();
            return Upper(e.Arenas[i]);
        }, White, 166, 239, 2, FirstModeOptions, labelRef);
        AddLive(p, [this]
        {
            const EndInfo e = _host ? _host->End() : EndInfo{};
            if (e.Arenas.empty()) return std::string(" ");
            const std::size_t i = static_cast<std::size_t>(_vars["vote"]) % e.Arenas.size();
            const int votes = i < e.Votes.size() ? e.Votes[i] : 0;
            return std::to_string(votes) + (votes == 1 ? " vote" : " votes") + (static_cast<int>(i) == e.Mine ? "  -  yours" : "  -  tap to vote");
        }, Pale, 166, 254, 2, FirstModeOptions, labelRef);
        const auto arenaStep = [this](int d)
        {
            const EndInfo e = _host ? _host->End() : EndInfo{};
            const int n = std::max<int>(1, static_cast<int>(e.Arenas.size()));
            _vars["vote"] = (_vars["vote"] + d + n) % n;
        };
        Hot(p, 91, 230, 106, 248, [arenaStep] { arenaStep(-1); });
        Hot(p, 226, 230, 241, 248, [arenaStep] { arenaStep(1); });
        Hot(p, 106, 231, 226, 247, [this] { if (_host) _host->EndVote(_vars["vote"]); });
        const struct { const char* Var; const char* Label; float Y; } rows[] = {{"hunter", "hunter", 272}, {"suit", "suit", 292}};
        for (const auto& r : rows)
        {
            Clone(p, FirstModeOptions, arrowsRef, 0, r.Y - 236);
            Clone(p, FirstModeOptions, boxRef, 0, r.Y - 235);
            AddText(p, r.Label, Orange, 86, r.Y + 7, 1, FirstModeOptions, labelRef);
            const std::string var = r.Var;
            AddLive(p, [this, var] { return var == "hunter" ? std::string(HunterNames[_vars["hunter"] % 7]) : std::to_string(_vars["suit"] + 1); },
                White, 166, r.Y + 7, 2, FirstModeOptions, labelRef);
            const auto step = [this, var](int d)
            {
                const int n = var == "hunter" ? 7 : 4;
                _vars[var] = (_vars[var] + d + n) % n;
                if (_host) _host->EndPick(_vars["hunter"], _vars["suit"]);
            };
            Hot(p, 91, r.Y - 2, 106, r.Y + 16, [step] { step(-1); });
            Hot(p, 226, r.Y - 2, 241, r.Y + 16, [step] { step(1); });
            Hot(p, 106, r.Y - 1, 226, r.Y + 15, [step] { step(1); });
        }
        AddLive(p, [this] { const EndInfo e = _host ? _host->End() : EndInfo{}; return e.ReadyLine.empty() ? std::string(" ") : e.ReadyLine; },
            Pale, 128, 318, 2, FirstModeOptions, labelRef);
        CloneModel(p, SingleCardLobby, "startgame", 40 - 135, 0);
        AddLive(p, [this] { const EndInfo e = _host ? _host->End() : EndInfo{}; return e.Ready ? std::string("READY!") : std::string("READY"); },
            Pale, 88, 365, 2, SingleCardLobby, FindText(SingleCardLobby, "LOCK PLAYERS"));
        Hot(p, 40, 352, 152, 378, [this] { if (_host) _host->EndReady(); });
    }

    // After the game's own credits: who made this one.
    void Composer::BuildCredits()
    {
        const int p = CreditsPage = NewPage(Family::Logo);
        TopLogo(p, [] { return std::string("fruity prime, built on mphread."); });
        TouchFrame(p, "FRUITY PRIME");
        const int labelRef = FindText(FirstModeOptions, "ARENA");
        const char* lines[][2] = {
            {"LIVETEK", "this fork: multiplayer, the server, the launcher"},
            {"NONEGIVEN", "mphread: the renderer, the formats, the entities"},
            {"BOUNTYHUNTERKANDEN", "second hunt: the classic menus (mit)"},
            {"MSTAN", "the recomp the menus are checked against"},
            {"ZECTION", "the c++ port"}};
        for (std::size_t k = 0; k < 5; ++k)
        {
            const float y = 228 + static_cast<float>(k) * 22;
            AddText(p, lines[k][0], Orange, 128, y, 2, FirstModeOptions, labelRef);
            AddText(p, lines[k][1], White, 128, y + 9, 2, FirstModeOptions, labelRef);
        }
        OkButton(p, [this] { GoTo(OptionsPage); });
    }

    void Composer::PatchRom()
    {
        // ---- the options: DISPLAY and PROFILE where the rumble pak and the
        // wi-fi stats were; AUDIO and CONTROLS open the settings
        for (const char* t : {"show my stats on nintendowifi.com", "rumble\npak", "YES", "OFF"}) Remove(OptionsPage, FindText(OptionsPage, t));
        {
            const MenuPage& rom = _rom[OptionsPage];
            for (std::size_t i = 0; i < rom.Items.size(); ++i)
            {
                for (const MenuItemState& s : rom.Items[i].States)
                {
                    if (s.WidgetIndex >= 0 && BaseName(_file.Widgets.at(static_cast<std::size_t>(s.WidgetIndex)).ModelPath) == "box_arrows")
                    {
                        Remove(OptionsPage, static_cast<int>(i));
                    }
                }
            }
            const auto open = [this](const char* section) { return [this, section] { _section = section; _settingsReturn = OptionsPage; _settingsPage = 0; GoTo(SettingsPage); }; };
            std::vector<std::pair<std::function<std::string()>, std::function<void()>>> e = {
                {[] { return std::string("DISPLAY"); }, open("display")}, {[] { return std::string("PROFILE"); }, open("profile")}};
            const int off = FindModel(23, "selectaoff");
            const int ref = FindText(23, "stylus mode\nright");
            for (std::size_t k = 0; k < e.size(); ++k)
            {
                const float dx = 132, dy = 62 + static_cast<float>(k) * 30;
                Clone(OptionsPage, 23, off, dx, dy);
                AddLive(OptionsPage, e[k].first, ListText, 62 + dx, 237 + dy, 2, 23, ref);
                const int hot = Hot(OptionsPage, 8 + dx, 224 + dy, 116 + dx, 250 + dy, e[k].second);
                _rows[OptionsPage].push_back({hot, {8 + dx, 224 + dy}});
            }
            Hot(OptionsPage, 4, 42 + 192 - 2, 68, 74 + 192, open("audio"));      // over the AUDIO icon
            Hot(OptionsPage, 73, 42 + 192 - 2, 137, 74 + 192, open("keyboard")); // over CONTROLS
        }
        // ---- the movies are the clips
        Retext(MoviesPage, FindText(MoviesPage, "select the movie you want to see."), "select the clip you want to watch.");
        // ---- the main menu gets QUIT, in the results' orange bar
        {
            const int bar = CloneModel(MainMenuPage, ResultsPage, "quit", 186 - 82, 352 - 363);
            (void)bar;
            AddText(MainMenuPage, "QUIT", Black, 215, 361, 2, ResultsPage, FindText(ResultsPage, "QUIT"));
            Hot(MainMenuPage, 186, 352, 244, 370, [this] { if (_host) _host->Quit(); });
        }
        // ---- the adventure slots: "continue" where a file is in use
        {
            int slot = 0;
            const MenuPage& rom = _rom[FilePage];
            std::vector<std::pair<float, int>> labels;
            for (std::size_t i = 0; i < rom.Items.size(); ++i)
            {
                for (const MenuItemState& s : rom.Items[i].States)
                {
                    if (s.Text.has_value() && _strings[s.Text->StringId] == "create new game") labels.emplace_back(rom.Items[i].X, static_cast<int>(i));
                }
            }
            std::sort(labels.begin(), labels.end());
            labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
            for (const auto& [x, item] : labels)
            {
                const int id = _strings.Add("create new game");
                for (MenuItemState& s : _file.Pages[FilePage].Items[static_cast<std::size_t>(item)].States)
                {
                    if (s.Text.has_value()) s.Text->StringId = id;
                }
                _texts.push_back(Text{FilePage, item, id, [this, slot] { return _host && _host->SlotUsed(slot) ? std::string("continue") : std::string("create new game"); }, {}});
                ++slot;
            }
        }
        // ---- the hunter select: the suit under the hunters, and a tick to go on
        for (int pg : {HunterPage, HunterJoinPage})
        {
            const int labelRef = FindText(FirstModeOptions, "ARENA");
            const int a = Clone(pg, FirstModeOptions, FindModel(FirstModeOptions, "arrows_option"), -40, 352 - 269);
            const int b = Clone(pg, FirstModeOptions, FindModel(FirstModeOptions, "box_arrows"), -40, 352 - 269);
            (void)a; (void)b;
            AddText(pg, "suit", Orange, 132, 359, 1, FirstModeOptions, labelRef);
            AddLive(pg, [this] { return std::to_string(_vars["suit"] + 1); }, White, 170, 359, 2, FirstModeOptions, labelRef);
            Hot(pg, 138, 350, 153, 368, [this] { _vars["suit"] = (_vars["suit"] + 3) % 4; });
            Hot(pg, 186, 350, 201, 368, [this] { _vars["suit"] = (_vars["suit"] + 1) % 4; });
            OkButton(pg, [this, pg]
            {
                const int flow = _vars["flow"];
                if (flow == 3 || pg == HunterJoinPage)
                {
                    if (_host) _host->Join(_vars["join"], _vars["hunter"], _vars["suit"]);
                    // back to the list: what the connection says goes on its band,
                    // and the room opens over it when the server takes us
                    _vars["joining"] = 1;
                    GoTo(ServersPage);
                    return;
                }
                GoTo(LobbyPage);
            });
        }
        // ---- the wi-fi page is the online one
        Retext(WifiPage, FindText(WifiPage, "find game"), "servers");
        Retext(WifiPage, FindText(WifiPage, "friends and rivals"), "create\nserver");
        Retext(WifiPage, FindText(WifiPage, "configure\nwi-fi"), "configure\nonline");
        Retext(WifiPage, FindText(WifiPage, "nintendo wfc will find opponents who want to play the same game as you."),
            "every fruity prime server, with its arena, mode, players and ping.");
        Retext(WifiPage, FindText(WifiPage, "organize a wi-fi battle with your friends and rivals."),
            "run a server on this machine or on the directory's.");
        Remove(WifiPage, FindModel(WifiPage, "friendconfig"));
        Remove(WifiPage, FindText(WifiPage, "edit\nfriends\nand rivals"));
        // ---- the create-or-join page: join opens the server list
        {
            MenuPage& page = _file.Pages[CreateJoinPage];
            for (MenuItem& it : page.Items)
            {
                for (MenuAction& a : it.Actions)
                {
                    for (const auto& [x, call] : a.Calls)
                    {
                        if ((call >= 94 && call <= 98)) a.TargetPage = 0xFF;
                    }
                }
            }
        }
        // ---- the file select starts the adventure itself
        {
            MenuPage& page = _file.Pages[FilePage];
            for (MenuItem& it : page.Items)
            {
                for (MenuAction& a : it.Actions)
                {
                    for (const auto& [x, call] : a.Calls)
                    {
                        if (call == 77 || call == 79 || call == 81) a.TargetPage = 0xFF;
                    }
                }
            }
        }
        // ---- the mode pages: their values are this session's
        for (int m = 0; m < 7; ++m)
        {
            const int pg = ModePages[m];
            // the arena's arrows show only once the row has the D-pad's
            // focus; a touch goes straight to them
            const auto arena = [this](int d)
            {
                const int n = _host ? std::max<int>(1, static_cast<int>(_host->Arenas().size())) : 1;
                _vars["arena"] = (_vars["arena"] + d + n) % n;
            };
            Hot(pg, 86, 232, 108, 256, [arena] { arena(-1); });
            Hot(pg, 224, 232, 246, 256, [arena] { arena(1); });
            Hot(pg, 108, 234, 224, 254, [arena] { arena(1); });
            MenuPage& page = _file.Pages[static_cast<std::size_t>(pg)];
            std::vector<std::pair<float, int>> values;
            for (std::size_t i = 0; i < page.Items.size(); ++i)
            {
                const MenuItem& it = page.Items[i];
                for (const MenuItemState& s : it.States)
                {
                    // the rows' values: right of the arrows, under the arena
                    if (s.Text.has_value() && it.X > 200 && it.X < 240 && it.Y > 40 && it.Y < 120)
                    {
                        values.emplace_back(-it.Y, static_cast<int>(i));
                        break;
                    }
                }
            }
            std::sort(values.begin(), values.end());
            for (std::size_t k = 0; k < values.size() && k < ModeOptions[m].size(); ++k)
            {
                const int item = values[k].second;
                const int id = _strings.Add("-");
                for (MenuItemState& s : page.Items[static_cast<std::size_t>(item)].States)
                {
                    if (s.Text.has_value()) s.Text->StringId = id;
                }
                const std::string var = "opt" + std::to_string(m) + "_" + std::to_string(k);
                const auto options = ModeOptions[m][k];
                _vars[var] = static_cast<int>(options.size()) / 2;
                _texts.push_back(Text{pg, item, id, [this, var, options] { return Upper(options[static_cast<std::size_t>(_vars[var]) % options.size()]); }, {}});
            }
        }
    }

    // The multiplayer type: the DS's three are offline (single-card), LAN
    // (multi-card) and online (wi-fi).
    void Composer::BuildMultiplayer()
    {
        Retext(MultiplayerPage, FindText(MultiplayerPage, "single-card play"), "offline\nbots");
        Retext(MultiplayerPage, FindText(MultiplayerPage, "multi-card play"), "lan");
        Retext(MultiplayerPage, FindText(MultiplayerPage, "nintendo wi-fi connection"), "online");
        Retext(MultiplayerPage, FindText(MultiplayerPage, "allow up to 3 other players to download and play battle mode."),
            "play against up to 16 bots on this machine, in every mode and arena.");
        Retext(MultiplayerPage, FindText(MultiplayerPage, "up to 4 players or bots can hunt each other in 7 unique game modes."),
            "create or join a game on this network.");
        Retext(MultiplayerPage, FindText(MultiplayerPage, "hunt anyone in the world with nintendo wi-fi connection."),
            "join a server anywhere, or create your own.");
        // the keyboard's characters, by the calls its keys make
        const std::string rows = "1234567890-=qwertyuiopasdfghjklzxcvbnm,./;'[]";
        for (std::size_t i = 0; i < rows.size(); ++i) _keyChars[297 + static_cast<int>(i)] = rows[i];
        _keyChars[346] = ' ';
    }

    // ---------------------------------------------------------------- running

    [[nodiscard]] std::string Composer::ModeName(int mode) const
    {
        return ModeNames[(mode % 7 + 7) % 7];
    }

    std::vector<PlayerRow> Composer::OfflineRows() const
    {
        std::vector<PlayerRow> rows;
        const auto var = [this](const char* k) { const auto f = _vars.find(k); return f == _vars.end() ? 0 : f->second; };
        const int teams = var("teams");
        const auto team = [teams](int slot) { return teams == 0 ? std::string("-") : std::string(TeamNames[slot % (teams == 1 ? 2 : 4)]); };
        PlayerRow you;
        you.Name = _host ? Upper(_host->PlayerName()) : "YOU";
        you.Hunter = HunterNames[var("hunter") % 7];
        you.Team = team(0);
        you.State = "host";
        you.You = true;
        rows.push_back(you);
        for (int b = 0; b < var("bots"); ++b)
        {
            PlayerRow bot;
            bot.Name = std::string(BotNames[b % 7]) + (b < 7 ? "" : std::to_string(b / 7 + 1));
            bot.Hunter = HunterNames[b % 7];
            bot.Team = team(b + 1);
            bot.State = Skills[var("skill") % 4];
            bot.Bot = true;
            rows.push_back(bot);
        }
        return rows;
    }

    std::vector<PlayerRow> Composer::RoomRows() const
    {
        return _host ? _host->LobbyPlayers() : std::vector<PlayerRow>{};
    }

    void Composer::Say(const std::string& text)
    {
        if (_host) _host->Say(text);
    }

    void Composer::GoTo(int page)
    {
        if (_engine == nullptr || _engine->Page() == nullptr) return;
        _history.push_back(_engine->Page()->Index);
        _engine->GoTo(page);
    }

    void Composer::Back()
    {
        if (_engine == nullptr || _engine->Page() == nullptr) return;
        const int here = _engine->Page()->Index;
        if (here == SettingsPage && _host) _host->SaveSettings();
        if (here == RoomPage && _host) { _host->LobbyLeave(); _history.clear(); _engine->GoTo(MultiplayerPage); return; }
        if (here == SettingsPage && _settingsReturn >= 0) { const int to = _settingsReturn; _settingsReturn = -1; _engine->GoTo(to); return; }
        if (here == ServersPage && _host) _host->StopServers();
        if (_history.empty()) { _engine->GoTo(MainMenuPage); return; }
        const int to = _history.back();
        _history.pop_back();
        _engine->GoTo(to);
    }

    void Composer::OpenKeyboard(const std::string& title, const std::string& value, std::function<void(const std::string&)> done)
    {
        _keyboardTitle = title;
        _keyboardText = value;
        _keyboardDone = std::move(done);
        _keyboardReturn = _engine && _engine->Page() ? _engine->Page()->Index : MainMenuPage;
        if (_engine) _engine->GoTo(KeyboardPage);
    }

    bool Composer::Type(const std::string& text, bool backspace, bool enter)
    {
        if (_engine == nullptr || _engine->Page() == nullptr) return false;
        const int here = _engine->Page()->Index;
        if (here < KeyboardPage || here > KeyboardPage + 2) return false;
        if (backspace && !_keyboardText.empty()) _keyboardText.pop_back();
        if (!text.empty() && _keyboardText.size() < 24) _keyboardText += text;
        if (enter)
        {
            if (_keyboardDone) _keyboardDone(_keyboardText);
            _engine->GoTo(_keyboardReturn >= 0 ? _keyboardReturn : MainMenuPage);
        }
        return true;
    }

    bool Composer::Call(int b)
    {
        if (b >= CallBase && b - CallBase < static_cast<int>(_calls.size()))
        {
            _calls[static_cast<std::size_t>(b - CallBase)]();
            return true;
        }
        switch (b)
        {
        case 90: _vars["flow"] = 0; GoTo(ModePage); return true;            // offline: bots on this machine
        case 91: _vars["flow"] = 1; GoTo(CreateJoinPage); return true;      // LAN: create or join
        case 92: GoTo(WifiPage); return true;                               // online
        case 93: Say("The rival radar needs two DS systems."); return true;
        case 94: _vars["flow"] = 1; GoTo(CreatePage); return true;           // LAN: create
        case 95: case 96: case 97: case 98: GoTo(ServersPage); return true;  // LAN: join
        case 207: GoTo(ServersPage); return true;
        case 208: _vars["flow"] = 2; GoTo(CreatePage); return true;
        case 209: _section = "online"; _settingsReturn = WifiPage; _settingsPage = 0; GoTo(SettingsPage); return true;
        case 45: Say("Fruity Prime keeps no DS save data to erase."); return true;
        case 77: case 79: case 81:
        {
            const int slot = (b - 77) / 2;
            if (_host) _host->StartAdventure(slot, !_host->SlotUsed(slot));
            return true;
        }
        case 113: case 114:
        {
            // the mode pages' arena box: the right arrow is the one further right
            const int n = _host ? std::max<int>(1, static_cast<int>(_host->Arenas().size())) : 1;
            _vars["arena"] = (_vars["arena"] + (b == 113 ? 1 : -1) + n) % n;
            return true;
        }
        case 107: case 108: case 109: case 110: case 111: case 112:
        {
            if (_engine == nullptr || _engine->Page() == nullptr) return true;
            const int pg = _engine->Page()->Index;
            for (int m = 0; m < 7; ++m)
            {
                if (ModePages[m] != pg) continue;
                const int row = (b - 107) / 2;
                const std::string var = "opt" + std::to_string(m) + "_" + std::to_string(row);
                if (static_cast<std::size_t>(row) >= ModeOptions[m].size()) break;
                const int n = static_cast<int>(ModeOptions[m][static_cast<std::size_t>(row)].size());
                _vars[var] = (_vars[var] + ((b - 107) % 2 == 0 ? 1 : -1) + n) % n;
                _vars["mode"] = m;
            }
            return true;
        }
        case 5: Type("", false, false); if (_engine) _engine->GoTo(_keyboardReturn >= 0 ? _keyboardReturn : MainMenuPage); return true;
        case 6: case 344: Type("", false, true); return true;
        case 343: Type("", true, false); return true;
        case 342: case 345: _caps = !_caps; return true;
        case 146: return true;
        default: break;
        }
        if (b >= 132 && b <= 145)
        {
            _vars["hunter"] = (b - 132) % 7;
            return true;
        }
        if (b >= 297 && b <= 346)
        {
            const auto found = _keyChars.find(b);
            if (found != _keyChars.end())
            {
                std::string c(1, found->second);
                if (_caps) c = Upper(c);
                Type(c, false, false);
            }
            return true;
        }
        if (b >= 18 && b <= 32 && _engine && _engine->Page() && _engine->Page()->Index == MoviesPage)
        {
            if (_host) _host->WatchClip(b - 18);
            return true;
        }
        return false;
    }

    void Composer::PageEntered(int page)
    {
        for (Shown& s : _shown) if (s.Page == page) s.Last = -1;
        if (page == ServersPage)
        {
            // a join under way keeps the list (and the note saying how it went)
            if (_vars["joining"] == 0)
            {
                _vars["page"] = 0;
                if (_host) _host->RefreshServers();
            }
            _vars["joining"] = 0;
        }
        if (page >= FirstModeOptions && page <= FirstModeOptions + 6)
        {
            for (int m = 0; m < 7; ++m) if (ModePages[m] == page) _vars["mode"] = m;
        }
        if (page == KeyboardPage)
        {
            // the DS's hints for its own uses of the keyboard: not ours
            for (const char* t : {"enter a message to be seen by all your friends in this game.", "enter a temporary name for this friend.",
                "enter the new name you wish to use for\n\nMETROID PRIME HUNTERS."})
            {
                const int item = FindText(KeyboardPage, t);
                if (item >= 0 && _engine) _engine->SetState(item, MenuState::Hidden);
            }
        }
    }

    void Composer::Show(const std::string& what)
    {
        if (_engine == nullptr) return;
        if (what == "room") { _history.clear(); _engine->Enter(RoomPage); }
        else if (what == "pause") { _history.clear(); _engine->Enter(PausePage); }
        else if (what == "end") { _history.clear(); _vars["vote"] = 0; _engine->Enter(EndPage); }
        else if (what == "front") { _history.clear(); _engine->Enter(MainMenuPage); }
    }

    void Composer::Refresh()
    {
        if (_engine == nullptr || _engine->Page() == nullptr) return;
        const int page = _engine->Page()->Index;
        _listening = page == SettingsPage && _host != nullptr && _host->Listening();
        for (const Text& t : _texts)
        {
            if (t.Page != page || !t.Words) continue;
            std::string w = t.Words();
            if (w.empty()) w = " ";
            if (_strings[t.StringId] != w) _strings.Fill(t.StringId, w);
            if (t.Colour)
            {
                Recolour(_file.Pages[static_cast<std::size_t>(t.Page)].Items[static_cast<std::size_t>(t.Item)], t.Colour());
            }
        }
        // the keyboard's two lines: what it is for, what is typed
        if (page >= KeyboardPage && page <= KeyboardPage + 2)
        {
            static const int title = _strings.IndexOf("chat here");
            if (title >= 0) _strings.Fill(title, _keyboardTitle);
            static const int typed = _strings.IndexOf("tempname here");
            if (typed >= 0) _strings.Fill(typed, _keyboardText + "_");
            static const int typedNew = _strings.IndexOf("newname here");
            if (typedNew >= 0) _strings.Fill(typedNew, _keyboardText + "_");
        }
        // the match panels on the top screen: what is being put together
        FillPanels();
        // the arena on the mode pages and on the match panels
        if (_host)
        {
            const auto arenas = _host->Arenas();
            if (!arenas.empty())
            {
                const std::size_t a = static_cast<std::size_t>(_vars["arena"]) % arenas.size();
                static const int name = _strings.IndexOf("arena name");
                static const int count = _strings.IndexOf("1/20");
                if (name >= 0) _strings.Fill(name, Upper(arenas[a]));
                if (count >= 0) _strings.Fill(count, std::to_string(a + 1) + "/" + std::to_string(arenas.size()));
            }
        }
        for (Shown& s : _shown)
        {
            if (s.Page != page) continue;
            const int want = s.When() ? 1 : 0;
            if (want == s.Last) continue;
            if (want == 0) _engine->SetState(s.Item, MenuState::Hidden);
            else if (s.Last == 0) _engine->SetState(s.Item, MenuState::Idle);
            s.Last = want;
        }
    }

    // The top screen's match panels hold the ROM's placeholders ("setting
    // a", "client a"): the game writes the match over them, and so does this.
    void Composer::FillPanels()
    {
        static const char* const labels[7][4] = {
            {"point goal", "time limit", "team play", ""}, {"lives", "time limit", "team play", ""},
            {"octolith goal", "time limit", "auto reset", "team play"}, {"time goal", "time limit", "team play", ""},
            {"time goal", "time limit", "", ""}, {"octolith goal", "time limit", "auto reset", ""}, {"point goal", "time limit", "team play", ""}};
        if (_panelIds.empty())
        {
            const auto all = [this](const std::string& words)
            {
                std::vector<int> ids;
                for (int i = 0; i < _strings.Count(); ++i) if (_strings[i] == words) ids.push_back(i);
                return ids;
            };
            for (const char* k : {"setting a", "setting b", "setting c", "setting d", "10", "20", "30", "40",
                "adv setting a", "adv setting b", "adv setting c", "adv setting d", "map size: s m l", "client a", "client b"})
            {
                const auto ids = all(k);
                _panelIds[k] = ids.empty() ? -1 : ids.front();
            }
            const auto modes = all("GAME MODE");
            _panelIds["mode"] = modes.size() > 1 ? modes[1] : -1;
            const auto ws = all("WWWWWWWWWW");
            _panelIds["w1"] = ws.size() > 0 ? ws[0] : -1;
            _panelIds["w2"] = ws.size() > 1 ? ws[1] : -1;
        }
        const auto fill = [this](const char* key, const std::string& words)
        {
            const int id = _panelIds[key];
            if (id >= 0 && _strings[id] != words) _strings.Fill(id, words.empty() ? " " : words);
        };
        const int m = (_vars["mode"] % 7 + 7) % 7;
        fill("mode", Upper(ModeName(m)));
        const char* names[4] = {"setting a", "setting b", "setting c", "setting d"};
        const char* values[4] = {"10", "20", "30", "40"};
        for (int r = 0; r < 4; ++r)
        {
            const bool has = static_cast<std::size_t>(r) < ModeOptions[m].size();
            fill(names[r], labels[m][r]);
            const std::string var = "opt" + std::to_string(m) + "_" + std::to_string(r);
            fill(values[r], has ? Upper(ModeOptions[m][static_cast<std::size_t>(r)][static_cast<std::size_t>(_vars[var]) % ModeOptions[m][static_cast<std::size_t>(r)].size()]) : "");
        }
        fill("adv setting a", "affinity off");
        fill("adv setting b", "damage medium");
        fill("adv setting c", "friendly fire off");
        fill("adv setting d", "radar on");
        fill("map size: s m l", "");
        const auto rows = OfflineRows();
        const auto at = [&rows](std::size_t i) { return i < rows.size() ? rows[i].Name : std::string(); };
        fill("w1", at(0));
        fill("client a", at(1));
        fill("client b", at(2));
        fill("w2", at(3));
    }

    void Composer::Hover(int item)
    {
        if (_engine == nullptr || _engine->Page() == nullptr) return;
        const int page = _engine->Page()->Index;
        const auto bar = _rowBar.find(page);
        const auto rows = _rows.find(page);
        if (bar == _rowBar.end() || rows == _rows.end() || bar->second < 0) return;
        MenuItem& it = _file.Pages[static_cast<std::size_t>(page)].Items[static_cast<std::size_t>(bar->second)];
        const MenuItem& rom = _rom[CreateJoinPage].Items[static_cast<std::size_t>(FindModel(CreateJoinPage, "redbar"))];
        for (const auto& [hot, at] : rows->second)
        {
            if (hot != item) continue;
            // the bar is 153..243 x 242..256 where the ROM has it: put it on the row
            it.X = rom.X + (at.first - 153);
            it.Y = rom.Y - (at.second - 242);
            SetStretch(page, bar->second, at.first, (page == RoomPage ? 194.0F : 392.0F) / 90.0F);
            if (_engine->ItemState(bar->second) != MenuState::Focused) _engine->SetState(bar->second, MenuState::Focused);
            return;
        }
        if (_engine->ItemState(bar->second) == MenuState::Focused) _engine->SetState(bar->second, MenuState::Idle);
    }

    Family Composer::FamilyOf(int page) const
    {
        const auto f = _family.find(page);
        return f == _family.end() ? Family::Split : f->second;
    }

    const Stretch* Composer::StretchOf(int page, int item) const
    {
        const auto f = _stretch.find({page, item});
        return f == _stretch.end() ? nullptr : &f->second;
    }

    bool Composer::OverGame(int page) const
    {
        return page == PausePage || page == EndPage;
    }
}
