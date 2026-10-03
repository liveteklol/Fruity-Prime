#include "Metadata.hpp"
#include "../NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <tuple>

using ::MphRead::NativeRuntime::StringReplace;

namespace MphRead
{
namespace
{
const std::string& DirectoryFor(MetaDir dir)
{
    static const std::map<MetaDir, std::string> dirs =
    {
        {MetaDir::CharSelect, "characterselect"},
        {MetaDir::CreateJoin, "createjoin"},
        {MetaDir::GameOption, "gameoptions"},
        {MetaDir::GamersCard, "gamerscard"},
        {MetaDir::Hud, "hud"},
        {MetaDir::Keyboard, "keyboard"},
        {MetaDir::Keypad, "keypad"},
        {MetaDir::Logo, R"(logo_screen\MAYA)"},
        {MetaDir::MainMenu, "main menu"},
        {MetaDir::Models, "models"},
        {MetaDir::MoviePlayer, "movieplayer"},
        {MetaDir::MultiMaster, "multimaster"},
        {MetaDir::Multiplayer, "multiplayer"},
        {MetaDir::PaxControls, "pax_controls"},
        {MetaDir::Popup, "popup"},
        {MetaDir::Results, "results"},
        {MetaDir::ScStartGame, "sc_startgame"},
        {MetaDir::Stage, "stage"},
        {MetaDir::StartGame, "startgame"},
        {MetaDir::ToStart, "tostart"},
        {MetaDir::TouchToStart, "touchtostart"},
        {MetaDir::TouchToStart2, "touchtostart_2"},
        {MetaDir::WifiCreate, "wifi_createjoin"},
        {MetaDir::WifiGames, "wifi_games"}
    };
    return dirs.at(dir);
}
}

RecolorMetadata::RecolorMetadata(std::string name, std::string modelPath)
    : Name(std::move(name)), ModelPath(std::move(modelPath)),
      TexturePath(ModelPath), PalettePath(ModelPath)
{
}

RecolorMetadata::RecolorMetadata(std::string name, std::string modelPath, std::string texturePath)
    : Name(std::move(name)), ModelPath(std::move(modelPath)),
      TexturePath(std::move(texturePath)), PalettePath(TexturePath)
{
}

RecolorMetadata::RecolorMetadata(std::string name, std::string modelPath, std::string texturePath,
    std::string palettePath, std::map<int, std::vector<int>> replaceIds, bool separateReplace)
    : Name(std::move(name)), ModelPath(std::move(modelPath)), TexturePath(std::move(texturePath)),
      PalettePath(separateReplace ? TexturePath : palettePath),
      ReplacePath(separateReplace ? std::optional<std::string>(std::move(palettePath)) : std::nullopt),
      ReplaceIds(std::move(replaceIds))
{
}

ModelMetadata::ModelMetadata(Values values)
    : Name(std::move(values.Name)), ModelPath(std::move(values.ModelPath)),
      AnimationPath(std::move(values.AnimationPath)), AnimationShare(std::move(values.AnimationShare)),
      CollisionPath(std::move(values.CollisionPath)), ExtraCollisionPath(std::move(values.ExtraCollisionPath)),
      Recolors(std::move(values.Recolors)), UseLightSources(values.UseLightSources), FirstHunt(values.FirstHunt)
{
}

ModelMetadata::ModelMetadata(std::string name, std::string modelPath,
    std::optional<std::string> animationPath, std::optional<std::string> collisionPath,
    std::vector<RecolorMetadata> recolors, std::optional<std::string> animationShare,
    bool useLightSources)
    : ModelMetadata([name = std::move(name), modelPath = std::move(modelPath),
        animationPath = std::move(animationPath), collisionPath = std::move(collisionPath),
        recolors = std::move(recolors), animationShare = std::move(animationShare), useLightSources]() mutable
    {
        Values values;
        values.Name = std::move(name);
        values.ModelPath = std::move(modelPath);
        values.AnimationPath = std::move(animationPath);
        values.AnimationShare = std::move(animationShare);
        values.CollisionPath = std::move(collisionPath);
        values.Recolors = std::move(recolors);
        values.UseLightSources = useLightSources;
        return values;
    }())
{
}

ModelMetadata::ModelMetadata(std::string name, MetaDir dir, std::optional<std::string> anim)
    : ModelMetadata([name = std::move(name), dir, anim = std::move(anim)]() mutable
    {
        Values values;
        values.Name = std::move(name);
        const std::string& directory = DirectoryFor(dir);
        values.ModelPath = directory + "\\" + values.Name + "_Model.bin";
        if (anim)
        {
            values.AnimationPath = directory + "\\" + *anim + "_Anim.bin";
        }
        values.Recolors.emplace_back("default", values.ModelPath, values.ModelPath);
        return values;
    }())
{
}

ModelMetadata::ModelMetadata(std::string name, std::string texturePath, MetaDir dir)
    : ModelMetadata([name = std::move(name), texturePath = std::move(texturePath), dir]() mutable
    {
        Values values;
        values.Name = std::move(name);
        const std::string& directory = DirectoryFor(dir);
        values.ModelPath = directory + "\\" + values.Name + "_Model.bin";
        values.Recolors.emplace_back("default", values.ModelPath, std::move(texturePath));
        return values;
    }())
{
}

ModelMetadata::ModelMetadata(std::string name, std::optional<std::string> animationPath,
    std::optional<std::string> texturePath)
    : ModelMetadata([name = std::move(name), animationPath = std::move(animationPath),
        texturePath = std::move(texturePath)]() mutable
    {
        Values values;
        values.Name = std::move(name);
        values.ModelPath = "models\\" + values.Name + "_Model.bin";
        values.AnimationPath = std::move(animationPath);
        values.Recolors.emplace_back("default", values.ModelPath,
            texturePath ? *texturePath : values.ModelPath);
        return values;
    }())
{
}

ModelMetadata::ModelMetadata(std::string name, std::string remove, bool animation,
    std::optional<std::string> animationPath, bool collision, bool firstHunt)
    : ModelMetadata([name = std::move(name), remove = std::move(remove), animation,
        animationPath = std::move(animationPath), collision, firstHunt]() mutable
    {
        Values values;
        values.Name = std::move(name);
        const std::string directory = "models";
        values.ModelPath = directory + "\\" + values.Name + "_Model.bin";
        const std::string removed = StringReplace(values.Name, remove, "");
        if (animation)
        {
            values.AnimationPath = animationPath ? std::move(animationPath)
                : std::optional<std::string>(directory + "\\" + removed + "_Anim.bin");
        }
        if (collision)
        {
            values.CollisionPath = directory + "\\" + removed + "_Collision.bin";
        }
        values.Recolors.emplace_back("default", values.ModelPath);
        values.FirstHunt = firstHunt;
        return values;
    }())
{
}

ModelMetadata::ModelMetadata(std::string name, std::vector<std::string> recolors,
    std::optional<std::string> remove, bool animation, std::optional<std::string> animationPath,
    bool texture, MdlSuffix mdlSuffix, std::optional<std::string> archive,
    std::optional<std::string> recolorName, std::optional<std::string> animationShare,
    bool useLightSources, bool firstHunt, bool noUnderscore)
    : ModelMetadata([name = std::move(name), recolors = std::move(recolors), remove = std::move(remove),
        animation, animationPath = std::move(animationPath), texture, mdlSuffix,
        archive = std::move(archive), recolorName = std::move(recolorName),
        animationShare = std::move(animationShare), useLightSources, firstHunt, noUnderscore]() mutable
    {
        Values values;
        values.Name = std::move(name);
        std::string suffix = mdlSuffix == MdlSuffix::None ? "" : "_mdl";
        if (!archive)
        {
            values.ModelPath = "models\\" + values.Name + suffix + "_Model.bin";
        }
        else
        {
            values.ModelPath = "_archives\\" + *archive + "\\" + values.Name + "_Model.bin";
        }
        const std::string pathName = remove ? StringReplace(values.Name, *remove, "") : values.Name;
        if (mdlSuffix != MdlSuffix::All)
        {
            suffix.clear();
        }
        if (animationPath)
        {
            values.AnimationPath = std::move(animationPath);
        }
        else if (animation)
        {
            values.AnimationPath = archive
                ? std::optional<std::string>("_archives\\" + *archive + "\\" + pathName + "_Anim.bin")
                : std::optional<std::string>("models\\" + pathName + suffix + "_Anim.bin");
        }
        values.AnimationShare = std::move(animationShare);
        for (const std::string& recolor : recolors)
        {
            std::string recolorString = (recolorName ? *recolorName : pathName)
                + (noUnderscore ? "" : "_") + recolor;
            if (!recolor.empty() && recolor.front() == '*')
            {
                recolorString = StringReplace(recolor, "*", "");
            }
            const std::string recolorModel = "models\\" + recolorString + "_Model.bin";
            const std::string texturePathValue = texture
                ? "models\\" + recolorString + "_Tex.bin" : recolorModel;
            values.Recolors.emplace_back(recolor, recolorModel, texturePathValue);
        }
        values.UseLightSources = useLightSources;
        values.FirstHunt = firstHunt;
        return values;
    }())
{
}

ModelMetadata::ModelMetadata(std::string name, bool animation, bool collision, bool texture,
    std::optional<std::string> share, MdlSuffix mdlSuffix, std::optional<std::string> archive,
    std::optional<std::string> addToAnim, bool firstHunt,
    std::optional<std::string> animationPath, std::optional<std::string> extraCollision)
    : ModelMetadata([name = std::move(name), animation, collision, texture,
        share = std::move(share), mdlSuffix, archive = std::move(archive), addToAnim = std::move(addToAnim),
        firstHunt, animationPath = std::move(animationPath), extraCollision = std::move(extraCollision)]() mutable
    {
        Values values;
        values.Name = std::move(name);
        const std::string path = archive ? "_archives\\" + *archive : "models";
        std::string suffix = mdlSuffix == MdlSuffix::None ? "" : "_mdl";
        values.ModelPath = path + "\\" + values.Name + suffix + "_Model.bin";
        if (mdlSuffix != MdlSuffix::All)
        {
            suffix.clear();
        }
        if (animation)
        {
            values.AnimationPath = animationPath ? std::move(animationPath)
                : std::optional<std::string>(path + "\\" + values.Name + addToAnim.value_or("") + suffix + "_Anim.bin");
        }
        if (collision)
        {
            values.CollisionPath = path + "\\" + values.Name + suffix + "_Collision.bin";
        }
        if (extraCollision)
        {
            values.ExtraCollisionPath = path + "\\" + *extraCollision + "_Collision.bin";
        }
        std::string recolorModel = values.ModelPath;
        if (share)
        {
            texture = false;
            recolorModel = *share;
        }
        values.Recolors.emplace_back("default", recolorModel,
            texture ? "models\\" + values.Name + suffix + "_Tex.bin" : recolorModel);
        values.FirstHunt = firstHunt;
        return values;
    }())
{
}

ModelMetadata::ModelMetadata(std::string name, std::string modelPath,
    std::optional<std::string> animationPath, std::optional<std::string> collisionPath,
    bool firstHunt)
    : ModelMetadata([name = std::move(name), modelPath = std::move(modelPath),
        animationPath = std::move(animationPath), collisionPath = std::move(collisionPath), firstHunt]() mutable
    {
        Values values;
        values.Name = std::move(name);
        values.ModelPath = std::move(modelPath);
        values.AnimationPath = std::move(animationPath);
        values.CollisionPath = std::move(collisionPath);
        values.Recolors.emplace_back("default", values.ModelPath, values.ModelPath);
        values.FirstHunt = firstHunt;
        return values;
    }())
{
}

namespace
{
std::vector<int> NormalizeAnimationIds(std::optional<std::vector<int>> animationIds,
    std::array<int, 4> defaults)
{
    if (!animationIds)
    {
        return {defaults.begin(), defaults.end()};
    }
    if (animationIds->size() != 4)
    {
        throw std::invalid_argument("animationIds");
    }
    return std::move(*animationIds);
}
}

ObjectMetadata::ObjectMetadata(std::string name, bool lighting, int paletteId,
    bool ignoreAnim, std::optional<std::vector<int>> animationIds)
    : Lighting(lighting), IgnoreAnimation(ignoreAnim), Name(std::move(name)),
      AnimationIds(NormalizeAnimationIds(std::move(animationIds), {0, 0, 0, 0})), RecolorId(paletteId)
{
}

PlatformMetadata::PlatformMetadata(std::string name, bool lighting,
    std::optional<std::vector<int>> animationIds)
    : Animation(animationIds.has_value()), Lighting(lighting), Name(std::move(name)),
      AnimationIds(NormalizeAnimationIds(std::move(animationIds), {-1, -1, -1, -1}))
{
}

DoorMetadata::DoorMetadata(std::string name, std::string lockName, float lockOffset, float radius)
    : Name(std::move(name)), LockName(std::move(lockName)), LockOffset(lockOffset), Radius(radius)
{
}
}

namespace MphRead::Metadata
{
namespace
{
OpenTK::Mathematics::Vector3 GetColor(std::uint16_t value) noexcept
{
    return OpenTK::Mathematics::Vector3(
        static_cast<float>((value >> 0) & 0x1F) / 31.0F,
        static_cast<float>((value >> 5) & 0x1F) / 31.0F,
        static_cast<float>((value >> 10) & 0x1F) / 31.0F);
}

const std::array<std::pair<int, std::vector<int>>, 13> ModeLayers{{
    {static_cast<int>(GameMode::Battle), std::vector<int>{0, 1, 2}},
    {static_cast<int>(GameMode::BattleTeams), std::vector<int>{3}},
    {static_cast<int>(GameMode::Survival), std::vector<int>{15}},
    {static_cast<int>(GameMode::SurvivalTeams), std::vector<int>{15}},
    {static_cast<int>(GameMode::Capture), std::vector<int>{12}},
    {static_cast<int>(GameMode::Bounty), std::vector<int>{8, 9, 10}},
    {static_cast<int>(GameMode::BountyTeams), std::vector<int>{11}},
    {static_cast<int>(GameMode::Nodes), std::vector<int>{4, 5, 6}},
    {static_cast<int>(GameMode::NodesTeams), std::vector<int>{7}},
    {static_cast<int>(GameMode::Defender), std::vector<int>{14}},
    {static_cast<int>(GameMode::DefenderTeams), std::vector<int>{14}},
    {static_cast<int>(GameMode::PrimeHunter), std::vector<int>{0, 1, 2}},
    {static_cast<int>(GameMode::Unknown15), std::vector<int>{13}},
}};

std::string_view ModeName(int value)
{
    switch (value)
    {
    case static_cast<int>(GameMode::Battle): return "Battle";
    case static_cast<int>(GameMode::BattleTeams): return "BattleTeams";
    case static_cast<int>(GameMode::Survival): return "Survival";
    case static_cast<int>(GameMode::SurvivalTeams): return "SurvivalTeams";
    case static_cast<int>(GameMode::Capture): return "Capture";
    case static_cast<int>(GameMode::Bounty): return "Bounty";
    case static_cast<int>(GameMode::BountyTeams): return "BountyTeams";
    case static_cast<int>(GameMode::Nodes): return "Nodes";
    case static_cast<int>(GameMode::NodesTeams): return "NodesTeams";
    case static_cast<int>(GameMode::Defender): return "Defender";
    case static_cast<int>(GameMode::DefenderTeams): return "DefenderTeams";
    case static_cast<int>(GameMode::PrimeHunter): return "PrimeHunter";
    case 15: return "Unknown15";
    default: return "";
    }
}

const std::vector<int>& LayersFor(GameMode mode)
{
    const int value = static_cast<int>(mode);
    for (const auto& item : ModeLayers)
    {
        if (item.first == value)
        {
            return item.second;
        }
    }
    throw std::out_of_range("mode");
}

const ::MphRead::ModelMetadata* FindModel(
    const std::unordered_map<std::string, ::MphRead::ModelMetadata>& values,
    std::string_view name) noexcept
{
    const auto it = values.find(std::string(name));
    return it == values.end() ? nullptr : &it->second;
}

const ::MphRead::ModelMetadata* FindFrontendModel(
    const std::unordered_map<std::string, std::shared_ptr<::MphRead::ModelMetadata>>& values,
    std::string_view name) noexcept
{
    auto it = values.find(std::string(name));
    return it == values.end() ? nullptr : it->second.get();
}

const std::array<std::shared_ptr<ObjectMetadata>, 54> Objects = []
{
    std::array<std::shared_ptr<ObjectMetadata>, 54> values{};
    auto make = [](std::string name, bool lighting = false, int palette = 0,
        bool ignore = false, std::optional<std::vector<int>> ids = std::nullopt)
    {
        return std::make_shared<ObjectMetadata>(std::move(name), lighting, palette, ignore, std::move(ids));
    };
    values[0]=make("AlimbicGhost_01"); values[1]=make("AlimbicLightPole");
    values[2]=make("AlimbicStationShieldControl"); values[3]=make("AlimbicComputerStationControl");
    values[4]=make("AlimbicEnergySensor"); values[5]=make("SamusShip");
    values[6]=make("Guardbot01_Dead"); values[7]=make("Guardbot02_Dead");
    values[8]=make("Guardian_Dead"); values[9]=make("Psychobit_Dead");
    values[10]=make("AlimbicLightPole02"); values[11]=make("AlimbicComputerStationControl02");
    values[12]=make("Generic_Console",false,0,false,std::vector<int>{2,1,0,0});
    values[13]=make("Generic_Monitor",false,0,false,std::vector<int>{2,1,0,0});
    values[14]=make("Generic_Power"); values[15]=make("Generic_Scanner",false,0,false,std::vector<int>{2,1,0,0});
    values[16]=make("Generic_Switch",true,0,false,std::vector<int>{2,1,0,0});
    values[17]=make("Alimbic_Console",false,0,false,std::vector<int>{2,1,0,0});
    values[18]=make("Alimbic_Monitor",false,0,false,std::vector<int>{2,1,0,0});
    values[19]=make("Alimbic_Power"); values[20]=make("Alimbic_Scanner",false,0,false,std::vector<int>{2,1,0,0});
    values[21]=make("Alimbic_Switch",true,0,false,std::vector<int>{2,1,0,0});
    values[22]=make("Lava_Console",false,0,false,std::vector<int>{2,1,0,0});
    values[23]=make("Lava_Monitor",false,0,false,std::vector<int>{2,1,0,0}); values[24]=make("Lava_Power");
    values[25]=make("Lava_Scanner",false,0,false,std::vector<int>{2,1,0,0});
    values[26]=make("Lava_Switch",true,0,false,std::vector<int>{2,1,0,0});
    values[27]=make("Ice_Console",false,0,false,std::vector<int>{2,1,0,0});
    values[28]=make("Ice_Monitor",false,0,false,std::vector<int>{2,1,0,0}); values[29]=make("Ice_Power");
    values[30]=make("Ice_Scanner",false,0,false,std::vector<int>{2,1,0,0});
    values[31]=make("Ice_Switch",true,0,false,std::vector<int>{2,1,0,0});
    values[32]=make("Ruins_Console",false,0,false,std::vector<int>{2,1,0,0});
    values[33]=make("Ruins_Monitor",false,0,false,std::vector<int>{2,1,0,0}); values[34]=make("Ruins_Power");
    values[35]=make("Ruins_Scanner",false,0,false,std::vector<int>{2,1,0,0});
    values[36]=make("Ruins_Switch",true,0,false,std::vector<int>{2,1,0,0});
    values[37]=make("PlantCarnivarous_Branched"); values[38]=make("PlantCarnivarous_Pod");
    values[39]=make("PlantCarnivarous_PodLeaves"); values[40]=make("PlantCarnivarous_Vine");
    values[41]=make("GhostSwitch"); values[42]=make("Switch",true);
    values[43]=make("Guardian_Stasis",false,0,false,std::vector<int>{-1,0,0,0});
    values[44]=make("AlimbicStatue_lod0",false,0,true,std::vector<int>{-1,0,0,0});
    values[45]=make("AlimbicCapsule");
    values[46]=make("SniperTarget",true,0,false,std::vector<int>{0,2,1,0});
    for (int i=1;i<=6;i++) values[46+i]=make("SecretSwitch",false,i,false,std::vector<int>{1,2,0,0});
    values[53]=make("WallSwitch",true,0,false,std::vector<int>{2,0,1,0});
    return values;
}();

const std::array<std::shared_ptr<PlatformMetadata>, 45> Platforms = []
{
    std::array<std::shared_ptr<PlatformMetadata>,45> values{};
    auto make=[](std::string name, bool lighting=false, std::optional<std::vector<int>> ids=std::nullopt)
    { return std::make_shared<PlatformMetadata>(std::move(name),lighting,std::move(ids)); };
    values[0]=make("platform"); values[1]=values[0];
    values[3]=make("Elevator"); values[4]=make("smasher"); values[5]=make("Platform_Unit4_C1",true);
    values[6]=make("pillar"); values[7]=make("Door_Unit4_RM1");
    values[8]=make("SyluxShip",false,std::vector<int>{-1,1,0,2}); values[9]=make("pistonmp7");
    values[10]=make("unit3_brain",false,std::vector<int>{0,0,0,0});
    values[11]=make("unit4_mover1",false,std::vector<int>{0,0,0,0});
    values[12]=make("unit4_mover2",false,std::vector<int>{0,0,0,0});
    values[13]=make("ElectroField1",false,std::vector<int>{0,0,0,0}); values[14]=make("Unit3_platform1");
    values[15]=make("unit3_pipe1",false,std::vector<int>{0,0,0,0}); values[16]=make("unit3_pipe2",false,std::vector<int>{0,0,0,0});
    values[17]=make("cylinderbase"); values[18]=make("unit3_platform"); values[19]=make("unit3_platform2");
    values[20]=make("unit3_jar",false,std::vector<int>{0,2,1,0}); values[21]=make("SyluxTurret",false,std::vector<int>{3,2,1,0});
    values[22]=make("unit3_jartop",false,std::vector<int>{0,2,1,0}); values[23]=make("SamusShip",false,std::vector<int>{1,3,2,4});
    values[24]=make("unit1_land_plat1"); values[25]=make("unit1_land_plat2"); values[26]=make("unit1_land_plat3");
    values[27]=make("unit1_land_plat4"); values[28]=make("unit1_land_plat5"); values[29]=make("unit2_c4_plat");
    values[30]=make("unit2_land_elev"); values[31]=make("unit4_platform1");
    values[32]=make("Crate01",false,std::vector<int>{-1,-1,0,1});
    values[33]=make("unit1_mover1",false,std::vector<int>{0,0,0,0}); values[34]=make("unit1_mover2");
    values[35]=make("unit2_mover1"); values[36]=make("unit4_mover3"); values[37]=make("unit4_mover4"); values[38]=make("unit3_mover1");
    values[39]=make("unit2_c1_mover"); values[40]=make("unit3_mover2",false,std::vector<int>{0,0,0,0});
    values[41]=make("piston_gorealand"); values[42]=make("unit4_tp2_artifact_wo"); values[43]=make("unit4_tp1_artifact_wo");
    values[44]=make("SamusShip",false,std::vector<int>{1,0,2,4});
    return values;
}();
}

int GetMultiplayerEntityLayer(GameMode mode, int playerCount)
{
    const std::vector<int>& list = LayersFor(mode);
    if (list.size() == 1)
    {
        return list[0];
    }
    const int index = playerCount >= 4 ? 2 : playerCount == 3 ? 1 : 0;
    return list.at(static_cast<std::size_t>(index));
}

std::string GetLayerName(int layerId, bool multiplayer)
{
    if (multiplayer)
    {
        const std::uint32_t bit = std::uint32_t{1} << (static_cast<std::uint32_t>(layerId) & 31U);
        return GetLayerNames(std::bit_cast<std::int32_t>(bit), true);
    }
    switch (layerId)
    {
    case 0: return "FirstVisit";
    case 1: return "Escape";
    case 2: return "Cleared";
    case 3: return "SpLayer3";
    default: return "NoLayer" + std::to_string(layerId);
    }
}

std::string GetLayerNames(int layerMask, bool multiplayer)
{
    if (!multiplayer)
    {
        switch (layerMask & 3)
        {
        case 0: return "FirstVisit";
        case 1: return "Escape";
        case 2: return "Cleared";
        case 3: return "SpLayer3";
        default: return "UNKNOWN" + std::to_string(layerMask);
        }
    }
    std::vector<int> layers;
    for (int i = 0; i < 16; ++i)
    {
        if ((static_cast<std::uint32_t>(layerMask) & (std::uint32_t{1} << i)) != 0)
        {
            layers.push_back(i);
        }
    }
    std::vector<std::string> names;
    for (const auto& item : ModeLayers)
    {
        const bool all = std::all_of(item.second.begin(), item.second.end(),
            [&](int value){ return std::find(layers.begin(), layers.end(), value) != layers.end(); });
        const std::string modeName(ModeName(item.first));
        if (all)
        {
            names.push_back(modeName);
        }
        else if (item.second.size() > 1)
        {
            std::vector<std::string> players;
            if (std::find(layers.begin(), layers.end(), item.second[0]) != layers.end()) players.emplace_back("2P");
            if (std::find(layers.begin(), layers.end(), item.second[1]) != layers.end()) players.emplace_back("3P");
            if (std::find(layers.begin(), layers.end(), item.second[2]) != layers.end()) players.emplace_back("4P");
            if (!players.empty())
            {
                std::string value = modeName;
                for (std::size_t i=0;i<players.size();++i)
                {
                    if (i != 0) value += '/';
                    value += players[i];
                }
                names.push_back(std::move(value));
            }
        }
    }
    std::string result;
    for (std::size_t i=0;i<names.size();++i)
    {
        if (i != 0) result += " | ";
        result += names[i];
    }
    return result;
}

const OpenTK::Mathematics::Vector3 EmissionOrange = GetColor(0x14F0);
const OpenTK::Mathematics::Vector3 EmissionGreen = GetColor(0x1565);
const OpenTK::Mathematics::Vector3 EmissionGray = GetColor(0x35AD);
const std::array<ColorRgb, 4> TeamColors{{
    ColorRgb(31, 19, 0), ColorRgb(0, 31, 0),
    ColorRgb(5, 19, 31), ColorRgb(27, 8, 31)
}};
const OpenTK::Mathematics::Vector3 OctolithLight1Vector(0.0F,0.3005371F,-0.5F);
const OpenTK::Mathematics::Vector3 OctolithLight2Vector(0.0F,0.0F,-0.5F);
const OpenTK::Mathematics::Vector3 OctolithLightColor(1.0F,1.0F,1.0F);
const std::array<OpenTK::Mathematics::Vector3,32> ToonTable{{
    GetColor(0x2000),
    GetColor(0x2000),
    GetColor(0x2020),
    GetColor(0x2021),
    GetColor(0x2021),
    GetColor(0x2041),
    GetColor(0x2441),
    GetColor(0x2461),
    GetColor(0x2461),
    GetColor(0x2462),
    GetColor(0x2482),
    GetColor(0x2482),
    GetColor(0x28C3),
    GetColor(0x2CE4),
    GetColor(0x3105),
    GetColor(0x3546),
    GetColor(0x3967),
    GetColor(0x3D88),
    GetColor(0x41C9),
    GetColor(0x45EA),
    GetColor(0x4A0B),
    GetColor(0x4E4B),
    GetColor(0x526C),
    GetColor(0x568D),
    GetColor(0x5ACE),
    GetColor(0x5EEF),
    GetColor(0x6310),
    GetColor(0x6751),
    GetColor(0x6B72),
    GetColor(0x6F93),
    GetColor(0x73D4),
    GetColor(0x77F5),
}};

const std::unordered_map<std::string, std::vector<PaletteData>> PowerPalettes{
    {R"(Alimbic_Power)", std::vector<PaletteData>{PaletteData(32576), PaletteData(32576), PaletteData(32608), PaletteData(32640), PaletteData(32711), PaletteData(32719), PaletteData(32758), PaletteData(32733)}},
    {R"(Generic_Power)", std::vector<PaletteData>{PaletteData(19393), PaletteData(18369), PaletteData(17345), PaletteData(16321), PaletteData(19400), PaletteData(23535), PaletteData(26614), PaletteData(31741)}},
    {R"(Ice_Power)", std::vector<PaletteData>{PaletteData(29453), PaletteData(29453), PaletteData(29485), PaletteData(29517), PaletteData(30578), PaletteData(30614), PaletteData(31705), PaletteData(32734)}},
    {R"(Lava_Power)", std::vector<PaletteData>{PaletteData(671), PaletteData(639), PaletteData(607), PaletteData(575), PaletteData(7807), PaletteData(16127), PaletteData(23391), PaletteData(30719)}},
};

const std::unordered_map<Hunter,float> HunterScales{
    {Hunter::Samus,1.0F},{Hunter::Kanden,static_cast<float>(0x10F5)/4096.0F},
    {Hunter::Trace,1.0F},{Hunter::Sylux,1.0F},{Hunter::Noxus,1.0F},
    {Hunter::Spire,static_cast<float>(0x123D)/4096.0F},{Hunter::Weavel,1.0F},{Hunter::Guardian,1.0F}
};
const std::unordered_map<Hunter,std::array<std::string,4>> HunterModels{
    {Hunter::Samus,{"Samus_lod0","Samus_lod1","SamusAlt_lod0","SamusGun"}},
    {Hunter::Kanden,{"Kanden_lod0","Kanden_lod1","KandenAlt_lod0","KandenGun"}},
    {Hunter::Trace,{"Trace_lod0","Trace_lod1","TraceAlt_lod0","TraceGun"}},
    {Hunter::Sylux,{"Sylux_lod0","Sylux_lod1","SyluxAlt_lod0","SyluxGun"}},
    {Hunter::Noxus,{"Nox_lod0","Nox_lod1","NoxAlt_lod0","NoxGun"}},
    {Hunter::Spire,{"Spire_lod0","Spire_lod1","SpireAlt_lod0","SpireGun"}},
    {Hunter::Weavel,{"Weavel_lod0","Weavel_lod1","WeavelAlt_lod0","WeavelGun"}},
    {Hunter::Guardian,{"Guardian_lod0","Guardian_lod1","SamusAlt_lod0","SamusGun"}}
};
const std::array<int,89> AdpcmTable{{7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767}};
const std::array<int,16> ImaIndexTable{{-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8}};
const std::array<std::string, 60> MusicSeqs{{
    R"(SEQ_BRINSTAR)",
    R"(SEQ_MP1)",
    R"(SEQ_MP2)",
    R"(SEQ_PARASITE)",
    R"(SEQ_SHIP)",
    R"(SEQ_YELLOW)",
    R"(SEQ_RESULTS)",
    R"(SEQ_TIMEOUT)",
    R"(SEQ_WIN)",
    R"(SEQ_GARLIC)",
    R"(SEQ_MP2_X)",
    R"(SEQ_PARASITE_X)",
    R"(SEQ_RED)",
    R"(SEQ_BLUE)",
    R"(SEQ_AMBIENT_1)",
    R"(SEQ_TELEPORT)",
    R"(SEQ_DRONE)",
    R"(SEQ_MENU1)",
    R"(SEQ_GREY)",
    R"(SEQ_SAFFRON)",
    R"(SEQ_GUMBO)",
    R"(SEQ_INTRO_SYLUX)",
    R"(SEQ_INTRO_TRACE)",
    R"(SEQ_INTRO_NOXUS)",
    R"(SEQ_INTRO_WEAVEL)",
    R"(SEQ_INTRO_KANDEN)",
    R"(SEQ_INTRO_SPIRE)",
    R"(SEQ_FLY_IN_2)",
    R"(SEQ_FLY_IN_1)",
    R"(SEQ_FLY_IN_3)",
    R"(SEQ_FLY_IN_4)",
    R"(SEQ_SHIP_LAND1)",
    R"(SEQ_SHIP_LAND2)",
    R"(SEQ_SHIP_LAND3)",
    R"(SEQ_SHIP_LAND4)",
    R"(SEQ_GET_WEAPON)",
    R"(SEQ_GET_OCTOLITH)",
    R"(SEQ_NEW_GAME)",
    R"(SEQ_BEAT_HUNTER1)",
    R"(SEQ_INTRO_GUARDIAN)",
    R"(SEQ_GUARDIAN)",
    R"(SEQ_BEAT_CYLBOSS1)",
    R"(SEQ_GREEN)",
    R"(SEQ_CHUTNEY)",
    R"(SEQ_DILL)",
    R"(SEQ_GOREA_1)",
    R"(SEQ_ENEMY_1)",
    R"(SEQ_GOREA_2)",
    R"(SEQ_PEPPER)",
    R"(SEQ_SINGLE_CART_MENU)",
    R"(SEQ_SINGLE_CART_INGAME)",
    R"(SEQ_SINGLE_CART_TIMEOUT)",
    R"(SEQ_OREGANO)",
    R"(SEQ_ENEMY_2)",
    R"(SEQ_WHITE)",
    R"(SEQ_ENERGY_TIMER)",
    R"(SEQ_BLACK)",
    R"(SEQ_INDIGO)",
    R"(SEQ_CREDITS)",
    R"(SEQ_FLY_IN_GOREA)",
}};
const std::array<float,3> DamageLevels{{0.75F,1.0F,1.25F}};
const std::array<DoorMetadata,4> Doors{{
    DoorMetadata("AlimbicDoor","AlimbicDoorLock",1.4F,2.4F),
    DoorMetadata("AlimbicMorphBallDoor","AlimbicMorphBallDoorLock",0.7F,1.0F),
    DoorMetadata("AlimbicBossDoor","AlimbicBossDoorLock",3.5F,3.5F),
    DoorMetadata("AlimbicThinDoor","ThinDoorLock",1.4F,2.0F)
}};
const std::array<std::string, 3> FhDoors{{
    R"(door)",
    R"(door2)",
    R"(door2_holo)",
}};
const std::array<int,10> DoorPalettes{{0,1,2,7,6,3,4,5,0,0}};
const std::array<std::string, 6> JumpPads{{
    R"(JumpPad)",
    R"(JumpPad_Alimbic)",
    R"(JumpPad_Ice)",
    R"(JumpPad_IceStation)",
    R"(JumpPad_Lava)",
    R"(JumpPad_Station)",
}};
const std::array<std::string, 23> Items{{
    R"(pick_health_B)",
    R"(pick_health_A)",
    R"(pick_health_C)",
    R"(pick_dblDamage)",
    R"(PickUp_EnergyExp)",
    R"(pick_wpn_electro)",
    R"(PickUp_MissileExp)",
    R"(pick_wpn_jackhammer)",
    R"(pick_wpn_snipergun)",
    R"(pick_wpn_shotgun)",
    R"(pick_wpn_mortar)",
    R"(pick_wpn_ghostbuster)",
    R"(pick_wpn_gorea)",
    R"(pick_ammo_green)",
    R"(pick_ammo_green)",
    R"(pick_ammo_orange)",
    R"(pick_ammo_orange)",
    R"(pick_invis)",
    R"(PickUp_AmmoExp)",
    R"(Artifact_Key)",
    R"(pick_deathball)",
    R"(pick_wpn_all)",
    R"(pick_wpn_missile)",
}};
const std::array<std::string, 8> FhItems{{
    R"(pick_ammo_A)",
    R"(pick_ammo_B)",
    R"(pick_health_A)",
    R"(pick_health_B)",
    R"(pick_dblDamage)",
    R"(pick_morphball)",
    R"(pick_wpn_electro)",
    R"(pick_wpn_missile)",
}};
std::array<OpenTK::Mathematics::Vector3,54> ObjectVisPosOffsets{{
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(-0.05F,1.5F,-0.4F),
    OpenTK::Mathematics::Vector3(-0.05F,1.5F,-0.4F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(-0.05F,1.5F,0.0F),
    OpenTK::Mathematics::Vector3(0.01F,1.5F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(-0.03F,1.5F,-0.3F),
    OpenTK::Mathematics::Vector3(0.0F,1.5F,-0.3F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,1.5F,-0.2F),
    OpenTK::Mathematics::Vector3(0.0F,1.5F,-0.2F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,1.4F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,1.4F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,1.75F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
    OpenTK::Mathematics::Vector3(0.0F,0.0F,0.0F),
}};
const PlatformMetadata InvisiblePlat("N/A");
const std::array<std::string, 11> WeaponNames{{
    R"(Power Beam)",
    R"(Volt Driver)",
    R"(Missiles)",
    R"(Battlehammer)",
    R"(Imperialist)",
    R"(Judicator)",
    R"(Magmaul)",
    R"(Shock Coil)",
    R"(Omega Cannon)",
    R"(Platform)",
    R"(Enemy)",
}};
const std::array<std::string, 11> WeaponNamesUpper{{
    R"(POWER BEAM)",
    R"(VOLT DRIVER)",
    R"(MISSILES)",
    R"(BATTLEHAMMER)",
    R"(IMPERIALIST)",
    R"(JUDICATOR)",
    R"(MAGMAUL)",
    R"(SHOCK COIL)",
    R"(OMEGA CANNON)",
    R"(PLATFORM)",
    R"(ENEMY)",
}};
const std::array<int,11> WeaponMessageIds{{0,109,0,110,111,112,113,114,115,0,0}};
const std::array<std::pair<std::string,std::optional<std::string>>,247> Effects{{
    {"", std::nullopt},
    {R"(powerBeam)", std::optional<std::string>{R"(effects)"}},
    {R"(powerBeamNoSplat)", std::optional<std::string>{R"(effects)"}},
    {R"(blastCapHit)", std::nullopt},
    {R"(blastCapBlow)", std::nullopt},
    {R"(missile1)", std::optional<std::string>{R"(effects)"}},
    {R"(mortar1)", std::optional<std::string>{R"(effects)"}},
    {R"(shotGunCol)", std::optional<std::string>{R"(effects)"}},
    {R"(shotGunShrapnel)", std::optional<std::string>{R"(effects)"}},
    {R"(bombStart)", std::nullopt},
    {R"(ballDeath)", std::nullopt},
    {R"(jackHammerCol)", std::optional<std::string>{R"(effects)"}},
    {R"(effectiveHitPB)", std::nullopt},
    {R"(effectiveHitElectric)", std::nullopt},
    {R"(effectiveHitMsl)", std::nullopt},
    {R"(effectiveHitJack)", std::nullopt},
    {R"(effectiveHitSniper)", std::nullopt},
    {R"(effectiveHitIce)", std::nullopt},
    {R"(effectiveHitMortar)", std::nullopt},
    {R"(effectiveHitGhost)", std::nullopt},
    {R"(sprEffectivePB)", std::nullopt},
    {R"(sprEffectiveElectric)", std::nullopt},
    {R"(sprEffectiveMsl)", std::nullopt},
    {R"(sprEffectiveJack)", std::nullopt},
    {R"(sprEffectiveSniper)", std::nullopt},
    {R"(sprEffectiveIce)", std::nullopt},
    {R"(sprEffectiveMortar)", std::nullopt},
    {R"(sprEffectiveGhost)", std::nullopt},
    {R"(sniperCol)", std::optional<std::string>{R"(effects)"}},
    {R"(shriekBatTrail)", std::nullopt},
    {R"(samusFurl)", std::nullopt},
    {R"(spawnEffect)", std::nullopt},
    {R"(test)", std::nullopt},
    {R"(spawnEffectMP)", std::nullopt},
    {R"(burstFlame)", std::nullopt},
    {R"(gunSmoke)", std::nullopt},
    {R"(jetFlame)", std::nullopt},
    {R"(spireAltSlam)", std::nullopt},
    {R"(steamBurst)", std::nullopt},
    {R"(steamSamusShip)", std::nullopt},
    {R"(steamDoorway)", std::nullopt},
    {R"(goreaArmChargeUp)", std::nullopt},
    {R"(goreaBallExplode)", std::nullopt},
    {R"(goreaShoulderDamageLoop)", std::nullopt},
    {R"(goreaShoulderHits)", std::nullopt},
    {R"(goreaShoulderKill)", std::nullopt},
    {R"(goreaChargeElc)", std::nullopt},
    {R"(goreaChargeIce)", std::nullopt},
    {R"(goreaChargeJak)", std::nullopt},
    {R"(goreaChargeMrt)", std::nullopt},
    {R"(goreaChargeSnp)", std::nullopt},
    {R"(goreaFireElc)", std::nullopt},
    {R"(goreaFireGst)", std::nullopt},
    {R"(goreaFireIce)", std::nullopt},
    {R"(goreaFireJak)", std::nullopt},
    {R"(goreaFireMrt)", std::nullopt},
    {R"(goreaFireSnp)", std::nullopt},
    {R"(muzzleElc)", std::nullopt},
    {R"(muzzleGst)", std::nullopt},
    {R"(muzzleIce)", std::nullopt},
    {R"(muzzleJak)", std::nullopt},
    {R"(muzzleMrt)", std::nullopt},
    {R"(muzzlePB)", std::nullopt},
    {R"(muzzleSnp)", std::nullopt},
    {R"(tear)", std::nullopt},
    {R"(cylCrystalCharge)", std::nullopt},
    {R"(cylCrystalKill)", std::nullopt},
    {R"(cylCrystalShot)", std::nullopt},
    {R"(tearSplat)", std::nullopt},
    {R"(eyeShieldCharge)", std::nullopt},
    {R"(eyeShieldHit)", std::nullopt},
    {R"(goreaSlam)", std::nullopt},
    {R"(goreaBallExplode2)", std::nullopt},
    {R"(cylCrystalKill2)", std::nullopt},
    {R"(cylCrystalKill3)", std::nullopt},
    {R"(goreaCrystalExplode)", std::nullopt},
    {R"(DeathBio1)", std::nullopt},
    {R"(DeathMech1)", std::nullopt},
    {R"(iceWave)", std::nullopt},
    {R"(goreaMeteor)", std::nullopt},
    {R"(goreaTeleport)", std::nullopt},
    {R"(tearChargeUp)", std::nullopt},
    {R"(eyeShield)", std::nullopt},
    {R"(eyeShieldDefeat)", std::nullopt},
    {R"(grateSparks)", std::nullopt},
    {R"(electroCharge)", std::nullopt},
    {R"(electroHit)", std::nullopt},
    {R"(torch)", std::nullopt},
    {R"(jetFlameBlue)", std::nullopt},
    {R"(lavaBurstLarge)", std::nullopt},
    {R"(lavaBurstSmall)", std::nullopt},
    {R"(ember)", std::nullopt},
    {R"(powerBeamCharge)", std::nullopt},
    {R"(lavaDemonDive)", std::nullopt},
    {R"(lavaDemonHurl)", std::nullopt},
    {R"(lavaDemonRise)", std::nullopt},
    {R"(iceDemonHurl)", std::nullopt},
    {R"(lavaBurstExtraLarge)", std::nullopt},
    {R"(powerBeamChargeNoSplat)", std::nullopt},
    {R"(powerBeamHolo)", std::nullopt},
    {R"(powerBeamLava)", std::nullopt},
    {R"(hangingDrip)", std::nullopt},
    {R"(hangingSpit)", std::nullopt},
    {R"(hangingSplash)", std::nullopt},
    {R"(goreaEyeFlash)", std::nullopt},
    {R"(smokeBurst)", std::nullopt},
    {R"(sparks)", std::nullopt},
    {R"(sparksFall)", std::nullopt},
    {R"(shriekBatCol)", std::nullopt},
    {R"(eyeTurretCharge)", std::nullopt},
    {R"(lavaDemonSplat)", std::nullopt},
    {R"(tearDrips)", std::nullopt},
    {R"(syluxShipExhaust)", std::nullopt},
    {R"(bombStartSylux)", std::nullopt},
    {R"(lockDefeat)", std::nullopt},
    {R"(ineffectivePsycho)", std::nullopt},
    {R"(cylCrystalProjectile)", std::nullopt},
    {R"(cylWeakSpotShot)", std::nullopt},
    {R"(eyeLaser)", std::nullopt},
    {R"(bombStartMP)", std::nullopt},
    {R"(enemyMslCol)", std::nullopt},
    {R"(powerBeamHoloBG)", std::nullopt},
    {R"(powerBeamHoloB)", std::nullopt},
    {R"(powerBeamIce)", std::nullopt},
    {R"(powerBeamRock)", std::nullopt},
    {R"(powerBeamSand)", std::nullopt},
    {R"(powerBeamSnow)", std::nullopt},
    {R"(bubblesRising)", std::nullopt},
    {R"(bombKanden)", std::nullopt},
    {R"(collapsingStreaks)", std::nullopt},
    {R"(fireProjectile)", std::nullopt},
    {R"(iceDemonSplat)", std::nullopt},
    {R"(iceDemonRise)", std::nullopt},
    {R"(iceDemonDive)", std::nullopt},
    {R"(hammerProjectile)", std::nullopt},
    {R"(synapseKill)", std::nullopt},
    {R"(samusDash)", std::nullopt},
    {R"(electroProjectile)", std::nullopt},
    {R"(cylHomingProjectile)", std::nullopt},
    {R"(cylHomingKill)", std::nullopt},
    {R"(energyRippleB)", std::nullopt},
    {R"(energyRippleBG)", std::nullopt},
    {R"(energyRippleO)", std::nullopt},
    {R"(columnCrash)", std::nullopt},
    {R"(artifactKeyEffect)", std::nullopt},
    {R"(bombBlue)", std::nullopt},
    {R"(bombSylux)", std::nullopt},
    {R"(columnBreak)", std::nullopt},
    {R"(grappleEnd)", std::nullopt},
    {R"(bombStartSyluxG)", std::nullopt},
    {R"(bombStartSyluxO)", std::nullopt},
    {R"(bombStartSyluxP)", std::nullopt},
    {R"(bombStartSyluxR)", std::nullopt},
    {R"(bombStartSyluxW)", std::nullopt},
    {R"(mpEffectivePB)", std::nullopt},
    {R"(mpEffectiveElectric)", std::nullopt},
    {R"(mpEffectiveMsl)", std::nullopt},
    {R"(mpEffectiveJack)", std::nullopt},
    {R"(mpEffectiveSniper)", std::nullopt},
    {R"(mpEffectiveIce)", std::nullopt},
    {R"(mpEffectiveMortar)", std::nullopt},
    {R"(mpEffectiveGhost)", std::nullopt},
    {R"(pipeTricity)", std::nullopt},
    {R"(breakableExplode)", std::nullopt},
    {R"(goreaCrystalHit)", std::nullopt},
    {R"(chargeElc)", std::nullopt},
    {R"(chargeIce)", std::nullopt},
    {R"(chargeJak)", std::nullopt},
    {R"(chargeMrt)", std::nullopt},
    {R"(chargePB)", std::nullopt},
    {R"(chargeMsl)", std::nullopt},
    {R"(electroChargeNA)", std::nullopt},
    {R"(mortarSecondary)", std::nullopt},
    {R"(jackHammerColNA)", std::optional<std::string>{R"(effects)"}},
    {R"(goreaMeteorLaunch)", std::nullopt},
    {R"(goreaReveal)", std::nullopt},
    {R"(goreaMeteorDamage)", std::nullopt},
    {R"(goreaMeteorDestroy)", std::nullopt},
    {R"(goreaMeteorHit)", std::nullopt},
    {R"(goreaGrappleDamage)", std::nullopt},
    {R"(goreaGrappleDie)", std::nullopt},
    {R"(deathBall)", std::nullopt},
    {R"(nozzleJet)", std::nullopt},
    {R"(syluxMissile)", std::nullopt},
    {R"(syluxMissileCol)", std::nullopt},
    {R"(syluxMissileFlash)", std::nullopt},
    {R"(sphereTricity)", std::nullopt},
    {R"(flamingAltForm)", std::nullopt},
    {R"(flamingGun)", std::nullopt},
    {R"(flamingHunter)", std::nullopt},
    {R"(missileCharged)", std::optional<std::string>{R"(effects)"}},
    {R"(mortarCharged)", std::optional<std::string>{R"(effects)"}},
    {R"(mortarChargedAffinity)", std::optional<std::string>{R"(effects)"}},
    {R"(DeathBio2)", std::nullopt},
    {R"(chargeLoopElc)", std::nullopt},
    {R"(chargeLoopIce)", std::nullopt},
    {R"(chargeLoopMrt)", std::nullopt},
    {R"(chargeLoopMsl)", std::nullopt},
    {R"(chargeLoopPB)", std::nullopt},
    {R"(sphereTricitySmall)", std::nullopt},
    {R"(generatorExplosion)", std::nullopt},
    {R"(eyeDamageLoop)", std::nullopt},
    {R"(eyeHit)", std::nullopt},
    {R"(eyelKill)", std::nullopt},
    {R"(eyeKill2)", std::nullopt},
    {R"(eyeKill3)", std::nullopt},
    {R"(eyeFinalKill)", std::nullopt},
    {R"(chargeTurret)", std::nullopt},
    {R"(flashTurret)", std::nullopt},
    {R"(ultimateProjectile)", std::nullopt},
    {R"(goreaLaserCharge)", std::nullopt},
    {R"(mortarProjectile)", std::nullopt},
    {R"(fallingSnow)", std::nullopt},
    {R"(fallingDust)", std::nullopt},
    {R"(fallingRock)", std::nullopt},
    {R"(DeathMech2)", std::nullopt},
    {R"(deathAlt)", std::nullopt},
    {R"(iceDemonDeath)", std::nullopt},
    {R"(lavaDemonDeath)", std::nullopt},
    {R"(DeathBio3)", std::nullopt},
    {R"(DeathBio4)", std::nullopt},
    {R"(DeathBio5)", std::nullopt},
    {R"(DeathStatue)", std::nullopt},
    {R"(DeathTick)", std::nullopt},
    {R"(goreaLaserCol)", std::nullopt},
    {R"(goreaHurt)", std::nullopt},
    {R"(explosionAbove)", std::nullopt},
    {R"(fireFlurry)", std::nullopt},
    {R"(snowFlurry)", std::nullopt},
    {R"(enemySpawn)", std::nullopt},
    {R"(teleporter)", std::nullopt},
    {R"(iceShatter)", std::optional<std::string>{R"(effects)"}},
    {R"(sphereTricityDeath)", std::nullopt},
    {R"(greenFlurry)", std::nullopt},
    {R"(pmagAbsorb)", std::nullopt},
    {R"(noxHit)", std::nullopt},
    {R"(spireBurst)", std::nullopt},
    {R"(electroProjectileUncharged)", std::nullopt},
    {R"(enemyProjectile1)", std::nullopt},
    {R"(enemyCol1)", std::nullopt},
    {R"(psychoCharge)", std::nullopt},
    {R"(hammerProjectileSml)", std::nullopt},
    {R"(nozzleJetOff)", std::nullopt},
    {R"(powerBeamChargeNoSplatMP)", std::nullopt},
    {R"(doubleDamageGun)", std::nullopt},
    {R"(ultimateCol)", std::nullopt},
    {R"(enemyMortarProjectile)", std::nullopt},
}};
const std::array<float,4> BeamRadiusValues{{0.15F,0.25F,0.5F,0.75F}};
const std::array<int,23> BeamDrawEffects{{0,237,137,0,211,130,0,0,0,0,134,209,64,0,102,94,96,0,116,138,183,238,246}};
const std::array<int,6> SyluxBombEffects{{113,152,151,153,150,149}};
const std::unordered_map<SingleType,std::pair<std::string,std::string>> SingleParticles{
    {SingleType::Death,{"deathParticle","death"}}, {SingleType::Fuzzball,{"particles","fuzzBall"}},
    {SingleType::Lore,{"icons","lore"}}, {SingleType::LoreDim,{"icons","lore_dim"}},
    {SingleType::Enemy,{"icons","enemy"}}, {SingleType::EnemyDim,{"icons","enemy_dim"}},
    {SingleType::Object,{"icons","object"}}, {SingleType::ObjectDim,{"icons","object_dim"}},
    {SingleType::Equipment,{"icons","equipment"}}, {SingleType::EquipmentDim,{"icons","equipment_dim"}},
    {SingleType::Red,{"icons","red"}}, {SingleType::RedDim,{"icons","red_dim"}}
};
const std::unordered_map<std::string,bool> PreloadResources{
    {"deathParticle",true},{"particles",true},{"particles2",true},{"TearParticle",true},
    {"icons",true},{"iceWave",true},{"sniperBeam",true},{"cylBossLaserBurn",true}
};
const OpenTK::Mathematics::Vector4 RedPalette(189.0F/255.0F,66.0F/255.0F,0.0F,1.0F);
const OpenTK::Mathematics::Vector4 WhitePalette(1.0F,1.0F,1.0F,1.0F);
const ::MphRead::ModelMetadata DoubleDamageImg("doubleDamage_img",false,false,false,
    std::nullopt,MdlSuffix::None,std::optional<std::string>{"common"},std::nullopt,false,std::nullopt,std::nullopt);

const std::unordered_map<std::string, ::MphRead::ModelMetadata> ModelMetadata{
    {R"(AlimbicBossDoorLock)", ::MphRead::ModelMetadata(R"(AlimbicBossDoorLock)", true, false, false, std::optional<std::string>{R"(models\AlimbicTextureShare_img_Model.bin)"}, MdlSuffix::All, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicBossDoor)", ::MphRead::ModelMetadata(R"(AlimbicBossDoor)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicCapsule)", ::MphRead::ModelMetadata(R"(AlimbicCapsule)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::optional<std::string>{R"(AlmbCapsuleShld)"})},
    {R"(AlimbicComputerStationControl)", ::MphRead::ModelMetadata(R"(AlimbicComputerStationControl)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicComputerStationControl02)", ::MphRead::ModelMetadata(R"(AlimbicComputerStationControl02)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicDoorLock)", ::MphRead::ModelMetadata(R"(AlimbicDoorLock)", true, false, false, std::optional<std::string>{R"(models\AlimbicTextureShare_img_Model.bin)"}, MdlSuffix::All, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicDoor)", ::MphRead::ModelMetadata(R"(AlimbicDoor)", R"(models\AlimbicDoor_Model.bin)", std::optional<std::string>{R"(models\AlimbicDoor_Anim.bin)"}, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(pal_01)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 0, std::vector<int>{1} }}, false), RecolorMetadata(R"(pal_02)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 1, std::vector<int>{1} }}, false), RecolorMetadata(R"(pal_03)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 2, std::vector<int>{1} }}, false), RecolorMetadata(R"(pal_04)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 3, std::vector<int>{1} }}, false), RecolorMetadata(R"(pal_05)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 4, std::vector<int>{1} }}, false), RecolorMetadata(R"(pal_06)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 5, std::vector<int>{1} }}, false), RecolorMetadata(R"(pal_07)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 6, std::vector<int>{1} }}, false), RecolorMetadata(R"(pal_08)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 7, std::vector<int>{1} }}, false)}, std::nullopt, false)},
    {R"(AlimbicEnergySensor)", ::MphRead::ModelMetadata(R"(AlimbicEnergySensor)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicGhost_01)", ::MphRead::ModelMetadata(R"(AlimbicGhost_01)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicLightPole)", ::MphRead::ModelMetadata(R"(AlimbicLightPole)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicLightPole02)", ::MphRead::ModelMetadata(R"(AlimbicLightPole02)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicMorphBallDoor)", ::MphRead::ModelMetadata(R"(AlimbicMorphBallDoor)", true, false, false, std::optional<std::string>{R"(models\AlimbicTextureShare_img_Model.bin)"}, MdlSuffix::All, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicMorphBallDoorLock)", ::MphRead::ModelMetadata(R"(AlimbicMorphBallDoorLock)", true, false, false, std::optional<std::string>{R"(models\AlimbicTextureShare_img_Model.bin)"}, MdlSuffix::All, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicStationShieldControl)", ::MphRead::ModelMetadata(R"(AlimbicStationShieldControl)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(AlimbicStatue_lod0)", ::MphRead::ModelMetadata(R"(AlimbicStatue_lod0)", R"(_lod0)", true, std::nullopt, true, false)},
    {R"(AlimbicThinDoor)", ::MphRead::ModelMetadata(R"(AlimbicThinDoor)", R"(models\AlimbicThinDoor_Model.bin)", std::optional<std::string>{R"(models\AlimbicThinDoor_Anim.bin)"}, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(pal_01)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 0, std::vector<int>{1, 2} }}, false), RecolorMetadata(R"(pal_02)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 1, std::vector<int>{1, 2} }}, false), RecolorMetadata(R"(pal_03)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 2, std::vector<int>{1, 2} }}, false), RecolorMetadata(R"(pal_04)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 3, std::vector<int>{1, 2} }}, false), RecolorMetadata(R"(pal_05)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 4, std::vector<int>{1, 2} }}, false), RecolorMetadata(R"(pal_06)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 5, std::vector<int>{1, 2} }}, false), RecolorMetadata(R"(pal_07)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 6, std::vector<int>{1, 2} }}, false), RecolorMetadata(R"(pal_08)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicThinDoor_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 7, std::vector<int>{1, 2} }}, false)}, std::nullopt, false)},
    {R"(Alimbic_Console)", ::MphRead::ModelMetadata(R"(Alimbic_Console)", true, true, false, std::optional<std::string>{R"(models\AlimbicEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Alimbic_Monitor)", ::MphRead::ModelMetadata(R"(Alimbic_Monitor)", true, true, false, std::optional<std::string>{R"(models\AlimbicEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Alimbic_Power)", ::MphRead::ModelMetadata(R"(Alimbic_Power)", true, false, false, std::optional<std::string>{R"(models\AlimbicEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Alimbic_Scanner)", ::MphRead::ModelMetadata(R"(Alimbic_Scanner)", true, false, false, std::optional<std::string>{R"(models\AlimbicEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Alimbic_Switch)", ::MphRead::ModelMetadata(R"(Alimbic_Switch)", true, true, false, std::optional<std::string>{R"(models\AlimbicEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Alimbic_Turret)", ::MphRead::ModelMetadata(R"(Alimbic_Turret)", std::vector<std::string>{R"(img_00)", R"(img_04)", R"(img_05)"}, std::nullopt, false, std::optional<std::string>{R"(models\AlimbicTurret_Anim.bin)"}, false, MdlSuffix::Model, std::nullopt, std::nullopt, std::nullopt, false, false, false)},
    {R"(alt_ice)", ::MphRead::ModelMetadata(R"(alt_ice)", R"(_archives\common\alt_ice_mdl_Model.bin)", std::nullopt, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(default)", R"(_archives\common\samus_ice_img_Model.bin)", R"(_archives\common\samus_ice_img_Model.bin)", R"(_archives\common\samus_ice_img_Model.bin)", std::map<int, std::vector<int>>{}, false)}, std::nullopt, true)},
    {R"(arcWelder)", ::MphRead::ModelMetadata(R"(arcWelder)", false, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(arcWelder1)", ::MphRead::ModelMetadata(R"(arcWelder1)", std::vector<std::string>{R"(1)", R"(2)", R"(3)", R"(4)", R"(5)"}, std::optional<std::string>{R"(1)"}, false, std::nullopt, false, MdlSuffix::None, std::nullopt, std::nullopt, std::nullopt, false, false, true)},
    {R"(ArtifactBase)", ::MphRead::ModelMetadata(R"(ArtifactBase)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Artifact_Key)", ::MphRead::ModelMetadata(R"(Artifact_Key)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Artifact01)", ::MphRead::ModelMetadata(R"(Artifact01)", true, false, false, std::optional<std::string>{R"(models\ArtifactTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Artifact_Anim.bin)"}, std::nullopt)},
    {R"(Artifact02)", ::MphRead::ModelMetadata(R"(Artifact02)", true, false, false, std::optional<std::string>{R"(models\ArtifactTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Artifact_Anim.bin)"}, std::nullopt)},
    {R"(Artifact03)", ::MphRead::ModelMetadata(R"(Artifact03)", true, false, false, std::optional<std::string>{R"(models\ArtifactTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Artifact_Anim.bin)"}, std::nullopt)},
    {R"(Artifact04)", ::MphRead::ModelMetadata(R"(Artifact04)", true, false, false, std::optional<std::string>{R"(models\ArtifactTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Artifact_Anim.bin)"}, std::nullopt)},
    {R"(Artifact05)", ::MphRead::ModelMetadata(R"(Artifact05)", true, false, false, std::optional<std::string>{R"(models\ArtifactTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Artifact_Anim.bin)"}, std::nullopt)},
    {R"(Artifact06)", ::MphRead::ModelMetadata(R"(Artifact06)", true, false, false, std::optional<std::string>{R"(models\ArtifactTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Artifact_Anim.bin)"}, std::nullopt)},
    {R"(Artifact07)", ::MphRead::ModelMetadata(R"(Artifact07)", true, false, false, std::optional<std::string>{R"(models\ArtifactTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Artifact_Anim.bin)"}, std::nullopt)},
    {R"(Artifact08)", ::MphRead::ModelMetadata(R"(Artifact08)", true, false, false, std::optional<std::string>{R"(models\ArtifactTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Artifact_Anim.bin)"}, std::nullopt)},
    {R"(balljump)", ::MphRead::ModelMetadata(R"(balljump)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(balljump_ray)", ::MphRead::ModelMetadata(R"(balljump_ray)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(BarbedWarWasp)", ::MphRead::ModelMetadata(R"(BarbedWarWasp)", std::vector<std::string>{R"(img_00)", R"(img_02)", R"(img_03)"}, std::nullopt, false, std::optional<std::string>{R"(models\warWasp_Anim.bin)"}, false, MdlSuffix::Model, std::nullopt, std::nullopt, std::nullopt, false, false, false)},
    {R"(BigEyeBall)", ::MphRead::ModelMetadata(R"(BigEyeBall)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(BigEyeNest)", ::MphRead::ModelMetadata(R"(BigEyeNest)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(BigEyeShield)", ::MphRead::ModelMetadata(R"(BigEyeShield)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(BigEyeSynapse_01)", ::MphRead::ModelMetadata(R"(BigEyeSynapse_01)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\BigEyeSynapse_Anim.bin)"}, std::nullopt)},
    {R"(BigEyeSynapse_02)", ::MphRead::ModelMetadata(R"(BigEyeSynapse_02)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\BigEyeSynapse_Anim.bin)"}, std::nullopt)},
    {R"(BigEyeSynapse_03)", ::MphRead::ModelMetadata(R"(BigEyeSynapse_03)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\BigEyeSynapse_Anim.bin)"}, std::nullopt)},
    {R"(BigEyeSynapse_04)", ::MphRead::ModelMetadata(R"(BigEyeSynapse_04)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\BigEyeSynapse_Anim.bin)"}, std::nullopt)},
    {R"(BigEyeTurret)", ::MphRead::ModelMetadata(R"(BigEyeTurret)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(blastcap)", ::MphRead::ModelMetadata(R"(blastcap)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(brain_unit3_c2)", ::MphRead::ModelMetadata(R"(brain_unit3_c2)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Chomtroid)", ::MphRead::ModelMetadata(R"(Chomtroid)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::optional<std::string>{R"(models\Mochtroid_Anim.bin)"}, std::nullopt)},
    {R"(Crate01)", ::MphRead::ModelMetadata(R"(Crate01)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(cylBossLaserBurn)", ::MphRead::ModelMetadata(R"(cylBossLaserBurn)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(cylBossLaserColl)", ::MphRead::ModelMetadata(R"(cylBossLaserColl)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(cylBossLaserG)", ::MphRead::ModelMetadata(R"(cylBossLaserG)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(cylBossLaserY)", ::MphRead::ModelMetadata(R"(cylBossLaserY)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(cylBossLaser)", ::MphRead::ModelMetadata(R"(cylBossLaser)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(cylinderbase)", ::MphRead::ModelMetadata(R"(cylinderbase)", R"(models\cylinderbase_model.bin)", std::nullopt, std::optional<std::string>{R"(models\cylinderbase_collision.bin)"}, false)},
    {R"(CylinderBossEye)", ::MphRead::ModelMetadata(R"(CylinderBossEye)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(CylinderBoss)", ::MphRead::ModelMetadata(R"(CylinderBoss)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(deepspace)", ::MphRead::ModelMetadata(R"(deepspace)", true, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(shipSpace)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Door_Unit4_RM1)", ::MphRead::ModelMetadata(R"(Door_Unit4_RM1)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(DripStank_lod0)", ::MphRead::ModelMetadata(R"(DripStank_lod0)", R"(_lod0)", true, std::nullopt, false, false)},
    {R"(ElectroField1)", ::MphRead::ModelMetadata(R"(ElectroField1)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(electroTrail)", ::MphRead::ModelMetadata(R"(electroTrail)", false, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Elevator)", ::MphRead::ModelMetadata(R"(Elevator)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(EnemySpawner)", ::MphRead::ModelMetadata(R"(EnemySpawner)", true, false, false, std::optional<std::string>{R"(models\AlimbicTextureShare_img_Model.bin)"}, MdlSuffix::All, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(energyBeam)", ::MphRead::ModelMetadata(R"(energyBeam)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(filter)", ::MphRead::ModelMetadata(R"(filter)", false, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(hudfont)", ::MphRead::ModelMetadata(R"(hudfont)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(flagbase_bounty)", ::MphRead::ModelMetadata(R"(flagbase_bounty)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(flagbase_cap)", ::MphRead::ModelMetadata(R"(flagbase_cap)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(flagbase_ctf)", ::MphRead::ModelMetadata(R"(flagbase_ctf)", std::vector<std::string>{R"(orange_img)", R"(green_img)"}, std::nullopt, true, std::nullopt, false, MdlSuffix::Model, std::nullopt, std::nullopt, std::nullopt, false, false, false)},
    {R"(ForceField)", ::MphRead::ModelMetadata(R"(ForceField)", R"(models\ForceField_Model.bin)", std::optional<std::string>{R"(models\ForceField_Anim.bin)"}, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(pal_01)", R"(models\ForceField_Model.bin)", R"(models\ForceField_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 0, std::vector<int>{0} }}, false), RecolorMetadata(R"(pal_02)", R"(models\ForceField_Model.bin)", R"(models\ForceField_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 1, std::vector<int>{0} }}, false), RecolorMetadata(R"(pal_03)", R"(models\ForceField_Model.bin)", R"(models\ForceField_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 2, std::vector<int>{0} }}, false), RecolorMetadata(R"(pal_04)", R"(models\ForceField_Model.bin)", R"(models\ForceField_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 3, std::vector<int>{0} }}, false), RecolorMetadata(R"(pal_05)", R"(models\ForceField_Model.bin)", R"(models\ForceField_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 4, std::vector<int>{0} }}, false), RecolorMetadata(R"(pal_06)", R"(models\ForceField_Model.bin)", R"(models\ForceField_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 5, std::vector<int>{0} }}, false), RecolorMetadata(R"(pal_07)", R"(models\ForceField_Model.bin)", R"(models\ForceField_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 6, std::vector<int>{0} }}, false), RecolorMetadata(R"(pal_08)", R"(models\ForceField_Model.bin)", R"(models\ForceField_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 7, std::vector<int>{0} }}, false)}, std::nullopt, false)},
    {R"(ForceFieldLock)", ::MphRead::ModelMetadata(R"(ForceFieldLock)", R"(models\ForceFieldLock_mdl_Model.bin)", std::optional<std::string>{R"(models\ForceFieldLock_mdl_Anim.bin)"}, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(pal_01)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 0, std::vector<int>{3} }}, true), RecolorMetadata(R"(pal_02)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 1, std::vector<int>{3} }}, true), RecolorMetadata(R"(pal_03)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 2, std::vector<int>{3} }}, true), RecolorMetadata(R"(pal_04)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 3, std::vector<int>{3} }}, true), RecolorMetadata(R"(pal_05)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 4, std::vector<int>{3} }}, true), RecolorMetadata(R"(pal_06)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 5, std::vector<int>{3} }}, true), RecolorMetadata(R"(pal_07)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 6, std::vector<int>{3} }}, true), RecolorMetadata(R"(pal_08)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicTextureShare_img_Model.bin)", R"(models\AlimbicPalettes_pal_Model.bin)", std::map<int, std::vector<int>>{{ 7, std::vector<int>{3} }}, true)}, std::nullopt, false)},
    {R"(furlEffect)", ::MphRead::ModelMetadata(R"(furlEffect)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(geemer)", ::MphRead::ModelMetadata(R"(geemer)", R"(models\geemer_Model.bin)", std::optional<std::string>{R"(models\Geemer_Anim.bin)"}, std::nullopt, false)},
    {R"(Generic_Console)", ::MphRead::ModelMetadata(R"(Generic_Console)", true, true, false, std::optional<std::string>{R"(models\GenericEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Generic_Monitor)", ::MphRead::ModelMetadata(R"(Generic_Monitor)", true, true, false, std::optional<std::string>{R"(models\GenericEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Generic_Power)", ::MphRead::ModelMetadata(R"(Generic_Power)", true, false, false, std::optional<std::string>{R"(models\GenericEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Generic_Scanner)", ::MphRead::ModelMetadata(R"(Generic_Scanner)", true, false, false, std::optional<std::string>{R"(models\GenericEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Generic_Switch)", ::MphRead::ModelMetadata(R"(Generic_Switch)", true, true, false, std::optional<std::string>{R"(models\GenericEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(GhostSwitch)", ::MphRead::ModelMetadata(R"(GhostSwitch)", true, false, false, std::optional<std::string>{R"(models\AlimbicTextureShare_img_Model.bin)"}, MdlSuffix::All, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Gorea1A_lod0)", ::MphRead::ModelMetadata(R"(Gorea1A_lod0)", R"(_lod0)", true, std::nullopt, false, false)},
    {R"(Gorea1B_lod0)", ::MphRead::ModelMetadata(R"(Gorea1B_lod0)", R"(_lod0)", true, std::nullopt, false, false)},
    {R"(Gorea2_lod0)", ::MphRead::ModelMetadata(R"(Gorea2_lod0)", R"(_lod0)", true, std::nullopt, false, false)},
    {R"(goreaArmRegen)", ::MphRead::ModelMetadata(R"(goreaArmRegen)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(goreaGeo)", ::MphRead::ModelMetadata(R"(goreaGeo)", false, false, true, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(goreaGrappleBeam)", ::MphRead::ModelMetadata(R"(goreaGrappleBeam)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},

    {R"(goreaLaserColl)", ::MphRead::ModelMetadata(R"(goreaLaserColl)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(goreaLaser)", ::MphRead::ModelMetadata(R"(goreaLaser)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(goreaMeteor)", ::MphRead::ModelMetadata(R"(goreaMeteor)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(goreaMindTrick)", ::MphRead::ModelMetadata(R"(goreaMindTrick)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(gorea_gun)", ::MphRead::ModelMetadata(R"(gorea_gun)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Guardbot01_Dead)", ::MphRead::ModelMetadata(R"(Guardbot01_Dead)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Guardbot02_Dead)", ::MphRead::ModelMetadata(R"(Guardbot02_Dead)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(GuardBot1)", ::MphRead::ModelMetadata(R"(GuardBot1)", std::vector<std::string>{R"(img_00)", R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)"}, std::nullopt, false, std::optional<std::string>{R"(models\GuardBot01_Anim.bin)"}, false, MdlSuffix::Model, std::nullopt, std::nullopt, std::nullopt, false, false, false)},
    {R"(GuardBot2_lod0)", ::MphRead::ModelMetadata(R"(GuardBot2_lod0)", R"(_lod0)", true, std::optional<std::string>{R"(models\GuardBot02_Anim.bin)"}, false, false)},
    {R"(Guardian_Dead)", ::MphRead::ModelMetadata(R"(Guardian_Dead)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Guardian_lod0)", ::MphRead::ModelMetadata(R"(Guardian_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, false, std::optional<std::string>{R"(_archives\Guardian\Guardian_Anim.bin)"}, true, MdlSuffix::None, std::optional<std::string>{R"(Guardian)"}, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Guardian_lod1)", ::MphRead::ModelMetadata(R"(Guardian_lod1)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod1)"}, false, std::optional<std::string>{R"(_archives\Guardian\Guardian_Anim.bin)"}, true, MdlSuffix::None, std::nullopt, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Guardian_Stasis)", ::MphRead::ModelMetadata(R"(Guardian_Stasis)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(gunSmoke)", ::MphRead::ModelMetadata(R"(gunSmoke)", true, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ice_Console)", ::MphRead::ModelMetadata(R"(Ice_Console)", true, true, false, std::optional<std::string>{R"(models\IceEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ice_Monitor)", ::MphRead::ModelMetadata(R"(Ice_Monitor)", true, true, false, std::optional<std::string>{R"(models\IceEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ice_Power)", ::MphRead::ModelMetadata(R"(Ice_Power)", true, false, false, std::optional<std::string>{R"(models\IceEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ice_Scanner)", ::MphRead::ModelMetadata(R"(Ice_Scanner)", true, false, false, std::optional<std::string>{R"(models\IceEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ice_Switch)", ::MphRead::ModelMetadata(R"(Ice_Switch)", true, true, false, std::optional<std::string>{R"(models\IceEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(iceShard)", ::MphRead::ModelMetadata(R"(iceShard)", false, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(iceWave)", ::MphRead::ModelMetadata(R"(iceWave)", true, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(items_base)", ::MphRead::ModelMetadata(R"(items_base)", false, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(JumpPad_Alimbic)", ::MphRead::ModelMetadata(R"(JumpPad_Alimbic)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(JumpPad_Beam)", ::MphRead::ModelMetadata(R"(JumpPad_Beam)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(JumpPad_IceStation)", ::MphRead::ModelMetadata(R"(JumpPad_IceStation)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(JumpPad_Ice)", ::MphRead::ModelMetadata(R"(JumpPad_Ice)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(JumpPad_Lava)", ::MphRead::ModelMetadata(R"(JumpPad_Lava)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(JumpPad)", ::MphRead::ModelMetadata(R"(JumpPad)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(JumpPad_Station)", ::MphRead::ModelMetadata(R"(JumpPad_Station)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Kanden_lod0)", ::MphRead::ModelMetadata(R"(Kanden_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Kanden)"}, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Kanden_lod1)", ::MphRead::ModelMetadata(R"(Kanden_lod1)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod1)"}, true, std::optional<std::string>{R"(_archives\Kanden\Kanden_Anim.bin)"}, true, MdlSuffix::None, std::nullopt, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(KandenAlt_lod0)", ::MphRead::ModelMetadata(R"(KandenAlt_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Kanden)"}, std::optional<std::string>{R"(Kanden)"}, std::nullopt, true, false, false)},
    {R"(KandenAlt_TailBomb)", ::MphRead::ModelMetadata(R"(KandenAlt_TailBomb)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::nullopt, false, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Kanden)"}, std::optional<std::string>{R"(Kanden)"}, std::nullopt, false, false, false)},
    {R"(KandenGun)", ::MphRead::ModelMetadata(R"(KandenGun)", std::vector<std::string>{R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)", R"(img_Team01)", R"(img_Team02)"}, std::nullopt, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(localKanden)"}, std::nullopt, std::nullopt, true, false, false)},
    {R"(koth_data_flow)", ::MphRead::ModelMetadata(R"(koth_data_flow)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(koth_terminal)", ::MphRead::ModelMetadata(R"(koth_terminal)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(LavaDemon)", ::MphRead::ModelMetadata(R"(LavaDemon)", std::vector<std::string>{R"(img_00)", R"(img_03)"}, std::nullopt, true, std::nullopt, false, MdlSuffix::Model, std::nullopt, std::nullopt, std::nullopt, false, false, false)},
    {R"(Lava_Console)", ::MphRead::ModelMetadata(R"(Lava_Console)", true, true, false, std::optional<std::string>{R"(models\LavaEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Lava_Monitor)", ::MphRead::ModelMetadata(R"(Lava_Monitor)", true, true, false, std::optional<std::string>{R"(models\LavaEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Lava_Power)", ::MphRead::ModelMetadata(R"(Lava_Power)", true, false, false, std::optional<std::string>{R"(models\LavaEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Lava_Scanner)", ::MphRead::ModelMetadata(R"(Lava_Scanner)", true, false, false, std::optional<std::string>{R"(models\LavaEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Lava_Switch)", ::MphRead::ModelMetadata(R"(Lava_Switch)", true, true, false, std::optional<std::string>{R"(models\LavaEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(lines)", ::MphRead::ModelMetadata(R"(lines)", true, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(frontend2d)"}, std::optional<std::string>{R"(_Idle)"}, false, std::nullopt, std::nullopt)},
    {R"(MoverTest)", ::MphRead::ModelMetadata(R"(MoverTest)", R"(models\MoverTest_Model.bin)", std::optional<std::string>{R"(models\movertest_Anim.bin)"}, std::nullopt, false)},
    {R"(Nox_lod0)", ::MphRead::ModelMetadata(R"(Nox_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Nox)"}, std::nullopt, std::optional<std::string>{R"(models\NoxSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Nox_lod1)", ::MphRead::ModelMetadata(R"(Nox_lod1)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod1)"}, true, std::optional<std::string>{R"(_archives\Nox\Nox_Anim.bin)"}, true, MdlSuffix::None, std::nullopt, std::nullopt, std::optional<std::string>{R"(models\NoxSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(NoxAlt_lod0)", ::MphRead::ModelMetadata(R"(NoxAlt_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Nox)"}, std::optional<std::string>{R"(Nox)"}, std::nullopt, true, false, false)},
    {R"(NoxGun)", ::MphRead::ModelMetadata(R"(NoxGun)", std::vector<std::string>{R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)", R"(img_Team01)", R"(img_Team02)"}, std::nullopt, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(localNox)"}, std::nullopt, std::nullopt, true, false, false)},
    {R"(nox_ice)", ::MphRead::ModelMetadata(R"(nox_ice)", R"(_archives\common\nox_ice_mdl_Model.bin)", std::nullopt, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(default)", R"(_archives\common\samus_ice_img_Model.bin)", R"(_archives\common\samus_ice_img_Model.bin)", R"(_archives\common\samus_ice_img_Model.bin)", std::map<int, std::vector<int>>{}, false)}, std::nullopt, true)},
    {R"(octolith_ctf)", ::MphRead::ModelMetadata(R"(octolith_ctf)", std::vector<std::string>{R"(orange_img)", R"(green_img)", R"(*octolith_bounty_img)"}, std::nullopt, true, std::nullopt, false, MdlSuffix::Model, std::nullopt, std::nullopt, std::nullopt, false, false, false)},
    {R"(Octolith)", ::MphRead::ModelMetadata(R"(Octolith)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(octolith_simple)", ::MphRead::ModelMetadata(R"(octolith_simple)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(PickUp_AmmoExp)", ::MphRead::ModelMetadata(R"(PickUp_AmmoExp)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(PickUp_EnergyExp)", ::MphRead::ModelMetadata(R"(PickUp_EnergyExp)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(PickUp_MissileExp)", ::MphRead::ModelMetadata(R"(PickUp_MissileExp)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_ammo_green)", ::MphRead::ModelMetadata(R"(pick_ammo_green)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_ammo_orange)", ::MphRead::ModelMetadata(R"(pick_ammo_orange)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_dblDamage)", ::MphRead::ModelMetadata(R"(pick_dblDamage)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_deathball)", ::MphRead::ModelMetadata(R"(pick_deathball)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_health_A)", ::MphRead::ModelMetadata(R"(pick_health_A)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_health_B)", ::MphRead::ModelMetadata(R"(pick_health_B)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_health_C)", ::MphRead::ModelMetadata(R"(pick_health_C)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_invis)", ::MphRead::ModelMetadata(R"(pick_invis)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_all)", ::MphRead::ModelMetadata(R"(pick_wpn_all)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_electro)", ::MphRead::ModelMetadata(R"(pick_wpn_electro)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_ghostbuster)", ::MphRead::ModelMetadata(R"(pick_wpn_ghostbuster)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_gorea)", ::MphRead::ModelMetadata(R"(pick_wpn_gorea)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_jackhammer)", ::MphRead::ModelMetadata(R"(pick_wpn_jackhammer)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_missile)", ::MphRead::ModelMetadata(R"(pick_wpn_missile)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_mortar)", ::MphRead::ModelMetadata(R"(pick_wpn_mortar)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_shotgun)", ::MphRead::ModelMetadata(R"(pick_wpn_shotgun)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pick_wpn_snipergun)", ::MphRead::ModelMetadata(R"(pick_wpn_snipergun)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pillar)", ::MphRead::ModelMetadata(R"(pillar)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(pistonmp7)", ::MphRead::ModelMetadata(R"(pistonmp7)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(piston_gorealand)", ::MphRead::ModelMetadata(R"(piston_gorealand)", R"(models\piston_gorealand_model.bin)", std::nullopt, std::optional<std::string>{R"(models\piston_gorealand_collision.bin)"}, false)},
    {R"(PlantCarnivarous_Branched)", ::MphRead::ModelMetadata(R"(PlantCarnivarous_Branched)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(PlantCarnivarous_PodLeaves)", ::MphRead::ModelMetadata(R"(PlantCarnivarous_PodLeaves)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(PlantCarnivarous_Pod)", ::MphRead::ModelMetadata(R"(PlantCarnivarous_Pod)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(PlantCarnivarous_Vine)", ::MphRead::ModelMetadata(R"(PlantCarnivarous_Vine)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(platform)", ::MphRead::ModelMetadata(R"(platform)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Platform_Unit4_C1)", ::MphRead::ModelMetadata(R"(Platform_Unit4_C1)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(PowerBomb)", ::MphRead::ModelMetadata(R"(PowerBomb)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Psychobit_Dead)", ::MphRead::ModelMetadata(R"(Psychobit_Dead)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(PsychoBit)", ::MphRead::ModelMetadata(R"(PsychoBit)", std::vector<std::string>{R"(img_00)", R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)"}, std::nullopt, true, std::nullopt, false, MdlSuffix::Model, std::nullopt, std::nullopt, std::nullopt, false, false, false)},
    {R"(quads)", ::MphRead::ModelMetadata(R"(quads)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ruins_Console)", ::MphRead::ModelMetadata(R"(Ruins_Console)", true, true, false, std::optional<std::string>{R"(models\RuinsEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ruins_Monitor)", ::MphRead::ModelMetadata(R"(Ruins_Monitor)", true, true, false, std::optional<std::string>{R"(models\RuinsEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ruins_Power)", ::MphRead::ModelMetadata(R"(Ruins_Power)", true, false, false, std::optional<std::string>{R"(models\RuinsEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ruins_Scanner)", ::MphRead::ModelMetadata(R"(Ruins_Scanner)", true, false, false, std::optional<std::string>{R"(models\RuinsEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Ruins_Switch)", ::MphRead::ModelMetadata(R"(Ruins_Switch)", true, true, false, std::optional<std::string>{R"(models\RuinsEquipTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(SamusShip)", ::MphRead::ModelMetadata(R"(SamusShip)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Samus_lod0)", ::MphRead::ModelMetadata(R"(Samus_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_team01)", R"(pal_team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Samus)"}, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Samus_lod1)", ::MphRead::ModelMetadata(R"(Samus_lod1)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_team01)", R"(pal_team02)"}, std::optional<std::string>{R"(_lod1)"}, true, std::optional<std::string>{R"(_archives\Samus\Samus_Anim.bin)"}, true, MdlSuffix::None, std::nullopt, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(SamusAlt_lod0)", ::MphRead::ModelMetadata(R"(SamusAlt_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_team01)", R"(pal_team02)"}, std::optional<std::string>{R"(_lod0)"}, false, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Samus)"}, std::optional<std::string>{R"(Samus)"}, std::nullopt, true, false, false)},
    {R"(SamusGun)", ::MphRead::ModelMetadata(R"(SamusGun)", std::vector<std::string>{R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)", R"(img_Team01)", R"(img_Team02)"}, std::nullopt, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(localSamus)"}, std::nullopt, std::nullopt, true, false, false)},
    {R"(samus_ice)", ::MphRead::ModelMetadata(R"(samus_ice)", R"(_archives\common\samus_ice_mdl_Model.bin)", std::nullopt, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(default)", R"(_archives\common\samus_ice_img_Model.bin)", R"(_archives\common\samus_ice_img_Model.bin)", R"(_archives\common\samus_ice_img_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(dbl_dmg)", R"(_archives\common\doubleDamage_img_Model.bin)", R"(_archives\common\doubleDamage_img_Model.bin)", R"(_archives\common\doubleDamage_img_Model.bin)", std::map<int, std::vector<int>>{}, false)}, std::nullopt, true)},
    {R"(SecretSwitch)", ::MphRead::ModelMetadata(R"(SecretSwitch)", R"(models\SecretSwitch_Model.bin)", std::optional<std::string>{R"(models\SecretSwitch_Anim.bin)"}, std::optional<std::string>{R"(models\SecretSwitch_Collision.bin)"}, std::vector<RecolorMetadata>{RecolorMetadata(R"(default)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_01)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_pal_01_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_02)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_pal_02_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_03)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_pal_03_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_04)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_pal_04_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_05)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_pal_05_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_06)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_Model.bin)", R"(models\SecretSwitch_pal_06_Model.bin)", std::map<int, std::vector<int>>{}, false)}, std::nullopt, false)},
    {R"(shriekbat)", ::MphRead::ModelMetadata(R"(shriekbat)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(slots)", ::MphRead::ModelMetadata(R"(slots)", true, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(frontend2d)"}, std::optional<std::string>{R"(_Idle)"}, false, std::nullopt, std::nullopt)},
    {R"(smasher)", ::MphRead::ModelMetadata(R"(smasher)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(sniperBeam)", ::MphRead::ModelMetadata(R"(sniperBeam)", true, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(SniperTarget)", ::MphRead::ModelMetadata(R"(SniperTarget)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(SphinkTick_lod0)", ::MphRead::ModelMetadata(R"(SphinkTick_lod0)", R"(_lod0)", true, std::nullopt, false, false)},
    {R"(Spire_lod0)", ::MphRead::ModelMetadata(R"(Spire_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Spire)"}, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Spire_lod1)", ::MphRead::ModelMetadata(R"(Spire_lod1)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod1)"}, true, std::optional<std::string>{R"(_archives\Spire\Spire_Anim.bin)"}, true, MdlSuffix::None, std::nullopt, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(SpireAlt_lod0)", ::MphRead::ModelMetadata(R"(SpireAlt_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Spire)"}, std::optional<std::string>{R"(Spire)"}, std::nullopt, true, false, false)},
    {R"(SpireGun)", ::MphRead::ModelMetadata(R"(SpireGun)", std::vector<std::string>{R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)", R"(img_Team01)", R"(img_Team02)"}, std::nullopt, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(localSpire)"}, std::nullopt, std::nullopt, true, false, false)},
    {R"(splashRing)", ::MphRead::ModelMetadata(R"(splashRing)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Switch)", ::MphRead::ModelMetadata(R"(Switch)", false, false, false, std::optional<std::string>{R"(models\AlimbicTextureShare_img_Model.bin)"}, MdlSuffix::Model, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Sylux_lod0)", ::MphRead::ModelMetadata(R"(Sylux_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Sylux)"}, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Sylux_lod1)", ::MphRead::ModelMetadata(R"(Sylux_lod1)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod1)"}, true, std::optional<std::string>{R"(_archives\Sylux\Sylux_Anim.bin)"}, true, MdlSuffix::None, std::nullopt, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(SyluxAlt_lod0)", ::MphRead::ModelMetadata(R"(SyluxAlt_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Sylux)"}, std::optional<std::string>{R"(Sylux)"}, std::nullopt, true, false, false)},
    {R"(SyluxGun)", ::MphRead::ModelMetadata(R"(SyluxGun)", std::vector<std::string>{R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)", R"(img_Team01)", R"(img_Team02)"}, std::nullopt, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(localSylux)"}, std::nullopt, std::nullopt, true, false, false)},
    {R"(SyluxShip)", ::MphRead::ModelMetadata(R"(SyluxShip)", R"(models\SyluxShip_Model.bin)", std::optional<std::string>{R"(models\Syluxship_Anim.bin)"}, std::optional<std::string>{R"(models\SyluxShip_Collision.bin)"}, false)},
    {R"(SyluxTurret)", ::MphRead::ModelMetadata(R"(SyluxTurret)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Teleporter)", ::MphRead::ModelMetadata(R"(Teleporter)", R"(models\Teleporter_mdl_Model.bin)", std::optional<std::string>{R"(models\Teleporter_mdl_Anim.bin)"}, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(pal_01)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_01_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_02)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_02_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_03)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_03_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_04)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_04_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_05)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_05_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_06)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_06_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_07)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_07_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_08)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_08_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_09)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_09_Model.bin)", std::map<int, std::vector<int>>{}, false)}, std::nullopt, false)},
    {R"(TeleporterSmall)", ::MphRead::ModelMetadata(R"(TeleporterSmall)", R"(models\TeleporterSmall_mdl_Model.bin)", std::optional<std::string>{R"(models\TeleporterSmall_mdl_Anim.bin)"}, std::nullopt, std::vector<RecolorMetadata>{RecolorMetadata(R"(pal_01)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_01_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_02)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_02_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_03)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_03_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_04)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_04_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_05)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_05_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_06)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_06_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_07)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_07_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_08)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_08_Model.bin)", std::map<int, std::vector<int>>{}, false), RecolorMetadata(R"(pal_09)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\TeleporterTextureShare_img_Model.bin)", R"(models\Teleporter_pal_09_Model.bin)", std::map<int, std::vector<int>>{}, false)}, std::nullopt, false)},
    {R"(TeleporterMP)", ::MphRead::ModelMetadata(R"(TeleporterMP)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Temroid_lod0)", ::MphRead::ModelMetadata(R"(Temroid_lod0)", R"(_lod0)", false, std::nullopt, false, false)},

    {R"(ThinDoorLock)", ::MphRead::ModelMetadata(R"(ThinDoorLock)", true, false, false, std::optional<std::string>{R"(models\AlimbicTextureShare_img_Model.bin)"}, MdlSuffix::All, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Trace_lod0)", ::MphRead::ModelMetadata(R"(Trace_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Trace)"}, std::nullopt, std::optional<std::string>{R"(models\NoxSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Trace_lod1)", ::MphRead::ModelMetadata(R"(Trace_lod1)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod1)"}, true, std::optional<std::string>{R"(_archives\Trace\Trace_Anim.bin)"}, true, MdlSuffix::None, std::nullopt, std::nullopt, std::optional<std::string>{R"(models\NoxSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(TraceAlt_lod0)", ::MphRead::ModelMetadata(R"(TraceAlt_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Trace)"}, std::optional<std::string>{R"(Trace)"}, std::nullopt, true, false, false)},
    {R"(TraceGun)", ::MphRead::ModelMetadata(R"(TraceGun)", std::vector<std::string>{R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)", R"(img_Team01)", R"(img_Team02)"}, std::nullopt, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(localTrace)"}, std::nullopt, std::nullopt, true, false, false)},
    {R"(trail)", ::MphRead::ModelMetadata(R"(trail)", false, false, false, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(common)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(unit1_land_plat1)", ::MphRead::ModelMetadata(R"(unit1_land_plat1)", R"(models\unit1_land_plat1_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit1_land_plat1_collision.bin)"}, false)},
    {R"(unit1_land_plat2)", ::MphRead::ModelMetadata(R"(unit1_land_plat2)", R"(models\unit1_land_plat2_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit1_land_plat2_collision.bin)"}, false)},
    {R"(unit1_land_plat3)", ::MphRead::ModelMetadata(R"(unit1_land_plat3)", R"(models\unit1_land_plat3_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit1_land_plat3_collision.bin)"}, false)},
    {R"(unit1_land_plat4)", ::MphRead::ModelMetadata(R"(unit1_land_plat4)", R"(models\unit1_land_plat4_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit1_land_plat4_collision.bin)"}, false)},
    {R"(unit1_land_plat5)", ::MphRead::ModelMetadata(R"(unit1_land_plat5)", R"(models\unit1_land_plat5_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit1_land_plat5_collision.bin)"}, false)},
    {R"(unit1_mover1)", ::MphRead::ModelMetadata(R"(unit1_mover1)", R"(models\unit1_mover1_model.bin)", std::optional<std::string>{R"(models\unit1_mover1_anim.bin)"}, std::optional<std::string>{R"(models\unit1_mover1_collision.bin)"}, false)},
    {R"(unit1_mover2)", ::MphRead::ModelMetadata(R"(unit1_mover2)", R"(models\unit1_mover2_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit1_mover2_collision.bin)"}, false)},
    {R"(unit2_c1_mover)", ::MphRead::ModelMetadata(R"(unit2_c1_mover)", R"(models\unit2_c1_mover_Model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit2_c1_mover_collision.bin)"}, false)},
    {R"(unit2_c4_plat)", ::MphRead::ModelMetadata(R"(unit2_c4_plat)", R"(models\unit2_c4_plat_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit2_c4_plat_collision.bin)"}, false)},
    {R"(unit2_land_elev)", ::MphRead::ModelMetadata(R"(unit2_land_elev)", R"(models\unit2_land_elev_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit2_land_elev_collision.bin)"}, false)},
    {R"(unit2_mover1)", ::MphRead::ModelMetadata(R"(unit2_mover1)", R"(models\unit2_mover1_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit2_mover1_collision.bin)"}, false)},
    {R"(unit3_brain)", ::MphRead::ModelMetadata(R"(unit3_brain)", R"(models\unit3_brain_Model.bin)", std::optional<std::string>{R"(models\Unit3_brain_Anim.bin)"}, std::optional<std::string>{R"(models\unit3_brain_Collision.bin)"}, false)},
    {R"(unit3_jar)", ::MphRead::ModelMetadata(R"(unit3_jar)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(unit3_jartop)", ::MphRead::ModelMetadata(R"(unit3_jartop)", R"(models\unit3_jartop_model.bin)", std::optional<std::string>{R"(models\unit3_jartop_anim.bin)"}, std::nullopt, false)},
    {R"(unit3_mover1)", ::MphRead::ModelMetadata(R"(unit3_mover1)", R"(models\unit3_mover1_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit3_mover1_collision.bin)"}, false)},
    {R"(unit3_mover2)", ::MphRead::ModelMetadata(R"(unit3_mover2)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(unit3_pipe1)", ::MphRead::ModelMetadata(R"(unit3_pipe1)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(unit3_pipe2)", ::MphRead::ModelMetadata(R"(unit3_pipe2)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(Unit3_platform1)", ::MphRead::ModelMetadata(R"(Unit3_platform1)", R"(models\Unit3_platform1_Model.bin)", std::optional<std::string>{R"(models\unit3_platform1_Anim.bin)"}, std::optional<std::string>{R"(models\unit3_platform1_Collision.bin)"}, false)},
    {R"(unit3_platform)", ::MphRead::ModelMetadata(R"(unit3_platform)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(unit3_platform2)", ::MphRead::ModelMetadata(R"(unit3_platform2)", R"(models\unit3_platform2_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit3_platform2_collision.bin)"}, false)},
    {R"(unit4_mover1)", ::MphRead::ModelMetadata(R"(unit4_mover1)", true, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(unit4_mover2)", ::MphRead::ModelMetadata(R"(unit4_mover2)", R"(models\unit4_mover2_model.bin)", std::optional<std::string>{R"(models\unit4_mover2_anim.bin)"}, std::optional<std::string>{R"(models\unit4_mover2_collision.bin)"}, false)},
    {R"(unit4_mover3)", ::MphRead::ModelMetadata(R"(unit4_mover3)", R"(models\unit4_mover3_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit4_mover3_collision.bin)"}, false)},
    {R"(unit4_mover4)", ::MphRead::ModelMetadata(R"(unit4_mover4)", R"(models\unit4_mover4_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit4_mover4_collision.bin)"}, false)},
    {R"(unit4_platform1)", ::MphRead::ModelMetadata(R"(unit4_platform1)", R"(models\unit4_platform1_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit4_platform1_collision.bin)"}, false)},
    {R"(unit4_tp1_artifact_wo)", ::MphRead::ModelMetadata(R"(unit4_tp1_artifact_wo)", R"(models\unit4_tp1_artifact_wo_model.bin)", std::nullopt, std::optional<std::string>{R"(models\unit4_tp1_artifact_wo_collision.bin)"}, false)},
    {R"(unit4_tp2_artifact_wo)", ::MphRead::ModelMetadata(R"(unit4_tp2_artifact_wo)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(WallSwitch)", ::MphRead::ModelMetadata(R"(WallSwitch)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(warwasp_lod0)", ::MphRead::ModelMetadata(R"(warwasp_lod0)", R"(models\warwasp_lod0_Model.bin)", std::optional<std::string>{R"(models\warWasp_Anim.bin)"}, std::nullopt, false)},
    {R"(Weavel_lod0)", ::MphRead::ModelMetadata(R"(Weavel_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Weavel)"}, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(Weavel_lod1)", ::MphRead::ModelMetadata(R"(Weavel_lod1)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod1)"}, true, std::optional<std::string>{R"(_archives\Weavel\Weavel_Anim.bin)"}, true, MdlSuffix::None, std::nullopt, std::nullopt, std::optional<std::string>{R"(models\SamusSharedAnim_Anim.bin)"}, true, false, false)},
    {R"(WeavelAlt_lod0)", ::MphRead::ModelMetadata(R"(WeavelAlt_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Weavel)"}, std::optional<std::string>{R"(Weavel)"}, std::nullopt, true, false, false)},
    {R"(WeavelAlt_Turret_lod0)", ::MphRead::ModelMetadata(R"(WeavelAlt_Turret_lod0)", std::vector<std::string>{R"(pal_01)", R"(pal_02)", R"(pal_03)", R"(pal_04)", R"(pal_Team01)", R"(pal_Team02)"}, std::optional<std::string>{R"(_lod0)"}, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(Weavel)"}, std::optional<std::string>{R"(Weavel)"}, std::nullopt, true, false, false)},
    {R"(WeavelGun)", ::MphRead::ModelMetadata(R"(WeavelGun)", std::vector<std::string>{R"(img_01)", R"(img_02)", R"(img_03)", R"(img_04)", R"(img_Team01)", R"(img_Team02)"}, std::nullopt, true, std::nullopt, true, MdlSuffix::None, std::optional<std::string>{R"(localWeavel)"}, std::nullopt, std::nullopt, true, false, false)},
    {R"(zoomer)", ::MphRead::ModelMetadata(R"(zoomer)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(deathParticle)", ::MphRead::ModelMetadata(R"(deathParticle)", false, false, true, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(effectsBase)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(geo1)", ::MphRead::ModelMetadata(R"(geo1)", false, false, true, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(effectsBase)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(particles)", ::MphRead::ModelMetadata(R"(particles)", false, false, true, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(effectsBase)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(particles2)", ::MphRead::ModelMetadata(R"(particles2)", false, false, true, std::nullopt, MdlSuffix::None, std::optional<std::string>{R"(effectsBase)"}, std::nullopt, false, std::nullopt, std::nullopt)},
    {R"(TearParticle)", ::MphRead::ModelMetadata(R"(TearParticle)", false, false, true, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt)},
};

const std::unordered_map<std::string, ::MphRead::ModelMetadata> FirstHuntModels{
    {R"(ballDeath)", ::MphRead::ModelMetadata(R"(ballDeath)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(balljump)", ::MphRead::ModelMetadata(R"(balljump)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(balljump_ray)", ::MphRead::ModelMetadata(R"(balljump_ray)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(bomb)", ::MphRead::ModelMetadata(R"(bomb)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(bombLite)", ::MphRead::ModelMetadata(R"(bombLite)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(bombStart)", ::MphRead::ModelMetadata(R"(bombStart)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(bombStartLite)", ::MphRead::ModelMetadata(R"(bombStartLite)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(bombStartLiter)", ::MphRead::ModelMetadata(R"(bombStartLiter)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(dashEffect)", ::MphRead::ModelMetadata(R"(dashEffect)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(door)", ::MphRead::ModelMetadata(R"(door)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(door2)", ::MphRead::ModelMetadata(R"(door2)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(door2_holo)", ::MphRead::ModelMetadata(R"(door2_holo)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(effWaspDeath)", ::MphRead::ModelMetadata(R"(effWaspDeath)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(furlEffect)", ::MphRead::ModelMetadata(R"(furlEffect)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(fuzzball)", ::MphRead::ModelMetadata(R"(fuzzball)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(genericMover)", ::MphRead::ModelMetadata(R"(genericMover)", R"(models\genericMover_Model.bin)", std::optional<std::string>{R"(models\genericmover_Anim.bin)"}, std::optional<std::string>{R"(models\genericMover_Collision.bin)"}, false)},
    {R"(gun_idle)", ::MphRead::ModelMetadata(R"(gun_idle)", R"(_idle)", true, std::nullopt, false, true)},
    {R"(gunEffElectroCharge)", ::MphRead::ModelMetadata(R"(gunEffElectroCharge)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(gunEffMissileCharge)", ::MphRead::ModelMetadata(R"(gunEffMissileCharge)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(gunLobFlash)", ::MphRead::ModelMetadata(R"(gunLobFlash)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(gunMuzzleFlash)", ::MphRead::ModelMetadata(R"(gunMuzzleFlash)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(gunSmoke)", ::MphRead::ModelMetadata(R"(gunSmoke)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(jumpad_ray)", ::MphRead::ModelMetadata(R"(jumpad_ray)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(jumppad_base)", ::MphRead::ModelMetadata(R"(jumppad_base)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(jumppad_ray)", ::MphRead::ModelMetadata(R"(jumppad_ray)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(lightningCol)", ::MphRead::ModelMetadata(R"(lightningCol)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(lightningColLite)", ::MphRead::ModelMetadata(R"(lightningColLite)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(lightningColLiter)", ::MphRead::ModelMetadata(R"(lightningColLiter)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(lightningColLiterER)", ::MphRead::ModelMetadata(R"(lightningColLiterER)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(lightningLob)", ::MphRead::ModelMetadata(R"(lightningLob)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(metroid)", ::MphRead::ModelMetadata(R"(metroid)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(Metroid_Lo)", ::MphRead::ModelMetadata(R"(Metroid_Lo)", R"(models\Metroid_Lo_Model.bin)", std::optional<std::string>{R"(models\metroid_Anim.bin)"}, std::nullopt, false)},
    {R"(missileCollide)", ::MphRead::ModelMetadata(R"(missileCollide)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(missileColLite)", ::MphRead::ModelMetadata(R"(missileColLite)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(missileColLiter)", ::MphRead::ModelMetadata(R"(missileColLiter)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(missileColLiterER)", ::MphRead::ModelMetadata(R"(missileColLiterER)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(Mochtroid)", ::MphRead::ModelMetadata(R"(Mochtroid)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(Mochtroid_Lo)", ::MphRead::ModelMetadata(R"(Mochtroid_Lo)", R"(_Lo)", true, std::nullopt, false, true)},
    {R"(morphBall)", ::MphRead::ModelMetadata(R"(morphBall)", std::vector<std::string>{R"(*morphBall)", R"(Green)", R"(White)", R"(Blue)"}, std::nullopt, false, std::nullopt, false, MdlSuffix::None, std::nullopt, std::nullopt, std::nullopt, false, true, false)},
    {R"(morphBall_Blue)", ::MphRead::ModelMetadata(R"(morphBall_Blue)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(morphBall_Green)", ::MphRead::ModelMetadata(R"(morphBall_Green)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(morphBall_White)", ::MphRead::ModelMetadata(R"(morphBall_White)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pb_charged)", ::MphRead::ModelMetadata(R"(pb_charged)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pb_normal)", ::MphRead::ModelMetadata(R"(pb_normal)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pick_ammo_A)", ::MphRead::ModelMetadata(R"(pick_ammo_A)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pick_ammo_B)", ::MphRead::ModelMetadata(R"(pick_ammo_B)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pick_dblDamage)", ::MphRead::ModelMetadata(R"(pick_dblDamage)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pick_health_A)", ::MphRead::ModelMetadata(R"(pick_health_A)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pick_health_B)", ::MphRead::ModelMetadata(R"(pick_health_B)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pick_morphball)", ::MphRead::ModelMetadata(R"(pick_morphball)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pick_wpn_electro)", ::MphRead::ModelMetadata(R"(pick_wpn_electro)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(pick_wpn_missile)", ::MphRead::ModelMetadata(R"(pick_wpn_missile)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(platform)", ::MphRead::ModelMetadata(R"(platform)", false, true, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(samus_hi_yellow)", ::MphRead::ModelMetadata(R"(samus_hi_yellow)", std::vector<std::string>{R"(*samus_hi_yellow)", R"(hi_green)", R"(hi_white)", R"(hi_blue)"}, std::optional<std::string>{R"(_hi_yellow)"}, false, std::optional<std::string>{R"(models\samus_Anim.bin)"}, false, MdlSuffix::None, std::nullopt, std::nullopt, std::nullopt, false, true, false)},
    {R"(samus_low_yellow)", ::MphRead::ModelMetadata(R"(samus_low_yellow)", std::vector<std::string>{R"(*samus_low_yellow)", R"(hi_green)", R"(hi_white)", R"(hi_blue)"}, std::optional<std::string>{R"(_low_yellow)"}, false, std::optional<std::string>{R"(models\samus_Anim.bin)"}, false, MdlSuffix::None, std::nullopt, std::nullopt, std::nullopt, false, true, false)},
    {R"(samus_hi_blue)", ::MphRead::ModelMetadata(R"(samus_hi_blue)", R"(_hi_blue)", true, std::nullopt, false, true)},
    {R"(samus_hi_green)", ::MphRead::ModelMetadata(R"(samus_hi_green)", R"(_hi_green)", true, std::nullopt, false, true)},
    {R"(samus_hi_white)", ::MphRead::ModelMetadata(R"(samus_hi_white)", R"(_hi_white)", true, std::nullopt, false, true)},
    {R"(spawnEffect)", ::MphRead::ModelMetadata(R"(spawnEffect)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(trail)", ::MphRead::ModelMetadata(R"(trail)", false, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(warWasp)", ::MphRead::ModelMetadata(R"(warWasp)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
    {R"(zoomer)", ::MphRead::ModelMetadata(R"(zoomer)", true, false, false, std::nullopt, MdlSuffix::None, std::nullopt, std::nullopt, true, std::nullopt, std::nullopt)},
};


const ::MphRead::ModelMetadata* GetModelByName(std::string_view name, MetaDir dir) noexcept
{
    if (name == "doubleDamage_img") return &DoubleDamageImg;
    if (name == "ad2_dm2") return Ad2Dm2.get();
    if (dir == MetaDir::Logo) return FindFrontendModel(LogoModels,name);
    if (dir == MetaDir::Multiplayer) return FindFrontendModel(MultiplayerModels,name);
    if (dir == MetaDir::TouchToStart) return FindFrontendModel(TouchToStartModels,name);
    if (dir == MetaDir::Hud) return FindFrontendModel(HudModels,name);
    if (dir != MetaDir::Models) return FindFrontendModel(FrontendModels,name);
    return FindModel(ModelMetadata,name);
}

const ::MphRead::ModelMetadata* GetFirstHuntModelByName(std::string_view name) noexcept
{
    return FindModel(FirstHuntModels,name);
}

const ::MphRead::ModelMetadata* GetEntityByPath(std::string_view path) noexcept
{
    for (const auto& value : ModelMetadata)
    {
        if (value.second.ModelPath == path) return &value.second;
    }
    return nullptr;
}

const ObjectMetadata& GetObjectById(int id)
{
    if (id < 0 || id > static_cast<int>(Objects.size())) throw std::invalid_argument("id");
    return *Objects.at(static_cast<std::size_t>(id));
}

const ObjectMetadata& GetObjectById(std::uint32_t id)
{
    return GetObjectById(std::bit_cast<std::int32_t>(id));
}

const PlatformMetadata* GetPlatformById(int id)
{
    if (id < 0 || id > static_cast<int>(Platforms.size())) throw std::invalid_argument("id");
    if (id == 1) id = 0;
    const auto& value=Platforms.at(static_cast<std::size_t>(id));
    return value.get();
}

OpenTK::Mathematics::Vector3 GetEventColor(Message eventId) noexcept
{
    using V=OpenTK::Mathematics::Vector3;
    switch (eventId)
    {
    case Message::None:return V(0,0,0); case Message::SetActive:return V(.615F,0,.909F);
    case Message::Damage:return V(1,0,0); case Message::Gravity:return V(.141F,1,1);
    case Message::Activate:return V(0,1,0); case Message::Death:return V(0,0,.858F);
    case Message::ShipHatch:return V(1,1,.6F); case Message::Unused25:return V(1,.792F,.6F);
    case Message::PreventFormSwitch:return V(.964F,1,.058F); case Message::PlatformWakeup:return V(.5F,.5F,.5F);
    case Message::DripMoatPlatform:return V(.596F,.658F,.964F); case Message::UnlockOubliette:return V(.964F,.596F,.596F);
    case Message::Checkpoint:return V(.972F,.086F,.831F); case Message::EscapeUpdate1:return V(.619F,.980F,.678F);
    case Message::Trigger:return V(.549F,.18F,.18F); case Message::UpdateMusic:return V(.094F,.506F,.51F);
    case Message::Unlock:return V(.094F,.094F,.557F); case Message::Lock:return V(.647F,.663F,.169F);
    case Message::ShowPrompt:return V(.118F,.588F,.118F); case Message::ShowWarning:return V(.784F,.325F,1);
    case Message::ShowOverlay:return V(1,.612F,.153F); case Message::UnlockConnectors:return V(.906F,.702F,1);
    case Message::LockConnectors:return V(.784F,.984F,.988F); case Message::Gorea2Trigger:return V(1,.325F,.294F);
    case Message::SetTriggerState:return V(.988F,.463F,.824F); case Message::PlatformSleep:return V(.165F,.894F,.678F);
    case Message::SetPlatformIndex:return V(.549F,.345F,.102F); case Message::PlaySfxScript:return V(.471F,.769F,.525F);
    case Message::LoadOubliette:return V(1,.765F,.49F); case Message::EscapeUpdate2:return V(.165F,.816F,.894F);
    default:return V(1,1,1);
    }
}

OpenTK::Mathematics::Vector3 GetEventColor(FhMessage eventId) noexcept
{
    using V=OpenTK::Mathematics::Vector3;
    switch(eventId)
    {
    case FhMessage::None:return V(0,0,0); case FhMessage::Activate:return V(0,1,0);
    case FhMessage::Unlock:return V(.094F,.094F,.557F); case FhMessage::SetActive:return V(.615F,0,.909F);
    case FhMessage::Death:return V(0,0,.858F); default:return V(1,1,1);
    }
}

std::pair<const ::MphRead::RoomMetadata*,int> GetRoomByName(std::string_view name)
{
    auto it=RoomMetadata.find(std::string(name));
    if (it==RoomMetadata.end()) return {nullptr,-1};
    int id=-1;
    for(std::size_t i=0;i<_roomIds.size();++i)
    {
        if(_roomIds[i]==it->second->Name){id=static_cast<int>(i);break;}
    }
    return {it->second.get(),id};
}

const ::MphRead::RoomMetadata* GetRoomById(int id, bool noThrow)
{
    if(id<0 || id>static_cast<int>(_roomIds.size()))
    {
        if(noThrow) return nullptr;
        throw std::invalid_argument("id");
    }
    const std::string& key=_roomIds.at(static_cast<std::size_t>(id));
    auto it=RoomMetadata.find(key);
    return it==RoomMetadata.end()?nullptr:it->second.get();
}

int GetAreaInfo(int roomId) noexcept
{
    int areaId=8;
    if(roomId>=27 && roomId<36) areaId=0;
    else if(roomId>=36 && roomId<45) areaId=1;
    else if(roomId>=45 && roomId<56) areaId=2;
    else if(roomId>=56 && roomId<65) areaId=3;
    else if(roomId>=65 && roomId<72) areaId=4;
    else if(roomId>=72 && roomId<77) areaId=5;
    else if(roomId>=77 && roomId<83) areaId=6;
    else if(roomId>=83 && roomId<89) areaId=7;
    return areaId;
}
}

namespace MphRead::Metadata
{
    std::uint32_t TimeLimit(
        std::uint32_t minutes, std::uint32_t seconds, std::uint32_t frames)
    {
        return minutes * 1800 + seconds * 30 + frames;
    }
}
