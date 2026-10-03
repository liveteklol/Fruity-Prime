#include "SceneSetup.hpp"

#include "Entities/CamSeq/CamSeqEntity.hpp"
#include "Features.hpp"
#include "Metadata/Enemies.hpp"
#include "Strings.hpp"
#include "Entities/CamSeq/CameraSequence.hpp"
#include "Formats/AiPersonality.hpp"
#include "GameState.hpp"
#include "Menu.hpp"
#include "Mods/Multiplayer/MapResourceRules.hpp"
#include "Mods/ThumbnailMode.hpp"
#include "Mods/Network/NetHealthSync.hpp"
#include "Mods/Network/NetLaunch.hpp"
#include "Mods/Network/NetSession.hpp"
#include "NativeRuntime/System/Runtime.hpp"
#include "Sound/Music.hpp"
#include "Sound/Sfx.hpp"
#include "Utility/Extract.hpp"
#include "Utility/Rng.hpp"

#include "Scene.hpp"
#include "MemoryArrays.hpp"
#include "Read.hpp"
#include "Formats/Entity.hpp"
#include "Formats/Formats.hpp"
#include "Metadata/Metadata.hpp"
#include "Metadata/Rooms.hpp"
#include "Entities/AreaVolumeEntity.hpp"
#include "Entities/ArtifactEntity.hpp"
#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/CamSeq/CamSeqEntity.hpp"
#include "Entities/DoorEntity.hpp"
#include "Entities/EnemySpawnEntity.hpp"
#include "Entities/EntityBase.hpp"
#include "Entities/FlagBaseEntity.hpp"
#include "Entities/ForceFieldEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "Entities/JumpPadEntity.hpp"
#include "Entities/LightSourceEntity.hpp"
#include "Entities/MorphCameraEntity.hpp"
#include "Entities/NodeDefenseEntity.hpp"
#include "Entities/ObjectEntity.hpp"
#include "Entities/OctolithFlagEntity.hpp"
#include "Entities/PlatformEntity.hpp"
#include "Entities/PlayerSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Entities/PointModuleEntity.hpp"
#include "Entities/RoomEntity.hpp"
#include "Entities/TeleporterEntity.hpp"
#include "Entities/TriggerVolumeEntity.hpp"
#include "Formats/Types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using ::OpenTK::Mathematics::AddY;
using ::OpenTK::Mathematics::WithY;

namespace
{
    // `((AreaVolumeEntity)GetEntity(...)).Active = value;`
    void SetAreaVolumeActiveFlag(MphRead::Entities::AreaVolumeEntity& entity, bool value)
    {
        entity.Active = value;
    }
}

namespace MphRead
{
    enum class AreaState : std::int32_t;
}

namespace MphRead
{
    namespace
    {
        using EntityList = std::shared_ptr<const std::vector<std::shared_ptr<Entities::EntityBase>>>;

        [[nodiscard]] bool IsMode(GameMode value, GameMode expected)
        {
            return static_cast<std::int32_t>(value) == static_cast<std::int32_t>(expected);
        }

        [[nodiscard]] std::string EntityTypeToString(EntityType value)
        {
            return ::MphRead::ToString(value);
        }

        [[nodiscard]] constexpr OpenTK::Mathematics::Vector3 UnitX() noexcept
        {
            return {1.0F, 0.0F, 0.0F};
        }

        [[nodiscard]] constexpr OpenTK::Mathematics::Vector3 UnitZ() noexcept
        {
            return {0.0F, 0.0F, 1.0F};
        }

        [[nodiscard]] constexpr OpenTK::Mathematics::Vector3 NegativeUnitX() noexcept
        {
            return {-1.0F, 0.0F, 0.0F};
        }

        [[nodiscard]] constexpr OpenTK::Mathematics::Vector3 NegativeUnitZ() noexcept
        {
            return {0.0F, 0.0F, -1.0F};
        }
    }

    std::tuple<std::shared_ptr<Entities::RoomEntity>, const RoomMetadata*,
        std::shared_ptr<Formats::Collision::CollisionInstance>, EntityList>
        SceneSetup::LoadGame(const std::string& name, Scene* scene, std::int32_t playerCount,
            BossFlags bossFlags, std::int32_t nodeLayerMask, std::int32_t entityLayerId)
    {
        auto [metadata, roomId] = Metadata::GetRoomByName(name);
        scene->AreaId(Metadata::GetAreaInfo(roomId));
        if (metadata == nullptr)
        {
            throw ProgramException("No room with this name is known.");
        }

        GameMode mode = GameState::Mode();
        if (IsMode(mode, GameMode::None))
        {
            mode = metadata->Multiplayer
                ? GameMode::Battle
                : GameMode::SinglePlayer;
            if (IsMode(mode, GameMode::Battle)
                && metadata->Name == "AD1 TRANSFER LOCK BT")
            {
                mode = GameMode::Bounty;
            }
            Weapons::Current = metadata->Multiplayer ? Weapons::WeaponsMP : Weapons::Weapons1P;
        }
        else
        {
            Weapons::Current = GameState::Multiplayer() ? Weapons::WeaponsMP : Weapons::Weapons1P;
        }

        GameState::Mode(mode);
        if (IsMode(mode, GameMode::SinglePlayer))
        {
            Menu::ApplyAdventureSettings();
        }

        Extract::LoadRuntimeData();
        LoadResources(scene);

        if (Menu::SaveSlot != Menu::PreviousSaveSlot)
        {
            Rng::SetRng1(Rng::Rng1StartValue);
            Rng::SetRng2(Rng::Rng2StartValue);
            Menu::PreviousSaveSlot = Menu::SaveSlot;
        }

        Entities::CamSeqEntity::ClearData();
        Entities::CamSeqEntity::Current(nullptr);
        Formats::CameraSequence::Current(nullptr);
        Formats::CameraSequence::Intro(nullptr);
        if (GameState::Multiplayer() && Entities::PlayerEntity::PlayerCount() > 0)
        {
            const std::int32_t seqId = roomId - 93 + 172;
            if (seqId >= 172 && seqId < 199)
            {
                Formats::CameraSequence::Intro(
                    Formats::CameraSequence::Load(seqId, scene).get());
            }
        }

        Sound::Sfx::Load(*scene);
        auto room = std::make_shared<Entities::RoomEntity>(scene);
        auto [collision, entities] = SetUpRoom(mode, playerCount, bossFlags,
            nodeLayerMask, entityLayerId, metadata, room, scene, false);

        StorySave* save = GameState::StorySave.get();
        const bool bossDone = GameState::SinglePlayer()
            && ((static_cast<std::uint32_t>((*save).BossFlags)
                >> (2 * scene->AreaId())) & 3U) != 0;
        Music::TryPlayRoomMusic(room->RoomId(), bossDone ? 1 : 0);

        if (GameState::SinglePlayer())
        {
            UpdateAreaHunters();
            InitHunterSpawns(scene, entities, false);
            scene->LoadMapSymbolEntities(scene->AreaId());
        }

        Formats::AiPersonality::LoadAll(mode);
        room->SetNodeData(LoadNodeData(metadata->NodePath, room->RoomId(), mode,
            entities, metadata->FirstHunt));
        (*save).CheckpointRoomId = room->RoomId();
        return {room, metadata, collision, entities};
    }

    std::shared_ptr<Formats::NodeData> SceneSetup::LoadNodeData(
        const std::optional<std::string>& requestedNodePath, std::int32_t roomId,
        GameMode mode, const EntityList& entities, bool firstHunt)
    {
        std::optional<std::string> nodePath = requestedNodePath;
        if (IsMode(mode, GameMode::SinglePlayer))
        {
            const std::int32_t count = static_cast<std::int32_t>(Entities::PlayerEntity::Players().size());
            auto encounterState = GameState::EncounterState();
            for (std::int32_t i = 0; i < count; ++i)
            {
                const auto player = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(i));
                const std::int32_t state = encounterState[static_cast<std::size_t>(i)];
                if ((*player).IsBot() && state >= 1 && state <= 4)
                {
                    auto overridePath = Metadata::EncounterNodeDataOverrides.find(roomId);
                    if (overridePath != Metadata::EncounterNodeDataOverrides.end())
                    {
                        nodePath = overridePath->second;
                    }
                    break;
                }
            }
        }
        else if (IsMode(mode, GameMode::Capture))
        {
            auto overridePath = Metadata::CtfNodeDataOverrides.find(roomId);
            if (overridePath != Metadata::CtfNodeDataOverrides.end())
            {
                nodePath = overridePath->second;
            }
        }
        else if ((IsMode(mode, GameMode::Nodes)
            || IsMode(mode, GameMode::NodesTeams)
            || IsMode(mode, GameMode::Defender)
            || IsMode(mode, GameMode::DefenderTeams)) && roomId == 107)
        {
            nodePath = R"(levels\nodeData\mp14_KOTH_node.bin)";
        }

        if (nodePath.has_value())
        {
            const std::string root = firstHunt
                ? Paths::FhFileSystem()
                : Paths::FileSystem();
            std::error_code statusError;
            if (!std::filesystem::is_regular_file(
                Paths::Combine(root, *nodePath), statusError))
            {
                std::cout << "[nodes] " << *nodePath
                    << " is missing; bots in this room will not navigate." << std::endl;
                nodePath.reset();
            }
        }

        std::shared_ptr<Formats::NodeData> nodeData;
        if (nodePath.has_value())
        {
            nodeData = Formats::ReadNodeData::ReadData(
                Paths::Combine("", *nodePath), firstHunt);
            if ((*nodeData).Simple())
            {
                for (const auto& entity : *entities)
                {
                    if (entity->Type == EntityType::JumpPad)
                    {
                        auto jumpPad = std::static_pointer_cast<Entities::JumpPadEntity>(entity);
                        jumpPad->ClosestNode(Formats::ReadNodeData::FindClosestNode(nodeData, jumpPad->Position, true));
                    }
                    else if (entity->Type == EntityType::OctolithFlag)
                    {
                        auto flag = std::static_pointer_cast<Entities::OctolithFlagEntity>(entity);
                        flag->SetClosestNode(Formats::ReadNodeData::FindClosestNode(nodeData, flag->Position, false));
                        flag->SetBaseClosestNode(Formats::ReadNodeData::FindClosestNode(nodeData, flag->BasePosition(), false));
                    }
                    else if (entity->Type == EntityType::FlagBase)
                    {
                        auto flagBase = std::static_pointer_cast<Entities::FlagBaseEntity>(entity);
                        flagBase->SetClosestNode(Formats::ReadNodeData::FindClosestNode(nodeData, flagBase->Position, false));
                    }
                    else if (entity->Type == EntityType::NodeDefense)
                    {
                        auto defense = std::static_pointer_cast<Entities::NodeDefenseEntity>(entity);
                        defense->SetClosestNode(Formats::ReadNodeData::FindClosestNode(nodeData, defense->Position, false));
                    }
                }
            }
        }
        return nodeData;
    }

    void SceneSetup::UpdateAreaHunters(StorySave* save)
    {
        if (save == nullptr)
        {
            save = GameState::StorySave.get();
            auto completed = GameState::CompletedRandomEncounterRooms();
            std::fill(completed.begin(), completed.end(), false);
        }

        auto areaHunters = (*save).AreaHunters;
        for (std::size_t i = 0; i < areaHunters->Length(); ++i)
        {
            (*areaHunters)[i] = 0;
        }
        std::uint8_t chance = 0;
        std::array<std::uint8_t, 4> chances{};
        std::array<std::uint8_t, 4> counts{};

        for (std::int32_t i = 0; i < 4; ++i)
        {
            const std::int32_t area1 = i * 2;
            if (GameState::GetAreaState(area1, save) == AreaState::Clear)
            {
                const std::uint32_t lostOctoliths = (*save).LostOctoliths;
                if (((lostOctoliths >> (8 * i)) & 15U) == 15U
                    || ((lostOctoliths >> (4 * (2 * i + 1))) & 15U) == 15U)
                {
                    chance = static_cast<std::uint8_t>(chance + 2);
                }
                else
                {
                    chance = static_cast<std::uint8_t>(chance + 1);
                }
                chances[static_cast<std::size_t>(i)] = chance;
            }
        }

        for (std::int32_t i = 0; i < 8; ++i)
        {
            if (((*save).DefeatedHunters & (1 << i)) == 0)
            {
                continue;
            }
            const std::uint32_t rand = Rng::GetRandomInt2(chance);
            for (std::int32_t j = 0; j < 4; ++j)
            {
                if (rand < chances[static_cast<std::size_t>(j)])
                {
                    (*areaHunters)[static_cast<std::size_t>(j)]
                        |= static_cast<std::uint8_t>(1 << i);
                    counts[static_cast<std::size_t>(j)]
                        = static_cast<std::uint8_t>(counts[static_cast<std::size_t>(j)] + 1);
                    if (counts[static_cast<std::size_t>(j)] >= 3)
                    {
                        for (std::int32_t k = 3; k > j; --k)
                        {
                            chances[static_cast<std::size_t>(k)]
                                = chances[static_cast<std::size_t>(k - 1)];
                        }
                        chances[static_cast<std::size_t>(j)] = 0;
                        chance = chances[3];
                    }
                    break;
                }
            }
        }
    }

    void SceneSetup::InitHunterSpawns(Scene* scene, const EntityList& entities, bool initialize)
    {
        for (std::int32_t i = 1; i < Entities::PlayerEntity::MaxPlayers(); ++i)
        {
            auto player = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(i));
            (*player).SetLoadFlags((*player).LoadFlags() & ~Entities::LoadFlags::Active);
            (*player).SetLoadFlags((*player).LoadFlags() & ~Entities::LoadFlags::SlotActive);
            (*player).SetIsBot(false);
            (*player).SetBotLevel(0);
            (*player).ResetAdventureModeBotWeapon();
        }

        Entities::PlayerEntity::SetPlayerCount(1);
        Entities::PlayerEntity::SetPlayersCreated(1);
        auto encounterState = GameState::EncounterState();
        std::fill(encounterState.begin(), encounterState.end(), 0);

        const std::int32_t areaId = scene->AreaId();
        if (areaId >= 8)
        {
            return;
        }

        const auto mainPlayer = Entities::PlayerEntity::Main();
        if (GameState::GetAreaState(areaId) != AreaState::Clear
            || scene->RoomId() != 50
            || (*mainPlayer).AvailableWeapons()[BeamType::Battlehammer])
        {
            StorySave* save = GameState::StorySave.get();
            auto areaHunters = (*save).AreaHunters;
            std::int32_t randomHunters
                = (*areaHunters)[static_cast<std::size_t>(areaId / 2)] & 0x7E;
            std::int32_t randomHunterCount
                = std::popcount(static_cast<std::uint32_t>(randomHunters));
            std::int32_t extraCount = 0;

            for (const auto& entity : *entities)
            {
                if (Entities::PlayerEntity::PlayerCount() >= Entities::PlayerEntity::MaxPlayers())
                {
                    break;
                }
                if (entity->Type != EntityType::EnemySpawn)
                {
                    continue;
                }
                auto spawner = std::static_pointer_cast<Entities::EnemySpawnEntity>(entity);
                if ((*spawner).Data.EnemyType != EnemyType::Hunter)
                {
                    continue;
                }

                const std::int32_t hunterId = (*spawner).Data.Fields.S09().HunterId;
                const std::int32_t roomId = scene->RoomId();
                auto completed = GameState::CompletedRandomEncounterRooms();
                if (hunterId == 8
                    && (Cheats::NoRandomEncounters()
                        || (Features::NoRepeatEncounters()
                            && roomId >= 27 && roomId <= 92
                            && completed[static_cast<std::size_t>(roomId - 27)])))
                {
                    return;
                }
                if (Rng::GetRandomInt2(100)
                    >= static_cast<std::uint32_t>((*spawner).Data.Fields.S09().HunterChance))
                {
                    continue;
                }

                auto player = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(Entities::PlayerEntity::PlayerCount()));
                (*player).SetIsBot(true);
                (*player).SetEnemySpawner(spawner);

                Hunter hunter{};
                if (hunterId == 8)
                {
                    const std::uint32_t rand = Rng::GetRandomInt2(
                        static_cast<std::uint32_t>(randomHunterCount + extraCount));
                    if (rand < static_cast<std::uint32_t>(randomHunterCount))
                    {
                        std::int32_t index = 0;
                        std::int32_t j = 0;
                        for (; j < 8; ++j)
                        {
                            if ((randomHunters & (1 << j)) != 0)
                            {
                                if (index++ == static_cast<std::int32_t>(rand))
                                {
                                    break;
                                }
                            }
                        }
                        hunter = static_cast<Hunter>(j);
                        if (hunter != Hunter::Samus && hunter != Hunter::Guardian)
                        {
                            Music::PlayEncounterMusic(hunter);
                        }
                    }
                    else
                    {
                        hunter = Hunter::Guardian;
                    }
                }
                else
                {
                    hunter = static_cast<Hunter>(hunterId);
                }

                if (hunter != Hunter::Guardian)
                {
                    extraCount = 1;
                }
                if ((randomHunters & (1 << static_cast<std::int32_t>(hunter))) != 0)
                {
                    randomHunters &= ~(1 << static_cast<std::int32_t>(hunter));
                    --randomHunterCount;
                }

                std::int32_t suitColor = (*spawner).Data.Fields.S09().HunterColor;
                if (hunter == (*mainPlayer).Hunter()
                    && suitColor == (*mainPlayer).Recolor()
                    && Features::AlternateHunters1P())
                {
                    suitColor = (*mainPlayer).Recolor() == 0 ? 1 : 0;
                }

                Entities::PlayerEntity::Create(hunter, suitColor);
                if (initialize)
                {
                    (*player).SetLoadFlags((*player).LoadFlags() | Entities::LoadFlags::SlotActive);
                    (*player).Initialized = (false);
                    scene->AddEntity(player);
                }
                GameState::EncounterState()[
                    static_cast<std::size_t>(Entities::PlayerEntity::PlayerCount())]
                    = (*spawner).Data.Fields.S09().EncounterType;
                (*player).SetBotLevel(1);
                Entities::PlayerEntity::SetPlayerCount(Entities::PlayerEntity::PlayerCount() + 1);
            }
        }

        StorySave* save = GameState::StorySave.get();
        for (std::int32_t i = 1; i < Entities::PlayerEntity::MaxPlayers(); ++i)
        {
            auto player = Entities::PlayerEntity::Players().at(static_cast<std::size_t>(i));
            if ((*player).IsBot())
            {
                const std::int32_t dropId = (*save).GetEnemyOctolithDrop(static_cast<std::int32_t>((*player).Hunter()));
                if (dropId < 8)
                {
                    const EntityDataHeader header(
                        static_cast<std::uint16_t>(EntityType::Artifact), -1,
                        OpenTK::Mathematics::Vector3{}, ::OpenTK::Mathematics::Vector3::UnitY, UnitX());
                    const ArtifactEntityData data(header, 8,
                        static_cast<std::uint8_t>(dropId), 0, 0,
                        0, Message::None, 0, Message::None, 0, Message::None, -1);
                    auto artifact = std::make_shared<Entities::ArtifactEntity>(data, "", scene);
                    scene->AddEntity(artifact);
                }
            }
        }
    }

    std::pair<std::shared_ptr<Formats::Collision::CollisionInstance>, EntityList>
        SceneSetup::SetUpRoom(GameMode mode, std::int32_t playerCount, BossFlags bossFlags,
            std::int32_t nodeLayerMask, std::int32_t entityLayerId,
            const RoomMetadata* metadata, const std::shared_ptr<Entities::RoomEntity>& room,
            Scene* scene, bool isRoomTransition)
    {
        if (playerCount == 0)
        {
            playerCount = Entities::PlayerEntity::PlayerCount();
        }
        if (entityLayerId < 0 || entityLayerId > 15)
        {
            if (IsMode(mode, GameMode::SinglePlayer))
            {
                if (static_cast<std::uint32_t>(bossFlags) == 0xFFFFFFFFU)
                {
                    bossFlags = static_cast<BossFlags>(
                        static_cast<std::uint32_t>((*GameState::StorySave.get()).BossFlags));
                }
                entityLayerId = (static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(bossFlags))
                    >> (2 * scene->AreaId())) & 3;
            }
            else
            {
                entityLayerId = Metadata::GetMultiplayerEntityLayer(mode, playerCount);
            }
        }
        if (nodeLayerMask == 0)
        {
            const std::int32_t nodePlayerCount
                = Features::MaxRoomDetail() ? 2 : playerCount;
            nodeLayerMask = GetNodeLayer(mode, metadata->NodeLayer, nodePlayerCount);
        }

        auto collision = Formats::Collision::Collision::GetCollision(metadata, nodeLayerMask);
        if (isRoomTransition)
        {
            (*collision).Active = false;
        }
        room->Setup(metadata->Name, metadata, collision, nodeLayerMask, metadata->Id);
        Mods::Network::NetHealthSync::BeginRoom();
        const Mods::Multiplayer::ResourceSpawnProfile resources = IsMode(mode, GameMode::SinglePlayer)
            ? Mods::Multiplayer::ResourceSpawnProfile::Low
            : Mods::Network::NetSession::Active()
                ? Mods::Network::NetLaunch::WorldProfile().Resources
                : Mods::Multiplayer::MatchWorldProfile::Resolve(playerCount).Resources;
        EntityList entities = LoadEntities(metadata, entityLayerId, scene, resources);
        entities = GetExtraEntities(room->RoomId(), entities, scene);
        return {collision, entities};
    }

    std::int32_t SceneSetup::GetNodeLayer(
        GameMode mode, std::int32_t roomLayer, std::int32_t playerCount)
    {
        std::int32_t nodeLayerMask = 0;
        if (IsMode(mode, GameMode::SinglePlayer))
        {
            if (roomLayer > 0)
            {
                const std::uint32_t shift = static_cast<std::uint32_t>(roomLayer) & 0x1FU;
                nodeLayerMask = nodeLayerMask & 0xC03F
                    | static_cast<std::int32_t>(
                        ((std::uint32_t{1} << shift) & 0xFFU) << 6);
            }
        }
        else
        {
            nodeLayerMask |= static_cast<std::int32_t>(NodeLayer::MultiplayerU);
            if (playerCount <= 2)
            {
                nodeLayerMask |= static_cast<std::int32_t>(NodeLayer::MultiplayerLod0);
            }
            else
            {
                nodeLayerMask |= static_cast<std::int32_t>(NodeLayer::MultiplayerLod1);
            }
            if (IsMode(mode, GameMode::Capture))
            {
                nodeLayerMask |= static_cast<std::int32_t>(NodeLayer::CaptureTheFlag);
            }
        }
        return nodeLayerMask;
    }

    EntityList SceneSetup::LoadEntities(
        const RoomMetadata* metadata, std::int32_t layerId, Scene* scene,
        Mods::Multiplayer::ResourceSpawnProfile resources)
    {
        auto results = std::make_shared<std::vector<std::shared_ptr<Entities::EntityBase>>>();
        if (!metadata->EntityPath.has_value())
        {
            return results;
        }

        auto entities = Read::GetEntities(*metadata->EntityPath, layerId,
            metadata->FirstHunt, true);
        entities = Mods::Multiplayer::MapResourceRules::Resolve(*metadata, resources, entities);
        for (const auto& entity : *entities)
        {
            const std::string& nodeName = *entity->NodeName;
            if (entity->Type == EntityType::Platform)
            {
                const auto raw = std::static_pointer_cast<EntityOf<PlatformEntityData>>(entity);
                results->push_back(std::make_shared<Entities::PlatformEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::FhPlatform)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FhPlatformEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FhPlatformEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::Object)
            {
                const auto raw = std::static_pointer_cast<EntityOf<ObjectEntityData>>(entity);
                results->push_back(std::make_shared<Entities::ObjectEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::PlayerSpawn || entity->Type == EntityType::FhPlayerSpawn)
            {
                const auto raw = std::static_pointer_cast<EntityOf<PlayerSpawnEntityData>>(entity);
                results->push_back(std::make_shared<Entities::PlayerSpawnEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::Door)
            {
                const auto raw = std::static_pointer_cast<EntityOf<DoorEntityData>>(entity);
                results->push_back(std::make_shared<Entities::DoorEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::FhDoor)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FhDoorEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FhDoorEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::ItemSpawn)
            {
                const auto raw = std::static_pointer_cast<EntityOf<ItemSpawnEntityData>>(entity);
                const ItemSpawnEntityData data
                    = Mods::Multiplayer::MapResourceRules::ResolveData(*metadata, resources, raw->Data);
                results->push_back(std::make_shared<Entities::ItemSpawnEntity>(data, nodeName, scene));
            }
            else if (entity->Type == EntityType::FhItemSpawn)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FhItemSpawnEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FhItemSpawnEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::EnemySpawn)
            {
                const auto raw = std::static_pointer_cast<EntityOf<EnemySpawnEntityData>>(entity);
                results->push_back(std::make_shared<Entities::EnemySpawnEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::FhEnemySpawn)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FhEnemySpawnEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FhEnemySpawnEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::TriggerVolume)
            {
                const auto raw = std::static_pointer_cast<EntityOf<TriggerVolumeEntityData>>(entity);
                results->push_back(std::make_shared<Entities::TriggerVolumeEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::FhTriggerVolume)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FhTriggerVolumeEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FhTriggerVolumeEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::AreaVolume)
            {
                const auto raw = std::static_pointer_cast<EntityOf<AreaVolumeEntityData>>(entity);
                results->push_back(std::make_shared<Entities::AreaVolumeEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::FhAreaVolume)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FhAreaVolumeEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FhAreaVolumeEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::JumpPad)
            {
                const auto raw = std::static_pointer_cast<EntityOf<JumpPadEntityData>>(entity);
                results->push_back(std::make_shared<Entities::JumpPadEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::FhJumpPad)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FhJumpPadEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FhJumpPadEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::PointModule || entity->Type == EntityType::FhPointModule)
            {
                const auto raw = std::static_pointer_cast<EntityOf<PointModuleEntityData>>(entity);
                results->push_back(std::make_shared<Entities::PointModuleEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::MorphCamera)
            {
                const auto raw = std::static_pointer_cast<EntityOf<MorphCameraEntityData>>(entity);
                results->push_back(std::make_shared<Entities::MorphCameraEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::FhMorphCamera)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FhMorphCameraEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FhMorphCameraEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::OctolithFlag)
            {
                const auto raw = std::static_pointer_cast<EntityOf<OctolithFlagEntityData>>(entity);
                results->push_back(std::make_shared<Entities::OctolithFlagEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::FlagBase)
            {
                const auto raw = std::static_pointer_cast<EntityOf<FlagBaseEntityData>>(entity);
                results->push_back(std::make_shared<Entities::FlagBaseEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::Teleporter)
            {
                const auto raw = std::static_pointer_cast<EntityOf<TeleporterEntityData>>(entity);
                results->push_back(std::make_shared<Entities::TeleporterEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::NodeDefense)
            {
                const auto raw = std::static_pointer_cast<EntityOf<NodeDefenseEntityData>>(entity);
                results->push_back(std::make_shared<Entities::NodeDefenseEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::LightSource)
            {
                const auto raw = std::static_pointer_cast<EntityOf<LightSourceEntityData>>(entity);
                results->push_back(std::make_shared<Entities::LightSourceEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::Artifact)
            {
                const auto raw = std::static_pointer_cast<EntityOf<ArtifactEntityData>>(entity);
                results->push_back(std::make_shared<Entities::ArtifactEntity>(raw->Data, nodeName, scene));
            }
            else if (entity->Type == EntityType::CameraSequence)
            {
                const auto raw = std::static_pointer_cast<EntityOf<CameraSequenceEntityData>>(entity);
                results->push_back(std::make_shared<Entities::CamSeqEntity>(raw->Data, scene));
            }
            else if (entity->Type == EntityType::ForceField)
            {
                const auto raw = std::static_pointer_cast<EntityOf<ForceFieldEntityData>>(entity);
                results->push_back(std::make_shared<Entities::ForceFieldEntity>(raw->Data, nodeName, scene));
            }
            else
            {
                throw ProgramException(
                    "Invalid entity type " + EntityTypeToString(entity->Type));
            }
        }
        return results;
    }

    void SceneSetup::LoadResources(Scene* scene)
    {
        if (scene != nullptr)
        {
            if (Paths::IsMphJapan() || Paths::IsMphKorea())
            {
                auto [count, charData] = Read::ReadKanjiFont(
                    GameState::SinglePlayer());
                std::vector<std::uint8_t> widths(static_cast<std::size_t>(count));
                if (Paths::IsMphJapan())
                {
                    std::fill(widths.begin(), widths.end(), std::uint8_t{10});
                }
                else
                {
                    std::fill(widths.begin(), widths.end(), std::uint8_t{11});
                    widths[1] = 2;
                    widths[32] = 6;
                }
                std::vector<std::uint8_t> offsets(static_cast<std::size_t>(count));
                Text::Font::Kanji()->SetData(
                    std::make_shared<std::vector<std::uint8_t>>(std::move(widths)),
                    std::make_shared<std::vector<std::uint8_t>>(std::move(offsets)),
                    std::make_shared<std::vector<std::uint8_t>>(std::move(charData)), 0);
            }

            LoadBombResources(scene);
            LoadBeamEffectResources(scene);
            LoadBeamProjectileResources(scene);
            LoadRoomResources(scene);
            LoadHunterResources(Hunter::Samus, scene);
            if (!Mods::ThumbnailMode::Active())
            {
                LoadHunterResources(Hunter::Kanden, scene);
                LoadHunterResources(Hunter::Trace, scene);
                LoadHunterResources(Hunter::Sylux, scene);
                LoadHunterResources(Hunter::Noxus, scene);
                LoadHunterResources(Hunter::Spire, scene);
                LoadHunterResources(Hunter::Weavel, scene);
                LoadHunterResources(Hunter::Guardian, scene);
            }
            LoadCommonHunterResources(scene);
        }
    }

    void SceneSetup::LoadCommonHunterResources(Scene* scene)
    {
        for (const char* name : {"doubleDamage_img", "alt_ice", "gunSmoke",
            "trail", "octolith_simple", "Octolith"})
        {
            scene->LoadModel(name);
        }
        for (std::int32_t id : {10, 216, 187, 188, 189})
        {
            scene->LoadEffect(id, true);
        }
        for (std::int32_t i = 0; i < 9; ++i)
        {
            scene->LoadEffect(Metadata::MuzzleEffectIds[i], true);
            scene->LoadEffect(Metadata::ChargeEffectIds[i], true);
            scene->LoadEffect(Metadata::ChargeLoopEffectIds[i], true);
        }
        Entities::PlayerEntity::LoadWeaponNames();
        Entities::PlayerEntity::GeneratePlayerVolumes();
        Text::Strings::ReadStringTable(Text::StringTables::HudMsgsCommon);
        Text::Strings::ReadStringTable(Text::StringTables::HudMessagesSP);
        Text::Strings::ReadStringTable(Text::StringTables::HudMessagesMP);
        if (GameState::SinglePlayer())
        {
            Text::Strings::ReadStringTable(Text::StringTables::ScanLog);
        }
    }

    void SceneSetup::LoadHunterResources(Hunter hunter, Scene* scene)
    {
        scene->LoadModel(hunter == Hunter::Noxus || hunter == Hunter::Trace ? "nox_ice" : "samus_ice");
        for (const std::string& modelName : Metadata::HunterModels.at(hunter))
        {
            scene->LoadModel(modelName);
        }
        if (hunter == Hunter::Samus)
        {
            scene->LoadEffect(30, true);
            scene->LoadEffect(136, true);
        }
        else if (hunter == Hunter::Kanden)
        {
            Entities::PlayerEntity::GenerateKandenAltNodeDistances();
        }
        else if (hunter == Hunter::Spire)
        {
            scene->LoadEffect(37, true);
        }
        else if (hunter == Hunter::Noxus)
        {
            scene->LoadEffect(235, true);
        }
    }

    void SceneSetup::LoadBombResources(Scene* scene)
    {
        for (const char* name : {"KandenAlt_TailBomb", "arcWelder", "arcWelder1"})
        {
            scene->LoadModel(name);
        }
        for (std::int32_t id : {9, 113, 119, 128, 129, 145, 146,
            149, 150, 151, 152, 153})
        {
            scene->LoadEffect(id, true);
        }
    }

    void SceneSetup::LoadBeamEffectResources(Scene* scene)
    {
        scene->LoadModel("iceWave");
        scene->LoadModel("sniperBeam");
        scene->LoadModel("cylBossLaserBurn");
    }

    void SceneSetup::LoadBeamProjectileResources(Scene* scene)
    {
        for (const char* name : {"iceShard", "energyBeam", "trail", "electroTrail", "arcWelder"})
        {
            scene->LoadModel(name);
        }
        for (std::int32_t id : {57, 58, 59, 60, 61, 62, 63, 78, 85, 86,
            92, 98, 99, 100, 121, 122, 123, 124, 125, 126, 130, 134, 137,
            140, 141, 142, 171, 211, 237, 238, 246})
        {
            scene->LoadEffect(id, true);
        }
    }

    void SceneSetup::LoadRoomResources(Scene* scene)
    {
        for (std::int32_t id : {1, 2, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16,
            17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 31, 33, 99,
            115, 154, 155, 156, 157, 158, 159, 160, 161, 173, 190, 191,
            192, 231, 239})
        {
            scene->LoadEffect(id, true);
        }
        scene->LoadModel(Read::GetSingleParticle(SingleType::Death)->Model);
        scene->LoadModel(Read::GetSingleParticle(SingleType::Fuzzball)->Model);
        if (GameState::SinglePlayer())
        {
            scene->LoadModel(
                Read::GetModelInstance("icons", false, MetaDir::Hud)->Model());
        }
        scene->LoadEffect(209, true);
        scene->LoadEffect(245, true);
    }

    void SceneSetup::LoadEntityResources(
        const std::shared_ptr<Entities::EntityBase>& entity, Scene* scene)
    {
        if (auto obj = std::dynamic_pointer_cast<Entities::ObjectEntity>(entity))
        {
            LoadObjectResources(obj, scene);
        }
        else if (auto platform = std::dynamic_pointer_cast<Entities::PlatformEntity>(entity))
        {
            LoadPlatformResources(platform, scene);
        }
        else if (auto spawner = std::dynamic_pointer_cast<Entities::EnemySpawnEntity>(entity))
        {
            LoadEnemyResources(spawner, scene);
        }
        else if (auto itemSpawner = std::dynamic_pointer_cast<Entities::ItemSpawnEntity>(entity))
        {
            LoadItemResources(itemSpawner, scene);
        }
    }

    void SceneSetup::LoadObjectResources(Scene* scene)
    {
        auto objEnumerator = scene->GetObjectEntities().GetEnumerator();
        while (objEnumerator.MoveNext())
        {
            const auto obj = objEnumerator.Current();
            LoadObjectResources(obj, scene);
        }
    }

    void SceneSetup::LoadObjectResources(
        const std::shared_ptr<Entities::ObjectEntity>& obj, Scene* scene)
    {
        const std::int32_t effectId = (*obj).Data().EffectId;
        if (effectId != 0)
        {
            scene->LoadEffect(effectId, false);
        }
    }

    void SceneSetup::LoadPlatformResources(Scene* scene)
    {
        auto platformEnumerator = scene->GetPlatformEntities().GetEnumerator();
        while (platformEnumerator.MoveNext())
        {
            const auto platform = platformEnumerator.Current();
            LoadPlatformResources(platform, scene);
        }
    }

    void SceneSetup::LoadPlatformResources(
        const std::shared_ptr<Entities::PlatformEntity>& platform, Scene* scene)
    {
        const std::array<std::int32_t, 3> effects{
            (*platform).Data().ResistEffectId,
            (*platform).Data().DamageEffectId,
            (*platform).Data().DeadEffectId
        };
        for (std::int32_t effectId : effects)
        {
            if (effectId != 0)
            {
                scene->LoadEffect(effectId, false);
            }
        }
        if (TypeExtensions::TestFlag((*platform).Data().Flags, Entities::PlatformFlags::SamusShip))
        {
            scene->LoadEffect(182, false);
        }
        if (TypeExtensions::TestFlag((*platform).Data().Flags, Entities::PlatformFlags::BeamSpawner)
            && (*platform).Data().BeamId == 0)
        {
            scene->LoadEffect(183, false);
            scene->LoadEffect(184, false);
            scene->LoadEffect(185, false);
        }
        if ((*platform).Data().ItemChance > 0)
        {
            LoadItem((*platform).Data().ItemType, scene);
        }
    }

    void SceneSetup::LoadEnemyResources(Scene* scene)
    {
        auto spawnerEnumerator = scene->GetEnemySpawnEntities().GetEnumerator();
        while (spawnerEnumerator.MoveNext())
        {
            const auto spawner = spawnerEnumerator.Current();
            LoadEnemyResources(spawner, scene);
        }
    }

    void SceneSetup::LoadEnemyResources(
        const std::shared_ptr<Entities::EnemySpawnEntity>& spawner, Scene* scene)
    {
        const EnemyType type = (*spawner).Data.EnemyType;
        LoadEnemy(type, scene);
        if ((*spawner).Data.SpawnerHealth > 0)
        {
            scene->LoadModel(type == EnemyType::WarWasp || type == EnemyType::BarbedWarWasp
                    ? "PlantCarnivarous_Pod" : "EnemySpawner");
        }
        if ((*spawner).Data.ItemChance > 0)
        {
            LoadItem((*spawner).Data.ItemType, scene);
        }

        switch (type)
        {
        case EnemyType::Cretaphid:
            LoadEnemy(EnemyType::CretaphidEye, scene);
            for (std::int32_t id : {65, 66, 67, 73, 74, 116, 117, 138, 139})
                scene->LoadEffect(id, false);
            LoadItem(ItemType::HealthMedium, scene);
            LoadItem(ItemType::UASmall, scene);
            LoadItem(ItemType::MissileSmall, scene);
            break;
        case EnemyType::Gorea1A:
            LoadEnemy(EnemyType::Gorea1B, scene);
            for (std::int32_t id : {48, 46, 49, 47, 50, 41, 42, 43, 44, 45,
                54, 51, 55, 53, 56, 52, 71, 72, 104, 148, 175, 179, 180})
                scene->LoadEffect(id, false);
            LoadItem(ItemType::HealthBig, scene);
            LoadItem(ItemType::UABig, scene);
            LoadItem(ItemType::MissileBig, scene);
            break;
        case EnemyType::Trocra:
            scene->LoadEffect(164, false);
            scene->LoadEffect(75, false);
            LoadItem(ItemType::HealthSmall, scene);
            LoadItem(ItemType::UASmall, scene);
            LoadItem(ItemType::MissileSmall, scene);
            break;
        case EnemyType::Gorea2:
            LoadEnemy(EnemyType::GoreaMeteor, scene);
            for (std::int32_t id : {104, 224, 79, 176, 177, 178, 80, 225, 44, 72, 174, 210})
                scene->LoadEffect(id, false);
            LoadItem(ItemType::HealthSmall, scene);
            LoadItem(ItemType::UASmall, scene);
            LoadItem(ItemType::MissileSmall, scene);
            break;
        case EnemyType::Slench:
            LoadEnemy(EnemyType::SlenchNest, scene);
            LoadEnemy(EnemyType::SlenchSynapse, scene);
            for (std::int32_t id : {64, 81, 68, 82, 70, 69, 83, 109, 135,
                201, 202, 203, 204, 205, 206})
                scene->LoadEffect(id, false);
            LoadItem(ItemType::HealthMedium, scene);
            LoadItem(ItemType::UASmall, scene);
            LoadItem(ItemType::MissileSmall, scene);
            break;
        case EnemyType::Blastcap:
            scene->LoadEffect(3, false);
            scene->LoadEffect(4, false);
            break;
        case EnemyType::PsychoBit1:
            scene->LoadEffect(240, false);
            break;
        case EnemyType::AlimbicTurret:
            scene->LoadEffect(207, false);
            scene->LoadEffect(208, false);
            break;
        case EnemyType::FireSpawn:
            if ((*spawner).Data.Fields.S06().EnemySubtype == 1)
            {
                for (std::int32_t id : {96, 132, 133, 131, 217})
                    scene->LoadEffect(id, false);
            }
            else
            {
                for (std::int32_t id : {94, 95, 93, 110, 218})
                    scene->LoadEffect(id, false);
            }
            break;
        case EnemyType::GreaterIthrak:
            for (std::int32_t id : {102, 101, 103})
                scene->LoadEffect(id, false);
            break;
        case EnemyType::Shriekbat:
            scene->LoadEffect(29, false);
            scene->LoadEffect(108, false);
            break;
        case EnemyType::CarnivorousPlant:
            {
                const ObjectMetadata& meta = Metadata::GetObjectById(
                    (*spawner).Data.Fields.S07().EnemySubtype);
                scene->LoadModel(meta.Name);
            }
            break;
        default:
            break;
        }
    }

    void SceneSetup::LoadEnemy(EnemyType enemy, Scene* scene)
    {
        if (enemy == EnemyType::SlenchSynapse)
        {
            const std::int32_t roomId = scene->RoomId();
            if (roomId == 76)
                scene->LoadModel("BigEyeSynapse_04");
            else if (roomId == 64)
                scene->LoadModel("BigEyeSynapse_03");
            else if (roomId == 82)
                scene->LoadModel("BigEyeSynapse_02");
            else
                scene->LoadModel("BigEyeSynapse_01");
        }
        else
        {
            if (auto model = Metadata::GetEnemyModelName(enemy); model.has_value())
            {
                scene->LoadModel(*model);
            }
            if (enemy == EnemyType::Gorea1A)
            {
                scene->LoadModel("Gorea1B_lod0");
                scene->LoadModel("goreaArmRegen");
                scene->LoadModel("goreaMindTrick");
                scene->LoadModel("goreaMindTrick");
            }
            else if (enemy == EnemyType::Gorea2)
            {
                scene->LoadModel("goreaMeteor");
                scene->LoadModel("goreaLaser");
                scene->LoadModel("goreaLaserColl");
            }
        }

        const std::int32_t effectId = Metadata::GetEnemyDeathEffect(enemy);
        if (effectId > 0)
        {
            scene->LoadEffect(effectId, false);
        }
    }

    void SceneSetup::LoadItemResources(Scene* scene)
    {
        if (GameState::Multiplayer())
        {
            LoadItem(ItemType::UASmall, scene);
            LoadItem(ItemType::UABig, scene);
            LoadItem(ItemType::MissileSmall, scene);
            LoadItem(ItemType::MissileBig, scene);
        }
        auto itemSpawnerEnumerator = scene->GetItemSpawnEntities().GetEnumerator();
        while (itemSpawnerEnumerator.MoveNext())
        {
            const auto itemSpawner = itemSpawnerEnumerator.Current();
            LoadItemResources(itemSpawner, scene);
        }
    }

    void SceneSetup::LoadItemResources(
        const std::shared_ptr<Entities::ItemSpawnEntity>& itemSpawner, Scene* scene)
    {
        LoadItem((*itemSpawner).Data().ItemType, scene);
        if ((*itemSpawner).Data().HasBase != 0)
        {
            scene->LoadModel("items_base");
        }
    }

    void SceneSetup::LoadItem(ItemType item, Scene* scene)
    {
        if (item == ItemType::None)
        {
            return;
        }
        const std::int32_t index = static_cast<std::int32_t>(item);
        assert(index < static_cast<std::int32_t>(Metadata::Items.size()));
        scene->LoadModel(Metadata::Items[static_cast<std::size_t>(index)]);
        if (item == ItemType::ArtifactKey)
            scene->LoadEffect(144, false);
        else if (item == ItemType::Deathalt)
            scene->LoadEffect(181, true);
        else if (item == ItemType::OmegaCannon)
        {
            scene->LoadEffect(209, true);
            scene->LoadEffect(245, true);
        }
        else if (item == ItemType::DoubleDamage)
            scene->LoadEffect(244, true);
    }

    BeamProjectileArray::BeamProjectileArray(std::int32_t length)
        : _length(length),
          _items(std::make_unique<std::shared_ptr<Entities::BeamProjectileEntity>[]>(
              static_cast<std::size_t>(length)))
    {
    }

    void BeamProjectileArray::CheckIndex(std::int32_t index) const
    {
        if (index < 0 || index >= _length)
        {
            throw Memory::Detail::IndexOutOfRangeException();
        }
    }

    std::shared_ptr<Entities::BeamProjectileEntity>&
        BeamProjectileArray::operator[](std::int32_t index)
    {
        CheckIndex(index);
        return _items[static_cast<std::size_t>(index)];
    }

    const std::shared_ptr<Entities::BeamProjectileEntity>&
        BeamProjectileArray::operator[](std::int32_t index) const
    {
        CheckIndex(index);
        return _items[static_cast<std::size_t>(index)];
    }

    std::shared_ptr<BeamProjectileArray>
        SceneSetup::CreateBeamList(std::int32_t size, Scene* scene)
    {
        NativeRuntime::DebugAssert(size > 0);
        if (size < 0)
        {
            throw Memory::Detail::OverflowException();
        }
        auto beams = std::shared_ptr<BeamProjectileArray>(new BeamProjectileArray(size));
        for (std::int32_t i = 0; i < size; ++i)
        {
            (*beams)[i] = std::make_shared<Entities::BeamProjectileEntity>(scene);
        }
        return beams;
    }

    void BeamProjectileArray::ReleaseReferences() noexcept
    {
        for (std::int32_t i = 0; i < _length; ++i)
        {
            if (_items[static_cast<std::size_t>(i)])
            {
                _items[static_cast<std::size_t>(i)]->ReleaseReferences();
            }
        }
    }

    EntityList SceneSetup::GetExtraEntities(
        std::int32_t roomId, const EntityList& entities, Scene* scene)
    {
        const auto mainPlayer = Entities::PlayerEntity::Main();
        const Hunter hunter = (*mainPlayer).Hunter();
        if (!GameState::SinglePlayer() || hunter == Hunter::Samus
            || !Features::AlternateHunters1P())
        {
            return entities;
        }

        const auto getEntity = [&](EntityType type, std::int32_t id)
            -> std::shared_ptr<Entities::EntityBase>
        {
            for (const auto& entity : *entities)
            {
                if (entity->Type == type && entity->Id == id)
                {
                    return entity;
                }
            }
            throw ProgramException("Could not find entity to update.");
        };

        std::int16_t nextId = 30000;
        const auto createJumpPad =
            [&](const std::string& nodeName, OpenTK::Mathematics::Vector3 position,
                float speed, float radius = 1.0F, float height = 1.0F,
                float offset = 0.0F, std::uint16_t frames = 20,
                Entities::TriggerFlags flags = Entities::TriggerFlags::PlayerAlt)
                -> std::shared_ptr<Entities::JumpPadEntity>
        {
            const Fixed radiusFx(Fixed::ToInt(radius));
            const Fixed heightFx(Fixed::ToInt(height));
            const EntityDataHeader header(
                static_cast<std::uint16_t>(EntityType::JumpPad), nextId++,
                position, ::OpenTK::Mathematics::Vector3::UnitY, UnitZ());
            const Vector3Fx cylPos(0, Fixed::ToInt(offset), 0);
            const RawCollisionVolume volume(TypeExtensions::ToVector3Fx(::OpenTK::Mathematics::Vector3::UnitY), cylPos, radiusFx, heightFx);
            const JumpPadEntityData data(header, -1, volume, TypeExtensions::ToVector3Fx(::OpenTK::Mathematics::Vector3::UnitY),
                Fixed(Fixed::ToInt(speed)), 0, frames, 1, 0, 0, flags);
            return std::make_shared<Entities::JumpPadEntity>(data, nodeName, scene);
        };

        const auto createTeleporter =
            [&](OpenTK::Mathematics::Vector3 position,
                OpenTK::Mathematics::Vector3 targetPos,
                OpenTK::Mathematics::Vector3 facing,
                const std::string& nodeName, const std::string& targetNode)
                -> std::shared_ptr<Entities::TeleporterEntity>
        {
            const EntityDataHeader header(
                static_cast<std::uint16_t>(EntityType::Teleporter), nextId++,
                position, ::OpenTK::Mathematics::Vector3::UnitY, facing);
            const TeleporterEntityData data(header, 0, 0, 8, 1, 1,
                std::nullopt, TypeExtensions::ToVector3Fx(targetPos), targetNode);
            return std::make_shared<Entities::TeleporterEntity>(
                data, nodeName, scene, true);
        };

        auto list = std::make_shared<std::vector<std::shared_ptr<Entities::EntityBase>>>(
            entities->begin(), entities->end());

        if (roomId == 27 && (hunter == Hunter::Noxus || hunter == Hunter::Trace))
        {
            getEntity(EntityType::ItemSpawn, 13)->Position
                = OpenTK::Mathematics::Vector3(-60.0F, 6.5F, 33.6F);
        }
        else if (roomId == 28 && (hunter == Hunter::Noxus || hunter == Hunter::Trace))
        {
            list->push_back(createJumpPad("rmC0B", {-14.3F, 0.0F, -8.9F}, 0.25F, 0.75F));
        }
        else if (roomId == 67 && (hunter == Hunter::Noxus || hunter == Hunter::Spire
            || hunter == Hunter::Trace || hunter == Hunter::Weavel))
        {
            if (hunter != Hunter::Weavel)
                list->push_back(createJumpPad("rmMain", {8.7F, -2.0F, 12.4F}, 0.3F, 0.5F));
            if (hunter == Hunter::Noxus || hunter == Hunter::Trace || hunter == Hunter::Weavel)
                list->push_back(createJumpPad("rmMain", {8.7F, 3.4F, 0.0F}, 0.5F, 0.5F));
        }
        else if (roomId == 79 && (hunter == Hunter::Noxus || hunter == Hunter::Trace))
        {
            auto entity = getEntity(EntityType::ItemSpawn, 29);
            entity->Position = AddY(entity->Position, -1.5F);
        }
        else if (roomId == 78 && hunter == Hunter::Noxus)
        {
            list->push_back(createJumpPad("rmChamberE", {18.5F, 33.5F, -38.6F}, 0.3F, 0.4F, 1.0F, 0.0F, 35));
            list->push_back(createJumpPad("rmChamberE", {16.4F, 35.5F, -38.6F}, 0.3F, 0.4F, 1.0F, 0.0F, 35));
            list->push_back(createJumpPad("rmChamberE", {19.1F, 40.5F, -38.6F}, 0.3F, 0.4F, 1.0F, 0.0F, 35));
        }
        else if (roomId == 78 && hunter == Hunter::Weavel)
        {
            list->push_back(createJumpPad("rmChamberE", {18.5F, 33.5F, -38.6F}, 0.3F, 0.4F, 1.0F, 0.0F, 35));
        }
        else if (roomId == 78 && (hunter == Hunter::Trace || hunter == Hunter::Sylux))
        {
            auto position = OpenTK::Mathematics::Vector3(18.5F, 33.0F, -38.6F);
            auto targetPos = OpenTK::Mathematics::Vector3(16.8F, 35.5F, -38.6F);
            list->push_back(createTeleporter(position, targetPos, NegativeUnitZ(), "rmChamberE", "rmChamberE"));
            if (hunter == Hunter::Trace)
            {
                position = {19.1F, 40.0F, -38.6F};
                targetPos = {17.4F, 43.3F, -22.5F};
                list->push_back(createTeleporter(position, targetPos, NegativeUnitZ(), "rmChamberE", "rmChamberE"));
            }
        }
        else if (roomId == 80)
        {
            if (hunter == Hunter::Noxus || hunter == Hunter::Spire)
                list->push_back(createJumpPad("rmC0b", {8.0F, 0.0F, 12.1F}, 0.5F, 0.3F, 1.0F, 0.0F, 60));
            else if (hunter == Hunter::Kanden)
            {
                auto entity = getEntity(EntityType::ItemSpawn, 31);
                entity->Position = AddY(entity->Position, -0.7F);
            }
            else if (hunter == Hunter::Weavel)
            {
                auto entity = getEntity(EntityType::ItemSpawn, 31);
                entity->Position = AddY(entity->Position, -2.0F);
            }
            else if (hunter == Hunter::Trace)
            {
                auto entity = getEntity(EntityType::ItemSpawn, 31);
                entity->Position = WithY(entity->Position, 0.0F);
            }
        }
        else if (roomId == 30)
        {
            if (hunter == Hunter::Noxus || hunter == Hunter::Spire
                || hunter == Hunter::Trace || hunter == Hunter::Weavel)
            {
                list->push_back(createJumpPad(
                    "rmLava", {0.6F, -26.5F, 4.8F}, 0.15F, 0.5F, 1.0F, 2.2F));
                list->push_back(createJumpPad("rmLava", {0.6F, 3.2F, -14.6F}, 0.6F, 0.5F));
                if (hunter != Hunter::Spire)
                {
                    list->push_back(createJumpPad("rmLava", {0.6F, -21.6F, -6.8F}, 0.5F, 0.5F));
                    list->push_back(createJumpPad("rmLava", {0.6F, 73.5F, -9.4F}, 0.5F, 0.5F));
                    list->push_back(createJumpPad("rmLava", {0.6F, 94.2F, -15.5F}, 0.5F, 0.5F));
                }
                else
                {
                    SetAreaVolumeActiveFlag(*std::static_pointer_cast<Entities::AreaVolumeEntity>(
                            getEntity(EntityType::AreaVolume, 4)), false);
                }
            }
            if (hunter == Hunter::Trace || hunter == Hunter::Sylux)
            {
                auto position = OpenTK::Mathematics::Vector3(0.59F, -27.63F, -11.91F);
                auto targetPos = OpenTK::Mathematics::Vector3(0.59F, -20.63F, -6.81F);
                list->push_back(createTeleporter(position, NegativeUnitX(), targetPos, "rmLava", "rmLava"));
                position = {0.59F, -23.03F, -10.61F};
                targetPos = {0.59F, -28.43F, -10.61F};
                list->push_back(createTeleporter(position, NegativeUnitX(), targetPos, "rmLava", "rmLava"));
            }
        }
        else if (roomId == 41 && hunter == Hunter::Spire)
        {
            for (std::int32_t id : {3, 11, 12, 13, 16})
            {
                SetAreaVolumeActiveFlag(*std::static_pointer_cast<Entities::AreaVolumeEntity>(
                        getEntity(EntityType::AreaVolume, id)), false);
            }
        }
        else if (roomId == 38 && (hunter == Hunter::Noxus || hunter == Hunter::Trace))
        {
            list->push_back(createJumpPad("rmMain", {-1.4F, 0.3F, 29.1F}, 0.3F, 0.4F));
            list->push_back(createJumpPad("rmMain", {29.1F, 0.3F, 1.4F}, 0.3F, 0.4F));
            list->push_back(createJumpPad("rmend", {-10.8F, 9.6F, -28.3F}, 0.3F, 0.4F));
            list->push_back(createJumpPad("rmend", {-19.6F, 12.0F, -28.3F}, 0.3F, 0.4F));
            list->push_back(createJumpPad("rmend", {-39.9F, 17.0F, -24.5F}, 0.5F, 0.4F));
        }
        else if (roomId == 38 && hunter == Hunter::Weavel)
        {
            list->push_back(createJumpPad("rmend", {-10.8F, 9.6F, -28.3F}, 0.3F, 0.4F));
            list->push_back(createJumpPad("rmend", {-19.6F, 12.0F, -28.3F}, 0.3F, 0.4F));
        }
        return list;
    }
}
