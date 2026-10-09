#pragma once

// Both DS screens on one screen of any shape. Every item belongs to the
// screen it rests on; items are moved in groups (the logo, the description
// band, the touch screen's body, its footer...) and never cut. A group's
// place depends on the page's Family and on the canvas: 240 units high,
// as wide as the window's shape makes it (portrait: 256 wide, as tall).
//
// Coordinates: "combined" is the DS's own pixels, x right, y down, the top
// screen 0..192 over the touch screen 192..384 (wide pages run to x 400).
// A group maps combined -> canvas as (ox + x * s, oy + y * s).

#include "Compose.hpp"
#include "MenuData.hpp"

#include <map>
#include <vector>

namespace MphRead::Mods::ClassicMenu
{
    struct GroupPlace final
    {
        float S = 1, Ox = 0, Oy = 0;
        bool Touch = false;
        float X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;   // its items' union, combined
    };

    class OneScreen final
    {
    public:
        OneScreen(const MenuFile& file, const MenuStrings& strings, MenuWidgets& widgets, const MenuFont& font, const Composer& composer);

        // Lay the page out for a canvas of W x H units.
        void Layout(int page, float w, float h);
        // The group an item is drawn with, by where it rests (or, an item
        // never seen at rest, where it is now: cx, cy combined).
        [[nodiscard]] const GroupPlace* PlaceOf(int item, float cx, float cy) const;
        // A canvas point on the touch screen, back to combined coordinates.
        [[nodiscard]] bool ToCombined(float x, float y, float& cx, float& cy) const;
        // The backdrop: the two Samus pictures as one column, cover-fitted.
        [[nodiscard]] GroupPlace Backdrop() const { return _backdrop; }
        [[nodiscard]] float Width() const noexcept { return _w; }
        [[nodiscard]] float Height() const noexcept { return _h; }

    private:
        struct Rest final
        {
            float X0, Y0, X1, Y1;
        };
        const std::map<int, Rest>& Measure(int page);
        [[nodiscard]] int GroupFor(Family family, bool top, float x, float y) const;

        const MenuFile& _file;
        const MenuStrings& _strings;
        MenuWidgets& _widgets;
        const MenuFont& _font;
        const Composer& _composer;
        std::map<int, std::map<int, Rest>> _rest;   // page -> item -> where it rests
        std::vector<GroupPlace> _groups;
        std::map<int, int> _itemGroup;
        Family _family = Family::Logo;
        bool _portrait = false;
        int _page = -1;
        float _w = 426, _h = 240;
        GroupPlace _backdrop;
    };
}
