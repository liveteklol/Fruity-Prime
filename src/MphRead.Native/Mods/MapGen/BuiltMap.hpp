#pragma once

#include "../../Formats/Enums.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace OpenTK::Mathematics
{
    struct Vector2;
    struct Vector3;
}

namespace MphRead
{
    template <typename T>
    class ManagedArray;

    namespace Editor
    {
        class EntityEditorBase;
    }

}

namespace MphRead::Mods::MapGen
{
    class MapDefinition;
    class MapBuilder;
    class MapPacker;
    class Q3Import;
    class BuiltFace;

    // A one-way route the walking graph cannot see: a jump pad to where it
    // lands, a teleporter to where it sends you. The bots' node data carries
    // it as an edge from the pad to the landing and not back.
    struct BotLink
    {
        float From[3]{};
        float To[3]{};
    };

    class BuiltMap
    {
    public:
        std::vector<BotLink> Links{};
        // The light layer's pages (Q3Import::LightLayer), 128 by 128, each
        // luxel how much it darkens what is under it, 0 none to 31 black;
        // their materials follow the texture pack's.
        std::vector<std::vector<std::uint8_t>> LightPages{};
        // Each Quake 3 mover's brushes, by brush model: a collision of its
        // own that moves with it (MapPacker, Q3MoverEntity).
        std::map<std::int32_t, std::vector<BuiltFace*>> MoverSolid{};

        explicit BuiltMap(MapDefinition* definition) noexcept;
        virtual ~BuiltMap();

        BuiltMap(const BuiltMap&) = delete;
        BuiltMap& operator=(const BuiltMap&) = delete;
        BuiltMap(BuiltMap&&) = delete;
        BuiltMap& operator=(BuiltMap&&) = delete;

        [[nodiscard]] MapDefinition* Definition() const noexcept;
        [[nodiscard]] std::vector<BuiltFace*>& Faces() noexcept;
        [[nodiscard]] std::vector<BuiltFace*>& Solid() noexcept;
        [[nodiscard]] std::vector<Editor::EntityEditorBase*>& Entities() noexcept;

    private:
        friend class MapBuilder;
        friend class MapPacker;
        friend class Q3Import;

        [[nodiscard]] BuiltFace* OwnFace(std::unique_ptr<BuiltFace> face);
        [[nodiscard]] Editor::EntityEditorBase* OwnEntity(
            std::unique_ptr<Editor::EntityEditorBase> entity);

        std::shared_ptr<MapDefinition> _definitionOwner{};
        MapDefinition* const _definition;
        std::vector<std::unique_ptr<BuiltFace>> _ownedFaces{};
        std::vector<std::unique_ptr<Editor::EntityEditorBase>> _ownedEntities{};
        std::vector<BuiltFace*> _faces{};
        std::vector<BuiltFace*> _solid{};
        std::vector<Editor::EntityEditorBase*> _entities{};
    };

    class BuiltFace
    {
    public:
        BuiltFace(
            std::unique_ptr<MphRead::ManagedArray<OpenTK::Mathematics::Vector3>> points,
            std::unique_ptr<MphRead::ManagedArray<OpenTK::Mathematics::Vector2>> texcoords,
            OpenTK::Mathematics::Vector3 normal,
            std::int32_t material,
            float shade) noexcept;
        virtual ~BuiltFace();

        BuiltFace(const BuiltFace&) = delete;
        BuiltFace& operator=(const BuiltFace&) = delete;
        BuiltFace(BuiltFace&&) = delete;
        BuiltFace& operator=(BuiltFace&&) = delete;

        [[nodiscard]] MphRead::ManagedArray<OpenTK::Mathematics::Vector3>* Points() const noexcept;
        [[nodiscard]] MphRead::ManagedArray<OpenTK::Mathematics::Vector2>* Texcoords() const noexcept;
        [[nodiscard]] OpenTK::Mathematics::Vector3 Normal() const noexcept;
        [[nodiscard]] std::int32_t Material() const noexcept;
        [[nodiscard]] float Shade() const noexcept;
        [[nodiscard]] bool Damaging() const noexcept;
        void Damaging(bool value) noexcept;
        [[nodiscard]] MphRead::Terrain Terrain() const noexcept;
        void Terrain(MphRead::Terrain value) noexcept;

        // The rest of what the collision format holds per face. Nothing in
        // the importers sets these; a hand-edited OBJ does (CollisionObj).
        // Sky: drawn sky, which is never collision, so the check that every
        // drawn surface has something solid behind it skips it.
        bool Sky = false;
        // Overlay: the level's light over a face already drawn, a decal
        // (Q3Import::LightLayer); never collision either.
        bool Overlay = false;
        // A Quake 3 mover's brush model (a door, a bobbing platform): drawn in
        // a node of its own, q3model<Mover>, which the room moves
        // (Q3Movers); -1 for the level itself.
        std::int32_t Mover = -1;
        // The light at each corner, red green blue, 1 the texture as painted;
        // none for one shade over the whole face.
        std::vector<OpenTK::Mathematics::Vector3> Colors{};
        std::int32_t Slipperiness = 0;
        bool ReflectBeams = false;
        bool IgnorePlayers = false;
        bool IgnoreBeams = false;
        bool IgnoreScan = false;

    private:
        std::unique_ptr<MphRead::ManagedArray<OpenTK::Mathematics::Vector3>> _points;
        std::unique_ptr<MphRead::ManagedArray<OpenTK::Mathematics::Vector2>> _texcoords;
        const float _normalX;
        const float _normalY;
        const float _normalZ;
        const std::int32_t _material;
        const float _shade;
        bool _damaging = false;
        MphRead::Terrain _terrain = MphRead::Terrain::Metal;
    };
}
