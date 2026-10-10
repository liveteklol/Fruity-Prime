#include "OneScreen.hpp"

#include <algorithm>
#include <cmath>

namespace MphRead::Mods::ClassicMenu
{
    OneScreen::OneScreen(const MenuFile& file, const MenuStrings& strings, MenuWidgets& widgets, const MenuFont& font,
        const Composer& composer)
        : _file(file), _strings(strings), _widgets(widgets), _font(font), _composer(composer)
    {
    }

    // Where every item of a page rests: the page run on its own engine to
    // a standstill (140 ticks: every entrance on the ROM's pages is over by
    // then), measured once.
    const std::map<int, OneScreen::Rest>& OneScreen::Measure(int page)
    {
        const auto found = _rest.find(page);
        if (found != _rest.end()) return found->second;
        std::map<int, Rest>& out = _rest[page];
        MenuEngine shadow(_file, _strings, _widgets, _font);
        shadow.OnCall = [](const MenuAction&, int, int, int) { return true; };
        shadow.Enter(page);
        for (int t = 0; t < 140 && shadow.Page() != nullptr && shadow.Page()->Index == page && shadow.PendingPage() == -1; ++t)
        {
            shadow.Tick();
        }
        if (shadow.Page() == nullptr || shadow.Page()->Index != page) return out;
        std::vector<WidgetTri> tris;
        shadow.Collect(tris);
        for (const WidgetTri& t : tris)
        {
            float pivot = 0, scale = 1;
            if (const Stretch* s = _composer.StretchOf(page, t.Item)) { pivot = s->Pivot; scale = s->Scale; }
            for (const UiVertex* v : {&t.A, &t.B, &t.C})
            {
                if (v->A <= 0.001F) continue;
                const float x = pivot + (v->X - pivot) * scale, y = 192.0F - v->Y;
                auto [it, fresh] = out.try_emplace(t.Item, Rest{x, y, x, y});
                if (!fresh)
                {
                    it->second.X0 = std::min(it->second.X0, x); it->second.X1 = std::max(it->second.X1, x);
                    it->second.Y0 = std::min(it->second.Y0, y); it->second.Y1 = std::max(it->second.Y1, y);
                }
            }
        }
        return out;
    }

    // Which group of the family an item at (x, y) combined belongs to.
    int OneScreen::GroupFor(Family family, bool top, float x, float y) const
    {
        const float ly = top ? y : y - 192;
        if (_portrait) return top ? 0 : 1;
        switch (family)
        {
        case Family::Title: return top ? (ly < 140 ? 0 : 1) : 2;
        case Family::Logo:
        case Family::Wide:
            if (top) return ly < 125 ? 0 : 1;
            return ly < 152 ? 2 : x < 80 ? 3 : 4;
        case Family::Keys: return top ? -1 : 0;
        case Family::Overlay: return top ? -1 : 0;
        case Family::Stacked:
        case Family::Split:
        default: return top ? 0 : 1;
        }
    }

    void OneScreen::Layout(int page, float w, float h)
    {
        _w = w; _h = h;
        _page = page;
        _portrait = h > w;
        _family = _composer.FamilyOf(page);
        const std::map<int, Rest>& rest = Measure(page);

        // the groups this family has, and whether each is on the touch screen
        int count = 0;
        std::vector<bool> touch;
        if (_portrait) { count = 2; touch = {false, true}; }
        else switch (_family)
        {
        case Family::Title: count = 3; touch = {false, false, true}; break;
        case Family::Logo: case Family::Wide: count = 5; touch = {false, false, true, true, true}; break;
        case Family::Keys: case Family::Overlay: count = 1; touch = {true}; break;
        default: count = 2; touch = {false, true}; break;
        }
        _groups.assign(static_cast<std::size_t>(count), GroupPlace{});
        std::vector<bool> any(static_cast<std::size_t>(count), false);
        _itemGroup.clear();
        for (const auto& [item, r] : rest)
        {
            const float cx = (r.X0 + r.X1) / 2, cy = (r.Y0 + r.Y1) / 2;
            const int g = GroupFor(_family, cy < 192, cx, cy);
            if (g < 0 || g >= count) continue;
            _itemGroup[item] = g;
            GroupPlace& gp = _groups[static_cast<std::size_t>(g)];
            const float oy = touch[static_cast<std::size_t>(g)] ? 192.0F : 0.0F;
            if (!any[static_cast<std::size_t>(g)])
            {
                gp.X0 = r.X0; gp.X1 = r.X1; gp.Y0 = r.Y0 - oy; gp.Y1 = r.Y1 - oy;
                any[static_cast<std::size_t>(g)] = true;
            }
            else
            {
                gp.X0 = std::min(gp.X0, r.X0); gp.X1 = std::max(gp.X1, r.X1);
                gp.Y0 = std::min(gp.Y0, r.Y0 - oy); gp.Y1 = std::max(gp.Y1, r.Y1 - oy);
            }
        }
        // place = where the group's union (local to its screen) lands, top left
        const auto put = [&](int g, float s, float x, float y)
        {
            GroupPlace& gp = _groups[static_cast<std::size_t>(g)];
            gp.S = s;
            gp.Touch = touch[static_cast<std::size_t>(g)];
            const float oy = gp.Touch ? 192.0F : 0.0F;
            gp.Ox = x - gp.X0 * s;
            gp.Oy = y - (gp.Y0 + oy) * s;
        };
        const auto uw = [&](int g) { return _groups[static_cast<std::size_t>(g)].X1 - _groups[static_cast<std::size_t>(g)].X0; };
        const auto uh = [&](int g) { return _groups[static_cast<std::size_t>(g)].Y1 - _groups[static_cast<std::size_t>(g)].Y0; };
        const auto ux = [&](int g) { return _groups[static_cast<std::size_t>(g)].X0; };
        const auto uy = [&](int g) { return _groups[static_cast<std::size_t>(g)].Y0; };

        if (_portrait)
        {
            // the DS's own arrangement; a wide page scaled to fit across
            const float tw = _family == Family::Wide ? 400.0F : 256.0F;
            const float s = std::min(w / tw, h / 392.0F);
            const float x = (w - tw * s) / 2, y = (h - 392 * s) / 2;
            put(0, std::min(w / 256.0F, s * 1.0F), (w - 256 * std::min(w / 256.0F, s)) / 2 + ux(0) * std::min(w / 256.0F, s), y + uy(0) * s);
            put(1, s, x + ux(1) * s, y + 200 * s + uy(1) * s);
        }
        else switch (_family)
        {
        case Family::Title:
            put(0, 1.12F, (w - uw(0) * 1.12F) / 2, 14);
            put(1, 0.85F, (w - uw(1) * 0.85F) / 2, 234 - uh(1) * 0.85F);
            put(2, 1.25F, (w - uw(2) * 1.25F) / 2, 168 - uh(2) * 1.25F / 2);
            break;
        case Family::Logo:
            put(0, 0.42F, (w - uw(0) * 0.42F) / 2, 3);
            put(1, 0.74F, (w - uw(1) * 0.74F) / 2, 211);
            put(2, 1.0F, (w - 256) / 2 + ux(2), 50);
            put(3, 1.0F, 6, 236 - uh(3));
            put(4, 1.0F, w - 6 - uw(4), 236 - uh(4));
            break;
        case Family::Wide:
        {
            const float s = std::min(1.0F, (w - 12) / 400.0F);
            put(0, 0.34F, 6, 4);
            put(1, 0.62F, w - 6 - uw(1) * 0.62F, 8);
            put(2, s, (w - 400 * s) / 2 + ux(2) * s, 44);
            put(3, s, (w - 400 * s) / 2 + 4, 236 - uh(3) * s);
            put(4, s, (w + 400 * s) / 2 - 4 - uw(4) * s, 236 - uh(4) * s);
            break;
        }
        case Family::Keys:
            put(0, 1.2F, (w - 256 * 1.2F) / 2 + ux(0) * 1.2F, (h - 192 * 1.2F) / 2 + uy(0) * 1.2F);
            break;
        case Family::Overlay:
            put(0, 1.0F, (w - 256) / 2 + ux(0), (h - 192) / 2 + uy(0));
            break;
        case Family::Stacked:
        {
            const float s = h / 384.0F;
            put(0, s, (w - 256 * s) / 2 + ux(0) * s, uy(0) * s);
            put(1, s, (w - 256 * s) / 2 + ux(1) * s, 192 * s + uy(1) * s);
            break;
        }
        case Family::Split:
        default:
        {
            const float ts = 0.62F, bs = 0.95F, gap = 10;
            const float x0 = (w - (256 * ts + gap + 256 * bs)) / 2;
            put(0, ts, x0 + ux(0) * ts, (h - 192 * ts) / 2 + uy(0) * ts);
            put(1, bs, x0 + 256 * ts + gap + ux(1) * bs, (h - 192 * bs) / 2 + uy(1) * bs);
            break;
        }
        }

        // the Samus column under everything, cover-fitted, on the visor
        const float s = std::max(w / 256.0F, h / 384.0F);
        _backdrop.S = s;
        _backdrop.Ox = (w - 256 * s) / 2;
        _backdrop.Oy = std::clamp(h / 2 - 150 * s, h - 384 * s, 0.0F);
    }

    const GroupPlace* OneScreen::PlaceOf(int item, float cx, float cy) const
    {
        const auto found = _itemGroup.find(item);
        int g = found != _itemGroup.end() ? found->second : GroupFor(_family, cy < 192, cx, cy);
        if (g < 0 || static_cast<std::size_t>(g) >= _groups.size()) return nullptr;
        return &_groups[static_cast<std::size_t>(g)];
    }

    bool OneScreen::ToCombined(float x, float y, float& cx, float& cy) const
    {
        // the touch groups, the smallest that holds the point first
        const GroupPlace* best = nullptr;
        float bestArea = 1e30F;
        for (const GroupPlace& g : _groups)
        {
            if (!g.Touch) continue;
            const float x0 = g.Ox + g.X0 * g.S, x1 = g.Ox + g.X1 * g.S;
            const float y0 = g.Oy + (g.Y0 + 192) * g.S, y1 = g.Oy + (g.Y1 + 192) * g.S;
            // a little room round each group: the ROM's touch areas overhang the art
            const float m = 6;
            if (x < x0 - m || x > x1 + m || y < y0 - m || y > y1 + m) continue;
            const float area = (x1 - x0) * (y1 - y0);
            if (area < bestArea) { bestArea = area; best = &g; }
        }
        if (best == nullptr) return false;
        cx = (x - best->Ox) / best->S;
        cy = (y - best->Oy) / best->S;
        return true;
    }
}
