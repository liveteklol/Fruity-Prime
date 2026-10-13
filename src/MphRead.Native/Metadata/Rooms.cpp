#include "Rooms.hpp"

#include "Metadata.hpp"
#include "../Mods/MapGen/CustomRooms.hpp"

#include <stdexcept>
#include <utility>

namespace MphRead
{
    namespace Metadata
    {
    }

    namespace
    {
        using OpenTK::Mathematics::Vector3;

        bool HasValue(Vector3 value) noexcept
        {
            return value.X != 0.0F || value.Y != 0.0F || value.Z != 0.0F;
        }

        struct RoomArgs
        {
            std::int32_t id = 0;
            std::string name;
            std::optional<std::string> inGameName;
            std::string archive;
            std::string modelPath;
            std::string animationPath;
            std::string collisionPath;
            std::optional<std::string> texturePath;
            std::optional<std::string> entityPath;
            std::optional<std::string> nodePath;
            std::optional<std::string> roomNodeName;
            std::uint32_t battleTimeLimit = 0;
            std::uint32_t timeLimit = 0;
            std::int16_t pointLimit = 0;
            std::int16_t nodeLayer = 0;
            bool fogEnabled = false;
            bool clearFog = false;
            ColorRgb fogColor{};
            std::int32_t fogSlope = 0;
            std::uint16_t fogOffset = 0;
            ColorRgb light1Color{};
            Vector3 light1Vector{};
            ColorRgb light2Color{};
            Vector3 light2Vector{};
            std::int32_t farClip = 0;
            std::int32_t killHeight = 0;
            RoomSize size = RoomSize::None;
            Vector3 cameraMin{};
            Vector3 cameraMax{};
            Vector3 playerMin{};
            Vector3 playerMax{};
            bool multiplayer = false;
            bool firstHunt = false;
            bool hybrid = false;
        };

        std::shared_ptr<RoomMetadata> MakeRoom(RoomArgs args)
        {
            return std::make_shared<RoomMetadata>(
                args.id,
                std::move(args.name),
                std::move(args.inGameName),
                std::move(args.archive),
                std::move(args.modelPath),
                std::move(args.animationPath),
                std::move(args.collisionPath),
                std::move(args.texturePath),
                std::move(args.entityPath),
                std::move(args.nodePath),
                std::move(args.roomNodeName),
                args.battleTimeLimit,
                args.timeLimit,
                args.pointLimit,
                args.nodeLayer,
                args.fogEnabled,
                args.clearFog,
                args.fogColor,
                args.fogSlope,
                args.fogOffset,
                args.light1Color,
                args.light1Vector,
                args.light2Color,
                args.light2Vector,
                args.farClip,
                args.killHeight,
                args.size,
                args.cameraMin,
                args.cameraMax,
                args.playerMin,
                args.playerMax,
                args.multiplayer,
                args.firstHunt,
                args.hybrid);
        }

        std::vector<std::shared_ptr<RoomMetadata>> CreateBaseRooms()
        {
            using Metadata::TimeLimit;

            return {

                MakeRoom(RoomArgs{
                    .id = 0, .name = "UNIT1_CX", .archive = "unit1_CX",
                    .modelPath = "unit1_cx_model.bin", .animationPath = "unit1_cx_anim.bin",
                    .collisionPath = "unit1_cx_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 2, .name = "UNIT1_CZ", .archive = "unit1_CZ",
                    .modelPath = "unit1_cz_model.bin", .animationPath = "unit1_cz_anim.bin",
                    .collisionPath = "unit1_cz_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 4, .name = "UNIT1_MORPH_CX", .archive = "unit1_morph_CX",
                    .modelPath = "unit1_morph_cx_model.bin", .animationPath = "unit1_morph_cx_anim.bin",
                    .collisionPath = "unit1_morph_cx_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 18, 6), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 18, 6),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 6, 4),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 6, .name = "UNIT1_MORPH_CZ", .archive = "unit1_morph_CZ",
                    .modelPath = "unit1_morph_cz_model.bin", .animationPath = "unit1_morph_cz_anim.bin",
                    .collisionPath = "unit1_morph_cz_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 8, .name = "UNIT2_CX", .archive = "unit2_CX",
                    .modelPath = "unit2_cx_model.bin", .animationPath = "unit2_cx_anim.bin",
                    .collisionPath = "unit2_cx_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 10, .name = "UNIT2_CZ", .archive = "unit2_CZ",
                    .modelPath = "unit2_cz_model.bin", .animationPath = "unit2_cz_anim.bin",
                    .collisionPath = "unit2_cz_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 12, .name = "UNIT3_CX", .archive = "unit3_CX",
                    .modelPath = "unit3_cx_model.bin", .animationPath = "unit3_cx_anim.bin",
                    .collisionPath = "unit3_cx_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 14, .name = "UNIT3_CZ", .archive = "unit3_CZ",
                    .modelPath = "unit3_cz_model.bin", .animationPath = "unit3_cz_anim.bin",
                    .collisionPath = "unit3_cz_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 16, .name = "UNIT4_CX", .archive = "unit4_CX",
                    .modelPath = "unit4_cx_model.bin", .animationPath = "unit4_cx_anim.bin",
                    .collisionPath = "unit4_cx_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(20, 27, 31),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(8, 8, 15),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 18, .name = "UNIT4_CZ", .archive = "unit4_CZ",
                    .modelPath = "unit4_cz_model.bin", .animationPath = "unit4_cz_anim.bin",
                    .collisionPath = "unit4_cz_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(20, 27, 31),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(8, 8, 15),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 20, .name = "CYLINDER_C1", .archive = "Cylinder_C1_CZ",
                    .modelPath = "Cylinder_C1_model.bin", .animationPath = "Cylinder_C1_anim.bin",
                    .collisionPath = "Cylinder_C1_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(8, 28, 20), .fogSlope = 4, .fogOffset = 65535,
                    .light1Color = ColorRgb(8, 28, 20),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 21, .name = "BIGEYE_C1", .archive = "BigEye_C1_CZ",
                    .modelPath = "bigeye_c1_model.bin", .animationPath = "bigeye_c1_anim.bin",
                    .collisionPath = "bigeye_c1_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(8, 28, 20), .fogSlope = 4, .fogOffset = 65535,
                    .light1Color = ColorRgb(8, 28, 20),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 22, .name = "UNIT1_RM1_CX", .archive = "unit1_RM1_CX",
                    .modelPath = "unit1_rm1_cx_model.bin", .animationPath = "unit1_rm1_cx_anim.bin",
                    .collisionPath = "unit1_rm1_cx_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65535,
                    .light1Color = ColorRgb(31, 24, 18),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 24, .name = "GOREA_C1", .archive = "Gorea_C1_CZ",
                    .modelPath = "Gorea_c1_Model.bin", .animationPath = "Gorea_c1_Anim.bin",
                    .collisionPath = "Gorea_c1_Collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(8, 28, 20), .fogSlope = 4, .fogOffset = 65535,
                    .light1Color = ColorRgb(8, 28, 20),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),
                MakeRoom(RoomArgs{
                    .id = 25, .name = "UNIT3_MORPH_CZ", .archive = "unit3_morph_CZ",
                    .modelPath = "unit3_morph_cz_model.bin", .animationPath = "unit3_morph_cz_anim.bin",
                    .collisionPath = "unit3_morph_cz_collision.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31),
                    .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14),
                    .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::None
                }),

                MakeRoom(RoomArgs{
                    .id = 27, .name = "UNIT1_LAND", .inGameName = "Alinos Gateway", .archive = "unit1_Land",
                    .modelPath = "unit1_land_model.bin", .animationPath = "unit1_land_anim.bin",
                    .collisionPath = "unit1_land_collision.bin", .texturePath = "unit1_land_tex.bin",
                    .entityPath = "Unit1_Land_Ent.bin", .nodePath = "unit1_Land_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 6553600, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 28, .name = "UNIT1_C0", .inGameName = "Echo Hall", .archive = "unit1_C0",
                    .modelPath = "unit1_c0_model.bin", .animationPath = "unit1_c0_anim.bin",
                    .collisionPath = "unit1_c0_collision.bin", .texturePath = "unit1_c0_tex.bin",
                    .entityPath = "Unit1_C0_Ent.bin", .nodePath = "unit1_C0_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 29, .name = "UNIT1_RM1", .inGameName = "High Ground", .archive = "unit1_RM1",
                    .modelPath = "unit1_RM1_model.bin", .animationPath = "unit1_RM1_anim.bin",
                    .collisionPath = "unit1_RM1_collision.bin", .texturePath = "unit1_rm1_tex.bin",
                    .entityPath = "unit1_RM1_Ent.bin", .nodePath = "unit1_RM1_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 30, .name = "UNIT1_C4", .inGameName = "Magma Drop", .archive = "unit1_C4",
                    .modelPath = "unit1_c4_model.bin", .animationPath = "unit1_c4_anim.bin",
                    .collisionPath = "unit1_c4_collision.bin", .texturePath = "unit1_c4_tex.bin",
                    .entityPath = "Unit1_C4_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -126976, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 31, .name = "UNIT1_RM6", .inGameName = "Elder Passage", .archive = "unit1_RM6",
                    .modelPath = "unit1_rm6_model.bin", .animationPath = "unit1_rm6_anim.bin",
                    .collisionPath = "unit1_rm6_collision.bin", .texturePath = "unit1_rm6_tex.bin",
                    .entityPath = "unit1_RM6_Ent.bin", .nodePath = "unit1_RM6_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 32, .name = "CRYSTALROOM", .inGameName = "Alimbic Cannon Control Room", .archive = "crystalroom",
                    .modelPath = "crystalroom_model.bin", .animationPath = "crystalroom_anim.bin",
                    .collisionPath = "crystalroom_collision.bin", .texturePath = "crystalroom_tex.bin",
                    .entityPath = "crystalroom_Ent.bin", .nodePath = "crystalroom_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(19, 29, 31), .fogSlope = 4, .fogOffset = 65535,
                    .light1Color = ColorRgb(19, 29, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 33, .name = "UNIT1_RM4", .inGameName = "Combat Hall", .archive = "mp3",
                    .modelPath = "mp3_Model.bin", .animationPath = "mp3_Anim.bin",
                    .collisionPath = "mp3_Collision.bin", .texturePath = "mp3_Tex.bin",
                    .entityPath = "unit1_rm4_Ent.bin", .nodePath = "unit1_RM4_Node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 34, .name = "UNIT1_TP1", .inGameName = "Stronghold Void A", .archive = "TeleportRoom",
                    .modelPath = "TeleportRoom_model.bin", .animationPath = "TeleportRoom_anim.bin",
                    .collisionPath = "TeleportRoom_collision.bin", .texturePath = "teleportroom_tex.bin",
                    .entityPath = "Unit1_TP1_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 18, 6), .fogSlope = 6, .fogOffset = 65350,
                    .light1Color = ColorRgb(8, 28, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(20, 8, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 35, .name = "UNIT1_B1", .inGameName = "Biodefense Chamber 02", .archive = "bigeyeroom",
                    .modelPath = "bigeyeroom_model.bin", .animationPath = "bigeyeroom_anim.bin",
                    .collisionPath = "bigeyeroom_collision.bin", .texturePath = "bigeyeroom_tex.bin",
                    .entityPath = "Unit1_b1_Ent.bin", .nodePath = "unit2_b2_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(12, 6, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(12, 6, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(31, 25, 21), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 36, .name = "UNIT1_C1", .inGameName = "Alimbic Gardens", .archive = "unit1_C1",
                    .modelPath = "unit1_c1_model.bin", .animationPath = "unit1_c1_anim.bin",
                    .collisionPath = "unit1_c1_collision.bin", .texturePath = "unit1_c1_tex.bin",
                    .entityPath = "Unit1_C1_Ent.bin", .nodePath = "unit1_C1_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 37, .name = "UNIT1_C2", .inGameName = "Thermal Vast", .archive = "unit1_C2",
                    .modelPath = "unit1_c2_model.bin", .animationPath = "unit1_c2_anim.bin",
                    .collisionPath = "unit1_c2_collision.bin", .texturePath = "unit1_c2_tex.bin",
                    .entityPath = "Unit1_C2_Ent.bin", .nodePath = "unit1_C2_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 38, .name = "UNIT1_C5", .inGameName = "Piston Cave", .archive = "unit1_C5",
                    .modelPath = "unit1_c5_model.bin", .animationPath = "unit1_c5_anim.bin",
                    .collisionPath = "unit1_c5_collision.bin", .texturePath = "unit1_c5_tex.bin",
                    .entityPath = "Unit1_C5_Ent.bin", .nodePath = "unit1_RM5_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 3, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 18, 6), .fogSlope = 4, .fogOffset = 65300,
                    .light1Color = ColorRgb(31, 18, 6), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 9, 4), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 39, .name = "UNIT1_RM2", .inGameName = "Alinos Perch", .archive = "unit1_RM2",
                    .modelPath = "unit1_rm2_model.bin", .animationPath = "unit1_rm2_anim.bin",
                    .collisionPath = "unit1_rm2_collision.bin", .texturePath = "unit1_rm2_tex.bin",
                    .entityPath = "unit1_RM2_ent.bin", .nodePath = "unit1_RM2_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 3, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 40, .name = "UNIT1_RM3", .inGameName = "Council Chamber", .archive = "unit1_RM3",
                    .modelPath = "unit1_rm3_model.bin", .animationPath = "unit1_rm3_anim.bin",
                    .collisionPath = "unit1_rm3_collision.bin", .texturePath = "unit1_rm3_tex.bin",
                    .entityPath = "unit1_rm3_Ent.bin", .nodePath = "unit1_RM3_Node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 3, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 41, .name = "UNIT1_RM5", .inGameName = "Processor Core", .archive = "mp7",
                    .modelPath = "mp7_model.bin", .animationPath = "mp7_anim.bin",
                    .collisionPath = "mp7_collision.bin", .texturePath = "mp7_tex.bin",
                    .entityPath = "unit1_rm5_Ent.bin", .nodePath = "unit1_RM5_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 3, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 18, 6), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 18, 6), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 6, 4), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 42, .name = "UNIT1_C3", .inGameName = "Crash Site", .archive = "unit1_C3",
                    .modelPath = "unit1_c3_model.bin", .animationPath = "unit1_c3_anim.bin",
                    .collisionPath = "unit1_c3_collision.bin", .texturePath = "unit1_c3_tex.bin",
                    .entityPath = "Unit1_C3_Ent.bin", .nodePath = "unit1_C3_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 43, .name = "UNIT1_TP2", .inGameName = "Stronghold Void B", .archive = "TeleportRoom",
                    .modelPath = "TeleportRoom_model.bin", .animationPath = "TeleportRoom_anim.bin",
                    .collisionPath = "TeleportRoom_collision.bin", .texturePath = "teleportroom_tex.bin",
                    .entityPath = "Unit1_TP2_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 2, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 18, 6), .fogSlope = 6, .fogOffset = 65350,
                    .light1Color = ColorRgb(8, 28, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 44, .name = "UNIT1_B2", .inGameName = "Biodefense Chamber 06", .archive = "cylinderroom",
                    .modelPath = "cylinderroom_model.bin", .animationPath = "cylinderroom_anim.bin",
                    .collisionPath = "cylinderroom_collision.bin", .texturePath = "cylinderroom_tex.bin",
                    .entityPath = "Unit1_b2_Ent.bin", .nodePath = "unit2_b1_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(12, 6, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(12, 6, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(31, 25, 21), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),

                MakeRoom(RoomArgs{
                    .id = 45, .name = "UNIT2_LAND", .inGameName = "Celestial Gateway", .archive = "unit2_Land",
                    .modelPath = "unit2_Land_model.bin", .animationPath = "unit2_Land_anim.bin",
                    .collisionPath = "unit2_Land_collision.bin", .texturePath = "unit2_land_tex.bin",
                    .entityPath = "unit2_Land_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 24, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 6553600, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 46, .name = "UNIT2_C0", .inGameName = "Helm Room", .archive = "unit2_C0",
                    .modelPath = "unit2_c0_model.bin", .animationPath = "unit2_c0_anim.bin",
                    .collisionPath = "unit2_c0_collision.bin", .texturePath = "unit2_c0_tex.bin",
                    .entityPath = "unit2_C0_Ent.bin", .nodePath = "unit2_C0_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 6553600, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 47, .name = "UNIT2_C1", .inGameName = "Meditation Room", .archive = "unit2_C1",
                    .modelPath = "unit2_c1_model.bin", .animationPath = "unit2_c1_anim.bin",
                    .collisionPath = "unit2_c1_collision.bin", .texturePath = "unit2_c1_tex.bin",
                    .entityPath = "unit2_C1_Ent.bin", .nodePath = "unit2_C1_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 48, .name = "UNIT2_RM1", .inGameName = "Data Shrine 01", .archive = "mp1",
                    .modelPath = "mp1_Model.bin", .animationPath = "mp1_Anim.bin",
                    .collisionPath = "mp1_Collision.bin", .texturePath = "mp1_tex.bin",
                    .entityPath = "unit2_RM1_Ent.bin", .nodePath = "unit2_RM1_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 49, .name = "UNIT2_C2", .inGameName = "Fan Room Alpha", .archive = "unit2_C2",
                    .modelPath = "unit2_c2_model.bin", .animationPath = "unit2_c2_anim.bin",
                    .collisionPath = "unit2_c2_collision.bin", .texturePath = "unit2_c2_tex.bin",
                    .entityPath = "unit2_C2_Ent.bin", .nodePath = "unit2_C2_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 50, .name = "UNIT2_RM2", .inGameName = "Data Shrine 02", .archive = "mp1",
                    .modelPath = "mp1_Model.bin", .animationPath = "mp1_Anim.bin",
                    .collisionPath = "mp1_Collision.bin", .texturePath = "mp1_tex.bin",
                    .entityPath = "unit2_RM2_Ent.bin", .nodePath = "unit2_RM2_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 2, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 51, .name = "UNIT2_C3", .inGameName = "Fan Room Beta", .archive = "unit2_C3",
                    .modelPath = "unit2_c3_model.bin", .animationPath = "unit2_c3_anim.bin",
                    .collisionPath = "unit2_c3_collision.bin", .texturePath = "unit2_c3_tex.bin",
                    .entityPath = "unit2_C3_Ent.bin", .nodePath = "unit2_C3_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 52, .name = "UNIT2_RM3", .inGameName = "Data Shrine 03", .archive = "unit2_RM3",
                    .modelPath = "unit2_RM3_model.bin", .animationPath = "unit2_RM3_anim.bin",
                    .collisionPath = "unit2_RM3_collision.bin", .texturePath = "unit2_rm3_tex.bin",
                    .entityPath = "unit2_RM3_Ent.bin", .nodePath = "unit2_RM3_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 3, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 53, .name = "UNIT2_C4", .inGameName = "Synergy Core", .archive = "unit2_C4",
                    .modelPath = "unit2_c4_model.bin", .animationPath = "unit2_c4_anim.bin",
                    .collisionPath = "unit2_c4_collision.bin", .texturePath = "unit2_c4_tex.bin",
                    .entityPath = "unit2_C4_Ent.bin", .nodePath = "unit2_C4_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 54, .name = "UNIT2_TP1", .inGameName = "Stronghold Void A", .archive = "TeleportRoom",
                    .modelPath = "TeleportRoom_model.bin", .animationPath = "TeleportRoom_anim.bin",
                    .collisionPath = "TeleportRoom_collision.bin", .texturePath = "teleportroom_tex.bin",
                    .entityPath = "Unit2_TP1_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 3, .fogEnabled = true,
                    .fogColor = ColorRgb(17, 29, 16), .fogSlope = 6, .fogOffset = 65350,
                    .light1Color = ColorRgb(8, 28, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 55, .name = "UNIT2_B1", .inGameName = "Biodefense Chamber 01", .archive = "cylinderroom",
                    .modelPath = "cylinderroom_model.bin", .animationPath = "cylinderroom_anim.bin",
                    .collisionPath = "cylinderroom_collision.bin", .texturePath = "cylinderroom_tex.bin",
                    .entityPath = "Unit2_b1_Ent.bin", .nodePath = "unit2_b1_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(12, 6, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(12, 6, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(31, 25, 21), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 56, .name = "UNIT2_C6", .inGameName = "Tetra Vista", .archive = "unit2_C6",
                    .modelPath = "unit2_c6_model.bin", .animationPath = "unit2_c6_anim.bin",
                    .collisionPath = "unit2_c6_collision.bin", .texturePath = "unit2_c6_tex.bin",
                    .entityPath = "Unit2_C6_Ent.bin", .nodePath = "unit2_C6_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(18, 31, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 16384000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 57, .name = "UNIT2_C7", .inGameName = "New Arrival Registration", .archive = "unit2_C7",
                    .modelPath = "unit2_c7_model.bin", .animationPath = "unit2_c7_anim.bin",
                    .collisionPath = "unit2_c7_collision.bin", .texturePath = "unit2_c7_tex.bin",
                    .entityPath = "Unit2_C7_Ent.bin", .nodePath = "unit2_C7_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 16384000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 58, .name = "UNIT2_RM4", .inGameName = "Transfer Lock", .archive = "unit2_RM4",
                    .modelPath = "unit2_rm4_model.bin", .animationPath = "unit2_rm4_anim.bin",
                    .collisionPath = "unit2_rm4_collision.bin", .texturePath = "unit2_rm4_tex.bin",
                    .entityPath = "Unit2_RM4_Ent.bin", .nodePath = "unit2_RM4_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 1, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(29, 20, 10), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(29, 20, 10), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 59, .name = "UNIT2_RM5", .inGameName = "Incubation Vault 01", .archive = "mp10",
                    .modelPath = "mp10_model.bin", .animationPath = "mp10_anim.bin",
                    .collisionPath = "mp10_collision.bin", .texturePath = "mp10_tex.bin",
                    .entityPath = "Unit2_RM5_Ent.bin", .nodePath = "unit2_RM5_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 1, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(0, 25, 31), .fogSlope = 4, .fogOffset = 31727,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 60, .name = "UNIT2_RM6", .inGameName = "Incubation Vault 02", .archive = "mp10",
                    .modelPath = "mp10_model.bin", .animationPath = "mp10_anim.bin",
                    .collisionPath = "mp10_collision.bin", .texturePath = "mp10_tex.bin",
                    .entityPath = "Unit2_RM6_Ent.bin", .nodePath = "unit2_RM6_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 2, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 819200, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 61, .name = "UNIT2_RM7", .inGameName = "Incubation Vault 03", .archive = "mp10",
                    .modelPath = "mp10_model.bin", .animationPath = "mp10_anim.bin",
                    .collisionPath = "mp10_collision.bin", .texturePath = "mp10_tex.bin",
                    .entityPath = "Unit2_RM7_Ent.bin", .nodePath = "unit2_RM7_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 3, .fogEnabled = true,
                    .fogColor = ColorRgb(0, 31, 10), .fogSlope = 4, .fogOffset = 31727,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 819200, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 62, .name = "UNIT2_RM8", .inGameName = "Docking Bay", .archive = "unit2_RM8",
                    .modelPath = "unit2_rm8_model.bin", .animationPath = "unit2_rm8_anim.bin",
                    .collisionPath = "unit2_rm8_collision.bin", .texturePath = "unit2_rm8_tex.bin",
                    .entityPath = "unit2_RM8_Ent.bin", .nodePath = "unit2_RM8_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 1, .nodeLayer = 1,
                    .fogColor = ColorRgb(29, 20, 10), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(29, 20, 10), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 63, .name = "UNIT2_TP2", .inGameName = "Stronghold Void B", .archive = "TeleportRoom",
                    .modelPath = "TeleportRoom_model.bin", .animationPath = "TeleportRoom_anim.bin",
                    .collisionPath = "TeleportRoom_collision.bin", .texturePath = "teleportroom_tex.bin",
                    .entityPath = "Unit2_TP2_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 4, .fogEnabled = true,
                    .fogColor = ColorRgb(17, 29, 16), .fogSlope = 6, .fogOffset = 65350,
                    .light1Color = ColorRgb(8, 28, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 64, .name = "UNIT2_B2", .inGameName = "Biodefense Chamber 05", .archive = "bigeyeroom",
                    .modelPath = "bigeyeroom_model.bin", .animationPath = "bigeyeroom_anim.bin",
                    .collisionPath = "bigeyeroom_collision.bin", .texturePath = "bigeyeroom_tex.bin",
                    .entityPath = "Unit2_b2_Ent.bin", .nodePath = "unit2_b2_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(12, 6, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(12, 6, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(31, 25, 21), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 65, .name = "UNIT3_LAND", .inGameName = "VDO Gateway", .archive = "unit3_Land",
                    .modelPath = "unit3_land_model.bin", .animationPath = "unit3_land_anim.bin",
                    .collisionPath = "unit3_land_collision.bin", .texturePath = "unit3_land_tex.bin",
                    .entityPath = "unit3_Land_Ent.bin", .nodePath = "unit3_Land_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 6553600, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 66, .name = "UNIT3_C0", .inGameName = "Bioweaponry Lab", .archive = "unit3_C0",
                    .modelPath = "unit3_c0_model.bin", .animationPath = "unit3_c0_anim.bin",
                    .collisionPath = "unit3_c0_collision.bin", .texturePath = "unit3_c0_tex.bin",
                    .entityPath = "unit3_C0_Ent.bin", .nodePath = "unit3_C0_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 67, .name = "UNIT3_C2", .inGameName = "Cortex CPU", .archive = "unit3_C2",
                    .modelPath = "unit3_c2_model.bin", .animationPath = "unit3_c2_anim.bin",
                    .collisionPath = "unit3_c2_collision.bin", .texturePath = "unit3_c2_tex.bin",
                    .entityPath = "Unit3_C2_Ent.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 50, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 68, .name = "UNIT3_RM1", .inGameName = "Weapons Complex", .archive = "unit3_RM1",
                    .modelPath = "unit3_rm1_model.bin", .animationPath = "unit3_rm1_anim.bin",
                    .collisionPath = "unit3_rm1_collision.bin", .texturePath = "unit3_rm1_Tex.bin",
                    .entityPath = "Unit3_RM1_Ent.bin", .nodePath = "unit3_RM1_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 50, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 69, .name = "UNIT3_RM4", .inGameName = "Compression Chamber", .archive = "mp5",
                    .modelPath = "mp5_Model.bin", .animationPath = "mp5_Anim.bin",
                    .collisionPath = "mp5_Collision.bin", .texturePath = "mp5_tex.bin",
                    .entityPath = "unit3_rm4_Ent.bin", .nodePath = "Unit3_RM4_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 50, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 70, .name = "UNIT3_TP1", .inGameName = "Stronghold Void A", .archive = "TeleportRoom",
                    .modelPath = "TeleportRoom_model.bin", .animationPath = "TeleportRoom_anim.bin",
                    .collisionPath = "TeleportRoom_collision.bin", .texturePath = "teleportroom_tex.bin",
                    .entityPath = "Unit3_TP1_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 5, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 6, .fogOffset = 65350,
                    .light1Color = ColorRgb(8, 28, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 71, .name = "UNIT3_B1", .inGameName = "Biodefense Chamber 03", .archive = "cylinderroom",
                    .modelPath = "cylinderroom_model.bin", .animationPath = "cylinderroom_anim.bin",
                    .collisionPath = "cylinderroom_collision.bin", .texturePath = "cylinderroom_tex.bin",
                    .entityPath = "Unit3_b1_Ent.bin", .nodePath = "unit2_b1_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(12, 6, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(12, 6, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(31, 25, 21), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 72, .name = "UNIT3_C1", .inGameName = "Ascension", .archive = "unit3_C1",
                    .modelPath = "unit3_c1_model.bin", .animationPath = "unit3_c1_anim.bin",
                    .collisionPath = "unit3_c1_collision.bin", .texturePath = "unit3_c1_tex.bin",
                    .entityPath = "unit3_C1_Ent.bin", .nodePath = "unit3_C1_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 73, .name = "UNIT3_RM2", .inGameName = "Fuel Stack", .archive = "unit3_RM2",
                    .modelPath = "unit3_rm2_model.bin", .animationPath = "unit3_rm2_anim.bin",
                    .collisionPath = "unit3_rm2_collision.bin", .texturePath = "unit3_rm2_tex.bin",
                    .entityPath = "Unit3_RM2_Ent.bin", .nodePath = "unit3_RM2_node.bin",
                    .battleTimeLimit = TimeLimit(16, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 2457600, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 74, .name = "UNIT3_RM3", .inGameName = "Stasis Bunker", .archive = "e3Level",
                    .modelPath = "e3Level_Model.bin", .animationPath = "e3Level_Anim.bin",
                    .collisionPath = "e3Level_Collision.bin", .texturePath = "e3level_tex.bin",
                    .entityPath = "Unit3_RM3_Ent.bin", .nodePath = "unit3_rm3_Node.bin",
                    .battleTimeLimit = TimeLimit(16, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 75, .name = "UNIT3_TP2", .inGameName = "Stronghold Void B", .archive = "TeleportRoom",
                    .modelPath = "TeleportRoom_model.bin", .animationPath = "TeleportRoom_anim.bin",
                    .collisionPath = "TeleportRoom_collision.bin", .texturePath = "teleportroom_tex.bin",
                    .entityPath = "Unit3_TP2_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 6, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 6, .fogOffset = 65350,
                    .light1Color = ColorRgb(8, 28, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 76, .name = "UNIT3_B2", .inGameName = "Biodefense Chamber 08", .archive = "bigeyeroom",
                    .modelPath = "bigeyeroom_model.bin", .animationPath = "bigeyeroom_anim.bin",
                    .collisionPath = "bigeyeroom_collision.bin", .texturePath = "bigeyeroom_tex.bin",
                    .entityPath = "Unit3_b2_Ent.bin", .nodePath = "unit2_b2_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 2, .fogEnabled = true,
                    .fogColor = ColorRgb(12, 6, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(12, 6, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(31, 25, 21), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 77, .name = "UNIT4_LAND", .inGameName = "Arcterra Gateway", .archive = "unit4_Land",
                    .modelPath = "unit4_land_model.bin", .animationPath = "unit4_land_anim.bin",
                    .collisionPath = "unit4_land_collision.bin", .texturePath = "unit4_land_tex.bin",
                    .entityPath = "unit4_Land_Ent.bin", .nodePath = "unit4_Land_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(10, 14, 16), .fogSlope = 4, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 78, .name = "UNIT4_RM1", .inGameName = "Ice Hive", .archive = "unit4_rm1",
                    .modelPath = "unit4_rm1_model.bin", .animationPath = "unit4_rm1_anim.bin",
                    .collisionPath = "unit4_rm1_collision.bin", .texturePath = "unit4_rm1_Tex.bin",
                    .entityPath = "Unit4_RM1_Ent.bin", .nodePath = "unit4_RM1_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(20, 27, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(8, 8, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 327680, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 79, .name = "UNIT4_RM3", .inGameName = "Sic Transit", .archive = "mp12",
                    .modelPath = "mp12_model.bin", .animationPath = "mp12_anim.bin",
                    .collisionPath = "mp12_collision.bin", .texturePath = "mp12_Tex.bin",
                    .entityPath = "unit4_rm3_Ent.bin", .nodePath = "Unit4_RM3_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(10, 14, 16), .fogSlope = 4, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 80, .name = "UNIT4_C0", .inGameName = "Frost Labyrinth", .archive = "unit4_C0",
                    .modelPath = "unit4_c0_model.bin", .animationPath = "unit4_c0_anim.bin",
                    .collisionPath = "unit4_c0_collision.bin", .texturePath = "unit4_c0_tex.bin",
                    .entityPath = "unit4_C0_Ent.bin", .nodePath = "unit4_C0_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 4, .fogOffset = 65300,
                    .light1Color = ColorRgb(20, 27, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(8, 8, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 81, .name = "UNIT4_TP1", .inGameName = "Stronghold Void A", .archive = "TeleportRoom",
                    .modelPath = "TeleportRoom_model.bin", .animationPath = "TeleportRoom_anim.bin",
                    .collisionPath = "TeleportRoom_collision.bin", .texturePath = "teleportroom_tex.bin",
                    .entityPath = "Unit4_TP1_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 7, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 6, .fogOffset = 65350,
                    .light1Color = ColorRgb(8, 28, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 82, .name = "UNIT4_B1", .inGameName = "Biodefense Chamber 04", .archive = "bigeyeroom",
                    .modelPath = "bigeyeroom_model.bin", .animationPath = "bigeyeroom_anim.bin",
                    .collisionPath = "bigeyeroom_collision.bin", .texturePath = "bigeyeroom_tex.bin",
                    .entityPath = "unit4_b1_Ent.bin", .nodePath = "unit2_b2_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(12, 6, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(12, 6, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(31, 25, 21), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 83, .name = "UNIT4_C1", .inGameName = "Drip Moat", .archive = "unit4_C1",
                    .modelPath = "unit4_c1_model.bin", .animationPath = "unit4_c1_anim.bin",
                    .collisionPath = "unit4_c1_collision.bin", .texturePath = "unit4_c1_tex.bin",
                    .entityPath = "unit4_C1_Ent.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 5, .nodeLayer = 1, .fogEnabled = true, .clearFog = true,
                    .fogColor = ColorRgb(0, 0, 0), .fogSlope = 4, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 84, .name = "UNIT4_RM2", .inGameName = "Subterranean", .archive = "unit4_rm2",
                    .modelPath = "unit4_rm2_model.bin", .animationPath = "unit4_rm2_anim.bin",
                    .collisionPath = "unit4_rm2_collision.bin", .texturePath = "unit4_rm2_tex.bin",
                    .entityPath = "Unit4_RM2_Ent.bin", .nodePath = "unit4_RM2_node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 5, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(10, 14, 16), .fogSlope = 4, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 85, .name = "UNIT4_RM4", .inGameName = "Sanctorus", .archive = "mp11",
                    .modelPath = "mp11_model.bin", .animationPath = "mp11_anim.bin",
                    .collisionPath = "mp11_collision.bin", .texturePath = "mp11_tex.bin",
                    .entityPath = "unit4_rm4_Ent.bin", .nodePath = "Unit4_RM4_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(20, 27, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(8, 8, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 327680, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 86, .name = "UNIT4_RM5", .inGameName = "Fault Line", .archive = "unit4_rm5",
                    .modelPath = "unit4_rm5_model.bin", .animationPath = "unit4_rm5_anim.bin",
                    .collisionPath = "unit4_rm5_collision.bin", .texturePath = "unit4_rm5_tex.bin",
                    .entityPath = "Unit4_RM5_Ent.bin", .nodePath = "Unit4_RM5_node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 5, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(10, 14, 16), .fogSlope = 4, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 87, .name = "UNIT4_TP2", .inGameName = "Stronghold Void B", .archive = "TeleportRoom",
                    .modelPath = "TeleportRoom_model.bin", .animationPath = "TeleportRoom_anim.bin",
                    .collisionPath = "TeleportRoom_collision.bin", .texturePath = "teleportroom_tex.bin",
                    .entityPath = "Unit4_TP2_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 7, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 6, .fogOffset = 65350,
                    .light1Color = ColorRgb(8, 28, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 88, .name = "UNIT4_B2", .inGameName = "Biodefense Chamber 07", .archive = "cylinderroom",
                    .modelPath = "cylinderroom_model.bin", .animationPath = "cylinderroom_anim.bin",
                    .collisionPath = "cylinderroom_collision.bin", .texturePath = "cylinderroom_tex.bin",
                    .entityPath = "unit4_b2_Ent.bin", .nodePath = "unit2_b1_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(12, 6, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(12, 6, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(31, 25, 21), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 89, .name = "Gorea_Land", .archive = "Gorea_Land",
                    .modelPath = "Gorea_Land_Model.bin", .animationPath = "Gorea_Land_Anim.bin",
                    .collisionPath = "Gorea_Land_collision.bin", .texturePath = "Gorea_Land_tex.bin",
                    .entityPath = "Gorea_Land_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(17, 29, 16), .fogSlope = 6, .fogOffset = 65330,
                    .light1Color = ColorRgb(17, 29, 16), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 8192000, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 90, .name = "Gorea_Peek", .archive = "Gorea_b2",
                    .modelPath = "gorea_b2_Model.bin", .animationPath = "gorea_b2_Anim.bin",
                    .collisionPath = "Gorea_b2_collision.bin", .texturePath = "gorea_b2_tex.bin",
                    .entityPath = "Gorea_Peek_Ent.bin", .nodePath = "gorea_b2_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(17, 29, 16), .fogSlope = 5, .fogOffset = 65535,
                    .light1Color = ColorRgb(19, 27, 16), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 6, 12), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -286720, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 91, .name = "Gorea_b1", .archive = "Gorea_b1",
                    .modelPath = "Gorea_b1_Model.bin", .animationPath = "Gorea_b1_Anim.bin",
                    .collisionPath = "Gorea_b1_collision.bin", .texturePath = "Gorea_b1_tex.bin",
                    .entityPath = "Gorea_b1_Ent.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true, .clearFog = true,
                    .fogColor = ColorRgb(9, 18, 24), .fogSlope = 4, .fogOffset = 32550,
                    .light1Color = ColorRgb(18, 24, 27), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 18, 24), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 92, .name = "Gorea_b2", .archive = "Gorea_b2",
                    .modelPath = "gorea_b2_Model.bin", .animationPath = "gorea_b2_Anim.bin",
                    .collisionPath = "Gorea_b2_collision.bin", .texturePath = "gorea_b2_tex.bin",
                    .entityPath = "gorea_b2_Ent.bin", .nodePath = "gorea_b2_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 2, .fogEnabled = true,
                    .fogColor = ColorRgb(16, 30, 25), .fogSlope = 4, .fogOffset = 65535,
                    .light1Color = ColorRgb(18, 16, 14), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(27, 18, 9), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -286720, .size = RoomSize::SinglePlayer
                }),
                MakeRoom(RoomArgs{
                    .id = 93, .name = "MP1 SANCTORUS", .inGameName = "Data Shrine", .archive = "mp1",
                    .modelPath = "mp1_Model.bin", .animationPath = "mp1_Anim.bin",
                    .collisionPath = "mp1_Collision.bin", .texturePath = "mp1_tex.bin",
                    .entityPath = "mp1_Ent.bin", .nodePath = "mp1_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 368640, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 94, .name = "MP2 HARVESTER", .inGameName = "Harvester", .archive = "mp2",
                    .modelPath = "mp2_model.bin", .animationPath = "mp2_anim.bin",
                    .collisionPath = "mp2_collision.bin", .texturePath = "mp2_tex.bin",
                    .entityPath = "mp2_Ent.bin", .nodePath = "mp2_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true, .clearFog = true,
                    .fogColor = ColorRgb(25, 30, 20), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(25, 30, 20), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 11, 6), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 3072000, .killHeight = 4096, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-167936, 24576, -294912).ToFloatVector(),
                    .cameraMax = Vector3Fx(167936, 131072, 294912).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 95, .name = "MP3 PROVING GROUND", .inGameName = "Combat Hall", .archive = "mp3",
                    .modelPath = "mp3_Model.bin", .animationPath = "mp3_Anim.bin",
                    .collisionPath = "mp3_Collision.bin", .texturePath = "mp3_Tex.bin",
                    .entityPath = "mp3_Ent.bin", .nodePath = "mp3_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 5, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .farClip = 1433600, .killHeight = -122880, .size = RoomSize::Small,
                    .cameraMin = Vector3Fx(-45056, 0, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(40960, 53248, 106496).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 96, .name = "MP4 HIGHGROUND - EXPANDED", .inGameName = "Elder Passage", .archive = "mp4",
                    .modelPath = "mp4_model.bin", .animationPath = "mp4_anim.bin",
                    .collisionPath = "mp4_collision.bin", .texturePath = "mp4_Tex.bin",
                    .entityPath = "mp4_Ent.bin", .nodePath = "mp4_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 5, .fogOffset = 65200,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1687552, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-151552, -4096, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(184320, 102400, 81920).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 97, .name = "MP4 HIGHGROUND", .inGameName = "High Ground", .archive = "unit1_RM1",
                    .modelPath = "unit1_RM1_model.bin", .animationPath = "unit1_RM1_anim.bin",
                    .collisionPath = "unit1_RM1_collision.bin", .texturePath = "unit1_rm1_tex.bin",
                    .entityPath = "mp4_dm1_Ent.bin", .nodePath = "mp4_dm1_Node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 5, .fogOffset = 65200,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1740800, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-151552, -4096, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(184320, 102400, 81920).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 98, .name = "MP5 FUEL SLUICE", .inGameName = "Compression Chamber", .archive = "mp5",
                    .modelPath = "mp5_Model.bin", .animationPath = "mp5_Anim.bin",
                    .collisionPath = "mp5_Collision.bin", .texturePath = "mp5_tex.bin",
                    .entityPath = "mp5_Ent.bin", .nodePath = "mp5_Node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 3, .fogOffset = 65152,
                    .light1Color = ColorRgb(18, 22, 30), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Small,
                    .cameraMin = Vector3Fx(-98304, 0, -57344).ToFloatVector(),
                    .cameraMax = Vector3Fx(110592, 69632, 57344).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 99, .name = "MP6 HEADSHOT", .inGameName = "Head Shot", .archive = "mp6",
                    .modelPath = "mp6_model.bin", .animationPath = "mp6_anim.bin",
                    .collisionPath = "mp6_collision.bin", .texturePath = "mp6_tex.bin",
                    .entityPath = "mp6_Ent.bin", .nodePath = "mp6_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 3, .fogOffset = 65152,
                    .light1Color = ColorRgb(18, 22, 30), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 5734400, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-200704, 0, -200704).ToFloatVector(),
                    .cameraMax = Vector3Fx(196608, 196608, 192512).ToFloatVector(),
                    .playerMin = Vector3Fx(-286720, -86949, -286720).ToFloatVector(),
                    .playerMax = Vector3Fx(286720, 196608, 286720).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 100, .name = "MP7 PROCESSOR CORE", .inGameName = "Processor Core", .archive = "mp7",
                    .modelPath = "mp7_model.bin", .animationPath = "mp7_anim.bin",
                    .collisionPath = "mp7_collision.bin", .texturePath = "mp7_tex.bin",
                    .entityPath = "mp7_Ent.bin", .nodePath = "mp7_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 18, 6), .fogSlope = 6, .fogOffset = 65300,
                    .light1Color = ColorRgb(31, 18, 6), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 6, 4), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 819200, .killHeight = -122880, .size = RoomSize::Small,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 101, .name = "MP8 FIRE CONTROL", .inGameName = "Weapons Complex", .archive = "mp8",
                    .modelPath = "mp8_model.bin", .animationPath = "mp8_anim.bin",
                    .collisionPath = "mp8_collision.bin", .texturePath = "mp8_Tex.bin",
                    .entityPath = "mp8_Ent.bin", .nodePath = "mp8_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 50, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 24, 31), .fogSlope = 3, .fogOffset = 65152,
                    .light1Color = ColorRgb(18, 22, 30), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 4096000, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-176128, 0, -135168).ToFloatVector(),
                    .cameraMax = Vector3Fx(180224, 135168, 225280).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 102, .name = "MP9 CRYOCHASM", .inGameName = "Ice Hive", .archive = "mp9",
                    .modelPath = "mp9_model.bin", .animationPath = "mp9_anim.bin",
                    .collisionPath = "mp9_collision.bin", .texturePath = "mp9_tex.bin",
                    .entityPath = "mp9_Ent.bin", .nodePath = "mp9_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(20, 27, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(8, 8, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 327680, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 103, .name = "MP10 OVERLOAD", .inGameName = "Incubation Vault", .archive = "mp10",
                    .modelPath = "mp10_model.bin", .animationPath = "mp10_anim.bin",
                    .collisionPath = "mp10_collision.bin", .texturePath = "mp10_tex.bin",
                    .entityPath = "mp10_Ent.bin", .nodePath = "mp10_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 409600, .killHeight = -122880, .size = RoomSize::Small,
                    .cameraMin = Vector3Fx(-90112, -8192, -61440).ToFloatVector(),
                    .cameraMax = Vector3Fx(73728, 54476, 45056).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 104, .name = "MP11 BREAKTHROUGH", .inGameName = "Sanctorus", .archive = "mp11",
                    .modelPath = "mp11_model.bin", .animationPath = "mp11_anim.bin",
                    .collisionPath = "mp11_collision.bin", .texturePath = "mp11_tex.bin",
                    .entityPath = "mp11_Ent.bin", .nodePath = "mp11_Node.bin",
                    .battleTimeLimit = TimeLimit(16, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(20, 27, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(20, 27, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(8, 8, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 409600, .killHeight = -122880, .size = RoomSize::Small,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 105, .name = "MP12 SIC TRANSIT", .inGameName = "Sic Transit", .archive = "mp12",
                    .modelPath = "mp12_model.bin", .animationPath = "mp12_anim.bin",
                    .collisionPath = "mp12_collision.bin", .texturePath = "mp12_Tex.bin",
                    .entityPath = "mp12_Ent.bin", .nodePath = "mp12_node.bin",
                    .battleTimeLimit = TimeLimit(16, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(10, 14, 18), .fogSlope = 6, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 819200, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-77824, -4096, -118784).ToFloatVector(),
                    .cameraMax = Vector3Fx(110592, 61440, 106496).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 106, .name = "MP13 ACCELERATOR", .inGameName = "Fuel Stack", .archive = "mp13",
                    .modelPath = "mp13_model.bin", .animationPath = "mp13_anim.bin",
                    .collisionPath = "mp13_collision.bin", .texturePath = "mp13_tex.bin",
                    .entityPath = "mp13_Ent.bin", .nodePath = "mp13_node.bin",
                    .battleTimeLimit = TimeLimit(16, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 3, .fogOffset = 65152,
                    .light1Color = ColorRgb(18, 22, 30), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-94208, -118784, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(167936, 122880, 172032).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 107, .name = "MP14 OUTER REACH", .inGameName = "Outer Reach", .archive = "mp14",
                    .modelPath = "mp14_model.bin", .animationPath = "mp14_anim.bin",
                    .collisionPath = "mp14_collision.bin", .texturePath = "mp14_tex.bin",
                    .entityPath = "mp14_Ent.bin", .nodePath = "mp14_node.bin",
                    .battleTimeLimit = TimeLimit(16, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 4915200, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-131072, -14745, -122880).ToFloatVector(),
                    .cameraMax = Vector3Fx(126976, 98304, 118784).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 108, .name = "CTF1 FAULT LINE - EXPANDED", .inGameName = "Fault Line", .archive = "ctf1",
                    .modelPath = "ctf1_model.bin", .animationPath = "ctf1_anim.bin",
                    .collisionPath = "ctf1_collision.bin", .texturePath = "ctf1_tex.bin",
                    .entityPath = "ctf1_Ent.bin", .nodePath = "ctf1_node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 5, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(10, 14, 18), .fogSlope = 6, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-294912, 0, -106496).ToFloatVector(),
                    .cameraMax = Vector3Fx(286720, 96256, 106496).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 109, .name = "CTF1_FAULT LINE", .inGameName = "Subterranean", .archive = "unit4_rm5",
                    .modelPath = "unit4_rm5_model.bin", .animationPath = "unit4_rm5_anim.bin",
                    .collisionPath = "unit4_rm5_collision.bin", .texturePath = "unit4_rm5_tex.bin",
                    .entityPath = "ctf1_dm1_Ent.bin", .nodePath = "ctf1_dm1_NODE.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 5, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(10, 14, 18), .fogSlope = 6, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-294912, 0, -106496).ToFloatVector(),
                    .cameraMax = Vector3Fx(286720, 96256, 106496).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 110, .name = "AD1 TRANSFER LOCK BT", .inGameName = "Transfer Lock", .archive = "ad1",
                    .modelPath = "ad1_model.bin", .animationPath = "ad1_anim.bin",
                    .collisionPath = "ad1_collision.bin", .texturePath = "ad1_tex.bin",
                    .entityPath = "ad1_Ent.bin", .nodePath = "ad1_node.bin",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 1, .nodeLayer = 1,
                    .fogColor = ColorRgb(29, 20, 10), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(29, 20, 10), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 5734400, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-311296, -29196, -172032).ToFloatVector(),
                    .cameraMax = Vector3Fx(331776, 131072, 126976).ToFloatVector(),
                    .playerMin = Vector3Fx(-311296, -29196, -166907).ToFloatVector(),
                    .playerMax = Vector3Fx(331776, 131072, 126976).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 111, .name = "AD1 TRANSFER LOCK DM", .inGameName = "Transfer Lock", .archive = "unit2_RM4",
                    .modelPath = "unit2_rm4_model.bin", .animationPath = "unit2_rm4_anim.bin",
                    .collisionPath = "unit2_rm4_collision.bin", .texturePath = "unit2_rm4_tex.bin",
                    .entityPath = "ad1_dm1_Ent.bin", .nodePath = "ad1_dm1_NODE.bin", .roomNodeName = "rmGoal",
                    .battleTimeLimit = TimeLimit(20, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 1, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(29, 20, 10), .fogSlope = 4, .fogOffset = 65300,
                    .light1Color = ColorRgb(29, 20, 10), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 3276800, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-294912, -12288, -172032).ToFloatVector(),
                    .cameraMax = Vector3Fx(294912, 94208, 172032).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 112, .name = "AD2 MAGMA VENTS", .inGameName = "Council Chamber", .archive = "ad2",
                    .modelPath = "ad2_model.bin", .animationPath = "ad2_anim.bin",
                    .collisionPath = "ad2_collision.bin", .texturePath = "ad2_tex.bin",
                    .entityPath = "ad2_Ent.bin", .nodePath = "ad2_node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 3, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 5, .fogOffset = 65200,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-278528, 0, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(245760, 147456, 106496).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 113, .name = "AD2 ALINOS PERCH", .inGameName = "Alinos Perch", .archive = "unit1_RM2",
                    .modelPath = "unit1_rm2_model.bin", .animationPath = "unit1_rm2_anim.bin",
                    .collisionPath = "unit1_rm2_collision.bin", .texturePath = "unit1_rm2_tex.bin",
                    .entityPath = "ad2_dm1_Ent.bin", .nodePath = "ad2_dm1_NODE.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 3, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 5, .fogOffset = 65200,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-278528, 0, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(245760, 159744, 106496).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 114, .name = "UNIT1 ALINOS LANDFALL", .inGameName = "Alinos Gateway", .archive = "unit1_Land",
                    .modelPath = "unit1_land_model.bin", .animationPath = "unit1_land_anim.bin",
                    .collisionPath = "unit1_land_collision.bin", .texturePath = "unit1_land_tex.bin",
                    .entityPath = "Unit1_Land_dm1_Ent.bin", .nodePath = "unit1_Land_dm1_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(31, 24, 18), .fogSlope = 4, .fogOffset = 65180,
                    .light1Color = ColorRgb(31, 24, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(13, 12, 7), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 4915200, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-290816, 8192, -217088).ToFloatVector(),
                    .cameraMax = Vector3Fx(348160, 86016, 241664).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 115, .name = "UNIT2 LANDING BAY", .inGameName = "Celestial Gateway", .archive = "unit2_Land",
                    .modelPath = "unit2_Land_model.bin", .animationPath = "unit2_Land_anim.bin",
                    .collisionPath = "unit2_Land_collision.bin", .texturePath = "unit2_land_tex.bin",
                    .entityPath = "unit2_land_dm1_Ent.bin", .nodePath = "unit2_land_dm1_NODE.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(18, 31, 18), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 31, 24), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(7, 11, 15), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 5734400, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-94208, -45056, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(102400, 114688, 106496).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 116, .name = "UNIT 3 VESPER STARPORT", .inGameName = "VDO Gateway", .archive = "unit3_Land",
                    .modelPath = "unit3_land_model.bin", .animationPath = "unit3_land_anim.bin",
                    .collisionPath = "unit3_land_collision.bin", .texturePath = "unit3_land_tex.bin",
                    .entityPath = "unit3_Land_dm1_Ent.bin", .nodePath = "unit3_Land_dm1_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 3, .fogOffset = 65152,
                    .light1Color = ColorRgb(18, 22, 30), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-180224, -20480, -172032).ToFloatVector(),
                    .cameraMax = Vector3Fx(172032, 118784, 172032).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 117, .name = "UNIT 4 ARCTERRA BASE", .inGameName = "Arcterra Gateway", .archive = "unit4_Land",
                    .modelPath = "unit4_land_model.bin", .animationPath = "unit4_land_anim.bin",
                    .collisionPath = "unit4_land_collision.bin", .texturePath = "unit4_land_tex.bin",
                    .entityPath = "unit4_Land_dm1_Ent.bin", .nodePath = "unit4_Land_dm1_node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(10, 14, 18), .fogSlope = 6, .fogOffset = 65300,
                    .light1Color = ColorRgb(10, 14, 18), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(4, 4, 8), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-90112, -20480, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(122880, 155648, 106496).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 118, .name = "Gorea Prison", .inGameName = "Oubliette", .archive = "Gorea_b2",
                    .modelPath = "gorea_b2_Model.bin", .animationPath = "gorea_b2_Anim.bin",
                    .collisionPath = "Gorea_b2_collision.bin", .texturePath = "gorea_b2_tex.bin",
                    .entityPath = "gorea_b2_dm_Ent.bin", .nodePath = "gorea_b2_dm_NODE.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 100, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(16, 30, 25), .fogSlope = 4, .fogOffset = 65535,
                    .light1Color = ColorRgb(18, 16, 14), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(27, 18, 9), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -286720, .size = RoomSize::Large,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 119, .name = "E3 FIRST HUNT", .inGameName = "Stasis Bunker", .archive = "e3Level",
                    .modelPath = "e3Level_Model.bin", .animationPath = "e3Level_Anim.bin",
                    .collisionPath = "e3Level_Collision.bin", .texturePath = "e3level_tex.bin",
                    .entityPath = "e3Level_Ent.bin", .nodePath = "e3Level_Node.bin",
                    .battleTimeLimit = TimeLimit(12, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 3, .nodeLayer = 1, .fogEnabled = true,
                    .fogColor = ColorRgb(24, 20, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(24, 20, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(9, 8, 14), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::Medium,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 120, .name = "Level TestLevel", .inGameName = "Test Level", .archive = "testLevel",
                    .modelPath = "testLevel_Model.bin", .animationPath = "testlevel_Anim.bin",
                    .collisionPath = "testlevel_Collision.bin",
                    .entityPath = "testlevel_Ent.bin", .nodePath = "testLevel_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .fogEnabled = true, .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(10, 10, 31), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 819200, .killHeight = -122880, .size = RoomSize::SinglePlayer,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .hybrid = true
                }),
                MakeRoom(RoomArgs{
                    .id = 121, .name = "Level AbeTest", .inGameName = "Abe Test Level", .archive = "testLevel",
                    .modelPath = "testLevel_Model.bin", .animationPath = "testlevel_Anim.bin",
                    .collisionPath = "testlevel_Collision.bin",
                    .entityPath = "testLevelAbe1_Ent.bin", .nodePath = "testLevelAbe1_Node.bin",
                    .battleTimeLimit = TimeLimit(40, 0, 0), .timeLimit = TimeLimit(4, 0, 0),
                    .pointLimit = 50, .fogEnabled = true,
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3Fx(409, -4096, 0).ToFloatVector(),
                    .light2Color = ColorRgb(10, 10, 31), .light2Vector = Vector3Fx(0, 4095, -409).ToFloatVector(),
                    .farClip = 819200, .killHeight = -122880, .size = RoomSize::SinglePlayer,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .hybrid = true
                }),
                MakeRoom(RoomArgs{
                    .id = 122, .name = "biodefense chamber 06", .inGameName = "Early Processor Core", .archive = "unit1_b2",
                    .modelPath = "unit1_b2_model.bin", .animationPath = "unit1_b2_anim.bin",
                    .collisionPath = "unit1_b2_collision.bin", .nodePath = "unit1_b2_node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0),
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(1.0F, 0.0F, 0.0F),
                    .light2Color = ColorRgb(31, 31, 31), .light2Vector = Vector3(0.0F, -1.0F, 0.0F),
                    .farClip = 1638400, .killHeight = -122880,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 123, .name = "biodefense chamber 05", .inGameName = "Early Stasis Bunker", .archive = "unit2_b2",
                    .modelPath = "unit2_b2_model.bin", .animationPath = "unit2_b2_anim.bin",
                    .collisionPath = "unit2_b2_collision.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0),
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(1.0F, 0.0F, 0.0F),
                    .light2Color = ColorRgb(31, 31, 31), .light2Vector = Vector3(0.0F, -1.0F, 0.0F),
                    .farClip = 1638400, .killHeight = -122880,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 124, .name = "biodefense chamber 03", .inGameName = "Early Head Shot", .archive = "unit3_b1",
                    .modelPath = "unit3_b1_model.bin", .animationPath = "unit3_b1_anim.bin",
                    .collisionPath = "unit3_b1_collision.bin", .nodePath = "unit3_b1_node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0),
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(1.0F, 0.0F, 0.0F),
                    .light2Color = ColorRgb(31, 31, 31), .light2Vector = Vector3(0.0F, -1.0F, 0.0F),
                    .farClip = 1638400, .killHeight = -122880,
                    .cameraMin = Vector3Fx(-200704, 0, -200704).ToFloatVector(),
                    .cameraMax = Vector3Fx(196608, 294912, 192512).ToFloatVector(),
                    .playerMin = Vector3Fx(-286720, -86949, -286720).ToFloatVector(),
                    .playerMax = Vector3Fx(286720, 294912, 286720).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 125, .name = "biodefense chamber 08", .inGameName = "Early Fuel Stack", .archive = "unit3_b2",
                    .modelPath = "unit3_b2_model.bin", .animationPath = "unit3_b2_anim.bin",
                    .collisionPath = "unit3_b2_collision.bin", .nodePath = "unit3_b2_node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0),
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(1.0F, 0.0F, 0.0F),
                    .light2Color = ColorRgb(31, 31, 31), .light2Vector = Vector3(0.0F, -1.0F, 0.0F),
                    .farClip = 1638400, .killHeight = -122880,
                    .cameraMin = Vector3Fx(-94208, -118784, -110592).ToFloatVector(),
                    .cameraMax = Vector3Fx(245760, 200704, 172032).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 126, .name = "biodefense chamber 04", .inGameName = "Early Sanctorus", .archive = "unit4_b1",
                    .modelPath = "unit4_b1_model.bin", .animationPath = "unit4_b1_anim.bin",
                    .collisionPath = "unit4_b1_collision.bin", .nodePath = "unit4_b1_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0),
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(1.0F, 0.0F, 0.0F),
                    .light2Color = ColorRgb(31, 31, 31), .light2Vector = Vector3(0.0F, -1.0F, 0.0F),
                    .farClip = 1638400, .killHeight = -122880,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 127, .name = "biodefense chamber 07", .inGameName = "Early Sic Transit", .archive = "unit4_b2",
                    .modelPath = "unit4_b2_model.bin", .animationPath = "unit4_b2_anim.bin",
                    .collisionPath = "unit4_b2_collision.bin", .nodePath = "unit4_b2_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0),
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(1.0F, 0.0F, 0.0F),
                    .light2Color = ColorRgb(31, 31, 31), .light2Vector = Vector3(0.0F, -1.0F, 0.0F),
                    .farClip = 1638400, .killHeight = -122880, .size = RoomSize::SinglePlayer, .multiplayer = true
                }),
                MakeRoom(RoomArgs{
                    .id = 128, .name = "Level MPH Morphball", .inGameName = "Morph Ball", .archive = "e3Level",
                    .modelPath = "e3Level_Model.bin", .animationPath = "e3Level_Anim.bin",
                    .collisionPath = "e3Level_Collision.bin", .entityPath = "morphBall_Ent.bin", .nodePath = "morphBall_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true,
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .killHeight = -122880, .size = RoomSize::SinglePlayer, .hybrid = true
                }),
                MakeRoom(RoomArgs{
                    .id = 129, .name = "Level MPH Regulator", .inGameName = "Regulator", .archive = "blueRoom",
                    .modelPath = "blueRoom_Model.bin", .animationPath = "blueRoom_Anim.bin",
                    .collisionPath = "blueRoom_Collision.bin", .entityPath = "regulator_Ent.bin", .nodePath = "regulator_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0),
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .killHeight = -122880, .size = RoomSize::SinglePlayer, .hybrid = true
                }),
                MakeRoom(RoomArgs{
                    .id = 130, .name = "Level MPH Survivor", .inGameName = "Survivor", .archive = "mp2",
                    .modelPath = "mp2_Model.bin", .animationPath = "mp2_Anim.bin",
                    .collisionPath = "mp2_Collision.bin", .entityPath = "survivor_Ent.bin", .nodePath = "survivor_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true, .clearFog = true,
                    .fogColor = ColorRgb(1, 6, 5), .fogSlope = 5, .fogOffset = 64900,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .killHeight = -122880, .size = RoomSize::SinglePlayer,
                    .cameraMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .cameraMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(),
                    .playerMin = Vector3Fx(-1228800, -1228800, -1228800).ToFloatVector(),
                    .playerMax = Vector3Fx(1228800, 1228800, 1228800).ToFloatVector(), .hybrid = true
                }),
                MakeRoom(RoomArgs{
                    .id = 0, .name = "Level MP1", .inGameName = "Trooper Module", .archive = "mp1",
                    .modelPath = "mp1_Model.bin", .animationPath = "mp1_Anim.bin", .collisionPath = "mp1_Collision.bin",
                    .entityPath = "mp1_Ent.bin", .nodePath = "mp1_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true, .clearFog = true,
                    .fogColor = ColorRgb(31, 31, 31), .fogSlope = 2, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 368640, .size = RoomSize::SinglePlayer, .multiplayer = true, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 1, .name = "Level MP2", .inGameName = "Assault Cradle", .archive = "mp2",
                    .modelPath = "mp2_Model.bin", .animationPath = "mp2_Anim.bin", .collisionPath = "mp2_Collision.bin",
                    .entityPath = "mp2_Ent.bin", .nodePath = "mp2_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true, .clearFog = true,
                    .fogColor = ColorRgb(6, 12, 11), .fogSlope = 5, .fogOffset = 64900,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .size = RoomSize::SinglePlayer, .multiplayer = true, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 2, .name = "Level MP3", .inGameName = "Ancient Vestige", .archive = "mp3",
                    .modelPath = "mp3_Model.bin", .animationPath = "mp3_Anim.bin", .collisionPath = "mp3_Collision.bin",
                    .entityPath = "mp3_Ent.bin", .nodePath = "mp3_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true,
                    .fogColor = ColorRgb(29, 26, 20), .fogSlope = 4, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 81920000, .size = RoomSize::SinglePlayer, .multiplayer = true, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 3, .name = "Level SP Morphball", .inGameName = "Morph Ball", .archive = "e3Level",
                    .modelPath = "e3Level_Model.bin", .animationPath = "e3Level_Anim.bin", .collisionPath = "e3Level_Collision.bin",
                    .entityPath = "morphBall_Ent.bin", .nodePath = "morphBall_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true,
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .size = RoomSize::SinglePlayer, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 4, .name = "Level SP Regulator", .inGameName = "Regulator", .archive = "blueRoom",
                    .modelPath = "blueRoom_Model.bin", .animationPath = "blueRoom_Anim.bin", .collisionPath = "blueRoom_Collision.bin",
                    .entityPath = "regulator_Ent.bin", .nodePath = "regulator_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0),
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .size = RoomSize::SinglePlayer, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 5, .name = "Level SP Survivor", .inGameName = "Survivor", .archive = "mp2",
                    .modelPath = "mp2_Model.bin", .animationPath = "mp2_Anim.bin", .collisionPath = "mp2_Collision.bin",
                    .entityPath = "survivor_Ent.bin", .nodePath = "survivor_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true, .clearFog = true,
                    .fogColor = ColorRgb(1, 6, 5), .fogSlope = 5, .fogOffset = 64900,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .size = RoomSize::SinglePlayer, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 6, .name = "Level FhTestLevel", .inGameName = "Test Level (First Hunt)", .archive = "testLevel",
                    .modelPath = "testLevel_Model.bin", .animationPath = "testlevel_Anim.bin", .collisionPath = "testlevel_Collision.bin",
                    .entityPath = "testlevel_Ent.bin", .nodePath = "testLevel_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true,
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .size = RoomSize::SinglePlayer, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 7, .name = "Level MP5", .inGameName = "Early Head Shot (First Hunt)", .archive = "mp5",
                    .modelPath = "mp5_Model.bin", .animationPath = "mp5_Anim.bin", .collisionPath = "mp5_Collision.bin",
                    .entityPath = "mp5_Ent.bin", .nodePath = "mp5_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true,
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .size = RoomSize::SinglePlayer, .multiplayer = true, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 8, .name = "Level MP1b", .inGameName = "Trooper Module", .archive = "mp1",
                    .modelPath = "mp1_Model.bin", .animationPath = "mp1_Anim.bin", .collisionPath = "mp1_Collision.bin",
                    .entityPath = "mp1_Ent.bin", .nodePath = "mp1_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true,
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .size = RoomSize::SinglePlayer, .multiplayer = true, .firstHunt = true
                }),
                MakeRoom(RoomArgs{
                    .id = 9, .name = "E3 level", .inGameName = "Stasis Bunker (First Hunt)", .archive = "e3Level",
                    .modelPath = "e3Level_Model.bin", .animationPath = "e3Level_Anim.bin", .collisionPath = "e3Level_Collision.bin",
                    .entityPath = "e3Level_Ent.bin", .nodePath = "e3Level_Node.bin",
                    .battleTimeLimit = TimeLimit(10, 0, 0), .timeLimit = TimeLimit(2, 0, 0), .fogEnabled = true,
                    .fogColor = ColorRgb(8, 16, 31), .fogSlope = 5, .fogOffset = 65152,
                    .light1Color = ColorRgb(31, 31, 31), .light1Vector = Vector3(0.25F, -0.5F, -0.25F),
                    .light2Color = ColorRgb(4, 4, 16), .light2Vector = Vector3(0.0F, 1.0F, -0.25F),
                    .farClip = 245760, .size = RoomSize::SinglePlayer, .multiplayer = true, .firstHunt = true
                })
            };
        }

        std::vector<std::string> CreateBaseRoomIds()
        {
            return {
                "UNIT1_CX", "UNIT1_CX", "UNIT1_CZ", "UNIT1_CZ", "UNIT1_MORPH_CX", "UNIT1_MORPH_CX",
                "UNIT1_MORPH_CZ", "UNIT1_MORPH_CZ", "UNIT2_CX", "UNIT2_CX", "UNIT2_CZ", "UNIT2_CZ",
                "UNIT3_CX", "UNIT3_CX", "UNIT3_CZ", "UNIT3_CZ", "UNIT4_CX", "UNIT4_CX", "UNIT4_CZ", "UNIT4_CZ",
                "CYLINDER_C1", "BIGEYE_C1", "UNIT1_RM1_CX", "UNIT1_RM1_CX", "GOREA_C1", "UNIT3_MORPH_CZ",
                "UNIT3_MORPH_CZ", "UNIT1_LAND", "UNIT1_C0", "UNIT1_RM1", "UNIT1_C4", "UNIT1_RM6", "CRYSTALROOM",
                "UNIT1_RM4", "UNIT1_TP1", "UNIT1_B1", "UNIT1_C1", "UNIT1_C2", "UNIT1_C5", "UNIT1_RM2",
                "UNIT1_RM3", "UNIT1_RM5", "UNIT1_C3", "UNIT1_TP2", "UNIT1_B2", "UNIT2_LAND", "UNIT2_C0",
                "UNIT2_C1", "UNIT2_RM1", "UNIT2_C2", "UNIT2_RM2", "UNIT2_C3", "UNIT2_RM3", "UNIT2_C4",
                "UNIT2_TP1", "UNIT2_B1", "UNIT2_C6", "UNIT2_C7", "UNIT2_RM4", "UNIT2_RM5", "UNIT2_RM6",
                "UNIT2_RM7", "UNIT2_RM8", "UNIT2_TP2", "UNIT2_B2", "UNIT3_LAND", "UNIT3_C0", "UNIT3_C2",
                "UNIT3_RM1", "UNIT3_RM4", "UNIT3_TP1", "UNIT3_B1", "UNIT3_C1", "UNIT3_RM2", "UNIT3_RM3",
                "UNIT3_TP2", "UNIT3_B2", "UNIT4_LAND", "UNIT4_RM1", "UNIT4_RM3", "UNIT4_C0", "UNIT4_TP1",
                "UNIT4_B1", "UNIT4_C1", "UNIT4_RM2", "UNIT4_RM4", "UNIT4_RM5", "UNIT4_TP2", "UNIT4_B2",
                "Gorea_Land", "Gorea_Peek", "Gorea_b1", "Gorea_b2", "MP1 SANCTORUS", "MP2 HARVESTER",
                "MP3 PROVING GROUND", "MP4 HIGHGROUND - EXPANDED", "MP4 HIGHGROUND", "MP5 FUEL SLUICE",
                "MP6 HEADSHOT", "MP7 PROCESSOR CORE", "MP8 FIRE CONTROL", "MP9 CRYOCHASM", "MP10 OVERLOAD",
                "MP11 BREAKTHROUGH", "MP12 SIC TRANSIT", "MP13 ACCELERATOR", "MP14 OUTER REACH",
                "CTF1 FAULT LINE - EXPANDED", "CTF1_FAULT LINE", "AD1 TRANSFER LOCK BT", "AD1 TRANSFER LOCK DM",
                "AD2 MAGMA VENTS", "AD2 ALINOS PERCH", "UNIT1 ALINOS LANDFALL", "UNIT2 LANDING BAY",
                "UNIT 3 VESPER STARPORT", "UNIT 4 ARCTERRA BASE", "Gorea Prison", "E3 FIRST HUNT", "Level TestLevel",
                "Level AbeTest", "biodefense chamber 06", "biodefense chamber 05", "biodefense chamber 03",
                "biodefense chamber 08", "biodefense chamber 04", "biodefense chamber 07", "Level MP1", "Level MP2",
                "Level MP3", "Level SP Morphball", "Level SP Regulator", "Level SP Survivor", "Level FhTestLevel",
                "Level MP5", "Level MP1b", "E3 level"
            };
        }

        std::unordered_map<std::string, std::shared_ptr<RoomMetadata>> CreateRoomMetadataMap(
            const std::vector<std::shared_ptr<RoomMetadata>>& rooms)
        {
            std::unordered_map<std::string, std::shared_ptr<RoomMetadata>> result;
            result.reserve(rooms.size());
            for (const std::shared_ptr<RoomMetadata>& room : rooms)
            {
                const auto [iterator, inserted] = result.emplace(room->Name, room);
                if (!inserted)
                {
                    throw std::invalid_argument("An item with the same key has already been added. Key: " + iterator->first);
                }
            }
            return result;
        }
    }

    RoomMetadata::RoomMetadata(
        std::int32_t id,
        std::string name,
        std::optional<std::string> inGameName,
        std::string archive,
        std::string modelPath,
        std::string animationPath,
        std::string collisionPath,
        std::optional<std::string> texturePath,
        std::optional<std::string> entityPath,
        std::optional<std::string> nodePath,
        std::optional<std::string> roomNodeName,
        std::uint32_t battleTimeLimit,
        std::uint32_t timeLimit,
        std::int16_t pointLimit,
        std::int16_t nodeLayer,
        bool fogEnabled,
        bool clearFog,
        ColorRgb fogColor,
        std::int32_t fogSlope,
        std::uint16_t fogOffset,
        ColorRgb light1Color,
        Vector3 light1Vector,
        ColorRgb light2Color,
        Vector3 light2Vector,
        std::int32_t farClip,
        std::int32_t killHeight,
        RoomSize size,
        Vector3 cameraMin,
        Vector3 cameraMax,
        Vector3 playerMin,
        Vector3 playerMax,
        bool multiplayer,
        bool firstHunt,
        bool hybrid)
        : Id(id),
          Name(std::move(name)),
          InGameName(std::move(inGameName)),
          Archive(archive),
          ModelPath((firstHunt || hybrid) ? "levels\\models\\" + modelPath : "_archives\\" + archive + "\\" + modelPath),
          AnimationPath((firstHunt || hybrid) ? "levels\\models\\" + animationPath : "_archives\\" + archive + "\\" + animationPath),
          CollisionPath((firstHunt || hybrid) ? "levels\\collision\\" + collisionPath : "_archives\\" + archive + "\\" + collisionPath),
          TexturePath(texturePath ? std::optional<std::string>("levels\\textures\\" + *texturePath) : std::nullopt),
          EntityPath(entityPath ? std::optional<std::string>("levels\\entities\\" + *entityPath) : std::nullopt),
          EntityFilename(std::move(entityPath)),
          NodePath(nodePath ? std::optional<std::string>("levels\\nodeData\\" + *nodePath) : std::nullopt),
          RoomNodeName(std::move(roomNodeName)),
          BattleTimeLimit(battleTimeLimit),
          TimeLimit(timeLimit),
          PointLimit(pointLimit),
          NodeLayer(nodeLayer),
          FogEnabled(fogEnabled),
          ClearFog(clearFog),
          FogColor(fogColor),
          FogSlope(fogSlope),
          FogOffset(static_cast<std::int32_t>(fogOffset & 0x7FFFU)),
          FarClip(Fixed::ToFloat(farClip)),
          FarClipInt(farClip),
          Light1Color(light1Color),
          Light1Vector(light1Vector.Normalized()),
          Light2Color(light2Color),
          Light2Vector(light2Vector.Normalized()),
          KillHeight(Fixed::ToFloat(killHeight)),
          Size(size),
          Multiplayer(multiplayer),
          FirstHunt(firstHunt),
          Hybrid(hybrid),
          CameraMin(cameraMin),
          CameraMax(cameraMax),
          PlayerMin(playerMin),
          PlayerMax(playerMax),
          HasLimits(HasValue(cameraMin) || HasValue(cameraMax) || HasValue(playerMin) || HasValue(playerMax))
    {
    }

    namespace Metadata
    {
        std::vector<std::string> _roomIds = Mods::MapGen::CustomRooms::AppendIds(CreateBaseRoomIds());

        std::vector<std::shared_ptr<MphRead::RoomMetadata>> RoomList
            = Mods::MapGen::CustomRooms::AppendRooms(CreateBaseRooms());

        std::unordered_map<std::string, std::shared_ptr<MphRead::RoomMetadata>> RoomMetadata
            = CreateRoomMetadataMap(RoomList);

        std::int32_t AppendLateRooms()
        {
            const std::size_t before = RoomList.size();
            Mods::MapGen::CustomRooms::AppendMissingRooms(_roomIds, RoomList);
            for (std::size_t i = before; i < RoomList.size(); ++i)
            {
                RoomMetadata.emplace(RoomList[i]->Name, RoomList[i]);
            }
            return static_cast<std::int32_t>(RoomList.size() - before);
        }

        const std::unordered_map<std::int32_t, std::string> EncounterNodeDataOverrides = {
            {28, R"(levels\nodeData\unit1_C0_Boss_Node.bin)"},
            {29, R"(levels\nodeData\unit1_RM1_Boss_Node.bin)"},
            {31, R"(levels\nodeData\unit1_RM6_Boss_Node.bin)"},
            {50, R"(levels\nodeData\unit2_RM2_Boss_Node.bin)"},
            {52, R"(levels\nodeData\unit2_RM3_Boss_Node.bin)"},
            {65, R"(levels\nodeData\unit3_Land_Boss_Node.bin)"},
            {68, R"(levels\nodeData\unit3_RM1_Boss_Node.bin)"},
            {79, R"(levels\nodeData\unit4_RM3_Boss_Node.bin)"}
        };

        const std::unordered_map<std::int32_t, std::string> CtfNodeDataOverrides = {
            {93, R"(levels\nodeData\mp1_CTF_node.bi))"},
            {99, R"(levels\nodeData\mp6_CTF_node.bi))"},
            {101, R"(levels\nodeData\mp8_CTF_node.bin)"},
            {102, R"(levels\nodeData\mp9_CTF_node.bin)"},
            {105, R"(levels\nodeData\mp12_CTF_node.bin)"},
            {107, R"(levels\nodeData\mp14_CTF_node.bin)"},
            {108, R"(levels\nodeData\ctf1_CTF_node.bin)"},
            {119, R"(levels\nodeData\e3Level_CTF_Node.bin)"}
        };
    }
}
