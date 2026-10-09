#pragma once

// The pages the DS menus get so they can stand in for Fruity Prime's own
// launcher on one screen: the server browser, creating a server, the room
// (eight players and up to sixteen bots), the settings, the pause menu and
// the results panel, plus the ROM pages that change hands (the multiplayer
// type is offline / LAN / online, the options gain DISPLAY and PROFILE).
//
// Every piece is the ROM's: an item cloned from a page that has it (found by
// its model or its words, never its number -- the item lists differ between
// releases) or a text item in the ROM's font and colours. A clone keeps the
// animations it has in the game, so it comes in and goes out the way the
// item it was copied from does. What a page shows is read from the Host on
// every tick; what a touch does is a numbered call this class answers.

#include "Host.hpp"
#include "MenuData.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::ClassicMenu
{
    // How a page's items share the one screen (OneScreen.cpp).
    enum class Family : std::uint8_t
    {
        Stacked,  // logos, credits: the two screens one over the other
        Title,    // the logo over TOUCH TO START
        Logo,     // logo, the touch screen, the description band, the footer in the corners
        Split,    // the top screen a panel on the left, the touch screen on the right
        Wide,     // lists that grow: the touch screen widened to 400 pixels
        Keys,     // the keyboard: the touch screen alone, larger
        Overlay   // over a match: the touch screen alone, the game behind
    };

    // An item drawn wider than the ROM made it, about Pivot (menu-space x).
    struct Stretch final
    {
        float Pivot = 0;
        float Scale = 1;
    };

    class Composer final
    {
    public:
        Composer(MenuFile& file, MenuStrings& strings, const MenuFont& font);

        // Add the pages and change the ROM's. Before the first Enter: the
        // page and item lists must not move under a running engine.
        void Build();
        void Attach(MenuEngine* engine) { _engine = engine; }
        void SetHost(Host* host) { _host = host; }

        [[nodiscard]] Family FamilyOf(int page) const;
        [[nodiscard]] const Stretch* StretchOf(int page, int item) const;
        // Drawn over a running match: no backdrop of its own.
        [[nodiscard]] bool OverGame(int page) const;

        // Calls from the ROM's own items (their numbers) and from the pieces
        // added here; true when handled.
        bool Call(int b);
        void PageEntered(int page);
        // Once a tick: the words on the page, what is shown and hidden.
        void Refresh();
        // The pointer is over this item (or none, -1): a row's red bar.
        void Hover(int item);
        // A key from a real keyboard, while the DS keyboard is up.
        bool Type(const std::string& text, bool backspace, bool enter);
        // Something outside the menus moved on: the room opened, a match paused.
        void Show(const std::string& what);
        [[nodiscard]] bool Listening() const { return _listening; }
        // A page this class made (B goes back through Back(), not the ROM's data).
        [[nodiscard]] bool Composed(int page) const { return page >= static_cast<int>(_rom.size()); }
        void Back();
        [[nodiscard]] bool HasRowBar(int page, int item) const
        {
            const auto rows = _rows.find(page);
            if (rows == _rows.end()) return false;
            for (const auto& r : rows->second) if (r.first == item) return true;
            return false;
        }

        // Page numbers the session needs.
        int ServersPage = -1, CreatePage = -1, LobbyPage = -1, RoomPage = -1, ManagePage = -1,
            PausePage = -1, EndPage = -1, SettingsPage = -1, CreditsPage = -1;

    private:
        struct Text final
        {
            int Page = -1;
            int Item = -1;
            int StringId = -1;
            std::function<std::string()> Words;
            std::function<std::uint32_t()> Colour;  // 0: keep
        };
        struct Shown final
        {
            int Page = -1;
            int Item = -1;
            std::function<bool()> When;
            int Last = -1;
        };

        // ---- finding the ROM's pieces ----
        [[nodiscard]] int FindModel(int page, const std::string& name) const;
        [[nodiscard]] int FindText(int page, const std::string& words) const;
        [[nodiscard]] float RestTop(int page, int item) const;

        // ---- building ----
        int NewPage(Family family);
        int Clone(int dst, int srcPage, int srcItem, float dx, float dy);
        int CloneModel(int dst, int srcPage, const std::string& model, float dx, float dy);
        int AddText(int dst, const std::string& words, std::uint32_t colour, float x, float cy, int align,
            int refPage, int refItem, int wrap = 0);
        int AddLive(int dst, std::function<std::string()> words, std::uint32_t colour, float x, float cy, int align,
            int refPage, int refItem, std::function<std::uint32_t()> liveColour = {});
        int Hot(int dst, float x0, float y0, float x1, float y1, std::function<void()> act);
        void ShowWhen(int page, int item, std::function<bool()> when);
        void SetStretch(int page, int item, float pivot, float scale);
        void Move(int page, int item, float dx, float dy);
        void Remove(int page, int item);
        void Retext(int page, int item, const std::string& words);
        void TouchFrame(int dst, const std::string& heading, float width = 256);
        void TopLogo(int dst, std::function<std::string()> band);
        void TopStatus(int dst);
        int OkButton(int dst, std::function<void()> act, float x = 214);
        void ListButtons(int dst, const std::vector<std::pair<std::function<std::string()>, std::function<void()>>>& entries,
            float top, std::vector<int>* boxes = nullptr);

        void BuildMultiplayer();
        void BuildServers();
        void BuildCreate();
        void BuildSettings();
        void BuildLobby();
        void BuildRoom();
        void BuildManage();
        void BuildPause();
        void BuildEnd();
        void BuildCredits();
        void PatchRom();

        // ---- running ----
        int Register(std::function<void()> act);
        void GoTo(int page);
        void OpenKeyboard(const std::string& title, const std::string& value, std::function<void(const std::string&)> done);
        [[nodiscard]] std::string ModeName(int mode) const;
        [[nodiscard]] std::vector<PlayerRow> RoomRows() const;
        [[nodiscard]] std::vector<PlayerRow> OfflineRows() const;
        void Say(const std::string& text);
        void FillPanels();
        std::map<std::string, int> _panelIds;

        MenuFile& _file;
        MenuStrings& _strings;
        const MenuFont& _font;
        MenuEngine* _engine = nullptr;
        Host* _host = nullptr;
        std::vector<MenuPage> _rom;                 // the pages as the ROM has them: what clones copy
        std::map<int, Family> _family;
        std::map<std::pair<int, int>, Stretch> _stretch;
        std::vector<std::function<void()>> _calls;
        std::vector<Text> _texts;
        std::vector<Shown> _shown;
        std::vector<int> _history;
        std::map<std::string, int> _vars;
        float _textAnchor = 0;                     // text Y -> combined centre, measured from the font

        // the pieces the session moves at run time
        std::map<int, int> _rowBar;                 // page -> its red bar item
        std::map<int, std::vector<std::pair<int, std::pair<float, float>>>> _rows;  // page -> hot item -> bar x0, y

        // the DS keyboard
        std::function<void(const std::string&)> _keyboardDone;
        std::string _keyboardText;
        std::string _keyboardTitle;
        int _keyboardReturn = -1;
        bool _caps = false;
        std::map<int, char> _keyChars;              // a key's call -> its character
        bool _listening = false;

        // settings
        std::string _section = "display";
        int _settingsReturn = -1;
        int _settingsPage = 0;
    };

    // 5-bit channels packed as the menu's text styles keep them.
    [[nodiscard]] constexpr std::uint32_t Rgb5(int r, int g, int b, int a = 31) noexcept
    {
        return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8)
            | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(a) << 24);
    }
}
