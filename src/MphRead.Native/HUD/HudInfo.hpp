#pragma once

#include "../Formats/Types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Hud
{
    template <typename T>
    using ReadOnlyList = std::shared_ptr<const std::vector<T>>;

    enum class Align : std::int32_t
    {
        Left = 0,
        Right = 1,
        Center = 2,
        PadCenter = 3
    };

    struct UiAnimParams
    {
        const std::uint8_t ImageIndex = 0;
        const std::uint8_t Delay = 0;
        const std::uint16_t Field2 = 0;
        const std::int32_t Field4 = 0;
        const std::uint16_t ParamPa = 0;
        const std::uint16_t ParamPb = 0;
        const std::uint16_t ParamPc = 0;
        const std::uint16_t ParamPd = 0;

        constexpr UiAnimParams() noexcept = default;
        constexpr UiAnimParams(std::uint8_t imageIndex, std::uint8_t delay,
            std::uint16_t field2, std::int32_t field4, std::uint16_t paramPa,
            std::uint16_t paramPb, std::uint16_t paramPc, std::uint16_t paramPd) noexcept
            : ImageIndex(imageIndex), Delay(delay), Field2(field2), Field4(field4),
              ParamPa(paramPa), ParamPb(paramPb), ParamPc(paramPc), ParamPd(paramPd)
        {
        }
        UiAnimParams(const UiAnimParams&) noexcept = default;
        UiAnimParams& operator=(const UiAnimParams& other) noexcept;
    };

    class HudObjectInstance;

    class HudMeter
    {
    public:
        bool Horizontal = false;
        std::int32_t Length = 0;
        std::int32_t TankSpacing = 0;
        std::int32_t TankOffsetX = 0;
        std::int32_t TankOffsetY = 0;
        std::int32_t BarOffsetX = 0;
        std::int32_t BarOffsetY = 0;
        std::int32_t TextOffsetX = 0;
        std::int32_t TextOffsetY = 0;
        MphRead::Hud::Align Align = MphRead::Hud::Align::Left;
        std::int32_t MessageId = 0;

        std::int32_t TankAmount = 0;
        std::int32_t TankCount = 0;
        std::shared_ptr<HudObjectInstance> BarInst{};
        std::shared_ptr<HudObjectInstance> TankInst{};
    };

    class HudObject
    {
    public:
        const std::int32_t Width;
        const std::int32_t Height;
        const ReadOnlyList<std::uint8_t> CharacterData;
        const ReadOnlyList<ColorRgba> PaletteData;
        const ReadOnlyList<UiAnimParams> AnimParams;

        HudObject(std::int32_t width, std::int32_t height,
            ReadOnlyList<std::uint8_t> characterData,
            ReadOnlyList<ColorRgba> paletteData,
            ReadOnlyList<UiAnimParams> animParams);
    };

    enum class HudObjectLoopType : std::int32_t
    {
        None = 0,
        Start = 1,
        Offset = 2
    };

    class HudObjectInstance
    {
    public:
        bool Enabled = false;
        bool Center = false;
        float PositionX = 0.0F;
        float PositionY = 0.0F;
        std::int32_t Width = 0;
        std::int32_t Height = 0;
        bool FlipHorizontal = false;
        bool FlipVertical = false;
        bool UseMask = false;
        bool Smooth = false;
        ReadOnlyList<std::uint8_t> CharacterData{};
        std::int32_t PaletteIndex = -1;
        ReadOnlyList<ColorRgba> PaletteData{};
        ReadOnlyList<std::int32_t> AnimFrames{};
        std::optional<ColorRgba> Color{};
        const std::shared_ptr<std::vector<ColorRgba>> Texture;
        float Alpha = 1.0F;
        std::int32_t BindingId = -1;
        std::int32_t CurrentFrame = 0;
        std::int32_t StartFrame = 0;
        std::int32_t TargetFrame = 0;
        float Timer = -1.0F;
        float Time = -1.0F;
        HudObjectLoopType Loop = HudObjectLoopType::None;
        std::int32_t AfterAnimFrame = -1;

        HudObjectInstance(std::int32_t width, std::int32_t height);
        HudObjectInstance(std::int32_t width, std::int32_t height,
            std::int32_t maxWidth, std::int32_t maxHeight);

        void SetAnimationFrames(ReadOnlyList<UiAnimParams> frames);
        void SetCharacterData(ReadOnlyList<std::uint8_t> data,
            std::int32_t width, std::int32_t height, Scene& scene);
        void SetCharacterData(ReadOnlyList<std::uint8_t> data, Scene& scene);
        void SetCharacterData(ReadOnlyList<std::uint8_t> data,
            std::int32_t frame, Scene& scene);
        void SetPaletteData(ReadOnlyList<ColorRgba> data, Scene& scene);
        void SetPalette(std::int32_t index, Scene& scene);
        void SetData(ReadOnlyList<std::uint8_t> charData, std::int32_t charFrame,
            ReadOnlyList<ColorRgba> palData, std::int32_t palIndex, Scene& scene);
        void SetData(std::int32_t charFrame, std::int32_t palIndex, Scene& scene);
        void SetData(std::int32_t charFrame, ColorRgba color, Scene& scene);
        void SetIndex(std::int32_t frame, Scene& scene);
        void SetAnimation(std::int32_t start, std::int32_t target,
            std::int32_t frames, bool loop = false);
        void SetAnimation(std::int32_t start, std::int32_t target,
            std::int32_t frames, std::int32_t afterAnim, bool loop = false);
        void SetAnimation(std::int32_t start, std::int32_t target,
            std::int32_t frames, std::int32_t afterAnim, HudObjectLoopType loopType);
        void ProcessAnimation(Scene& scene);
        // Bind the first width x height texels of Texture: the texture that
        // picture already has, or a new one. A bound texture is never rewritten.
        void BindPicture(Scene& scene, std::int32_t width, std::int32_t height);

    private:
        void DoTexture(Scene& scene);

        // One texture per picture this instance has shown: the HUD's text is
        // one 8x8 instance given every character in turn, and rewriting a
        // texture between draws ends a Vulkan render pass -- which, on a
        // tiled GPU, is the whole target written out and read back, a
        // hundred times a frame. A picture seen again binds its texture.
        static constexpr std::size_t MaxCachedPictures = 4096;
        Scene* _pictureScene = nullptr;
        std::unordered_map<std::string, std::int32_t> _pictures;
        // Past the cap, pictures are rewritten in place here, as before.
        std::int32_t _overflowBinding = -1;
        // The binding a decoded picture got, by what it was decoded from:
        // the text instance is given every character of every string each
        // frame, and decoding then keying the pixels again was ~10 % of a
        // frame. The sheets are kept alive here so their addresses stay
        // theirs.
        struct SourceKey
        {
            const void* Characters = nullptr;
            const void* Palette = nullptr;
            std::int32_t Frame = 0;
            std::int32_t PaletteIndex = 0;
            std::uint32_t Color = 0;
            bool HasColor = false;
            std::int32_t Width = 0;
            std::int32_t Height = 0;
            bool operator==(const SourceKey&) const = default;
        };
        struct SourceKeyHash
        {
            std::size_t operator()(const SourceKey& key) const noexcept;
        };
        struct SourceEntry
        {
            ReadOnlyList<std::uint8_t> Characters;
            ReadOnlyList<ColorRgba> Palette;
            std::int32_t BindingId = -1;
        };
        std::unordered_map<SourceKey, SourceEntry, SourceKeyHash> _sources;
    };

    class LayerInfo
    {
    public:
        std::int32_t BindingId = -1;
        std::int32_t MaskId = -1;
        float Alpha = 1.0F;
        float ScaleX = -1.0F;
        float ScaleY = -1.0F;
        float ShiftX = 0.0F;
        float ShiftY = 0.0F;
    };

    class HudInfo
    {
    public:
        struct UiPartHeader
        {
            const std::int32_t Magic = 0;
            const std::int32_t CharDataSize = 0;
            const std::int32_t PalDataSize = 0;

            constexpr UiPartHeader() noexcept = default;
            constexpr UiPartHeader(std::int32_t magic, std::int32_t charDataSize,
                std::int32_t palDataSize) noexcept
                : Magic(magic), CharDataSize(charDataSize), PalDataSize(palDataSize)
            {
            }
            UiPartHeader(const UiPartHeader&) noexcept = default;
            UiPartHeader& operator=(const UiPartHeader& other) noexcept;
        };

        struct ScrDatInfo
        {
            const std::uint16_t CharsX = 0;
            const std::uint16_t CharsY = 0;
            const std::int32_t ScrDataSize = 0;

            constexpr ScrDatInfo() noexcept = default;
            constexpr ScrDatInfo(std::uint16_t charsX, std::uint16_t charsY,
                std::int32_t scrDataSize) noexcept
                : CharsX(charsX), CharsY(charsY), ScrDataSize(scrDataSize)
            {
            }
            ScrDatInfo(const ScrDatInfo&) noexcept = default;
            ScrDatInfo& operator=(const ScrDatInfo& other) noexcept;
        };

        static std::pair<std::int32_t, ReadOnlyList<std::uint16_t>> CharMapToTexture(
            const std::string& path, Scene& scene,
            ReadOnlyList<std::uint16_t> paletteOverride = {}, std::int32_t paletteId = -1);
        static std::pair<std::int32_t, ReadOnlyList<std::uint16_t>> CharMapToTexture(
            const std::string& path, std::int32_t startX, std::int32_t startY,
            std::int32_t tilesX, std::int32_t tilesY, Scene& scene,
            ReadOnlyList<std::uint16_t> paletteOverride = {}, std::int32_t paletteId = -1);

        struct UiObjectHeader
        {
            const std::uint16_t FrameCount = 0;
            const std::uint16_t ImageCount = 0;
            const std::uint16_t Width = 0;
            const std::uint16_t Height = 0;
            const std::int32_t ParamDataSize = 0;
            const std::int32_t AttrDataSize = 0;
            const std::int32_t CharDataSize = 0;
            const std::int32_t PalDataSize = 0;

            constexpr UiObjectHeader() noexcept = default;
            constexpr UiObjectHeader(std::uint16_t frameCount, std::uint16_t imageCount,
                std::uint16_t width, std::uint16_t height, std::int32_t paramDataSize,
                std::int32_t attrDataSize, std::int32_t charDataSize, std::int32_t palDataSize) noexcept
                : FrameCount(frameCount), ImageCount(imageCount), Width(width), Height(height),
                  ParamDataSize(paramDataSize), AttrDataSize(attrDataSize),
                  CharDataSize(charDataSize), PalDataSize(palDataSize)
            {
            }
            UiObjectHeader(const UiObjectHeader&) noexcept = default;
            UiObjectHeader& operator=(const UiObjectHeader& other) noexcept;
        };

        struct RawUiOamAttrs
        {
            const std::uint16_t Attr0 = 0;
            const std::uint16_t Attr1 = 0;
            const std::uint16_t Attr2 = 0;
            const std::uint16_t Padding6 = 0;

            constexpr RawUiOamAttrs() noexcept = default;
            constexpr RawUiOamAttrs(std::uint16_t attr0, std::uint16_t attr1,
                std::uint16_t attr2, std::uint16_t padding6) noexcept
                : Attr0(attr0), Attr1(attr1), Attr2(attr2), Padding6(padding6)
            {
            }
            RawUiOamAttrs(const RawUiOamAttrs&) noexcept = default;
            RawUiOamAttrs& operator=(const RawUiOamAttrs& other) noexcept;
        };

        enum class ObjType : std::int32_t
        {
            Normal = 0,
            Transparent = 1,
            Window = 2,
            Bitmap = 3
        };

        enum class ObjColors : std::int32_t
        {
            Color16 = 0,
            Color256 = 1
        };

        enum class ObjShape : std::int32_t
        {
            Square = 0,
            Wide = 1,
            Tall = 2,
            Unused = 3
        };

        enum class ObjSize : std::int32_t
        {
            Tiny = 0,
            Small = 1,
            Medium = 2,
            Large = 3
        };

        struct UiOamAttrs
        {
            std::uint16_t XPos = 0;
            std::uint16_t YPos = 0;
            ObjType Type = ObjType::Normal;
            ObjSize Size = ObjSize::Tiny;
            ObjShape Shape = ObjShape::Square;
            ObjColors Colors = ObjColors::Color16;
            bool AffineEnable = false;
            bool DoubleSize = false;
            std::int32_t AffineIndex = 0;
            bool FlipHorizontal = false;
            bool FlipVertical = false;
            bool Mosaic = false;
            std::int32_t CharacterId = 0;
            std::int32_t PaletteId = 0;
            std::uint8_t Alpha = 0;
            std::uint8_t Priority = 0;

            UiOamAttrs() = default;
            explicit UiOamAttrs(RawUiOamAttrs raw);
        };

        static std::shared_ptr<HudObject> GetHudObject(const std::string& file);
        static void TestObjects(const std::optional<std::string>& filename,
            std::int32_t pInitial, std::int32_t pStart, std::int32_t pTimer,
            std::int32_t pTarget, bool exportImages = false);
        static void TestLayers(bool exportScreens = false, bool exportChars = false);
        static void Nop();

    private:
        static const std::int32_t _layerHeaderSize;
        static const std::int32_t _scrDatInfoSize;
        using ObjectDimensions = std::array<std::array<std::pair<std::int32_t, std::int32_t>, 4>, 3>;
        static const ObjectDimensions _objectDimensions;
        static const std::int32_t _objHeaderSize;
        static const std::int32_t _animParamSize;
        static const std::int32_t _oamAttrSize;

        struct ScreenData
        {
            std::int32_t CharacterId = 0;
            bool FlipHorizontal = false;
            bool FlipVertical = false;
            std::int32_t PaletteId = 0;

            explicit ScreenData(std::uint16_t data);
        };

        static std::pair<std::int32_t, ReadOnlyList<std::uint16_t>> CharMapToTexture(
            const std::vector<std::uint8_t>& bytes, std::int32_t startX, std::int32_t startY,
            std::int32_t tilesX, std::int32_t tilesY, Scene& scene,
            ReadOnlyList<std::uint16_t> paletteOverride, std::int32_t paletteId);
        static void TestAnimation(ReadOnlyList<UiAnimParams> animParams,
            std::int32_t pInitial, std::int32_t pStart, std::int32_t pTimer,
            std::int32_t pTarget);
    };

    class RulesInfo
    {
    public:
        using IntArray = std::shared_ptr<std::vector<std::int32_t>>;

        RulesInfo(std::int32_t count, IntArray messageIds, IntArray offsets);

        [[nodiscard]] std::int32_t Count() const noexcept;
        [[nodiscard]] const IntArray& MessageIds() const noexcept;
        [[nodiscard]] const IntArray& Offsets() const noexcept;

    private:
        const std::int32_t _count;
        const IntArray _messageIds;
        const IntArray _offsets;
    };

    class HudObjects
    {
    public:
        const std::string Helmet;
        const std::string HelmetDrop;
        const std::string Visor;
        const std::string ScanVisor;
        const std::string HealthBarA;
        const std::string HealthBarB;
        const std::optional<std::string> EnergyTanks;
        const std::string WeaponIcon;
        const std::string DoubleDamage;
        const std::string Cloaking;
        const std::string PrimeHunter;
        const std::string AmmoBar;
        const std::string Reticle;
        const std::string SniperReticle;
        const std::string WeaponSelect;
        const std::string SelectIcon;
        const std::string SelectBox;
        const std::string DamageBar;
        const std::int32_t HealthMainPosX;
        const std::int32_t HealthSubPosX;
        const std::int32_t HealthSubPosY;
        const std::int32_t HealthOffsetY;
        const std::int32_t HealthOffsetYAlt;
        const std::int32_t HealthMainPosY;
        const std::int32_t AmmoBarPosX;
        const std::int32_t AmmoBarPosY;
        const std::int32_t WeaponIconPosX;
        const std::int32_t WeaponIconPosY;
        const std::int32_t EnemyHealthPosX;
        const std::int32_t EnemyHealthPosY;
        const std::int32_t EnemyHealthTextPosX;
        const std::int32_t EnemyHealthTextPosY;
        const std::int32_t ScorePosX;
        const std::int32_t ScorePosY;
        const Align ScoreAlign;
        const std::int32_t OctolithPosX;
        const std::int32_t OctolithPosY;
        const std::int32_t PrimePosX;
        const std::int32_t PrimePosY;
        const std::int32_t PrimeTextPosX;
        const std::int32_t PrimeTextPosY;
        const Align PrimeAlign;
        const std::int32_t NodeBonusPosX;
        const std::int32_t NodeBonusPosY;
        const std::int32_t EnemyBonusPosX;
        const std::int32_t EnemyBonusPosY;
        const std::int32_t NodeIconPosX;
        const std::int32_t NodeIconPosY;
        const std::int32_t NodeTextPosX;
        const std::int32_t NodeTextPosY;
        const std::int32_t DblDmgPosX;
        const std::int32_t DblDmgPosY;
        const std::int32_t DblDmgTextPosX;
        const std::int32_t DblDmgTextPosY;
        const Align DblDmgAlign;
        const std::int32_t CloakPosX;
        const std::int32_t CloakPosY;
        const std::int32_t CloakTextPosX;
        const std::int32_t CloakTextPosY;
        const Align CloakAlign;

        HudObjects(std::string helmet, std::string helmetDrop, std::string visor,
            std::string scanVisor, std::string healthBarA, std::string healthBarB,
            std::optional<std::string> energyTanks, std::string weaponIcon,
            std::string doubleDamage, std::string cloaking, std::string primeHunter,
            std::string ammoBar, std::string reticle, std::string sniperReticle,
            std::string weaponSelect, std::string selectIcon, std::string selectBox,
            std::string damageBar, std::int32_t healthMainPosX,
            std::int32_t healthMainPosY, std::int32_t healthSubPosX,
            std::int32_t healthSubPosY, std::int32_t healthOffsetY,
            std::int32_t healthOffsetYAlt, std::int32_t ammoBarPosX,
            std::int32_t ammoBarPosY, std::int32_t weaponIconPosX,
            std::int32_t weaponIconPosY, std::int32_t enemyHealthPosX,
            std::int32_t enemyHealthPosY, std::int32_t enemyHealthTextPosX,
            std::int32_t enemyHealthTextPosY, std::int32_t scorePosX,
            std::int32_t scorePosY, Align scoreAlign, std::int32_t octolithPosX,
            std::int32_t octolithPosY, std::int32_t primePosX,
            std::int32_t primePosY, std::int32_t primeTextPosX,
            std::int32_t primeTextPosY, Align primeAlign,
            std::int32_t nodeBonusPosX, std::int32_t nodeBonusPosY,
            std::int32_t enemyBonusPosX, std::int32_t enemyBonusPosY,
            std::int32_t nodeIconPosX, std::int32_t nodeIconPosY,
            std::int32_t nodeTextPosX, std::int32_t nodeTextPosY,
            std::int32_t dblDmgPosX, std::int32_t dblDmgPosY,
            std::int32_t dblDmgTextPosX, std::int32_t dblDmgTextPosY,
            Align dblDmgAlign, std::int32_t cloakPosX, std::int32_t cloakPosY,
            std::int32_t cloakTextPosX, std::int32_t cloakTextPosY, Align cloakAlign);
    };

    class HudElements final
    {
    public:
        static const std::string IceLayer;
        static const std::string Boost;
        static const std::string Bombs;
        static const std::string Stars;
        static const std::string Octolith;
        static const std::string NodesOG;
        static const std::string NodesRB;
        static const std::string SystemLoad;
        static const std::string MessageBox;
        static const std::string MessageSpacer;
        static const std::string MapScan;
        static const std::string DialogButton;
        static const std::string DialogArrow;
        static const std::string DialogCrystal;
        static const std::string DialogPickup;
        static const std::string DialogFrame;
        static const std::string MapPortal;
        static const std::string MapOctolith;
        static const std::string MapLostOctolith;
        static const std::string MapLegendDoors;
        static const std::string MapLegendOther;
        static const std::string MapQuit;
        static const ReadOnlyList<std::string> Hunters;
        static const ReadOnlyList<std::string> MapDots;
        static ReadOnlyList<std::shared_ptr<MphRead::Hud::RulesInfo>> RulesInfo;
        static const std::string ScanCorner;
        static const std::string ScanCornerSmall;
        static const std::string ScanLineHoriz;
        static const std::string ScanLineVert;
        static ReadOnlyList<std::string> ScanIcons;
        static ReadOnlyList<std::shared_ptr<HudObjects>> HunterObjects;
        static const std::shared_ptr<HudMeter> EnemyHealthbar;
        static const std::shared_ptr<HudMeter> NodeProgressBar;
        static const ReadOnlyList<std::shared_ptr<HudMeter>> MainHealthbars;
        static const ReadOnlyList<std::shared_ptr<HudMeter>> SubHealthbars;
        static const ReadOnlyList<std::shared_ptr<HudMeter>> AmmoBars;

        HudElements() = delete;
    };
}
