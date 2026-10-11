#include "MapBuilder.hpp"

#include "../../Entities/TriggerVolumeEntity.hpp"
#include "../../Formats/EntityClass.hpp"
#include "../../Formats/Formats.hpp"
#include "../../Program.hpp"
#include "BuiltMap.hpp"
#include "MapDefinition.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::Length;

namespace
{
    using MphRead::ItemType;
    using MphRead::Terrain;
    using OpenTK::Mathematics::Vector2;
    using OpenTK::Mathematics::Vector3;

    [[noreturn]] void ArrayBounds()
    {
        throw System::IndexOutOfRangeException();
    }

    [[nodiscard]] float ArrayValue(const std::vector<float>* values, std::size_t index)
    {
        if (values == nullptr)
        {
            throw System::NullReferenceException();
        }
        if (index >= values->size())
        {
            ArrayBounds();
        }
        return (*values)[index];
    }

    [[nodiscard]] float ManagedAbs(float value) noexcept
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFU;
        return std::bit_cast<float>(bits);
    }

    [[nodiscard]] bool IsAsciiWhitespace(unsigned char value) noexcept
    {
        return value == 0x20U || (value >= 0x09U && value <= 0x0DU);
    }

    [[nodiscard]] std::size_t LeadingWhitespaceBytes(std::string_view value) noexcept
    {
        if (value.empty())
        {
            return 0;
        }
        const auto b0 = static_cast<unsigned char>(value[0]);
        if (IsAsciiWhitespace(b0))
        {
            return 1;
        }
        if (value.size() >= 2)
        {
            const auto b1 = static_cast<unsigned char>(value[1]);
            if (b0 == 0xC2U && (b1 == 0x85U || b1 == 0xA0U))
            {
                return 2;
            }
        }
        if (value.size() >= 3)
        {
            const auto b1 = static_cast<unsigned char>(value[1]);
            const auto b2 = static_cast<unsigned char>(value[2]);
            if ((b0 == 0xE1U && b1 == 0x9AU && b2 == 0x80U)
                || (b0 == 0xE2U && b1 == 0x80U
                    && ((b2 >= 0x80U && b2 <= 0x8AU)
                        || b2 == 0xA8U || b2 == 0xA9U || b2 == 0xAFU))
                || (b0 == 0xE2U && b1 == 0x81U && b2 == 0x9FU)
                || (b0 == 0xE3U && b1 == 0x80U && b2 == 0x80U))
            {
                return 3;
            }
        }
        return 0;
    }

    [[nodiscard]] std::size_t TrailingWhitespaceBytes(std::string_view value) noexcept
    {
        if (value.empty())
        {
            return 0;
        }
        const auto last = static_cast<unsigned char>(value.back());
        if (IsAsciiWhitespace(last))
        {
            return 1;
        }
        if (value.size() >= 2)
        {
            const auto b0 = static_cast<unsigned char>(value[value.size() - 2]);
            if (b0 == 0xC2U && (last == 0x85U || last == 0xA0U))
            {
                return 2;
            }
        }
        if (value.size() >= 3)
        {
            const auto b0 = static_cast<unsigned char>(value[value.size() - 3]);
            const auto b1 = static_cast<unsigned char>(value[value.size() - 2]);
            if ((b0 == 0xE1U && b1 == 0x9AU && last == 0x80U)
                || (b0 == 0xE2U && b1 == 0x80U
                    && ((last >= 0x80U && last <= 0x8AU)
                        || last == 0xA8U || last == 0xA9U || last == 0xAFU))
                || (b0 == 0xE2U && b1 == 0x81U && last == 0x9FU)
                || (b0 == 0xE3U && b1 == 0x80U && last == 0x80U))
            {
                return 3;
            }
        }
        return 0;
    }

    [[nodiscard]] bool TryParseItemType(
        const std::string* text, ItemType& result)
    {
        if (text == nullptr)
        {
            result = static_cast<ItemType>(0);
            return false;
        }
        return ::MphRead::TryParse(*text, true, result);
    }

    [[nodiscard]] bool TryParseTerrain(
        const std::optional<std::string>& text, Terrain& result)
    {
        if (!text)
        {
            result = static_cast<Terrain>(0);
            return false;
        }
        return ::MphRead::TryParse(*text, true, result);
    }

    [[nodiscard]] std::string JoinMultiplayerItems(
        const MphRead::Mods::MapGen::ItemTypeHashSet& values)
    {
        std::string result;
        bool first = true;
        for (const ItemType value : values)
        {
            if (!first)
            {
                result += ", ";
            }
            first = false;
            result += ::MphRead::ToString(value);
        }
        return result;
    }

    [[nodiscard]] std::optional<std::string> MapItemTypeText(
        MphRead::Mods::MapGen::MapItem* item)
    {
        if (item == nullptr)
        {
            throw System::NullReferenceException();
        }
        try
        {
            return item->Type();
        }
        catch (const std::runtime_error& exception)
        {
            if (std::string_view(exception.what())
                == "Object reference not set to an instance of an object.")
            {
                return std::nullopt;
            }
            throw;
        }
    }

    [[nodiscard]] std::int16_t TruncateToInt16(std::size_t value) noexcept
    {
        const std::uint16_t low = static_cast<std::uint16_t>(
            static_cast<std::uint64_t>(value) & 0xFFFFU);
        return std::bit_cast<std::int16_t>(low);
    }

    [[nodiscard]] std::int16_t PostIncrement(std::int16_t& value) noexcept
    {
        const std::int16_t old = value;
        const std::uint16_t bits = std::bit_cast<std::uint16_t>(value);
        value = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits + 1U));
        return old;
    }

    [[nodiscard]] const std::shared_ptr<std::string>& RmMainString()
    {
        static const auto value = std::make_shared<std::string>("rmMain");
        return value;
    }

}

namespace MphRead::Mods::MapGen
{
    ItemTypeHashSet::const_iterator::const_iterator(
        const ItemTypeHashSet* owner, std::size_t index) noexcept
        : _owner(owner), _index(index)
    {
        SkipFree();
    }

    void ItemTypeHashSet::const_iterator::SkipFree() noexcept
    {
        if (_owner == nullptr)
        {
            return;
        }
        while (_index < _owner->_slots.size()
            && !_owner->_slots[_index].Occupied)
        {
            ++_index;
        }
    }

    ItemType ItemTypeHashSet::const_iterator::operator*() const noexcept
    {
        return _owner->_slots[_index].Value;
    }

    ItemTypeHashSet::const_iterator& ItemTypeHashSet::const_iterator::operator++() noexcept
    {
        ++_index;
        SkipFree();
        return *this;
    }

    void ItemTypeHashSet::const_iterator::operator++(int) noexcept
    {
        ++*this;
    }

    ItemTypeHashSet::ItemTypeHashSet(std::initializer_list<ItemType> values)
    {
        for (const ItemType value : values)
        {
            (void)Add(value);
        }
    }

    bool ItemTypeHashSet::Add(ItemType value)
    {
        if (Contains(value))
        {
            return false;
        }

        if (_freeList >= 0)
        {
            const std::int32_t index = _freeList;
            Slot& slot = _slots[static_cast<std::size_t>(index)];
            _freeList = slot.NextFree;
            slot.Value = value;
            slot.Occupied = true;
            slot.NextFree = -1;
        }
        else
        {
            _slots.push_back(Slot{value, true, -1});
        }
        ++_count;
        return true;
    }

    bool ItemTypeHashSet::Remove(ItemType value) noexcept
    {
        for (std::size_t i = 0; i < _slots.size(); ++i)
        {
            Slot& slot = _slots[i];
            if (slot.Occupied && slot.Value == value)
            {
                slot.Occupied = false;
                slot.NextFree = _freeList;
                _freeList = static_cast<std::int32_t>(i);
                --_count;
                return true;
            }
        }
        return false;
    }

    bool ItemTypeHashSet::Contains(ItemType value) const noexcept
    {
        for (const Slot& slot : _slots)
        {
            if (slot.Occupied && slot.Value == value)
            {
                return true;
            }
        }
        return false;
    }

    void ItemTypeHashSet::Clear() noexcept
    {
        _slots.clear();
        _freeList = -1;
        _count = 0;
    }

    std::int32_t ItemTypeHashSet::Count() const noexcept
    {
        return _count;
    }

    ItemTypeHashSet::const_iterator ItemTypeHashSet::begin() const noexcept
    {
        return const_iterator(this, 0);
    }

    ItemTypeHashSet::const_iterator ItemTypeHashSet::end() const noexcept
    {
        return const_iterator(this, _slots.size());
    }

    ItemTypeHashSet MapBuilder::MultiplayerItems{
        ItemType::HealthSmall,
        ItemType::HealthMedium,
        ItemType::HealthBig,
        ItemType::UASmall,
        ItemType::UABig,
        ItemType::MissileSmall,
        ItemType::MissileBig,
        // No powerups: Double Damage, Cloak and Deathalt are refused.
        ItemType::VoltDriver,
        ItemType::Battlehammer,
        ItemType::Imperialist,
        ItemType::Judicator,
        ItemType::Magmaul,
        ItemType::ShockCoil,
        ItemType::OmegaCannon,
        ItemType::AffinityWeapon
    };

    const std::array<float, 6> MapBuilder::_faceShades{
        1.0F, 0.55F, 0.82F, 0.82F, 0.74F, 0.74F
    };

    std::shared_ptr<BuiltMap> MapBuilder::Build(MapDefinition* def)
    {
        auto map = std::make_shared<BuiltMap>(def);

        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }
        MapDefinition::BrushList* brushes = def->Brushes();
        if (brushes == nullptr)
        {
            throw System::NullReferenceException();
        }
        for (const std::shared_ptr<MapBrush>& brush : *brushes)
        {
            AddBrush(map.get(), def, brush.get());
        }
        AddEntities(map.get(), def);
        return map;
    }

    void MapBuilder::AddBrush(BuiltMap* map, MapDefinition* def, MapBrush* brush)
    {
        if (brush == nullptr)
        {
            throw System::NullReferenceException();
        }

        const float x0Left = ArrayValue(brush->Min(), 0);
        const float x0Right = ArrayValue(brush->Max(), 0);
        const float x0 = MathMin(x0Left, x0Right);
        const float y0Left = ArrayValue(brush->Min(), 1);
        const float y0Right = ArrayValue(brush->Max(), 1);
        const float y0 = MathMin(y0Left, y0Right);
        const float z0Left = ArrayValue(brush->Min(), 2);
        const float z0Right = ArrayValue(brush->Max(), 2);
        const float z0 = MathMin(z0Left, z0Right);
        const float x1Left = ArrayValue(brush->Min(), 0);
        const float x1Right = ArrayValue(brush->Max(), 0);
        const float x1 = MathMax(x1Left, x1Right);
        const float y1Left = ArrayValue(brush->Min(), 1);
        const float y1Right = ArrayValue(brush->Max(), 1);
        const float y1 = MathMax(y1Left, y1Right);
        const float z1Left = ArrayValue(brush->Min(), 2);
        const float z1Right = ArrayValue(brush->Max(), 2);
        const float z1 = MathMax(z1Left, z1Right);

        const Vector3 normals[6] = {
            Vector3(0.0F, 1.0F, 0.0F),
            Vector3(0.0F, -1.0F, 0.0F),
            Vector3(1.0F, 0.0F, 0.0F),
            Vector3(-1.0F, 0.0F, 0.0F),
            Vector3(0.0F, 0.0F, 1.0F),
            Vector3(0.0F, 0.0F, -1.0F)
        };
        const Vector3 sidePoints[6][4] = {
            {
                Vector3(x0, y1, z1), Vector3(x1, y1, z1),
                Vector3(x1, y1, z0), Vector3(x0, y1, z0)
            },
            {
                Vector3(x0, y0, z0), Vector3(x1, y0, z0),
                Vector3(x1, y0, z1), Vector3(x0, y0, z1)
            },
            {
                Vector3(x1, y0, z1), Vector3(x1, y0, z0),
                Vector3(x1, y1, z0), Vector3(x1, y1, z1)
            },
            {
                Vector3(x0, y0, z0), Vector3(x0, y0, z1),
                Vector3(x0, y1, z1), Vector3(x0, y1, z0)
            },
            {
                Vector3(x0, y0, z1), Vector3(x1, y0, z1),
                Vector3(x1, y1, z1), Vector3(x0, y1, z1)
            },
            {
                Vector3(x1, y0, z0), Vector3(x0, y0, z0),
                Vector3(x0, y1, z0), Vector3(x1, y1, z0)
            }
        };

        float texScale = 16.0F;
        if (brush->Material() >= 0)
        {
            const std::int32_t materialForCount = brush->Material();
            if (def == nullptr)
            {
                throw System::NullReferenceException();
            }
            MapDefinition::MaterialList* materialsForCount = def->Materials();
            if (materialsForCount == nullptr)
            {
                throw System::NullReferenceException();
            }
            if (static_cast<std::int64_t>(materialForCount)
                < static_cast<std::int64_t>(materialsForCount->size()))
            {
                MapDefinition::MaterialList* materialsForIndex = def->Materials();
                const std::int32_t materialForIndex = brush->Material();
                if (materialsForIndex == nullptr)
                {
                    throw System::NullReferenceException();
                }
                MapMaterial* material = &RequireReference(
                    materialsForIndex->at(static_cast<std::size_t>(materialForIndex)));
                texScale = material->TexScale();
            }
        }

        const Vector3 origin(x0, y0, z0);
        Terrain terrain = Terrain::Metal;
        Terrain parsed = Terrain::Metal;
        if (brush->Terrain().has_value())
        {
            const std::optional<std::string> terrainText = brush->Terrain();
            if (TryParseTerrain(terrainText, parsed))
            {
                terrain = parsed;
            }
        }

        for (std::size_t i = 0; i < 6; ++i)
        {
            auto points = std::make_unique<MphRead::ManagedArray<Vector3>>(4);
            auto texcoords = std::make_unique<MphRead::ManagedArray<Vector2>>(4);
            for (std::size_t j = 0; j < 4; ++j)
            {
                (*points)[j] = sidePoints[i][j];
                (*texcoords)[j] = Project(sidePoints[i][j], normals[i], origin, texScale);
            }

            const std::int32_t faceMaterial = brush->Material();
            const float faceShade = _faceShades[i] * brush->Shade();
            auto face = std::make_unique<BuiltFace>(
                std::move(points),
                std::move(texcoords),
                normals[i],
                faceMaterial,
                faceShade);
            face->Damaging(brush->Damaging());
            face->Terrain(terrain);
            face->IgnoreBeams = brush->NoBeams();

            if (map == nullptr)
            {
                throw System::NullReferenceException();
            }
            BuiltFace* ownedFace = map->OwnFace(std::move(face));
            if (brush->Visible())
            {
                map->Faces().push_back(ownedFace);
            }
            if (brush->Solid())
            {
                map->Solid().push_back(ownedFace);
            }
        }
    }

    Vector2 MapBuilder::Project(
        Vector3 point, Vector3 normal, Vector3 origin, float texScale) noexcept
    {
        const float ax = ManagedAbs(normal.X);
        const float ay = ManagedAbs(normal.Y);
        const float az = ManagedAbs(normal.Z);
        if (ay > ax && ay >= az)
        {
            return Vector2(
                (point.X - origin.X) * texScale,
                (point.Z - origin.Z) * texScale);
        }
        if (ax >= az)
        {
            return Vector2(
                (point.Z - origin.Z) * texScale,
                (origin.Y - point.Y) * texScale);
        }
        return Vector2(
            (point.X - origin.X) * texScale,
            (origin.Y - point.Y) * texScale);
    }

    void MapBuilder::AddEntities(BuiltMap* map, MapDefinition* def)
    {
        if (map == nullptr)
        {
            throw System::NullReferenceException();
        }
        std::int16_t id = TruncateToInt16(map->Entities().size());

        if (def == nullptr)
        {
            throw System::NullReferenceException();
        }
        MapDefinition::SpawnList* spawns = def->Spawns();
        if (spawns == nullptr)
        {
            throw System::NullReferenceException();
        }
        for (const std::shared_ptr<MapSpawn>& spawnValue : *spawns)
        {
            MapSpawn* spawn = &RequireReference(spawnValue);
            constexpr float DegreesToRadians = 0.017453292519943295769F;
            const float yaw = spawn->Yaw() * DegreesToRadians;

            auto entity = std::make_unique<Editor::PlayerSpawnEntityEditor>();
            entity->Id = PostIncrement(id);
            entity->LayerMask = 0xFFFFU;
            entity->Position = ToVector(spawn->Position());
            entity->Up = Vector3(0.0F, 1.0F, 0.0F);
            entity->Facing = Vector3(std::sin(yaw), 0.0F, std::cos(yaw)).Normalized();
            entity->NodeName = RmMainString();
            entity->Active = true;
            entity->Availability = 0;
            entity->TeamIndex = -1;
            map->Entities().push_back(map->OwnEntity(std::move(entity)));
        }

        MapDefinition::JumpPadList* jumpPads = def->JumpPads();
        if (jumpPads == nullptr)
        {
            throw System::NullReferenceException();
        }
        for (const std::shared_ptr<MapJumpPad>& padValue : *jumpPads)
        {
            MapJumpPad* pad = &RequireReference(padValue);
            // A Quake 3 teleporter keeps where it sends you in the beam.
            const auto [beam, speed] = pad->Q3Kind() == 2
                ? std::pair<Vector3, float>(ToVector(pad->Target()), pad->Speed())
                : SolveJumpPad(pad);

            auto entity = std::make_unique<Editor::JumpPadEntityEditor>();
            entity->Id = PostIncrement(id);
            entity->LayerMask = 0xFFFFU;
            entity->Position = ToVector(pad->Position());
            entity->Up = Vector3(0.0F, 1.0F, 0.0F);
            entity->Facing = Vector3(0.0F, 0.0F, 1.0F);
            entity->NodeName = RmMainString();
            entity->ParentId = -1;
            entity->Volume = pad->Box() != nullptr ? MakeBox(*pad->Box()) : MakeBox(pad->Size());
            entity->BeamVector = beam;
            entity->Speed = speed;
            entity->ControlLockTime = pad->ControlLockTime();
            entity->CooldownTime = pad->CooldownTime();
            entity->Active = true;
            entity->ModelId = pad->ModelId();
            entity->Padding81 = pad->Q3Kind();
            entity->Padding82 = pad->Q3Unit();
            entity->Unused28 = std::bit_cast<std::uint32_t>(pad->Q3Value());
            entity->BeamType = 0;
            entity->TriggerFlags = Entities::TriggerFlags::PlayerBiped
                | Entities::TriggerFlags::PlayerAlt
                | Entities::TriggerFlags::IncludeBots;
            map->Entities().push_back(map->OwnEntity(std::move(entity)));

            if (pad->Target() != nullptr)
            {
                const Vector3 from = ToVector(pad->Position());
                const Vector3 to = ToVector(pad->Target());
                map->Links.push_back(BotLink{{from.X, from.Y, from.Z}, {to.X, to.Y, to.Z}});
            }
        }

        // The cartridge's multiplayer teleporter: ArtifactId 8 and up is the
        // small pad with no artifact slots, and with no room named it moves
        // the player within this one, to TargetPosition, facing the way the
        // teleporter itself faces.
        if (const MapDefinition::TeleporterList* teleporters = def->Teleporters())
        {
            for (const std::shared_ptr<MapTeleporter>& teleporterValue : *teleporters)
            {
                MapTeleporter* teleporter = &RequireReference(teleporterValue);
                if (teleporter->Target() == nullptr)
                {
                    throw MphRead::ProgramException("A teleporter needs a target.");
                }
                constexpr float DegreesToRadians = 0.017453292519943295769F;
                const float yaw = teleporter->Yaw() * DegreesToRadians;

                auto entity = std::make_unique<Editor::TeleporterEntityEditor>();
                entity->Id = PostIncrement(id);
                entity->LayerMask = 0xFFFFU;
                entity->Position = ToVector(teleporter->Position());
                entity->Up = Vector3(0.0F, 1.0F, 0.0F);
                entity->Facing = Vector3(std::sin(yaw), 0.0F, std::cos(yaw)).Normalized();
                entity->NodeName = RmMainString();
                entity->LoadIndex = 0;
                entity->TargetIndex = 0;
                entity->ArtifactId = 0xFF;
                entity->Active = true;
                entity->Invisible = !teleporter->Visible();
                entity->TargetPosition = ToVector(teleporter->Target());
                entity->TeleporterNodeName = RmMainString();
                map->Entities().push_back(map->OwnEntity(std::move(entity)));

                const Vector3 from = ToVector(teleporter->Position());
                const Vector3 to = ToVector(teleporter->Target());
                map->Links.push_back(BotLink{{from.X, from.Y, from.Z}, {to.X, to.Y, to.Z}});
            }
        }

        MapDefinition::ItemList* items = def->Items();
        if (items == nullptr)
        {
            throw System::NullReferenceException();
        }
        for (const std::shared_ptr<MapItem>& itemValue : *items)
        {
            MapItem* item = &RequireReference(itemValue);
            const std::optional<std::string> typeText = MapItemTypeText(item);
            ItemType itemType{};
            if (!TryParseItemType(
                    typeText ? std::addressof(*typeText) : nullptr, itemType))
            {
                const std::optional<std::string> messageType = MapItemTypeText(item);
                throw MphRead::ProgramException(
                    "Unknown item type " + messageType.value_or(std::string()) + ".");
            }
            if (!MultiplayerItems.Contains(itemType))
            {
                const std::optional<std::string> messageType = MapItemTypeText(item);
                const bool powerup = itemType == ItemType::DoubleDamage
                    || itemType == ItemType::Cloak
                    || itemType == ItemType::Deathalt;
                throw MphRead::ProgramException(
                    messageType.value_or(std::string())
                    + (powerup
                        ? " does not belong in a custom map: custom maps carry no powerups -- no Double Damage, "
                          "no Cloak, no Deathalt -- by the project's rule. Use one of: "
                        : " does not belong in a multiplayer map. It is one of the story's permanent upgrades -- "
                          "an energy tank, a missile or UA expansion, an artifact -- which raise a hunter's capacity "
                          "for the rest of the game rather than topping it up for the rest of the match. Use one of: ")
                    + JoinMultiplayerItems(MultiplayerItems) + ".");
            }

            auto entity = std::make_unique<Editor::ItemSpawnEntityEditor>();
            entity->Id = PostIncrement(id);
            entity->LayerMask = 0xFFFFU;
            entity->Position = ToVector(item->Position());
            entity->Up = Vector3(0.0F, 1.0F, 0.0F);
            entity->Facing = Vector3(0.0F, 0.0F, 1.0F);
            entity->NodeName = RmMainString();
            entity->ParentId = -1;
            entity->ItemType = itemType;
            entity->Enabled = true;
            entity->HasBase = item->HasBase();
            entity->AlwaysActive = true;
            entity->MaxSpawnCount = 0;
            entity->SpawnInterval = item->SpawnInterval();
            entity->SpawnDelay = 0;
            entity->NotifyEntityId = -1;
            entity->CollectedMessage = Message::None;
            map->Entities().push_back(map->OwnEntity(std::move(entity)));
        }

        if (map->Entities().empty())
        {
            throw MphRead::ProgramException("A map needs at least one entity.");
        }
    }

    std::pair<Vector3, float> MapBuilder::SolveJumpPad(MapJumpPad* pad)
    {
        if (pad == nullptr)
        {
            throw System::NullReferenceException();
        }

        if (pad->Vector() != nullptr)
        {
            const Vector3 beam = ToVector(pad->Vector()).Normalized();
            const float speed = pad->Speed();
            return std::pair<Vector3, float>(beam, speed);
        }
        if (pad->Target() == nullptr)
        {
            throw MphRead::ProgramException(
                "A jump pad needs either a target or a vector and speed.");
        }

        const Vector3 from = ToVector(pad->Position());
        const Vector3 to = ToVector(pad->Target());
        const Vector3 delta(
            to.X - from.X,
            to.Y - from.Y,
            to.Z - from.Z);
        const float horizontal = Length(Vector3(delta.X, 0.0F, delta.Z));
        constexpr float Gravity = 77.0F / 4096.0F;
        const float rise = MathMax(delta.Y, 0.0F)
            + MathMax(2.0F, horizontal * 0.22F);
        const float up = std::sqrt(2.0F * Gravity * rise);
        const float fall = std::sqrt(
            2.0F * Gravity * MathMax(rise - delta.Y, 0.01F));
        const float frames = (up + fall) / Gravity;
        const Vector3 velocity(
            delta.X / frames,
            up,
            delta.Z / frames);
        const float speed = Length(velocity);
        return std::pair<Vector3, float>(Divide(velocity, speed), speed);
    }

    CollisionVolume MapBuilder::MakeBox(const std::vector<float>* size)
    {
        CollisionVolume volume;
        volume.Type = VolumeType::Box;
        volume.BoxVector1 = Vector3(1.0F, 0.0F, 0.0F);
        volume.BoxVector2 = Vector3(0.0F, 1.0F, 0.0F);
        volume.BoxVector3 = Vector3(0.0F, 0.0F, 1.0F);
        const float positionX = ArrayValue(size, 0);
        const float positionZ = ArrayValue(size, 2);
        volume.BoxPosition = Vector3(-positionX / 2.0F, 0.0F, -positionZ / 2.0F);
        volume.BoxDot1 = ArrayValue(size, 0);
        volume.BoxDot2 = ArrayValue(size, 1);
        volume.BoxDot3 = ArrayValue(size, 2);
        return volume;
    }

    CollisionVolume MapBuilder::MakeBox(const std::vector<float>& box)
    {
        if (box.size() != 6)
        {
            throw MphRead::ProgramException("A jump pad box is six numbers, its least and greatest corners.");
        }
        CollisionVolume volume;
        volume.Type = VolumeType::Box;
        volume.BoxVector1 = Vector3(1.0F, 0.0F, 0.0F);
        volume.BoxVector2 = Vector3(0.0F, 1.0F, 0.0F);
        volume.BoxVector3 = Vector3(0.0F, 0.0F, 1.0F);
        volume.BoxPosition = Vector3(box[0], box[1], box[2]);
        volume.BoxDot1 = box[3] - box[0];
        volume.BoxDot2 = box[4] - box[1];
        volume.BoxDot3 = box[5] - box[2];
        return volume;
    }

    Vector3 MapBuilder::ToVector(const std::vector<float>* values)
    {
        const float x = ArrayValue(values, 0);
        const float y = ArrayValue(values, 1);
        const float z = ArrayValue(values, 2);
        return Vector3(x, y, z);
    }
}
