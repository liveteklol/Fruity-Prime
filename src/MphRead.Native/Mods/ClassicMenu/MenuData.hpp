#pragma once

// The DS game's own front end, read from the player's files and run the way
// the data describes it. A C++ port of Second Hunt's MphRecomp.Frontend
// (github.com/BountyHunterKanden/SecondHunt, MIT, itself a fork of MphRead):
// the menu graph (frontend/metroidhunters.bin, "MARM"), its strings and font,
// the widget models evaluated on the CPU into flat triangles, the engine that
// moves items between states, and the layout that puts both DS screens on
// one screen of any shape.
//
// Everything here is simulation and geometry; ClassicMenu turns it into a
// draw list the scene renders.

#include "../../Formats/Types.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MphRead
{
    class Model;
    class ModelInstance;
}

namespace MphRead::Mods::ClassicMenu
{
    // Steady item states. A state code is either a steady state (1..7) or a
    // transition written as (to << 3) | from. 7 is "any" in link events.
    enum class MenuState : std::uint8_t
    {
        None = 0,
        Hidden = 1,
        Idle = 2,
        Selected = 3,
        Focused = 4,
        Disabled = 5,
        State6 = 6,
        Any = 7
    };

    namespace StateCode
    {
        [[nodiscard]] constexpr bool IsTransition(int code) noexcept { return code >= 8; }
        [[nodiscard]] constexpr MenuState To(int code) noexcept
        {
            return static_cast<MenuState>(code >= 8 ? (code >> 3) : code);
        }
        [[nodiscard]] constexpr int Transition(MenuState from, MenuState to) noexcept
        {
            return (static_cast<int>(to) << 3) | static_cast<int>(from);
        }
    }

    // The game's menu key bits (an action's Kind), not the DS KEYINPUT order.
    enum MenuKeys : std::uint16_t
    {
        KeyNone = 0,
        KeyA = 1, KeyB = 2, KeyX = 4, KeyY = 8, KeyL = 0x10, KeyR = 0x20,
        KeyStart = 0x40, KeySelect = 0x80,
        KeyUp = 0x100, KeyDown = 0x200, KeyLeft = 0x400, KeyRight = 0x800,
        KeyAny = 4095
    };

    struct MenuWidget final
    {
        std::string ModelPath;
        std::optional<std::string> AnimPath;
    };

    struct MenuTextStyle final
    {
        int StringId = 0;
        // 5-bit channels, R G B A in byte order; the text fades from Start to
        // End over Duration frames.
        std::uint32_t StartColor = 0;
        std::uint32_t EndColor = 0;
        std::uint16_t WrapWidth = 0;
        float Duration = 0;
        std::uint8_t Size = 0;
        // Added to every letter's advance (the format's second byte): "BEGIN
        // GAME" is spread 3 apart, "PLAYER" and "CONGRATULATIONS" 4.
        std::uint8_t LetterSpacing = 0;
        // Added to Size between lines (the format's third byte, signed): the
        // main menu's descriptions run at 9, its two-line labels at 7.
        std::int8_t LineSpacing = 0;
        std::uint8_t Align = 0; // 0 left, 1 right, 2 centre
    };

    struct MenuItemState final
    {
        int Code = 0;
        std::uint8_t Kind = 0; // 0 widget, 1 text
        int WidgetIndex = -1;
        std::optional<MenuTextStyle> Text;
    };

    // "When this item's state changes by Event, put item Target into State."
    struct MenuLink final
    {
        int Event = 0;
        MenuState State = MenuState::None;
        int Target = 0;
    };

    struct MenuAction final
    {
        std::uint16_t Kind = 0;
        std::uint8_t Field2 = 0;
        std::uint8_t Flags = 0;
        std::uint32_t Field4 = 0;
        std::int16_t RectX = 0, RectY = 0, RectW = 0, RectH = 0;
        std::vector<std::pair<int, int>> Calls;
        std::uint16_t Item = 0;
        std::uint8_t TargetPage = 0xFF;
        std::uint8_t Field17 = 0;
    };

    struct MenuTimer final
    {
        float Delay = 0;
        MenuAction Action;
    };

    struct MenuItem final
    {
        int Index = 0;
        float Delay = 0;
        float X = 0;
        float Y = 0;
        float Depth = 0;
        std::uint8_t InitialState = 0;
        std::vector<MenuItemState> States;
        std::vector<MenuLink> Links;
        std::vector<MenuAction> Actions;

        [[nodiscard]] const MenuItemState* GetState(int code) const noexcept
        {
            for (const MenuItemState& s : States)
            {
                if (s.Code == code) return &s;
            }
            return nullptr;
        }
        [[nodiscard]] bool IsText() const noexcept { return !States.empty() && States.front().Kind == 1; }
    };

    struct MenuPage final
    {
        int Index = 0;
        std::vector<MenuItem> Items;
        std::vector<MenuAction> Actions;
        std::vector<MenuTimer> Timers;
    };

    struct MenuFile final
    {
        std::vector<MenuPage> Pages;
        std::vector<MenuWidget> Widgets;

        [[nodiscard]] static MenuFile Parse(const std::vector<std::uint8_t>& data);
    };

    class MenuStrings final
    {
    public:
        [[nodiscard]] static MenuStrings Load(const std::string& fileSystemRoot, const std::string& lang);
        [[nodiscard]] const std::string& operator[](int id) const;
        [[nodiscard]] int IndexOf(const std::string& text) const;
        void Fill(int id, const std::string& text);
        // A string the ROM does not have (a label the single-screen pages add).
        int Add(const std::string& text);
        [[nodiscard]] int Count() const noexcept { return static_cast<int>(_strings.size()); }

    private:
        std::vector<std::string> _strings;
    };

    // ---- the draw list ----

    enum class UiWrap : std::uint8_t { Clamp = 0, Repeat = 1, Mirror = 2 };

    struct UiVertex final
    {
        float X = 0, Y = 0, U = 0, V = 0, R = 1, G = 1, B = 1, A = 1;
    };

    // An RGBA8 picture the draw list refers to by Id; never changes once made.
    struct UiTexture final
    {
        int Id = 0;
        int Width = 0;
        int Height = 0;
        std::vector<ColorRgba> Pixels;
    };

    class UiTextureCache final
    {
    public:
        [[nodiscard]] int GetOrAdd(const std::string& key,
            const std::function<UiTexture()>& create);
        [[nodiscard]] const UiTexture& operator[](int id) const { return *_byId.at(static_cast<std::size_t>(id)); }
        [[nodiscard]] int Count() const noexcept { return static_cast<int>(_byId.size()); }

    private:
        std::unordered_map<std::string, int> _byKey;
        std::vector<std::unique_ptr<UiTexture>> _byId;
    };

    struct UiBatch final
    {
        int TextureId = -1; // -1: vertex colour only
        UiWrap WrapS = UiWrap::Clamp;
        UiWrap WrapT = UiWrap::Clamp;
        // The DS layer: the backdrop bitmaps are the 2D one, drawn first;
        // everything else is the 3D layer, laid over it by its own alpha.
        bool Backdrop = false;
        // The DS polygon ID: a translucent pixel is not drawn over a
        // translucent pixel of its own ID (-1: none).
        int PolyId = -1;
        // A DS pixel's size in the picture's pixels: what a polygon thinner
        // than one is still drawn as.
        float Pixel = 1;
        int Start = 0;
        int Count = 0;
    };

    // Textured, vertex-coloured triangles in canvas pixels (origin top-left,
    // Y down), grouped by texture, wrap, layer and polygon ID.
    struct UiDrawList final
    {
        std::vector<UiVertex> Vertices;
        std::vector<UiBatch> Batches;
        // Triangles added while this is set go to the 2D backdrop layer.
        bool Backdrop = false;
        // A DS pixel's size in the picture's pixels, for the triangles added now.
        float Pixel = 1;

        void Clear() { Vertices.clear(); Batches.clear(); Backdrop = false; Pixel = 1; }
        void Triangle(int textureId, UiWrap wrapS, UiWrap wrapT,
            const UiVertex& a, const UiVertex& b, const UiVertex& c, int polyId = -1);
        void Quad(int textureId, float x0, float y0, float x1, float y1,
            float u0, float v0, float u1, float v1, float r, float g, float b, float a, int polyId = -1);
    };

    // A triangle in MENU SPACE: DS pixels, X right, Y up, origin on the seam
    // between the two screens (top screen y 0..192, touch screen -192..0).
    struct WidgetTri final
    {
        UiVertex A, B, C;
        float Z = 0;
        int Item = 0;
        int TextureId = -1;
        UiWrap WrapS = UiWrap::Clamp;
        UiWrap WrapT = UiWrap::Clamp;
        // The material's polygon mode: 0 modulate, 1 decal, 2 toon, 3 shadow.
        std::uint8_t Mode = 0;
        // The material's lights (POLYGON_ATTR bits 0-3).
        std::uint8_t Lights = 0;
    };

    class MenuFont final
    {
    public:
        explicit MenuFont(UiTextureCache& textures);
        // lineHeight places the first line under the anchor; each next one
        // is lineHeight + lineSpacing further down. letterSpacing is added to
        // every letter's advance, measuring included.
        void Emit(const std::string& text, float x, float y, int align, int wrapWidth, float lineHeight,
            float r, float g, float b, float a, float z, std::vector<WidgetTri>& output, float lineSpacing = 0,
            int letterSpacing = 0) const;

    private:
        [[nodiscard]] static int Glyph(const std::string& text, std::size_t& i);
        [[nodiscard]] int Advance(int glyph) const;
        [[nodiscard]] float Measure(const std::string& line, int spacing) const;
        [[nodiscard]] std::vector<std::string> Lines(const std::string& text, int wrapWidth, int spacing) const;

        std::vector<int> _widths;
        std::vector<int> _offsets;
        int _textureId = -1;
        int _atlasWidth = 128;
        int _atlasHeight = 256;
    };

    // The ROM's widget models, evaluated at a frame into menu-space triangles.
    class MenuWidgets final
    {
    public:
        MenuWidgets(const MenuFile& file, UiTextureCache& textures);
        [[nodiscard]] std::shared_ptr<ModelInstance> Instance(int widgetIndex);
        [[nodiscard]] std::shared_ptr<ModelInstance> Instance(const std::string& modelPath,
            const std::optional<std::string>& animPath);
        [[nodiscard]] int FrameCount(int widgetIndex);
        [[nodiscard]] static int FrameCount(const ModelInstance& inst);
        void Evaluate(int widgetIndex, int frame, bool loop, float alpha, std::vector<WidgetTri>& output);
        void Evaluate(const std::shared_ptr<ModelInstance>& inst, int frame, bool loop, float alpha,
            std::vector<WidgetTri>& output);
        [[nodiscard]] const UiTextureCache& Textures() const noexcept { return _textures; }

    private:
        void EmitNode(Model& model, int texcoordFrame, int index, const std::vector<std::vector<float>>& meshes,
            float alpha, std::vector<WidgetTri>& output, const std::shared_ptr<ModelInstance>& inst);
        int TextureFor(const Model& model, int textureId, int paletteId);

        const MenuFile& _file;
        UiTextureCache& _textures;
        std::map<std::string, std::shared_ptr<ModelInstance>> _instances;
        std::map<const ModelInstance*, std::vector<std::vector<float>>> _meshes;
    };

    class MenuEngine final
    {
    public:
        // A numbered game callback (the (A, B) pairs in the file). Return true
        // if handled. `item` is -1 for page and timer actions.
        std::function<bool(const MenuAction&, int a, int b, int item)> OnCall;
        std::function<void(int page)> OnPageEntered;

        MenuEngine(const MenuFile& file, const MenuStrings& strings, MenuWidgets& widgets, const MenuFont& font);

        [[nodiscard]] const MenuPage* Page() const noexcept { return _page; }
        [[nodiscard]] const MenuFile& File() const noexcept { return _file; }
        [[nodiscard]] int PendingPage() const noexcept { return _pendingPage; }
        [[nodiscard]] MenuState ItemState(int item) const { return _items.at(static_cast<std::size_t>(item)).State; }
        [[nodiscard]] int ItemCode(int item) const { return _items.at(static_cast<std::size_t>(item)).Code; }
        [[nodiscard]] std::string ItemModelPath(int item) const;

        // carry: coming from the page being left (GoTo), whose items the new
        // page has unchanged go on as they were instead of coming in again.
        void Enter(int page, bool carry = false);
        void GoTo(int page);
        void Tick();
        void SetState(int item, MenuState target);

        void Press(std::uint16_t keys);
        void Direction(int dx, int dy);
        void Touch(float x, float y);
        [[nodiscard]] int FocusedItem() const;
        [[nodiscard]] bool TryFocusFrame(float& x0, float& y0, float& x1, float& y1, bool& hasHighlight) const;

        void Collect(std::vector<WidgetTri>& output);

    private:
        struct ItemRuntime final
        {
            MenuState State = MenuState::Hidden;
            int Code = static_cast<int>(MenuState::Hidden);
            int Frame = 0;
            float Delay = 0;
            bool Started = false;
            // Leaving the page, an item with no way of hiding keeps the look
            // it had (this code, at this frame) until the next page comes in.
            int Linger = -1;
            int LingerFrame = 0;
        };

        [[nodiscard]] const MenuItemState* Visual(int item, int code) const;
        [[nodiscard]] int CodeLength(int item, int code) const;
        void StartItems();
        void Arrive(int item);
        void Fire(int item, int evt);
        void Run(const MenuAction& action, int item);
        [[nodiscard]] bool HasAction(int item, std::uint16_t key) const;
        [[nodiscard]] bool HasKeyAction(std::uint16_t keys) const;
        [[nodiscard]] std::vector<int> FocusableItems() const;
        void Focus(int item);
        void Navigate(int dx, int dy);
        void FocusFirst();
        [[nodiscard]] bool Ready(int item, const MenuAction& action) const;
        [[nodiscard]] std::pair<float, float> ActionCentre(int item) const;
        void EmitText(const MenuItem& item, const MenuTextStyle& style, int frame, bool steady,
            std::vector<WidgetTri>& output) const;

        const MenuFile& _file;
        const MenuStrings& _strings;
        MenuWidgets& _widgets;
        const MenuFont& _font;
        const MenuPage* _page = nullptr;
        int _pageFrame = 0;
        std::vector<ItemRuntime> _items;
        int _pendingPage = -1;
        int _exitFrames = 0;
        int _focus = -1;
        int _generation = 0;

    public:
        // An action's touch rectangle in text coordinates (Y up from the
        // bottom of the touch screen), moved by its item's offset.
        [[nodiscard]] static bool TryRect(const MenuAction& a, const MenuItem* it,
            float& x0, float& y0, float& x1, float& y1);
    };

    // ---- layout: both DS screens on one canvas ----

    enum class LayoutKind { SideBySide, Overlay, MainFamily, Stacked };

    struct LayoutRegion final
    {
        float X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
        float AnchorX = 0.5F, AnchorY = 0.5F, PivotX = 0.5F, PivotY = 0.5F;
        float OffsetX = 0, OffsetY = 0, Scale = 1;
        [[nodiscard]] bool Contains(float x, float y) const noexcept
        {
            return x >= X0 && x <= X1 && y >= Y0 && y <= Y1;
        }
    };

    struct Placement final
    {
        float SrcX = 0, SrcY = 0, Scale = 1, DstX = 0, DstY = 0;
        [[nodiscard]] std::pair<float, float> ToCanvas(float x, float y) const noexcept
        {
            return {DstX + (x - SrcX) * Scale, DstY - (y - SrcY) * Scale};
        }
        [[nodiscard]] std::pair<float, float> ToMenu(float cx, float cy) const noexcept
        {
            return {SrcX + (cx - DstX) / Scale, SrcY - (cy - DstY) / Scale};
        }
    };

    struct MenuLayout final
    {
        static constexpr float VirtualHeight = 240;
        LayoutKind Kind = LayoutKind::SideBySide;
        std::vector<LayoutRegion> Regions;

        [[nodiscard]] static MenuLayout For(const MenuFile& file, const MenuPage& page);
        [[nodiscard]] static MenuLayout SideBySide();
        [[nodiscard]] static MenuLayout Stacked();
        [[nodiscard]] static MenuLayout Overlay();
        [[nodiscard]] static MenuLayout MainFamily(bool actionsAboveRight);
        [[nodiscard]] std::vector<Placement> Resolve(float width, float height) const;
        [[nodiscard]] int RegionOf(float x, float y) const;
    };
}
