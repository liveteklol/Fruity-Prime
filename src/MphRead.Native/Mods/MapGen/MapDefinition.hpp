#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::MapGen
{
    class MapDefinitionJson;
    class MapTexturePack;
    class MapImport;
    class MapPreview;
    class MapCollision;
    class MapMaterial;
    class MapBrush;
    class MapSpawn;
    class MapJumpPad;
    class MapItem;
    class MapTeleporter;

    class MapDefinition : public std::enable_shared_from_this<MapDefinition>
    {
    public:
        using MaterialList = std::vector<std::shared_ptr<MapMaterial>>;
        using BrushList = std::vector<std::shared_ptr<MapBrush>>;
        using SpawnList = std::vector<std::shared_ptr<MapSpawn>>;
        using JumpPadList = std::vector<std::shared_ptr<MapJumpPad>>;
        using ItemList = std::vector<std::shared_ptr<MapItem>>;
        using TeleporterList = std::vector<std::shared_ptr<MapTeleporter>>;

        MapDefinition();
        ~MapDefinition() = default;
        MapDefinition(const MapDefinition&) = delete;
        MapDefinition& operator=(const MapDefinition&) = delete;
        MapDefinition(MapDefinition&&) = delete;
        MapDefinition& operator=(MapDefinition&&) = delete;

        [[nodiscard]] const std::string& Name() const;
        void Name(std::string value);
        void Name(std::nullptr_t) noexcept;

        [[nodiscard]] const std::optional<std::string>& InGameName() const noexcept;
        void InGameName(std::optional<std::string> value) noexcept;

        [[nodiscard]] const std::string& TextureSource() const;
        void TextureSource(std::string value);
        void TextureSource(std::nullptr_t) noexcept;

        [[nodiscard]] std::int32_t ScaleFactor() const noexcept;
        void ScaleFactor(std::int32_t value) noexcept;
        [[nodiscard]] float KillHeight() const noexcept;
        void KillHeight(float value) noexcept;
        [[nodiscard]] float FarClip() const noexcept;
        void FarClip(float value) noexcept;
        [[nodiscard]] bool FogEnabled() const noexcept;
        void FogEnabled(bool value) noexcept;

        [[nodiscard]] std::vector<std::int32_t>* FogColor() noexcept;
        [[nodiscard]] const std::vector<std::int32_t>* FogColor() const noexcept;
        void FogColor(std::shared_ptr<std::vector<std::int32_t>> value) noexcept;
        [[nodiscard]] std::int32_t FogSlope() const noexcept;
        void FogSlope(std::int32_t value) noexcept;
        [[nodiscard]] std::int32_t FogOffset() const noexcept;
        void FogOffset(std::int32_t value) noexcept;

        [[nodiscard]] std::vector<std::int32_t>* Light1Color() noexcept;
        [[nodiscard]] const std::vector<std::int32_t>* Light1Color() const noexcept;
        void Light1Color(std::shared_ptr<std::vector<std::int32_t>> value) noexcept;
        [[nodiscard]] std::vector<float>* Light1Vector() noexcept;
        [[nodiscard]] const std::vector<float>* Light1Vector() const noexcept;
        void Light1Vector(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] std::vector<std::int32_t>* Light2Color() noexcept;
        [[nodiscard]] const std::vector<std::int32_t>* Light2Color() const noexcept;
        void Light2Color(std::shared_ptr<std::vector<std::int32_t>> value) noexcept;
        [[nodiscard]] std::vector<float>* Light2Vector() noexcept;
        [[nodiscard]] const std::vector<float>* Light2Vector() const noexcept;
        void Light2Vector(std::shared_ptr<std::vector<float>> value) noexcept;

        [[nodiscard]] std::uint32_t BattleTimeLimit() const noexcept;
        void BattleTimeLimit(std::uint32_t value) noexcept;
        [[nodiscard]] std::int16_t PointLimit() const noexcept;
        void PointLimit(std::int16_t value) noexcept;

        [[nodiscard]] MapImport* Import() noexcept;
        [[nodiscard]] const MapImport* Import() const noexcept;
        void Import(std::shared_ptr<MapImport> value) noexcept;
        // Collision read from a Wavefront OBJ, replacing whatever the geometry
        // would have produced. See MapCollision.
        [[nodiscard]] MapCollision* Collision() noexcept;
        [[nodiscard]] const MapCollision* Collision() const noexcept;
        void Collision(std::shared_ptr<MapCollision> value) noexcept;
        [[nodiscard]] MapPreview* Preview() noexcept;
        [[nodiscard]] const MapPreview* Preview() const noexcept;
        void Preview(std::shared_ptr<MapPreview> value) noexcept;

        [[nodiscard]] MaterialList* Materials() noexcept;
        [[nodiscard]] const MaterialList* Materials() const noexcept;
        void Materials(std::shared_ptr<MaterialList> value) noexcept;
        [[nodiscard]] BrushList* Brushes() noexcept;
        [[nodiscard]] const BrushList* Brushes() const noexcept;
        void Brushes(std::shared_ptr<BrushList> value) noexcept;
        [[nodiscard]] SpawnList* Spawns() noexcept;
        [[nodiscard]] const SpawnList* Spawns() const noexcept;
        void Spawns(std::shared_ptr<SpawnList> value) noexcept;
        [[nodiscard]] JumpPadList* JumpPads() noexcept;
        [[nodiscard]] const JumpPadList* JumpPads() const noexcept;
        void JumpPads(std::shared_ptr<JumpPadList> value) noexcept;
        [[nodiscard]] ItemList* Items() noexcept;
        [[nodiscard]] const ItemList* Items() const noexcept;
        void Items(std::shared_ptr<ItemList> value) noexcept;
        [[nodiscard]] TeleporterList* Teleporters() noexcept;
        [[nodiscard]] const TeleporterList* Teleporters() const noexcept;
        void Teleporters(std::shared_ptr<TeleporterList> value) noexcept;

        [[nodiscard]] const std::optional<std::string>& BaseDirectory() const noexcept;
        void BaseDirectory(std::optional<std::string> value) noexcept;
        [[nodiscard]] const std::optional<std::string>& BundlePath() const noexcept;
        void BundlePath(std::optional<std::string> value) noexcept;
        [[nodiscard]] const std::optional<std::string>& SourcePath() const noexcept;
        void SourcePath(std::optional<std::string> value) noexcept;
        // Read from a Quake 3 level by Q3Convert::Translate rather than a
        // recipe; SourcePath is the level's archive.
        [[nodiscard]] bool Translated() const noexcept;
        void Translated(bool value) noexcept;
        // A translated level's portals: where each one's camera stands and
        // what it looks at, in world units, and its roll in degrees.
        struct Portal
        {
            std::array<float, 3> Camera{};
            std::array<float, 3> Aim{};
            float Roll = 0.0F;
            std::array<float, 3> Up{0.0F, 1.0F, 0.0F};
        };
        [[nodiscard]] const std::vector<Portal>& Portals() const noexcept;
        void Portals(std::vector<Portal> value) noexcept;

        [[nodiscard]] static std::shared_ptr<MapDefinition> Load(const std::string& path);
        void Save(const std::string& path) const;
        [[nodiscard]] std::string Serialize() const;

    private:
        friend class MapDefinitionJson;

        std::shared_ptr<std::string> _name;
        std::optional<std::string> _inGameName{};
        std::shared_ptr<std::string> _textureSource;
        std::int32_t _scaleFactor = 4;
        float _killHeight = -40.0F;
        float _farClip = 350.0F;
        bool _fogEnabled = true;
        std::shared_ptr<std::vector<std::int32_t>> _fogColor;
        std::int32_t _fogSlope = 5;
        std::int32_t _fogOffset = 65180;
        std::shared_ptr<std::vector<std::int32_t>> _light1Color;
        std::shared_ptr<std::vector<float>> _light1Vector;
        std::shared_ptr<std::vector<std::int32_t>> _light2Color;
        std::shared_ptr<std::vector<float>> _light2Vector;
        std::uint32_t _battleTimeLimit = 7U * 60U * 30U;
        std::int16_t _pointLimit = 7;
        std::shared_ptr<MapImport> _import{};
        std::shared_ptr<MapCollision> _collision{};
        std::shared_ptr<MapPreview> _preview{};
        std::shared_ptr<MaterialList> _materials;
        std::shared_ptr<BrushList> _brushes;
        std::shared_ptr<SpawnList> _spawns;
        std::shared_ptr<JumpPadList> _jumpPads;
        std::shared_ptr<ItemList> _items;
        std::shared_ptr<TeleporterList> _teleporters;
        std::optional<std::string> _baseDirectory{};
        std::optional<std::string> _bundlePath{};
        std::optional<std::string> _sourcePath{};
        bool _translated = false;
        std::vector<Portal> _portals{};
    };

    // Collision read from a Wavefront OBJ rather than derived from the
    // geometry. It replaces what the geometry produced: a converted level
    // carries collision nobody can ever reach, and adding could never delete it.
    class MapCollision
    {
    public:
        // The .obj, looked for beside the recipe, then in maps/, then beside
        // the game files.
        std::optional<std::string> Source{std::string{}};
        // Written with the exporter's --zup.
        bool ZUp = false;
        std::optional<std::string> BaseDirectory{};
        std::optional<std::string> BundlePath{};

        // The bytes, out of the bundle or off the disk, or nullopt.
        [[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadBytes() const;
        [[nodiscard]] std::optional<std::string> Resolve() const;

    private:
        [[nodiscard]] std::vector<std::string> Candidates() const;
    };

    class MapPreview
    {
    public:
        MapPreview();
        ~MapPreview() = default;
        MapPreview(const MapPreview&) = delete;
        MapPreview& operator=(const MapPreview&) = delete;
        MapPreview(MapPreview&&) = delete;
        MapPreview& operator=(MapPreview&&) = delete;

        [[nodiscard]] std::vector<float>* Position() noexcept;
        [[nodiscard]] const std::vector<float>* Position() const noexcept;
        void Position(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] std::vector<float>* Target() noexcept;
        [[nodiscard]] const std::vector<float>* Target() const noexcept;
        void Target(std::shared_ptr<std::vector<float>> value) noexcept;

    private:
        friend class MapDefinitionJson;
        std::shared_ptr<std::vector<float>> _position;
        std::shared_ptr<std::vector<float>> _target;
    };

    class MapImport
    {
    public:
        class ShaderMaterialDictionary final
        {
        public:
            using Value = std::pair<std::string, std::int32_t>;
            using const_iterator = std::vector<Value>::const_iterator;

            [[nodiscard]] std::int32_t Count() const noexcept;
            [[nodiscard]] bool TryGetValue(const std::string& key, std::int32_t& value) const noexcept;
            [[nodiscard]] std::vector<std::string> Keys() const;
            [[nodiscard]] std::int32_t& operator[](const std::string& key);
            [[nodiscard]] const std::int32_t& operator[](const std::string& key) const;
            [[nodiscard]] const_iterator begin() const noexcept;
            [[nodiscard]] const_iterator end() const noexcept;

        private:
            std::vector<Value> _values{};
        };

        MapImport();
        ~MapImport() = default;
        MapImport(const MapImport&) = delete;
        MapImport& operator=(const MapImport&) = delete;
        MapImport(MapImport&&) = delete;
        MapImport& operator=(MapImport&&) = delete;

        [[nodiscard]] const std::string& Source() const;
        void Source(std::string value);
        void Source(std::nullptr_t) noexcept;
        [[nodiscard]] const std::optional<std::string>& BundlePath() const noexcept;
        void BundlePath(std::optional<std::string> value) noexcept;
        [[nodiscard]] std::optional<std::string> Resolve() const;
        [[nodiscard]] const std::optional<std::string>& BaseDirectory() const noexcept;
        void BaseDirectory(std::optional<std::string> value) noexcept;
        [[nodiscard]] const std::optional<std::string>& MapName() const noexcept;
        void MapName(std::optional<std::string> value) noexcept;
        [[nodiscard]] float UnitsPerUnit() const noexcept;
        void UnitsPerUnit(float value) noexcept;
        [[nodiscard]] const std::optional<std::string>& Textures() const noexcept;
        void Textures(std::optional<std::string> value) noexcept;
        [[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadBundledTextures() const;
        [[nodiscard]] std::shared_ptr<MapTexturePack> LoadTexturePack() const;
        [[nodiscard]] std::optional<std::string> ResolveTextures() const;
        [[nodiscard]] ShaderMaterialDictionary* ShaderMaterials() noexcept;
        [[nodiscard]] const ShaderMaterialDictionary* ShaderMaterials() const noexcept;
        void ShaderMaterials(std::shared_ptr<ShaderMaterialDictionary> value) noexcept;
        [[nodiscard]] std::int32_t DefaultMaterial() const noexcept;
        void DefaultMaterial(std::int32_t value) noexcept;
        [[nodiscard]] float TexScale() const noexcept;
        void TexScale(float value) noexcept;
        [[nodiscard]] bool KeepSky() const noexcept;
        void KeepSky(bool value) noexcept;
        [[nodiscard]] bool KeepClip() const noexcept;
        void KeepClip(bool value) noexcept;
        [[nodiscard]] std::int32_t PatchLevel() const noexcept;
        void PatchLevel(std::int32_t value) noexcept;
        [[nodiscard]] bool KeepSpawns() const noexcept;
        void KeepSpawns(bool value) noexcept;
        // Take the level's own pickups as items, on top of the recipe's own.
        [[nodiscard]] bool KeepItems() const noexcept;
        void KeepItems(bool value) noexcept;
        // More archives to look for the level's images in, after the level's
        // own: the game's texture paks a level was built against and does not
        // carry. Resolved like Source; one that is not there is skipped.
        [[nodiscard]] const std::vector<std::string>& TexturePaks() const noexcept;
        void TexturePaks(std::vector<std::string> value) noexcept;
        [[nodiscard]] std::vector<std::string> ResolveTexturePaks() const;
        // Shader name -> the shader or image to paint it with instead.
        [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& TextureAliases() const noexcept;
        void TextureAliases(std::vector<std::pair<std::string, std::string>> value) noexcept;

    private:
        friend class MapDefinitionJson;

        [[nodiscard]] std::optional<std::string> ResolveCandidate(const std::string& name) const;

        std::shared_ptr<std::string> _source;
        std::optional<std::string> _bundlePath{};
        std::optional<std::string> _baseDirectory{};
        std::optional<std::string> _mapName{};
        float _unitsPerUnit = 28.0F;
        std::optional<std::string> _textures{};
        std::shared_ptr<ShaderMaterialDictionary> _shaderMaterials;
        std::int32_t _defaultMaterial = 0;
        float _texScale = 24.0F;
        bool _keepSky = false;
        bool _keepClip = true;
        std::int32_t _patchLevel = 3;
        bool _keepSpawns = true;
        bool _keepItems = true;
        std::vector<std::string> _texturePaks{};
        std::vector<std::pair<std::string, std::string>> _textureAliases{};
    };

    class MapMaterial
    {
    public:
        MapMaterial();
        ~MapMaterial() = default;
        MapMaterial(const MapMaterial&) = delete;
        MapMaterial& operator=(const MapMaterial&) = delete;
        MapMaterial(MapMaterial&&) = delete;
        MapMaterial& operator=(MapMaterial&&) = delete;

        [[nodiscard]] const std::string& Name() const;
        void Name(std::string value);
        void Name(std::nullptr_t) noexcept;
        [[nodiscard]] std::int32_t SourceMaterial() const noexcept;
        void SourceMaterial(std::int32_t value) noexcept;
        [[nodiscard]] float TexScale() const noexcept;
        void TexScale(float value) noexcept;

    private:
        friend class MapDefinitionJson;
        std::shared_ptr<std::string> _name;
        std::int32_t _sourceMaterial = 0;
        float _texScale = 16.0F;
    };

    class MapBrush
    {
    public:
        MapBrush();
        ~MapBrush() = default;
        MapBrush(const MapBrush&) = delete;
        MapBrush& operator=(const MapBrush&) = delete;
        MapBrush(MapBrush&&) = delete;
        MapBrush& operator=(MapBrush&&) = delete;

        [[nodiscard]] std::vector<float>* Min() noexcept;
        [[nodiscard]] const std::vector<float>* Min() const noexcept;
        void Min(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] std::vector<float>* Max() noexcept;
        [[nodiscard]] const std::vector<float>* Max() const noexcept;
        void Max(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] std::int32_t Material() const noexcept;
        void Material(std::int32_t value) noexcept;
        [[nodiscard]] float Shade() const noexcept;
        void Shade(float value) noexcept;
        [[nodiscard]] bool Solid() const noexcept;
        void Solid(bool value) noexcept;
        [[nodiscard]] bool Damaging() const noexcept;
        void Damaging(bool value) noexcept;
        // Stops a player and lets a shot through (the collision format's
        // IgnoreBeams), the way a Quake player-clip does.
        [[nodiscard]] bool NoBeams() const noexcept;
        void NoBeams(bool value) noexcept;
        // False: collision only, nothing drawn -- a pane of glass.
        [[nodiscard]] bool Visible() const noexcept;
        void Visible(bool value) noexcept;
        [[nodiscard]] const std::optional<std::string>& Terrain() const noexcept;
        void Terrain(std::optional<std::string> value) noexcept;

    private:
        friend class MapDefinitionJson;
        std::shared_ptr<std::vector<float>> _min;
        std::shared_ptr<std::vector<float>> _max;
        std::int32_t _material = 0;
        float _shade = 1.0F;
        bool _solid = true;
        bool _damaging = false;
        bool _noBeams = false;
        bool _visible = true;
        std::optional<std::string> _terrain{};
    };

    class MapSpawn
    {
    public:
        MapSpawn();
        ~MapSpawn() = default;
        MapSpawn(const MapSpawn&) = delete;
        MapSpawn& operator=(const MapSpawn&) = delete;
        MapSpawn(MapSpawn&&) = delete;
        MapSpawn& operator=(MapSpawn&&) = delete;

        [[nodiscard]] std::vector<float>* Position() noexcept;
        [[nodiscard]] const std::vector<float>* Position() const noexcept;
        void Position(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] float Yaw() const noexcept;
        void Yaw(float value) noexcept;

    private:
        friend class MapDefinitionJson;
        std::shared_ptr<std::vector<float>> _position;
        float _yaw = 0.0F;
    };

    class MapJumpPad
    {
    public:
        MapJumpPad();
        ~MapJumpPad() = default;
        MapJumpPad(const MapJumpPad&) = delete;
        MapJumpPad& operator=(const MapJumpPad&) = delete;
        MapJumpPad(MapJumpPad&&) = delete;
        MapJumpPad& operator=(MapJumpPad&&) = delete;

        [[nodiscard]] std::vector<float>* Position() noexcept;
        [[nodiscard]] const std::vector<float>* Position() const noexcept;
        void Position(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] std::vector<float>* Target() noexcept;
        [[nodiscard]] const std::vector<float>* Target() const noexcept;
        void Target(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] std::vector<float>* Vector() noexcept;
        [[nodiscard]] const std::vector<float>* Vector() const noexcept;
        void Vector(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] float Speed() const noexcept;
        void Speed(float value) noexcept;
        [[nodiscard]] std::vector<float>* Size() noexcept;
        [[nodiscard]] const std::vector<float>* Size() const noexcept;
        void Size(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] std::uint32_t ModelId() const noexcept;
        void ModelId(std::uint32_t value) noexcept;
        [[nodiscard]] std::uint16_t CooldownTime() const noexcept;
        void CooldownTime(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t ControlLockTime() const noexcept;
        void ControlLockTime(std::uint16_t value) noexcept;

        // Set by the Quake 3 importer only, never read from a recipe.
        // The trigger volume, minX minY minZ maxX maxY maxZ from Position,
        // in place of Size's box standing on it.
        [[nodiscard]] const std::vector<float>* Box() const noexcept;
        void Box(std::shared_ptr<std::vector<float>> value) noexcept;
        // JumpPadEntity::Q3Trigger and its float: 0 for a cartridge pad.
        // A teleporter (2) sends you to Target, leaving at Speed.
        [[nodiscard]] std::uint8_t Q3Kind() const noexcept;
        void Q3Kind(std::uint8_t value) noexcept;
        [[nodiscard]] float Q3Value() const noexcept;
        void Q3Value(float value) noexcept;
        [[nodiscard]] std::uint16_t Q3Unit() const noexcept;
        void Q3Unit(std::uint16_t value) noexcept;

    private:
        friend class MapDefinitionJson;
        std::shared_ptr<std::vector<float>> _position;
        std::shared_ptr<std::vector<float>> _target{};
        std::shared_ptr<std::vector<float>> _vector{};
        float _speed = 0.0F;
        std::shared_ptr<std::vector<float>> _size;
        std::uint32_t _modelId = 0;
        std::uint16_t _cooldownTime = 20;
        std::uint16_t _controlLockTime = 30;
        std::shared_ptr<std::vector<float>> _box{};
        std::uint8_t _q3Kind = 0;
        float _q3Value = 0.0F;
        std::uint16_t _q3Unit = 0;
    };

    class MapItem
    {
    public:
        MapItem();
        ~MapItem() = default;
        MapItem(const MapItem&) = delete;
        MapItem& operator=(const MapItem&) = delete;
        MapItem(MapItem&&) = delete;
        MapItem& operator=(MapItem&&) = delete;

        [[nodiscard]] std::vector<float>* Position() noexcept;
        [[nodiscard]] const std::vector<float>* Position() const noexcept;
        void Position(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] const std::string& Type() const;
        void Type(std::string value);
        void Type(std::nullptr_t) noexcept;
        [[nodiscard]] bool HasBase() const noexcept;
        void HasBase(bool value) noexcept;
        [[nodiscard]] std::uint16_t SpawnInterval() const noexcept;
        void SpawnInterval(std::uint16_t value) noexcept;

    private:
        friend class MapDefinitionJson;
        std::shared_ptr<std::vector<float>> _position;
        std::shared_ptr<std::string> _type;
        bool _hasBase = true;
        std::uint16_t _spawnInterval = 300;
    };

    // A pad that moves whoever steps on it to Target, facing Yaw, the way the
    // cartridge's multiplayer teleporters do. Position is the floor under it.
    class MapTeleporter
    {
    public:
        MapTeleporter() = default;
        ~MapTeleporter() = default;
        MapTeleporter(const MapTeleporter&) = delete;
        MapTeleporter& operator=(const MapTeleporter&) = delete;
        MapTeleporter(MapTeleporter&&) = delete;
        MapTeleporter& operator=(MapTeleporter&&) = delete;

        [[nodiscard]] std::vector<float>* Position() noexcept;
        [[nodiscard]] const std::vector<float>* Position() const noexcept;
        void Position(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] std::vector<float>* Target() noexcept;
        [[nodiscard]] const std::vector<float>* Target() const noexcept;
        void Target(std::shared_ptr<std::vector<float>> value) noexcept;
        [[nodiscard]] float Yaw() const noexcept;
        void Yaw(float value) noexcept;
        // The cartridge's teleporter model on the pad, or nothing drawn --
        // for a level that already draws its own.
        [[nodiscard]] bool Visible() const noexcept;
        void Visible(bool value) noexcept;

    private:
        friend class MapDefinitionJson;
        std::shared_ptr<std::vector<float>> _position;
        std::shared_ptr<std::vector<float>> _target;
        float _yaw = 0.0F;
        bool _visible = true;
    };
}
