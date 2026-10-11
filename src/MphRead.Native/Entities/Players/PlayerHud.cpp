#include "../../Mods/Input/GamepadInput.hpp"
#include "../../Mods/Input/StylusZone.hpp"
#include "../../Mods/Input/WeaponSelectionDirection.hpp"
#include "../../Mods/Input/WeaponWheel.hpp"
#include "../../Mods/Multiplayer/TeamVisuals.hpp"
#include "../../Mods/Network/NetHudHealth.hpp"
#include "../../Mods/Render/Radar.hpp"
#include "../CamSeq/CameraSequence.hpp"
#include "../NodeDefenseEntity.hpp"
#include "../ItemInstanceEntity.hpp"
#include "PlayerHud.hpp"

#include "../../Features.hpp"
#include "../../GameState.hpp"
#include "../../Read.hpp"
#include "../../Formats/Formats.hpp"
#include "../../Scene.hpp"
#include "../../Strings.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Mods/Network/NetHitPrediction.hpp"
#include "../../Mods/Render/SmoothHudIcon.hpp"
#include "../../Mods/RenderOptions.hpp"
#include "../../Mods/SpectatorMode.hpp"
#include "../../Mods/ThumbnailMode.hpp"
#include "../CamSeq/CameraSequence.hpp"
#include "../FlagBaseEntity.hpp"
#include "HalfturretEntity.hpp"
#include "../NodeDefenseEntity.hpp"
#include "../OctolithFlagEntity.hpp"
#include "../EnemyInstanceEntity.hpp"
#include "../Enemies/26_GoreaArm.hpp"
#include "../Enemies/29_GoreaSealSphere1.hpp"
#include "../Enemies/32_GoreaSealSphere2.hpp"
#include "../Enemies/41_Slench.hpp"
#include "../Enemies/42_SlenchShield.hpp"
#include "PlayerEntity.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::ManagedCast;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::RoundToEven;
using ::MphRead::TestFlag;

namespace
{
    [[nodiscard]] std::u16string ToManagedChars(std::string_view text)
    {
        std::u16string result;
        result.reserve(text.size());
        std::size_t i = 0;
        while (i < text.size())
        {
            const auto first = static_cast<unsigned char>(text[i]);
            if (first < 0x80)
            {
                result.push_back(static_cast<char16_t>(first));
                ++i;
                continue;
            }
            std::size_t length = 1;
            char32_t value = 0xFFFD;
            if ((first & 0xE0) == 0xC0 && i + 1 < text.size())
            {
                length = 2;
                value = first & 0x1F;
            }
            else if ((first & 0xF0) == 0xE0 && i + 2 < text.size())
            {
                length = 3;
                value = first & 0x0F;
            }
            else if ((first & 0xF8) == 0xF0 && i + 3 < text.size())
            {
                length = 4;
                value = first & 0x07;
            }
            if (length > 1)
            {
                bool valid = true;
                for (std::size_t j = 1; j < length; ++j)
                {
                    const auto next = static_cast<unsigned char>(text[i + j]);
                    if ((next & 0xC0) != 0x80)
                    {
                        valid = false;
                        break;
                    }
                    value = (value << 6) | (next & 0x3F);
                }
                if (!valid)
                {
                    length = 1;
                    value = 0xFFFD;
                }
            }
            if (value <= 0xFFFF)
            {
                result.push_back(static_cast<char16_t>(value));
            }
            else
            {
                value -= 0x10000;
                result.push_back(static_cast<char16_t>(0xD800 + (value >> 10)));
                result.push_back(static_cast<char16_t>(0xDC00 + (value & 0x3FF)));
            }
            i += length;
        }
        return result;
    }

    [[nodiscard]] std::string FromManagedChars(std::span<const char16_t> text)
    {
        std::string result;
        result.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            char32_t value = text[i];
            if (value >= 0xD800 && value <= 0xDBFF && i + 1 < text.size())
            {
                const char32_t low = text[i + 1];
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
            if (value <= 0x7F)
            {
                result.push_back(static_cast<char>(value));
            }
            else if (value <= 0x7FF)
            {
                result.push_back(static_cast<char>(0xC0 | (value >> 6)));
                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
            else if (value <= 0xFFFF)
            {
                result.push_back(static_cast<char>(0xE0 | (value >> 12)));
                result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
            else
            {
                result.push_back(static_cast<char>(0xF0 | (value >> 18)));
                result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
        }
        return result;
    }

    [[nodiscard]] std::string FormatInt(std::int32_t value)
    {
        return std::to_string(value);
    }

    [[nodiscard]] std::string FormatTwo(std::int32_t value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%02d", value);
        return buffer;
    }

    struct ManagedTime final
    {
        std::int32_t Hours = 0;
        std::int32_t Minutes = 0;
        std::int32_t Seconds = 0;
        std::int32_t Milliseconds = 0;
        double TotalSeconds = 0;
    };

    [[nodiscard]] ManagedTime FromSeconds(double seconds)
    {
        ManagedTime time{};
        time.TotalSeconds = seconds;
        const double whole = std::trunc(seconds);
        const std::int64_t totalMilliseconds = static_cast<std::int64_t>(std::trunc(seconds * 1000.0));
        const std::int64_t totalSeconds = static_cast<std::int64_t>(whole);
        time.Hours = static_cast<std::int32_t>((totalSeconds / 3600) % 24);
        time.Minutes = static_cast<std::int32_t>((totalSeconds / 60) % 60);
        time.Seconds = static_cast<std::int32_t>(totalSeconds % 60);
        time.Milliseconds = static_cast<std::int32_t>(totalMilliseconds % 1000);
        return time;
    }
}

namespace MphRead::Entities
{
    bool PlayerEntity::ShowScoreboard() const
    {
        return _showScoreboard
            || (Mods::SpectatorMode::ShowScoreboard() && IsMainPlayer())
            || (_modForceScoreboard && IsMainPlayer());
    }

    void PlayerEntity::SetUpHud()
    {
        auto [iceBinding, icePalette] = Hud::HudInfo::CharMapToTexture(
            Hud::HudElements::IceLayer, 16, 0, 32, 32, RequireReference(_scene));
        static_cast<void>(icePalette);
        _iceLayerBindingId = iceBinding;
        auto [helmetBinding, helmetPalette] = Hud::HudInfo::CharMapToTexture(
            RequireReference(_hudObjects).Helmet, RequireReference(_scene));
        static_cast<void>(helmetPalette);
        _helmetBindingId = helmetBinding;
        auto [helmetDropBinding, helmetDropPalette] = Hud::HudInfo::CharMapToTexture(
            RequireReference(_hudObjects).HelmetDrop, RequireReference(_scene));
        static_cast<void>(helmetDropPalette);
        _helmetDropBindingId = helmetDropBinding;
        auto [visorBinding, visorPal] = Hud::HudInfo::CharMapToTexture(
            RequireReference(_hudObjects).Visor, 0, 0, 0, 32, RequireReference(_scene));
        _visorBindingId = visorBinding;
        Hud::ReadOnlyList<std::uint16_t> sharedVisorPal = visorPal;
        if (_hunter == Hunter::Samus)
        {
            sharedVisorPal.reset();
        }
        auto [scanBinding, scanPal] = Hud::HudInfo::CharMapToTexture(
            RequireReference(_hudObjects).ScanVisor, 0, 96, 0, 32, RequireReference(_scene), sharedVisorPal);
        static_cast<void>(scanPal);
        _scanBindingId = scanBinding;
        auto [pauseBinding, pausePal] = Hud::HudInfo::CharMapToTexture(
            RequireReference(_hudObjects).ScanVisor, 0, 64, 0, 32, RequireReference(_scene), sharedVisorPal);
        static_cast<void>(pausePal);
        _pauseBindingId = pauseBinding;

        _filterModel = Read::GetModelInstance("filter");
        RequireReference(_scene).LoadModel(RequireReference(_filterModel).Model());
        _damageIndicator = Read::GetModelInstance("damage", false, MetaDir::Hud);
        RequireReference(_scene).LoadModel(RequireReference(_damageIndicator).Model());
        RequireReference(_damageIndicator).Active = false;
        _damageIndicatorTimers.fill(0);
        static constexpr std::array<std::string_view, 8> damageNodes{
            "north", "ne", "east", "se", "south", "sw", "west", "nw"};
        for (std::int32_t i = 0; i < 8; ++i)
        {
            _damageIndicatorNodes[static_cast<std::size_t>(i)]
                = RequireReference(RequireReference(_damageIndicator).Model()).GetNodeByName(std::string(damageNodes[static_cast<std::size_t>(i)]));
            RequireReference(_damageIndicatorNodes[static_cast<std::size_t>(i)]);
        }

        _playerLocator = Read::GetModelInstance("hud_icon_player", false, MetaDir::Hud);
        RequireReference(_scene).LoadModel(RequireReference(_playerLocator).Model());
        _arrowLocator = Read::GetModelInstance("hud_icon_arrow", false, MetaDir::Hud);
        RequireReference(_scene).LoadModel(RequireReference(_arrowLocator).Model());
        _nodeLocator = Read::GetModelInstance("hud_icon_nodes", false, MetaDir::Hud);
        RequireReference(_scene).LoadModel(RequireReference(_nodeLocator).Model());
        _octolithLocator = Read::GetModelInstance("hud_icon_octolith", false, MetaDir::Hud);
        RequireReference(_scene).LoadModel(RequireReference(_octolithLocator).Model());

        _targetCircleObj = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).Reticle);
        _sniperCircleObj = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).SniperReticle);
        assert(RequireReference(_sniperCircleObj).Width >= RequireReference(_targetCircleObj).Width);
        assert(RequireReference(_sniperCircleObj).Height >= RequireReference(_targetCircleObj).Height);
        _targetCircleInst = std::make_shared<Hud::HudObjectInstance>(
            RequireReference(_targetCircleObj).Width, RequireReference(_targetCircleObj).Height,
            RequireReference(_sniperCircleObj).Width, RequireReference(_sniperCircleObj).Height);
        RequireReference(_targetCircleInst).SetCharacterData(RequireReference(_targetCircleObj).CharacterData,
            RequireReference(_scene));
        RequireReference(_targetCircleInst).SetPaletteData(RequireReference(_targetCircleObj).PaletteData,
            RequireReference(_scene));
        RequireReference(_targetCircleInst).Center = true;
        RequireReference(_targetCircleInst).PositionX = 0.5F;
        RequireReference(_targetCircleInst).PositionY = 0.5F;

        auto cloak = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).Cloaking);
        _cloakInst = std::make_shared<Hud::HudObjectInstance>(RequireReference(cloak).Width, RequireReference(cloak).Height);
        RequireReference(_cloakInst).SetCharacterData(RequireReference(cloak).CharacterData, RequireReference(_scene));
        RequireReference(_cloakInst).SetPaletteData(RequireReference(cloak).PaletteData, RequireReference(_scene));
        RequireReference(_cloakInst).Enabled = true;

        auto doubleDamage = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).DoubleDamage);
        _doubleDamageInst = std::make_shared<Hud::HudObjectInstance>(RequireReference(doubleDamage).Width, RequireReference(doubleDamage).Height);
        RequireReference(_doubleDamageInst).SetCharacterData(RequireReference(doubleDamage).CharacterData, RequireReference(_scene));
        RequireReference(_doubleDamageInst).SetPaletteData(RequireReference(doubleDamage).PaletteData, RequireReference(_scene));
        RequireReference(_doubleDamageInst).Enabled = true;

        auto weaponSelectObj = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).WeaponSelect);
        auto selectBoxObj = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).SelectBox);
        const std::array<OpenTK::Mathematics::Vector2, 6> positions{
            OpenTK::Mathematics::Vector2(201.0F / 256.0F, 156.0F / 192.0F),
            OpenTK::Mathematics::Vector2(161.0F / 256.0F, 152.0F / 192.0F),
            OpenTK::Mathematics::Vector2(122.0F / 256.0F, 142.0F / 192.0F),
            OpenTK::Mathematics::Vector2(90.0F / 256.0F, 109.0F / 192.0F),
            OpenTK::Mathematics::Vector2(81.0F / 256.0F, 70.0F / 192.0F),
            OpenTK::Mathematics::Vector2(77.0F / 256.0F, 32.0F / 192.0F)};
        auto iconInst = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).SelectIcon);
        for (std::int32_t i = 0; i < 6; ++i)
        {
            auto weaponInst = std::make_shared<Hud::HudObjectInstance>(RequireReference(weaponSelectObj).Width,
                RequireReference(weaponSelectObj).Height);
            const std::int32_t frame = i == 0 ? 1 : i + 2;
            weaponInst->SetCharacterData(RequireReference(weaponSelectObj).CharacterData, frame, RequireReference(_scene));
            weaponInst->SetPaletteData(RequireReference(weaponSelectObj).PaletteData, RequireReference(_scene));
            weaponInst->Alpha = 0.722F;
            auto boxInst = std::make_shared<Hud::HudObjectInstance>(RequireReference(selectBoxObj).Width,
                RequireReference(selectBoxObj).Height);
            boxInst->SetCharacterData(RequireReference(selectBoxObj).CharacterData, RequireReference(_scene));
            boxInst->SetPaletteData(RequireReference(iconInst).PaletteData, RequireReference(_scene));
            boxInst->Enabled = true;
            const auto& position = positions[static_cast<std::size_t>(i)];
            weaponInst->PositionX = position.X;
            weaponInst->PositionY = position.Y;
            boxInst->PositionX = position.X;
            boxInst->PositionY = position.Y;
            // Kept, because with a pen zone marked out the wheel is drawn
            // inside it and has to be re-derived every frame from these.
            // See ModPlaceWeaponSelect.
            _weaponSelectHome[static_cast<std::size_t>(i)] = OpenTK::Mathematics::Vector2(position.X, position.Y);
            _weaponSelectInsts[static_cast<std::size_t>(i)] = weaponInst;
            _selectBoxInsts[static_cast<std::size_t>(i)] = boxInst;
        }

        auto healthbarMain = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).HealthBarA);
        auto healthbarSub = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).HealthBarB);
        _healthbarMainMeter = ManagedAt(*Hud::HudElements::MainHealthbars, static_cast<std::int32_t>(_hunter));
        _healthbarSubMeter = ManagedAt(*Hud::HudElements::SubHealthbars, static_cast<std::int32_t>(_hunter));
        RequireReference(_healthbarMainMeter).BarInst = std::make_shared<Hud::HudObjectInstance>(
            RequireReference(healthbarMain).Width, RequireReference(healthbarMain).Height);
        RequireReference(RequireReference(_healthbarMainMeter).BarInst).SetCharacterData(
            RequireReference(healthbarMain).CharacterData, RequireReference(_scene));
        RequireReference(RequireReference(_healthbarMainMeter).BarInst).SetPaletteData(
            RequireReference(healthbarMain).PaletteData, RequireReference(_scene));
        RequireReference(RequireReference(_healthbarMainMeter).BarInst).Enabled = true;
        RequireReference(_healthbarSubMeter).BarInst = std::make_shared<Hud::HudObjectInstance>(
            RequireReference(healthbarSub).Width, RequireReference(healthbarSub).Height);
        RequireReference(RequireReference(_healthbarSubMeter).BarInst).SetCharacterData(
            RequireReference(healthbarSub).CharacterData, RequireReference(_scene));
        RequireReference(RequireReference(_healthbarSubMeter).BarInst).SetPaletteData(
            RequireReference(healthbarSub).PaletteData, RequireReference(_scene));
        RequireReference(RequireReference(_healthbarSubMeter).BarInst).Enabled = true;

        auto samusSubBar = healthbarSub;
        if (_hunter != Hunter::Samus)
        {
            samusSubBar = Hud::HudInfo::GetHudObject(ManagedAt(*Hud::HudElements::HunterObjects,
                static_cast<std::int32_t>(Hunter::Samus))->HealthBarB);
        }
        if (GameState::Multiplayer())
        {
            auto damageBar = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).DamageBar);
            _enemyHealthMeter = std::make_shared<Hud::HudMeter>();
            _enemyHealthMeter->Horizontal = true;
            _enemyHealthMeter->BarInst = std::make_shared<Hud::HudObjectInstance>(
                RequireReference(damageBar).Width, RequireReference(damageBar).Height);
            _enemyHealthMeter->BarInst->SetCharacterData(RequireReference(damageBar).CharacterData, RequireReference(_scene));
            _enemyHealthMeter->BarInst->SetPaletteData(RequireReference(damageBar).PaletteData, RequireReference(_scene));
            _enemyHealthMeter->BarInst->Enabled = true;
        }
        else
        {
            _enemyHealthMeter = Hud::HudElements::EnemyHealthbar;
            _enemyHealthMeter->BarInst = std::make_shared<Hud::HudObjectInstance>(
                RequireReference(samusSubBar).Width, RequireReference(samusSubBar).Height);
            _enemyHealthMeter->BarInst->SetCharacterData(RequireReference(samusSubBar).CharacterData, RequireReference(_scene));
            _enemyHealthMeter->BarInst->SetPaletteData(RequireReference(healthbarSub).PaletteData, RequireReference(_scene));
            _enemyHealthMeter->BarInst->Enabled = true;

            auto teleporter = Hud::HudInfo::GetHudObject(Hud::HudElements::MapPortal);
            _mapTeleporterInst = std::make_shared<Hud::HudObjectInstance>(teleporter->Width, teleporter->Height);
            _mapTeleporterInst->SetCharacterData(teleporter->CharacterData, RequireReference(_scene));
            _mapTeleporterInst->SetPaletteData(teleporter->PaletteData, RequireReference(_scene));
            _mapTeleporterInst->Enabled = true;
            auto mapOctolith = Hud::HudInfo::GetHudObject(Hud::HudElements::MapOctolith);
            for (std::int32_t i = 0; i < 8; ++i)
            {
                auto dot = Hud::HudInfo::GetHudObject(ManagedAt(*Hud::HudElements::MapDots, i));
                auto dotInst = std::make_shared<Hud::HudObjectInstance>(dot->Width, dot->Height);
                dotInst->SetCharacterData(dot->CharacterData, RequireReference(_scene));
                dotInst->SetPaletteData(dot->PaletteData, RequireReference(_scene));
                dotInst->Enabled = true;
                _mapArtifactDotInsts[static_cast<std::size_t>(i)] = dotInst;
                auto mapInst = std::make_shared<Hud::HudObjectInstance>(mapOctolith->Width, mapOctolith->Height);
                mapInst->SetCharacterData(mapOctolith->CharacterData, RequireReference(_scene));
                mapInst->SetPaletteData(mapOctolith->PaletteData, RequireReference(_scene));
                mapInst->SetAnimationFrames(mapOctolith->AnimParams);
                mapInst->Enabled = true;
                _mapOctolithInsts[static_cast<std::size_t>(i)] = mapInst;
            }
            auto lost = Hud::HudInfo::GetHudObject(Hud::HudElements::MapLostOctolith);
            _mapLostOctolithInst = std::make_shared<Hud::HudObjectInstance>(lost->Width, lost->Height);
            _mapLostOctolithInst->SetCharacterData(lost->CharacterData, RequireReference(_scene));
            _mapLostOctolithInst->SetPaletteData(lost->PaletteData, RequireReference(_scene));
            _mapLostOctolithInst->SetAnimationFrames(lost->AnimParams);
            _mapLostOctolithInst->Enabled = true;
            auto legendDoor = Hud::HudInfo::GetHudObject(Hud::HudElements::MapLegendDoors);
            _mapLegendDoorInst = std::make_shared<Hud::HudObjectInstance>(legendDoor->Width, legendDoor->Height);
            _mapLegendDoorInst->SetCharacterData(legendDoor->CharacterData, RequireReference(_scene));
            _mapLegendDoorInst->SetPaletteData(legendDoor->PaletteData, RequireReference(_scene));
            _mapLegendDoorInst->Enabled = true;
            auto legendOther = Hud::HudInfo::GetHudObject(Hud::HudElements::MapLegendOther);
            _mapLegendOtherInst = std::make_shared<Hud::HudObjectInstance>(legendOther->Width, legendOther->Height);
            _mapLegendOtherInst->SetCharacterData(legendOther->CharacterData, RequireReference(_scene));
            _mapLegendOtherInst->SetPaletteData(legendOther->PaletteData, RequireReference(_scene));
            _mapLegendOtherInst->Enabled = true;

            _mapLegendInfo = {
                MapLegendInfo(false, 4, 0, 0, _mapLegendDoorInst, 4),
                MapLegendInfo(false, 2, 0, 0, _mapLegendDoorInst, 5),
                MapLegendInfo(false, 8, 0, 0, _mapLegendDoorInst, 1),
                MapLegendInfo(false, 5, 0, 0, _mapLegendDoorInst, 0),
                MapLegendInfo(false, 6, 0, 0, _mapLegendDoorInst, 3),
                MapLegendInfo(false, 7, 0, 0, _mapLegendDoorInst, 2),
                MapLegendInfo(true, 1, 0, 0, _mapLegendDoorInst, 7),
                MapLegendInfo(true, 3, 0, 0, _mapLegendDoorInst, 8),
                MapLegendInfo(true, 2, -3, -4, _mapLegendOtherInst, 0),
                MapLegendInfo(true, 4, -3, -4, _mapLegendOtherInst, 1)};

            auto quit = Hud::HudInfo::GetHudObject(Hud::HudElements::MapQuit);
            _mapQuitInst = std::make_shared<Hud::HudObjectInstance>(quit->Width, quit->Height);
            _mapQuitInst->SetCharacterData(quit->CharacterData, RequireReference(_scene));
            _mapQuitInst->SetPaletteData(quit->PaletteData, RequireReference(_scene));
            _mapQuitInst->Enabled = true;
            _navPlayerPosModel = Read::GetModelInstance("PlayerPos_NAV", false, MetaDir::Hud);
            RequireReference(_scene).LoadModel(_navPlayerPosModel->Model());
            _navPlayerPosModel->SetAnimation(0, AnimFlags::None);
            _navDoorModel = Read::GetModelInstance("Door_NAV", false, MetaDir::Hud);
            RequireReference(_scene).LoadModel(_navDoorModel->Model());
            for (std::int32_t i = 0; i < 7; ++i)
            {
                auto mapModel = Read::GetModelInstance(ManagedAt(Metadata::NavMapModelNames, i), false, MetaDir::Hud, true);
                for (const auto& material : RequireReference(RequireReference(mapModel->Model()).Materials))
                {
                    if (material->Culling == CullingMode::Front)
                    {
                        material->Culling = CullingMode::Back;
                    }
                    else
                    {
                        material->Culling = CullingMode::Front;
                    }
                    material->Wireframe = 0;
                    material->Lighting = 1;
                    material->Ambient = ColorRgb(8, 8, 8);
                }
                for (const auto& node : RequireReference(RequireReference(mapModel->Model()).Nodes))
                {
                    if (node->Name.starts_with("cent"))
                    {
                        node->Enabled = false;
                    }
                }
                RequireReference(_scene).LoadModel(mapModel->Model());
                _navMapModels[static_cast<std::size_t>(i)] = mapModel;
            }
        }

        if (GameState::SinglePlayer() && RequireReference(_hudObjects).EnergyTanks.has_value())
        {
            auto tank = Hud::HudInfo::GetHudObject(*RequireReference(_hudObjects).EnergyTanks);
            _healthbarMainMeter->TankInst = std::make_shared<Hud::HudObjectInstance>(tank->Width, tank->Height);
            _healthbarMainMeter->TankInst->SetCharacterData(tank->CharacterData, RequireReference(_scene));
            if (_hunter == Hunter::Samus || _hunter == Hunter::Guardian)
            {
                _healthbarMainMeter->TankInst->SetPaletteData(tank->PaletteData, RequireReference(_scene));
            }
            else
            {
                _healthbarMainMeter->TankInst->SetPaletteData(healthbarMain->PaletteData, RequireReference(_scene));
            }
            _healthbarMainMeter->TankInst->Enabled = true;
        }

        _healthbarYOffset = RequireReference(_hudObjects).HealthOffsetY;
        auto ammoBar = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).AmmoBar);
        _ammoBarMeter = ManagedAt(*Hud::HudElements::AmmoBars, static_cast<std::int32_t>(_hunter));
        _ammoBarMeter->BarInst = std::make_shared<Hud::HudObjectInstance>(ammoBar->Width, ammoBar->Height);
        _ammoBarMeter->BarInst->SetCharacterData(ammoBar->CharacterData, RequireReference(_scene));
        _ammoBarMeter->BarInst->SetPaletteData(ammoBar->PaletteData, RequireReference(_scene));
        _textPaletteData = ammoBar->PaletteData;
        auto weaponIcon = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).WeaponIcon);
        _weaponIconInst = std::make_shared<Hud::HudObjectInstance>(weaponIcon->Width, weaponIcon->Height);
        _weaponIconInst->SetCharacterData(weaponIcon->CharacterData, RequireReference(_scene));
        _weaponIconInst->SetPaletteData(weaponIcon->PaletteData, RequireReference(_scene));
        _weaponIconInst->SetAnimationFrames(weaponIcon->AnimParams);

        auto listSheet = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).WeaponSelect);
        _weaponListSheetData = listSheet->CharacterData;
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_weaponListIcons.size()); ++i)
        {
            auto listIcon = _weaponListIcons[static_cast<std::size_t>(i)];
            if (!listIcon || listIcon->Width != listSheet->Width || listIcon->Height != listSheet->Height)
            {
                listIcon = Mods::Render::SmoothHudIcon::Create(listSheet);
            }
            Mods::Render::SmoothHudIcon::Tint(listIcon, listSheet->CharacterData, i,
                _weaponListColors[static_cast<std::size_t>(i)], RequireReference(_scene));
            _weaponListIcons[static_cast<std::size_t>(i)] = listIcon;
            _weaponListIconBounds[static_cast<std::size_t>(i)] = ModIconBounds(
                listSheet->CharacterData, i, listSheet->Width, listSheet->Height);
        }

        auto boost = Hud::HudInfo::GetHudObject(Hud::HudElements::Boost);
        _boostInst = std::make_shared<Hud::HudObjectInstance>(boost->Width, boost->Height);
        _boostInst->SetCharacterData(boost->CharacterData, RequireReference(_scene));
        _boostInst->SetPaletteData(boost->PaletteData, RequireReference(_scene));
        _boostInst->SetAnimationFrames(boost->AnimParams);
        _boostInst->Enabled = true;
        auto bombs = Hud::HudInfo::GetHudObject(Hud::HudElements::Bombs);
        _bombInst = std::make_shared<Hud::HudObjectInstance>(bombs->Width, bombs->Height);
        _bombInst->SetCharacterData(bombs->CharacterData, RequireReference(_scene));
        _bombInst->SetPaletteData(bombs->PaletteData, RequireReference(_scene));
        _bombInst->Enabled = true;
        _boostBombsYOffset = 208.0F;
        auto stars = Hud::HudInfo::GetHudObject(Hud::HudElements::Stars);
        _starsInst = std::make_shared<Hud::HudObjectInstance>(stars->Width, stars->Height);
        _starsInst->SetCharacterData(stars->CharacterData, RequireReference(_scene));
        _starsInst->SetPaletteData(stars->PaletteData, RequireReference(_scene));
        _starsInst->Enabled = true;
        for (std::int32_t i = 0; i < 8; ++i)
        {
            auto hunter = Hud::HudInfo::GetHudObject(ManagedAt(*Hud::HudElements::Hunters, i));
            auto hunterInst = std::make_shared<Hud::HudObjectInstance>(hunter->Width, hunter->Height);
            hunterInst->SetCharacterData(hunter->CharacterData, RequireReference(_scene));
            if (i == 7)
            {
                auto palette = std::make_shared<std::vector<ColorRgba>>(*hunter->PaletteData);
                for (std::int32_t j = 0; j < static_cast<std::int32_t>(palette->size()); ++j)
                {
                    if (j != 1)
                    {
                        (*palette)[static_cast<std::size_t>(j)] = ColorRgba(0, 0, 0, 255);
                    }
                }
                hunterInst->SetPaletteData(palette, RequireReference(_scene));
            }
            else
            {
                hunterInst->SetPaletteData(hunter->PaletteData, RequireReference(_scene));
            }
            hunterInst->Enabled = true;
            _hunterInsts[static_cast<std::size_t>(i)] = hunterInst;
        }

        auto octolith = Hud::HudInfo::GetHudObject(Hud::HudElements::Octolith);
        _octolithInst = std::make_shared<Hud::HudObjectInstance>(octolith->Width, octolith->Height);
        _octolithInst->SetCharacterData(octolith->CharacterData, RequireReference(_scene));
        _octolithInst->SetPaletteData(octolith->PaletteData, RequireReference(_scene));
        _octolithInst->Enabled = true;
        auto primeHunter = Hud::HudInfo::GetHudObject(RequireReference(_hudObjects).PrimeHunter);
        _primeHunterInst = std::make_shared<Hud::HudObjectInstance>(primeHunter->Width, primeHunter->Height);
        _primeHunterInst->SetCharacterData(primeHunter->CharacterData, RequireReference(_scene));
        _primeHunterInst->SetPaletteData(primeHunter->PaletteData, RequireReference(_scene));
        _primeHunterInst->Enabled = true;
        auto nodes = Hud::HudInfo::GetHudObject(GameState::Teams() ? Hud::HudElements::NodesOG : Hud::HudElements::NodesRB);
        _nodesInst = std::make_shared<Hud::HudObjectInstance>(nodes->Width, nodes->Height);
        _nodesInst->SetCharacterData(nodes->CharacterData, RequireReference(_scene));
        _nodesInst->SetPaletteData(nodes->PaletteData, RequireReference(_scene));
        _nodesInst->Enabled = true;
        auto systemLoad = Hud::HudInfo::GetHudObject(Hud::HudElements::SystemLoad);
        _nodeProgressMeter = Hud::HudElements::NodeProgressBar;
        _nodeProgressMeter->BarInst = std::make_shared<Hud::HudObjectInstance>(systemLoad->Width, systemLoad->Height);
        _nodeProgressMeter->BarInst->SetCharacterData(systemLoad->CharacterData, RequireReference(_scene));
        _nodeProgressMeter->BarInst->SetPaletteData(systemLoad->PaletteData, RequireReference(_scene));
        _nodeProgressMeter->BarInst->Enabled = true;
        _textInst = std::make_shared<Hud::HudObjectInstance>(8, 8, 16, 16);
        _textInst->SetCharacterData(Text::Font::Normal()->CharacterData(), RequireReference(_scene));
        _textInst->SetPaletteData(ammoBar->PaletteData, RequireReference(_scene));
        _textInst->Enabled = true;
        for (const auto& message : _hudMessageQueue)
        {
            RequireReference(message).Lifetime = 0.0F;
        }

        if (GameState::Multiplayer())
        {
            LoadModeRules();
        }
        else
        {
            _scanCornerObj = Hud::HudInfo::GetHudObject(Hud::HudElements::ScanCorner);
            _scanCornerSmallObj = Hud::HudInfo::GetHudObject(Hud::HudElements::ScanCornerSmall);
            _scanCornerInst = std::make_shared<Hud::HudObjectInstance>(_scanCornerObj->Width, _scanCornerObj->Height);
            _scanCornerInst->SetCharacterData(_scanCornerObj->CharacterData, RequireReference(_scene));
            _scanCornerInst->Enabled = true;
            auto lineHoriz = Hud::HudInfo::GetHudObject(Hud::HudElements::ScanLineHoriz);
            _scanLineHorizInst = std::make_shared<Hud::HudObjectInstance>(lineHoriz->Width, lineHoriz->Height);
            _scanLineHorizInst->SetCharacterData(lineHoriz->CharacterData, RequireReference(_scene));
            _scanLineHorizInst->Enabled = true;
            auto lineVert = Hud::HudInfo::GetHudObject(Hud::HudElements::ScanLineVert);
            _scanLineVertInst = std::make_shared<Hud::HudObjectInstance>(lineVert->Width, lineVert->Height);
            _scanLineVertInst->SetCharacterData(lineVert->CharacterData, RequireReference(_scene));
            _scanLineVertInst->Enabled = true;
            if (_hunter == Hunter::Samus)
            {
                _scanCornerInst->SetPaletteData(_scanCornerObj->PaletteData, RequireReference(_scene));
                _scanLineHorizInst->SetPaletteData(lineHoriz->PaletteData, RequireReference(_scene));
                _scanLineVertInst->SetPaletteData(lineVert->PaletteData, RequireReference(_scene));
            }
            else
            {
                _scanCornerInst->SetPaletteData(_targetCircleObj->PaletteData, RequireReference(_scene));
                _scanLineHorizInst->SetPaletteData(_targetCircleObj->PaletteData, RequireReference(_scene));
                _scanLineVertInst->SetPaletteData(_targetCircleObj->PaletteData, RequireReference(_scene));
            }
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(_scanIconInsts.size()); ++i)
            {
                auto scanIcon = Hud::HudInfo::GetHudObject(ManagedAt(*Hud::HudElements::ScanIcons, i));
                auto scanIconInst = std::make_shared<Hud::HudObjectInstance>(scanIcon->Width, scanIcon->Height);
                scanIconInst->SetCharacterData(scanIcon->CharacterData, RequireReference(_scene));
                scanIconInst->SetPaletteData(scanIcon->PaletteData, RequireReference(_scene));
                scanIconInst->Enabled = true;
                _scanIconInsts[static_cast<std::size_t>(i)] = scanIconInst;
            }
            _scanProgressMeter = ManagedAt(*Hud::HudElements::SubHealthbars, static_cast<std::int32_t>(Hunter::Samus));
            _scanProgressMeter->BarInst = std::make_shared<Hud::HudObjectInstance>(samusSubBar->Width, samusSubBar->Height);
            _scanProgressMeter->BarInst->SetCharacterData(samusSubBar->CharacterData, RequireReference(_scene));
            _scanProgressMeter->BarInst->SetPaletteData(healthbarSub->PaletteData, RequireReference(_scene));
            _scanProgressMeter->Horizontal = true;
            _scanProgressMeter->BarInst->Enabled = true;
            auto samusAmmo = ammoBar;
            if (_hunter != Hunter::Samus)
            {
                samusAmmo = Hud::HudInfo::GetHudObject(ManagedAt(*Hud::HudElements::HunterObjects,
                    static_cast<std::int32_t>(Hunter::Samus))->AmmoBar);
            }
            _dialogPaletteData = samusAmmo->PaletteData;
            auto messageBox = Hud::HudInfo::GetHudObject(Hud::HudElements::MessageBox);
            _messageBoxInst = std::make_shared<Hud::HudObjectInstance>(messageBox->Width, messageBox->Height);
            _messageBoxInst->SetCharacterData(messageBox->CharacterData, RequireReference(_scene));
            _messageBoxInst->SetPaletteData(samusAmmo->PaletteData, RequireReference(_scene));
            _messageBoxInst->SetAnimationFrames(messageBox->AnimParams);
            _messageBoxInst->Enabled = true;
            auto messageSpacer = Hud::HudInfo::GetHudObject(Hud::HudElements::MessageSpacer);
            _messageSpacerInst = std::make_shared<Hud::HudObjectInstance>(messageSpacer->Width, messageSpacer->Height);
            _messageSpacerInst->SetCharacterData(messageSpacer->CharacterData, RequireReference(_scene));
            _messageSpacerInst->SetPaletteData(samusAmmo->PaletteData, RequireReference(_scene));
            _messageSpacerInst->Enabled = true;
            for (std::int32_t i = 0; i < 5; ++i)
            {
                auto [binding, palette] = Hud::HudInfo::CharMapToTexture(Hud::HudElements::MapScan,
                    0, 0, 32, 24, RequireReference(_scene), {}, i);
                static_cast<void>(palette);
                _dialogBindingIds[static_cast<std::size_t>(i)] = binding;
            }
            auto dialogButton = Hud::HudInfo::GetHudObject(Hud::HudElements::DialogButton);
            _dialogButtonInst = std::make_shared<Hud::HudObjectInstance>(dialogButton->Width, dialogButton->Height);
            _dialogButtonInst->SetCharacterData(dialogButton->CharacterData, RequireReference(_scene));
            _dialogButtonInst->SetPaletteData(dialogButton->PaletteData, RequireReference(_scene));
            _dialogButtonInst->SetAnimationFrames(dialogButton->AnimParams);
            _dialogButtonInst->Enabled = true;
            auto dialogArrow = Hud::HudInfo::GetHudObject(Hud::HudElements::DialogArrow);
            _dialogArrowInst = std::make_shared<Hud::HudObjectInstance>(dialogArrow->Width, dialogArrow->Height);
            _dialogArrowInst->SetCharacterData(dialogArrow->CharacterData, RequireReference(_scene));
            _dialogArrowInst->SetPaletteData(dialogArrow->PaletteData, RequireReference(_scene));
            _dialogArrowInst->SetAnimationFrames(dialogArrow->AnimParams);
            _dialogArrowInst->Enabled = true;
            auto dialogCrystal = Hud::HudInfo::GetHudObject(Hud::HudElements::DialogCrystal);
            _dialogCrystalInst = std::make_shared<Hud::HudObjectInstance>(dialogCrystal->Width, dialogCrystal->Height);
            _dialogCrystalInst->SetCharacterData(dialogCrystal->CharacterData, RequireReference(_scene));
            _dialogCrystalInst->SetPaletteData(dialogCrystal->PaletteData, RequireReference(_scene));
            _dialogCrystalInst->SetAnimationFrames(dialogCrystal->AnimParams);
            _dialogCrystalInst->Enabled = true;
            auto dialogPickup = Hud::HudInfo::GetHudObject(Hud::HudElements::DialogPickup);
            _dialogPickupInst = std::make_shared<Hud::HudObjectInstance>(dialogPickup->Width, dialogPickup->Height);
            _dialogPickupInst->SetCharacterData(dialogPickup->CharacterData, RequireReference(_scene));
            _dialogPickupInst->SetPaletteData(dialogPickup->PaletteData, RequireReference(_scene));
            _dialogPickupInst->Enabled = true;
            auto dialogFrame = Hud::HudInfo::GetHudObject(Hud::HudElements::DialogFrame);
            _dialogFrameInst = std::make_shared<Hud::HudObjectInstance>(dialogFrame->Width, dialogFrame->Height);
            _dialogFrameInst->SetCharacterData(dialogFrame->CharacterData, RequireReference(_scene));
            _dialogFrameInst->SetPaletteData(dialogFrame->PaletteData, RequireReference(_scene));
            _dialogFrameInst->Enabled = true;
        }
        _hudReady = true;
    }

    void PlayerEntity::LoadModeRules()
    {
        for (std::int32_t i = 0; i < 8; ++i)
        {
            _rulesLines[static_cast<std::size_t>(i)].reset();
            _rulesLengths[static_cast<std::size_t>(i)] = {0, 0};
        }
        const GameMode mode = GameState::Mode();
        std::int32_t index = -1;
        if (mode == GameMode::Battle || mode == GameMode::BattleTeams) index = 0;
        else if (mode == GameMode::Survival || mode == GameMode::SurvivalTeams) index = 1;
        else if (mode == GameMode::PrimeHunter) index = 2;
        else if (mode == GameMode::Bounty || mode == GameMode::BountyTeams) index = 3;
        else if (mode == GameMode::Capture) index = 4;
        else if (mode == GameMode::Defender || mode == GameMode::DefenderTeams) index = 5;
        else if (mode == GameMode::Nodes || mode == GameMode::NodesTeams) index = 6;
        _rulesInfo = ManagedAt(*Hud::HudElements::RulesInfo, index);
        std::array<char16_t, 256> buffer{};
        for (std::int32_t i = 0; i < RequireReference(_rulesInfo).Count(); ++i)
        {
            std::string line = Text::Strings::GetMessage('S', ManagedAt(*RequireReference(_rulesInfo).MessageIds(), i),
                Text::StringTables::HudMessagesMP);
            if (i == 0)
            {
                _rulesLines[0] = line;
                _rulesLengths[0] = {30, 0};
            }
            else
            {
                const std::int32_t offset = ManagedAt(*RequireReference(_rulesInfo).Offsets(), i);
                buffer.fill(u'\0');
                const std::int32_t lineCount = WrapText(line, 244 - (offset + 12), buffer);
                std::int32_t length = 0;
                while (length < static_cast<std::int32_t>(buffer.size())
                    && buffer[static_cast<std::size_t>(length)] != u'\0')
                {
                    ++length;
                }
                _rulesLines[static_cast<std::size_t>(i)] = FromManagedChars(
                    std::span<const char16_t>(buffer.data(), static_cast<std::size_t>(length)));
                _rulesLengths[static_cast<std::size_t>(i)] = {
                    _rulesLengths[static_cast<std::size_t>(i - 1)].first + length,
                    lineCount - 1};
            }
        }
    }

    void PlayerEntity::InitHudState()
    {
        RequireReference(_targetCircleInst).Enabled = false;
        RequireReference(RequireReference(_ammoBarMeter).BarInst).Enabled = false;
        RequireReference(_weaponIconInst).Enabled = false;
        RequireReference(_damageIndicator).Active = false;
        RequireReference(RequireReference(_scene).Layer1Info()).BindingId = -1;
        RequireReference(RequireReference(_scene).Layer2Info()).BindingId = -1;
        RequireReference(RequireReference(_scene).Layer3Info()).BindingId = -1;
        RequireReference(RequireReference(_scene).Layer4Info()).BindingId = -1;
        RequireReference(RequireReference(_scene).Layer5Info()).BindingId = -1;
    }

    void PlayerEntity::UpdateHud()
    {
        if (GameState::MenuPause())
        {
            InitHudState();
            auto& scene = RequireReference(_scene);
            RequireReference(scene.Layer1Info()).BindingId = _pauseBindingId;
            RequireReference(scene.Layer1Info()).Alpha = 0.75F;
            RequireReference(scene.Layer1Info()).ShiftX = 0.0F;
            RequireReference(scene.Layer1Info()).ShiftY = -1.0F / 3.0F;
            RequireReference(scene.Layer1Info()).MaskId = -1;
            RequireReference(scene.Layer2Info()).BindingId = _pausedPrevBindingId2;
            RequireReference(scene.Layer2Info()).Alpha = 1.0F;
            RequireReference(scene.Layer2Info()).ShiftX = 0.0F;
            RequireReference(scene.Layer2Info()).ShiftY = 0.0F;
            return;
        }
        if (GameState::DialogPause()) return;
        UpdateScanState();
        if (GameState::SinglePlayer()) UpdateDialogs();
        ProcessDoubleDamageHud();
        ProcessCloakHud();
        UpdateHealthbars();
        UpdateAmmoBar();
        UpdateVisorMessage();
        RequireReference(_weaponIconInst).ProcessAnimation(RequireReference(_scene));
        RequireReference(_boostInst).ProcessAnimation(RequireReference(_scene));
        UpdateBoostBombs();
        UpdateDamageIndicators();
        UpdateDisruptedState();
        UpdateWhiteoutState();
        _weaponSelection = _currentWeapon;
        if (TestFlag(_flags1, PlayerFlags1::WeaponMenuOpen))
        {
            if (!_hudWeaponMenuOpen)
            {
                if (_scanVisor) SwitchVisors(false);
                _soundSource.PlayFreeSfx(SfxId::HUD_WEAPON_SWITCH1);
            }
            _hudWeaponMenuOpen = true;
            UpdateWeaponSelect();
        }
        else
        {
            _hudWeaponMenuOpen = false;
            // The drag has no state worth keeping between two holds: the
            // next one starts from the weapon in the player's hands.
            Mods::Input::WeaponWheel::Close();
        }
        if (_scanVisor) UpdateScanHud();
        InitHudState();
        auto& scene = RequireReference(_scene);
        RequireReference(scene.Layer1Info()).ShiftX = 0.0F;
        RequireReference(scene.Layer1Info()).ShiftY = 0.0F;
        RequireReference(scene.Layer2Info()).ShiftX = 0.0F;
        RequireReference(scene.Layer2Info()).ShiftY = 0.0F;
        RequireReference(scene.Layer3Info()).ShiftX = 0.0F;
        RequireReference(scene.Layer3Info()).ShiftY = 0.0F;
        RequireReference(scene.Layer4Info()).ShiftX = 0.0F;
        RequireReference(scene.Layer4Info()).ShiftY = 0.0F;
        RequireReference(scene.Layer5Info()).ShiftX = 0.0F;
        RequireReference(scene.Layer5Info()).ShiftY = 0.0F;
        if (auto* current = Formats::CameraSequence::Current();
            current && TestFlag(current->Flags(), Formats::CamSeqFlags::BlockInput))
        {
            return;
        }
        if (_health > 0 || _deathCountdown > 0)
        {
            if (!IsAltForm() && !IsMorphing() && !IsUnmorphing())
            {
                if (!TestFlag(_flags1, PlayerFlags1::WeaponMenuOpen) && !ShowScoreboard()
                    && GameState::MatchState() == MatchState::InProgress)
                {
                    if (_drawIceLayer)
                    {
                        RequireReference(scene.Layer4Info()).BindingId = _iceLayerBindingId;
                        RequireReference(scene.Layer4Info()).Alpha = 9.0F / 16.0F;
                        RequireReference(scene.Layer4Info()).ScaleX = -1.0F;
                        RequireReference(scene.Layer4Info()).ScaleY = -1.0F;
                    }
                    RequireReference(scene.Layer3Info()).BindingId = _helmetDropBindingId;
                    RequireReference(scene.Layer3Info()).Alpha = Features::HelmetOpacity();
                    RequireReference(scene.Layer3Info()).ScaleX = 2.0F;
                    RequireReference(scene.Layer3Info()).ScaleY = 256.0F / 192.0F;
                    if (_scanVisor)
                    {
                        RequireReference(scene.Layer1Info()).BindingId = _scanBindingId;
                        RequireReference(scene.Layer1Info()).MaskId = _scanBindingId;
                    }
                    else
                    {
                        RequireReference(scene.Layer1Info()).BindingId = _visorBindingId;
                        RequireReference(scene.Layer1Info()).MaskId = -1;
                    }
                    RequireReference(scene.Layer1Info()).Alpha = Features::VisorOpacity();
                    RequireReference(scene.Layer1Info()).ScaleX = 1.0F;
                    RequireReference(scene.Layer1Info()).ScaleY = 256.0F / 192.0F;
                    RequireReference(scene.Layer2Info()).BindingId = _helmetBindingId;
                    RequireReference(scene.Layer2Info()).Alpha = Features::HelmetOpacity();
                    RequireReference(scene.Layer2Info()).ScaleX = 2.0F;
                    RequireReference(scene.Layer2Info()).ScaleY = 256.0F / 192.0F;
                    RequireReference(scene.Layer1Info()).ShiftX = _hudShiftX / 256.0F;
                    RequireReference(scene.Layer1Info()).ShiftY = _hudShiftY / 192.0F;
                    RequireReference(scene.Layer2Info()).ShiftX = _hudShiftX / 256.0F;
                    RequireReference(scene.Layer2Info()).ShiftY = _hudShiftY / 192.0F;
                    RequireReference(scene.Layer3Info()).ShiftX = -_hudShiftX / 4.0F / 256.0F;
                    RequireReference(scene.Layer3Info()).ShiftY = -_hudShiftY / 4.0F / 192.0F;
                }
                if (Features::NoIdleSway() || _timeSinceInput < static_cast<std::uint64_t>(Values().GunIdleTime) * 2ULL)
                {
                    UpdateReticle();
                }
                RequireReference(_ammoBarMeter->BarInst).Enabled = true;
                RequireReference(_weaponIconInst).Enabled = true;
            }
            RequireReference(_damageIndicator).Active = true;
        }
    }

    void PlayerEntity::UpdateHealthbars()
    {
        if (ModHudHealth() < 25)
        {
            if (!_healthbarChangedColor) { _healthbarPalette = 2; _healthbarChangedColor = true; }
        }
        else if (_timeSinceHeal < 20)
        {
            if (!_healthbarChangedColor) { _healthbarPalette = 1; _healthbarChangedColor = true; }
        }
        else if (_timeSinceDamage < 12)
        {
            if (!_healthbarChangedColor) { _healthbarPalette = 2; _healthbarChangedColor = true; }
        }
        else if (_healthbarChangedColor)
        {
            _healthbarPalette = 0;
            _healthbarChangedColor = false;
        }
        float targetOffsetY = RequireReference(_hudObjects).HealthOffsetY;
        if (IsAltForm() || IsMorphing()) targetOffsetY += RequireReference(_hudObjects).HealthOffsetYAlt;
        if (_healthbarYOffset > targetOffsetY) _healthbarYOffset -= 0.5F;
        else if (_healthbarYOffset < targetOffsetY) _healthbarYOffset += 0.5F;
    }

    void PlayerEntity::UpdateAmmoBar()
    {
        if (_timeSincePickup < 20)
        {
            if (!_ammoBarChangedColor) { _ammoBarPalette = 1; _ammoBarChangedColor = true; }
        }
        else if (_ammoBarChangedColor)
        {
            _ammoBarPalette = 0;
            _ammoBarChangedColor = false;
        }
    }

    void PlayerEntity::UpdateBoostBombs()
    {
        float targetOffsetY = (IsAltForm() || IsMorphing()) ? 160.0F : 208.0F;
        if (_boostBombsYOffset > targetOffsetY) _boostBombsYOffset -= 1.0F;
        else if (_boostBombsYOffset < targetOffsetY) _boostBombsYOffset += 1.0F;
    }

    void PlayerEntity::UpdateWeaponSelect()
    {
        const std::int32_t selection = Mods::Input::GamepadInput::WheelHeld() ? ModControllerWeaponSelection()
            : Mods::Input::WeaponWheel::Absolute()
            ? UpdateWeaponArc()
            : UpdateWeaponDrag();
        for (std::int32_t i = 0; i < 6; ++i)
        {
            auto& weaponInst = RequireReference(_weaponSelectInsts[static_cast<std::size_t>(i)]);
            const bool available = _availableWeapons[static_cast<BeamType>(weaponInst.CurrentFrame)];
            weaponInst.Enabled = available;
            RequireReference(_selectBoxInsts[static_cast<std::size_t>(i)]).SetIndex(
                available ? (i == selection ? 2 : 1) : 0, RequireReference(_scene));
        }
        if (selection != _hudPreviousWeaponSelection)
        {
            _soundSource.PlayFreeSfx(SfxId::HUD_WEAPON_SWITCH2);
            _hudPreviousWeaponSelection = selection;
        }
    }

    // The wheel answered by dragging: hold, move up or down, let go. A tenth
    // of the window's height per weapon. See Mods::Input::WeaponWheel.
    std::int32_t PlayerEntity::UpdateWeaponDrag()
    {
        std::int32_t current = -1;
        for (std::int32_t i = 0; i < Mods::Input::WeaponWheel::Slots; i++)
        {
            const Hud::HudObjectInstance& inst = RequireReference(_weaponSelectInsts[static_cast<std::size_t>(i)]);
            _wheelAvailable[static_cast<std::size_t>(i)] = _availableWeapons[inst.CurrentFrame];
            if (static_cast<BeamType>(inst.CurrentFrame) == _currentWeapon)
            {
                current = i;
            }
        }
        const std::int32_t selection = Mods::Input::WeaponWheel::Drag(_input.MouseDeltaY(),
            static_cast<float>(RequireReference(_scene).Size().Y) / 10.0F, _wheelAvailable, current);
        if (selection >= 0)
        {
            _weaponSelection = static_cast<BeamType>(
                RequireReference(_weaponSelectInsts[static_cast<std::size_t>(selection)]).CurrentFrame);
        }
        return selection;
    }

    // The DS's own: the weapon is the segment of the arc the pointer is
    // standing in, which needs a pointer that stands somewhere.
    std::int32_t PlayerEntity::UpdateWeaponArc()
    {
        std::int32_t selection = -1;
        const float x = _input.PointerX();
        const float y = _input.PointerY();
        const auto size = RequireReference(_scene).Size();
        float ratioX = size.X / 256.0F;
        float ratioY = size.Y / 192.0F;
        float originX = 0;
        float originY = 0;
        // With a pen zone marked out the wheel is the zone, not the window:
        // measured from its corner, in its units. See ModPlaceWeaponSelect.
        if (Mods::Input::StylusZone::Enabled())
        {
            originX = Mods::Input::StylusZone::Left() * size.X;
            originY = Mods::Input::StylusZone::Top() * size.Y;
            ratioX = Mods::Input::StylusZone::Width() * size.X / 256.0F;
            ratioY = Mods::Input::StylusZone::Height() * size.Y / 192.0F;
            if (x < originX || x >= originX + 256 * ratioX || y < originY || y >= originY + 192 * ratioY)
            {
                return -1;
            }
        }
        const float distX = originX + 224.0F * ratioX - x;
        const float distY = y - (originY + 38.0F * ratioY);
        if (distX > 0 && distY > 0 && distX * distX + distY * distY > 20.0F * ratioY * 20.0F * ratioY)
        {
            const float angleX = distX / ratioX;
            const float angleY = distY / ratioY;
            selection = ModResolveWeaponSlot(Mods::Input::WeaponSelectionDirection::Resolve(angleX, angleY));
        }
        return selection;
    }

    // The round motion-tracker overlay, top-right under the FPS counter.
    // Heading-up: this player's facing is always straight up on the dial.
    void PlayerEntity::DrawRadar()
    {
        using OpenTK::Mathematics::Vector2;
        using OpenTK::Mathematics::Vector4;
        if (!Mods::Render::Radar::Enabled || (GameState::Teams() && ShowScoreboard()))
        {
            return;
        }
        // Not during the match's own intro fly-through.
        const Formats::CameraSequence* sequence = Formats::CameraSequence::Current();
        if (sequence != nullptr && sequence->IsIntro())
        {
            return;
        }
        // Nor once the match is over: the results screen's pickers own the corner.
        if (GameState::Multiplayer() && GameState::MatchState() != MatchState::InProgress)
        {
            return;
        }
        Scene& scene = RequireReference(_scene);
        const auto size = scene.Size();
        const float u = size.Y / 192.0F;
        constexpr float dialGrow = 1.3F * 0.8F;
        constexpr float blipGrow = 1.3F * 1.2F;
        const float radius = 19.44F * dialGrow * u;
        const float rightGap = 5.0F * u;
        const float topGap = 10.0F * u;
        const float posX = (size.X - rightGap - radius) / size.X;
        const float posY = (topGap + radius) / size.Y;
        // The camera's own view direction, which is what is on screen.
        const OpenTK::Mathematics::Vector3 facing = RequireReference(CameraInfo()).Facing;
        float fx = facing.X;
        float fz = facing.Z;
        const float faceLen = std::sqrt(fx * fx + fz * fz);
        if (faceLen < 0.0001F)
        {
            fx = 0.0F;
            fz = 1.0F;
        }
        else
        {
            fx /= faceLen;
            fz /= faceLen;
        }
        const float rx = -fz;
        const float rz = fx;
        const Mods::Render::Radar::Palette& pal = Mods::Render::Radar::PaletteOf;
        if (Mods::Render::Radar::ShowBackground)
        {
            scene.DrawFlatDisc(posX, posY, Vector2::Zero, radius, pal.Background());
        }
        if (Mods::Render::Radar::ShowOutlines)
        {
            scene.DrawFlatRing(posX, posY, Vector2::Zero, radius, 0.35F * dialGrow * u, pal.Ring());
            scene.DrawFlatRing(posX, posY, Vector2::Zero, radius * 0.55F, 0.25F * dialGrow * u, pal.Ring());
            const float coneAngle = OpenTK::Mathematics::MathHelper::DegreesToRadians(55.0F);
            const Vector2 left(-radius * std::sin(coneAngle), radius * std::cos(coneAngle));
            const Vector2 right(radius * std::sin(coneAngle), radius * std::cos(coneAngle));
            scene.DrawFlatLine(posX, posY, Vector2::Zero, left, 0.25F * dialGrow * u, pal.Cone());
            scene.DrawFlatLine(posX, posY, Vector2::Zero, right, 0.25F * dialGrow * u, pal.Cone());
        }
        const float worldToPixel = radius / Mods::Render::Radar::Range;
        const OpenTK::Mathematics::Vector3 self = Position;
        const auto placeBlip = [&](OpenTK::Mathematics::Vector3 worldPos, bool isHunter, bool isWeapon, std::int32_t teamIndex)
        {
            const float dx = worldPos.X - self.X;
            const float dz = worldPos.Z - self.Z;
            const float sx = dx * rx + dz * rz;
            const float sy = dx * fx + dz * fz;
            if (sx * sx + sy * sy < 0.0004F)
            {
                return;
            }
            float px = sx * worldToPixel;
            float py = sy * worldToPixel;
            const float pixelLen = std::sqrt(px * px + py * py);
            if (pixelLen > radius)
            {
                // Beyond range: clamp to the rim rather than drop it.
                px *= radius / pixelLen;
                py *= radius / pixelLen;
            }
            const Vector2 local(px, py);
            if (isHunter)
            {
                Vector4 color = pal.Hunter();
                if (GameState::Teams())
                {
                    const Vector4 team = Mods::Multiplayer::TeamVisuals::Get(teamIndex).RadarColor().AsVector4();
                    color = Vector4(team.X * (255.0F / 31), team.Y * (255.0F / 31), team.Z * (255.0F / 31), team.W);
                }
                scene.DrawFlatRing(posX, posY, local, 0.59F * blipGrow * u, 0.2F * blipGrow * u, color);
            }
            else if (isWeapon)
            {
                const float d = 0.49F * blipGrow * u;
                const std::array<Vector2, 4> diamond{Vector2(0, d), Vector2(d, 0), Vector2(0, -d), Vector2(-d, 0)};
                scene.DrawFlatPolygon(posX, posY, local, diamond, pal.Weapon());
            }
            else
            {
                scene.DrawFlatDisc(posX, posY, local, 0.39F * blipGrow * u, pal.Powerup());
            }
        };
        for (const std::shared_ptr<PlayerEntity>& entry : Players())
        {
            PlayerEntity* other = entry.get();
            if (other == nullptr)
            {
                continue;
            }
            if (other == this || other->Health() <= 0 || !TestFlag(other->LoadFlags(), Entities::LoadFlags::Spawned))
            {
                continue;
            }
            placeBlip(other->Position, true, false, other->TeamIndex());
        }
        for (auto it = scene.GetItemInstanceEntities().GetEnumerator(); it.MoveNext(); )
        {
            const auto item = it.Current();
            if (item->Hidden || item->DespawnTimer() == 0)
            {
                continue;
            }
            placeBlip(item->Position, false, Mods::Render::Radar::IsWeaponItem(item->ItemType()), -1);
        }
        // The player's own marker, always last and always drawn.
        const float triSize = 1.25F * u;
        const std::array<Vector2, 3> tri{Vector2(0, triSize), Vector2(-triSize * 0.75F, -triSize * 0.7F),
            Vector2(triSize * 0.75F, -triSize * 0.7F)};
        scene.DrawFlatPolygon(posX, posY, Vector2::Zero, tri, pal.Player());
    }

    void PlayerEntity::UpdateDamageIndicators()
    {
        for (std::int32_t i = 0; i < 8; ++i)
        {
            std::uint16_t time = _damageIndicatorTimers[static_cast<std::size_t>(i)];
            if (time > 0) _damageIndicatorTimers[static_cast<std::size_t>(i)] = --time;
            RequireReference(_damageIndicatorNodes[static_cast<std::size_t>(i)]).Enabled = (time & 8U) != 0;
        }
    }

    void PlayerEntity::HudOnFiredShot()
    {
        if (_scanVisor || Features::FixedCrosshair()) return;
        if (!_smallReticle && !_sniperReticle)
        {
            _smallReticle = true;
            RequireReference(_targetCircleInst).SetAnimation(0, 3, 4);
        }
        _smallReticleTimer = 120;
    }

    void PlayerEntity::ResetReticle()
    {
        RequireReference(_targetCircleInst).SetCharacterData(RequireReference(_targetCircleObj).CharacterData,
            RequireReference(_targetCircleObj).Width, RequireReference(_targetCircleObj).Height, RequireReference(_scene));
        _smallReticle = false;
        _smallReticleTimer = 0;
    }

    void PlayerEntity::UpdateReticle()
    {
        if (_smallReticleTimer > 0 && !_sniperReticle)
        {
            --_smallReticleTimer;
            if (_smallReticleTimer == 0 && _smallReticle)
            {
                RequireReference(_targetCircleInst).SetAnimation(3, 0, 4);
                _smallReticle = false;
            }
        }
        if (Features::FixedWeapon())
        {
            RequireReference(_targetCircleInst).PositionX = 0.5F;
            RequireReference(_targetCircleInst).PositionY = 0.5F;
        }
        else
        {
            OpenTK::Mathematics::Vector2 pos{};
            Matrix::ProjectPosition(_aimPosition, RequireReference(_scene).ViewMatrix(),
                RequireReference(_scene).PerspectiveMatrix(), pos);
            RequireReference(_targetCircleInst).PositionX = RoundToEven(pos.X * 100000.0F) / 100000.0F;
            RequireReference(_targetCircleInst).PositionY = RoundToEven(pos.Y * 100000.0F) / 100000.0F;
        }
        RequireReference(_targetCircleInst).Enabled = true;
        RequireReference(_targetCircleInst).ProcessAnimation(RequireReference(_scene));
    }

    OpenTK::Mathematics::Vector3 PlayerEntity::GetCrosshairColor() const
    {
        if (_health > 60) return OpenTK::Mathematics::Vector3(0, 1, 0);
        if (_health > 33) return OpenTK::Mathematics::Vector3(1, 0.65F, 0);
        return OpenTK::Mathematics::Vector3(1, 0, 0);
    }

    void PlayerEntity::HudOnMorphStart()
    {
        RequireReference(_targetCircleInst).SetIndex(0, RequireReference(_scene));
        SetCombatVisor();
    }

    void PlayerEntity::HudOnWeaponSwitch(BeamType beam)
    {
        SetCombatVisor();
        if (beam != BeamType::Imperialist || _sniperReticle)
        {
            _sniperReticle = false;
            ResetReticle();
        }
        else
        {
            _sniperReticle = true;
            if (!_scanVisor)
            {
                RequireReference(_targetCircleInst).SetCharacterData(RequireReference(_sniperCircleObj).CharacterData,
                    RequireReference(_sniperCircleObj).Width, RequireReference(_sniperCircleObj).Height, RequireReference(_scene));
            }
        }
        RequireReference(_weaponIconInst).SetAnimation(9, 27, 19, static_cast<std::int32_t>(beam));
    }

    void PlayerEntity::HudOnZoom(bool zoom)
    {
        if (_hudZoom != zoom)
        {
            _hudZoom = zoom;
            if (_hudZoom) RequireReference(_targetCircleInst).SetAnimation(0, 2, 2);
            else RequireReference(_targetCircleInst).SetAnimation(2, 0, 2);
        }
    }

    void PlayerEntity::HudOnDisrupted()
    {
        if (!Formats::CameraSequence::Current())
        {
            _hudDisruptedState = 1;
            _hudDisruptedTimer = _disruptedTimer;
        }
    }

    void PlayerEntity::HudEndDisrupted()
    {
        if (_hudDisruptedState != 0)
        {
            _hudDisruptedState = 0;
            _hudDisruptedTimer = 0;
            _hudDisruptionFactor = 0.0F;
        }
    }

    void PlayerEntity::UpdateDisruptedState()
    {
        if (_hudDisruptedState == 1)
        {
            _hudDisruptionFactor += 0.125F;
            if (_hudDisruptionFactor >= 1.0F) { _hudDisruptionFactor = 1.0F; _hudDisruptedState = 2; }
        }
        else if (_hudDisruptedState == 2)
        {
            if (--_hudDisruptedTimer == 0) _hudDisruptedState = 3;
        }
        else if (_hudDisruptedState == 3)
        {
            _hudDisruptionFactor -= 0.0625F;
            if (_hudDisruptionFactor <= 0.0F)
            {
                _hudDisruptionFactor = 0.0F;
                _hudDisruptedState = 0;
                _hudDisruptedTimer = 64;
            }
        }
        else if (_hudDisruptedState != 0)
        {
            if (--_hudDisruptedTimer == 0) _hudDisruptedState = 1;
        }
    }

    void PlayerEntity::BeginWhiteout()
    {
        _hudWhiteoutState = 0;
        _hudWhiteoutFactor = 0.0F;
        _whiteoutTime = RequireReference(_scene).GlobalElapsedTime();
        UpdateWhiteoutTable(0.0F);
    }

    void PlayerEntity::EndWhiteout()
    {
        _hudWhiteoutState = -1;
        _hudWhiteoutFactor = 0.0F;
    }

    void PlayerEntity::UpdateWhiteoutState()
    {
        const auto getPosition = [this]()
        {
            const float time = (RequireReference(_scene).GlobalElapsedTime() - _whiteoutTime) * 60.0F;
            const float timeSquared = time * time;
            const float timeCubed = timeSquared * time;
            constexpr float jerk = 0.004F;
            constexpr float initAcceleration = 0.0F;
            constexpr float initVelocity = 1.0F;
            constexpr float initPosition = 0.0F;
            assert(initAcceleration + jerk * time < 120.0F);
            assert(initVelocity + initAcceleration * time + 0.5F * jerk * timeSquared < 480.0F);
            return initPosition + initVelocity * time + 0.5F * initAcceleration * timeSquared
                + (1.0F / 6.0F) * jerk * timeCubed;
        };
        if (_hudWhiteoutState == 0)
        {
            _hudWhiteoutFactor += 2.8125F * RequireReference(_scene).FrameTime();
            if (_hudWhiteoutFactor >= 1.0F) { _hudWhiteoutFactor = 1.0F; _hudWhiteoutState = 1; }
            _whiteoutAmount = getPosition();
            assert(_whiteoutAmount < 96.0F);
        }
        else if (_hudWhiteoutState == 1)
        {
            _whiteoutAmount = getPosition();
            if (_whiteoutAmount >= 96.0F)
            {
                _whiteoutAmount = 96.0F;
                _hudWhiteoutState = 2;
                _whiteoutTime = RequireReference(_scene).GlobalElapsedTime();
            }
            UpdateWhiteoutTable(_whiteoutAmount);
        }
        else if (_hudWhiteoutState == 2)
        {
            _hudWhiteoutFactor = -1.0F;
            const float time = RequireReference(_scene).GlobalElapsedTime() - _whiteoutTime;
            const float value = 1.0F - ::MphRead::NativeRuntime::MathMin(
                time / (16.0F / 30.0F), 1.0F);
            HudWhiteoutTable.fill(value);
        }
    }

    void PlayerEntity::UpdateWhiteoutTable(float value)
    {
        const std::int32_t trunc = static_cast<std::int32_t>(value);
        float amount = 0.9975F;
        for (std::int32_t i = 95; i >= 0; --i)
        {
            const std::int32_t index = i - trunc;
            if (index >= 0)
            {
                assert(index <= 95);
                const float factor = (std::pow(amount, 8.0F) * 32.0F - 16.0F) / 16.0F;
                assert(factor >= -1.0F && factor <= 1.0F);
                HudWhiteoutTable[static_cast<std::size_t>(index)] = factor;
                HudWhiteoutTable[static_cast<std::size_t>(191 - index)] = factor;
            }
            amount -= 0.0105F;
        }
        for (std::int32_t j = 95; j >= 95 - trunc && j >= 0; --j)
        {
            HudWhiteoutTable[static_cast<std::size_t>(j)] = 16.0F;
            HudWhiteoutTable[static_cast<std::size_t>(191 - j)] = 16.0F;
        }
    }

    void PlayerEntity::DrawHudObjects()
    {
        if (Mods::ThumbnailMode::Active()) return;
        if (Mods::RenderOptions::ShowFps()) DrawFps();
        DrawRadar();
        ModDrawStylusZone();
        ModDrawChat();
        ModDrawVote();
        ModDrawEndScreen();
        if (Mods::SpectatorMode::FreeCamera())
        {
            if (ShowScoreboard() && !GameState::MenuPause())
            {
                DrawMatchTime();
                DrawScoreboard();
            }
            return;
        }
        if (GameState::MenuPause()) return;
        if (GameState::MatchState() == MatchState::GameOver)
        {
            DrawText2D(128.0F, 40.0F, Hud::Align::Center, 0, Text::Strings::GetHudMessage(219),
                ColorRgba(0x3FEF), 1.0F, 8.0F);
        }
        else if (GameState::MatchState() == MatchState::Ending)
        {
            DrawScoreboard();
        }
        else if (auto* current = Formats::CameraSequence::Current();
            current && TestFlag(current->Flags(), Formats::CamSeqFlags::BlockInput))
        {
            DrawDialogs();
            return;
        }
        else if (auto* current = Formats::CameraSequence::Current();
            current && current->IsIntro())
        {
            DrawModeRules();
            DrawQueuedHudMessages();
            return;
        }
        else if (TestFlag(_flags1, PlayerFlags1::WeaponMenuOpen))
        {
            const float wheelScale = ModPlaceWeaponSelect();
            for (std::int32_t i = 0; i < 6; ++i)
            {
                RequireReference(_scene).DrawHudObject(_selectBoxInsts[static_cast<std::size_t>(i)], 1, wheelScale);
                RequireReference(_scene).DrawHudObject(_weaponSelectInsts[static_cast<std::size_t>(i)], 1, wheelScale);
            }
        }
        else if (ShowScoreboard())
        {
            DrawMatchTime();
            DrawScoreboard();
        }
        else
        {
            if (GameState::SinglePlayer() && !GameState::DialogPause()) DrawEscapeTime();
            if (_health > 0)
            {
                if (!GameState::DialogPause())
                {
                    if (IsAltForm() || IsMorphing() || IsUnmorphing())
                    {
                        DrawBoostBombs();
                    }
                    else if (!_scanVisor)
                    {
                        if (!Features::ProHud())
                        {
                            DrawAmmoBar();
                            _weaponIconInst->PositionX = (RequireReference(_hudObjects).WeaponIconPosX + _objShiftX) / 256.0F;
                            _weaponIconInst->PositionY = (RequireReference(_hudObjects).WeaponIconPosY + _objShiftY) / 192.0F;
                            _weaponIconInst->Alpha = Features::HudOpacity();
                            RequireReference(_scene).DrawHudObject(_weaponIconInst);
                        }
                        const float reticleX = _targetCircleInst->PositionX;
                        const float reticleY = _targetCircleInst->PositionY;
                        if (Features::CustomCrosshair())
                        {
                            RequireReference(_scene).DrawCustomCrosshair(GetCrosshairColor(), reticleX, reticleY);
                        }
                        else
                        {
                            _targetCircleInst->Alpha = Features::ReticleOpacity();
                            RequireReference(_scene).DrawHudObject(_targetCircleInst);
                        }
                        const float hitMarker = Mods::Network::NetHitPrediction::MarkerAlpha();
                        if (hitMarker > 0.0F)
                        {
                            RequireReference(_scene).DrawHitMarker(OpenTK::Mathematics::Vector4(1, 1, 1, hitMarker),
                                reticleX, reticleY);
                        }
                        if (Features::ModernHud()) DrawWeaponList();
                    }
                    DrawModeHud();
                    DrawDoubleDamageHud();
                    DrawCloakHud();
                }
                if (Features::ProHud())
                {
                    if (!_scanVisor) DrawProHud();
                }
                else if (!GameState::DialogPause()
                    || DialogType() != Entities::DialogType::Event
                        && (_hunter == Hunter::Samus || _hunter == Hunter::Guardian))
                {
                    DrawHealthbars();
                }
            }
            DrawQueuedHudMessages();
            DrawDialogs();
        }
    }

    void PlayerEntity::DrawHudModels()
    {
        if (Mods::ThumbnailMode::Active()) return;
        if (Mods::SpectatorMode::FreeCamera())
        {
            if (ShowScoreboard()) RequireReference(_scene).DrawHudFilterModel(_filterModel);
            return;
        }
        if (auto* current = Formats::CameraSequence::Current();
            current && current->IsIntro())
        {
            RequireReference(_scene).DrawHudFilterModel(_filterModel, 15.0F / 31.0F);
        }
        else if (GameState::MatchState() == MatchState::GameOver)
        {
            RequireReference(_scene).DrawHudFilterModel(_filterModel, 12.0F / 31.0F);
        }
        else if (TestFlag(_flags1, PlayerFlags1::WeaponMenuOpen) || ShowScoreboard()
            || GameState::MatchState() == MatchState::Ending)
        {
            RequireReference(_scene).DrawHudFilterModel(_filterModel);
        }
        else
        {
            if (_health > 0) DrawLocatorIcons();
            if (_damageIndicator->Active) RequireReference(_scene).DrawHudDamageModel(_damageIndicator);
        }
    }

    void PlayerEntity::AddLocatorInfo(OpenTK::Mathematics::Vector3 position,
        std::shared_ptr<ModelInstance> inst, ColorRgb color, float alpha)
    {
        _locatorInfo.emplace_back(position, std::move(inst), color, alpha);
    }

    void PlayerEntity::DrawLocatorIcons()
    {
        for (const LocatorInfo& info : _locatorInfo)
        {
            DrawLocatorIcon(info.Position, info.Model, info.Color, info.Alpha);
        }
    }

    void PlayerEntity::DrawLocatorIcon(OpenTK::Mathematics::Vector3 position,
        std::shared_ptr<ModelInstance> inst, ColorRgb color, float alpha)
    {
        const auto w = [this](float value) { return value / 256.0F * RequireReference(_scene).Size().X; };
        const auto h = [this](float value) { return value / 192.0F * RequireReference(_scene).Size().Y; };
        float x;
        float y;
        bool behind = false;
        OpenTK::Mathematics::Vector2 proj{};
        const auto mult = Matrix::Vec3MultMtx4(position, RequireReference(_scene).ViewMatrix());
        if (mult.Z < -1.0F)
        {
            Matrix::ProjectPosition(position, RequireReference(_scene).ViewMatrix(),
                RequireReference(_scene).PerspectiveMatrix(), proj);
            x = proj.X * RequireReference(_scene).Size().X - w(128.0F);
            y = proj.Y * RequireReference(_scene).Size().Y - h(106.0F);
        }
        else
        {
            x = w(mult.X);
            y = -h(mult.Y);
            behind = true;
        }
        const float absX = std::abs(x);
        const float absY = std::abs(y);
        if (behind || absX > w(100.0F) || absY > h(60.0F))
        {
            if (absY >= 1.0F / 4096.0F)
            {
                const float v15 = absX + std::trunc((h(60.0F) - absY) * absX / absY);
                if (v15 > w(100.0F))
                {
                    const float v17 = absY + std::trunc((w(100.0F) - absX) * absY / absX);
                    proj.X = x <= 0.0F ? w(28.0F) : w(228.0F);
                    proj.Y = y <= 0.0F ? h(106.0F) - v17 : v17 + h(106.0F);
                }
                else
                {
                    proj.X = x <= 0.0F ? w(128.0F) - v15 : v15 + w(128.0F);
                    proj.Y = y <= 0.0F ? h(46.0F) : h(166.0F);
                }
            }
            else
            {
                proj.X = x <= 0.0F ? w(28.0F) : w(228.0F);
            }
            proj.X /= RequireReference(_scene).Size().X;
            proj.Y /= RequireReference(_scene).Size().Y;
            constexpr float degrees = 180.0F / 3.14159265358979323846F;
            const float angle = std::atan2(-y, x) * degrees;
            RequireReference(_scene).DrawIconModel(proj, angle, _arrowLocator, color, alpha);
        }
        else
        {
            RequireReference(_scene).DrawIconModel(proj, 0.0F, inst, color, alpha);
        }
    }

    void PlayerEntity::DrawEscapeTime()
    {
        if (GameState::EscapeTimer() < 0.0F) return;
        const ManagedTime time = FromSeconds(GameState::EscapeTimer());
        const std::int32_t palette = time.TotalSeconds < 10.0 ? 2 : 0;
        const std::string text = std::to_string(time.Hours * 60 + time.Minutes) + ":"
            + FormatTwo(time.Seconds) + ":" + FormatTwo(time.Milliseconds / 10);
        DrawText2D(128.0F + _objShiftX, 180.0F + _objShiftY, Hud::Align::Center, palette, text);
    }

    std::string PlayerEntity::FormatTime(float seconds) const
    {
        const ManagedTime time = FromSeconds(seconds);
        return std::to_string(time.Hours * 60 + time.Minutes) + ":" + FormatTwo(time.Seconds);
    }

    void PlayerEntity::DrawMatchTime()
    {
        if (GameState::MatchTime() < 0.0F) return;
        const ManagedTime time = FromSeconds(GameState::MatchTime());
        const std::int32_t palette = time.TotalSeconds < 10.0 ? 2 : 0;
        constexpr float posY = 10.0F;
        DrawText2D(128.0F, posY, Hud::Align::Center, palette, Text::Strings::GetHudMessage(5));
        DrawText2D(128.0F, posY + 10.0F, Hud::Align::Center, palette,
            std::to_string(time.Hours * 60 + time.Minutes) + ":" + FormatTwo(time.Seconds));
    }

    float PlayerEntity::GetScoreboardRowSpace() const
    {
        const std::int32_t rows = GameState::ActivePlayers();
        if (rows <= 4) return _scorePlayerSpace;
        float available = 168.0F - _scoreStartSpace;
        if (GameState::MatchState() == MatchState::Ending) available -= _scoreStartSpace;
        if (GameState::Teams()) available -= static_cast<float>(GameState::TeamCount()) * _scoreTeamLineSpace;
        return std::clamp(available / static_cast<float>(rows), _scoreMinPlayerSpace, _scorePlayerSpace);
    }

    float PlayerEntity::GetScoreboardHeight() const
    {
        const float rowSpace = GetScoreboardRowSpace();
        float height = _scoreStartSpace;
        if (GameState::MatchState() == MatchState::Ending) height *= 2.0F;
        std::int32_t curTeam = 4;
        for (std::int32_t i = 0; i < GameState::ActivePlayers(); ++i)
        {
            PlayerEntity& player = RequireReference(ManagedAt(Players(), ManagedAt(GameState::ResultSlots(), i)));
            if (!TestFlag(player.LoadFlags(), Entities::LoadFlags::Active)) continue;
            if (GameState::Teams() && player.TeamIndex() != curTeam)
            {
                if (curTeam != 4) height -= _scoreTeamHeaderSpace;
                height += _scoreTeamLineSpace;
                curTeam = player.TeamIndex();
            }
            height += rowSpace;
        }
        return height;
    }

    float PlayerEntity::Lerp(float first, float second, float by) noexcept
    {
        return first * (1.0F - by) + second * by;
    }

    void PlayerEntity::DrawScoreboard()
    {
        if (GameState::Teams())
        {
            ModDrawTeamScoreboard();
            return;
        }
        const GameMode mode = GameState::Mode();
        const float rowSpace = GetScoreboardRowSpace();
        float posY = 104.0F - GetScoreboardHeight() / 2.0F;
        if (GameState::MatchState() == MatchState::Ending)
        {
            DrawText2D(128, posY, Hud::Align::Center, 0, Text::Strings::GetHudMessage(219),
                ColorRgba(0x53F4), 1.0F, 8.0F);
            posY += _scoreStartSpace;
        }
        std::string header1;
        std::string header2;
        if (mode == GameMode::Battle || mode == GameMode::BattleTeams || mode == GameMode::Nodes || mode == GameMode::NodesTeams)
            header1 = Text::Strings::GetHudMessage(225);
        else if (mode == GameMode::Capture || mode == GameMode::Bounty || mode == GameMode::BountyTeams)
            header1 = Text::Strings::GetHudMessage(227);
        else header1 = Text::Strings::GetHudMessage(224);
        if (mode == GameMode::Battle || mode == GameMode::BattleTeams || mode == GameMode::Survival || mode == GameMode::SurvivalTeams)
            header2 = Text::Strings::GetHudMessage(223);
        else header2 = Text::Strings::GetHudMessage(220);
        DrawText2D(ModScoreColumn1(), posY, Hud::Align::Center, 0, header1, ColorRgba(0x3FEF), 1.0F, 8.0F);
        DrawText2D(ModScoreColumn2(), posY, Hud::Align::Center, 0, header2, ColorRgba(0x3FEF), 1.0F, 8.0F);
        ModDrawPingHeader(posY);
        posY += _scoreStartSpace;
        const std::string maxText = Text::Strings::GetHudMessage(256);

        const auto chooseValue1 = [&](float time, std::int32_t points)
        {
            if (mode == GameMode::Survival || mode == GameMode::SurvivalTeams
                || mode == GameMode::Defender || mode == GameMode::DefenderTeams || mode == GameMode::PrimeHunter)
                return time < 0.0F ? maxText : FormatTime(time);
            return FormatInt(points);
        };
        const auto chooseValue2 = [&](std::int32_t deaths, std::int32_t kills)
        {
            if (mode == GameMode::Survival || mode == GameMode::SurvivalTeams
                || mode == GameMode::Battle || mode == GameMode::BattleTeams)
                return FormatInt(deaths);
            return FormatInt(kills);
        };
        const std::string teamText = Text::Strings::GetHudMessage(222);
        std::int32_t curTeam = 4;
        for (std::int32_t i = 0; i < GameState::ActivePlayers(); ++i)
        {
            const std::int32_t slot = ManagedAt(GameState::ResultSlots(), i);
            PlayerEntity& player = RequireReference(ManagedAt(Players(), slot));
            if (!TestFlag(player.LoadFlags(), Entities::LoadFlags::Active)) continue;
            if (GameState::Teams() && player.TeamIndex() != curTeam)
            {
                if (curTeam != 4) posY -= _scoreTeamHeaderSpace;
                curTeam = player.TeamIndex();
                const std::string teamValue1 = chooseValue1(ManagedAt(GameState::TeamTime(), curTeam), ManagedAt(GameState::TeamPoints(), curTeam));
                const std::string teamValue2 = chooseValue2(ManagedAt(GameState::TeamDeaths(), curTeam), ManagedAt(GameState::TeamKills(), curTeam));
                const ColorRgba teamColor = Mods::Multiplayer::TeamVisuals::Get(curTeam).Color;
                const std::string teamName = Mods::Multiplayer::TeamVisuals::Get(curTeam).Label;
                DrawText2D(ModScoreNameColumn() - 18, posY, Hud::Align::Center, 0, teamName, teamColor, 1.0F, 8.0F);
                DrawText2D(ModScoreColumn1(), posY, Hud::Align::Center, 0, teamValue1, teamColor, 1.0F, 8.0F);
                DrawText2D(ModScoreColumn2(), posY, Hud::Align::Center, 0, teamValue2, teamColor, 1.0F, 8.0F);
                posY += _scoreTeamLineSpace;
            }
            const std::string value1 = chooseValue1(ManagedAt(GameState::Time(), slot), ManagedAt(GameState::Points(), slot));
            const std::string value2 = chooseValue2(ManagedAt(GameState::Deaths(), slot), ManagedAt(GameState::Kills(), slot));
            ColorRgba color(0x7DEF);
            if (player.IsMainPlayer())
            {
                float rg;
                const float pct = std::fmod(RequireReference(_scene).ElapsedTime() / (32.0F / 30.0F), 1.0F);
                if (pct <= 0.5F) rg = Lerp(0.0F, 1.0F, pct * 2.0F);
                else rg = Lerp(1.0F, 0.0F, (pct - 0.5F) * 2.0F);
                const auto component = static_cast<std::uint8_t>(rg * 255.0F);
                color = ColorRgba(component, component, 255, 255);
            }
            DrawScoreboardPlayer(ModScoreNameColumn(), posY, color,
                _hunterInsts[static_cast<std::size_t>(player.Hunter())], slot);
            DrawText2D(ModScoreColumn1(), posY, Hud::Align::Center, 0, value1, color, 1.0F, 8.0F);
            DrawText2D(ModScoreColumn2(), posY, Hud::Align::Center, 0, value2, color, 1.0F, 8.0F);
            ModDrawPingRow(posY, color, slot);
            posY += rowSpace;
        }
    }

    void PlayerEntity::DrawScoreboardPlayer(float posX, float posY, ColorRgba color,
        const std::shared_ptr<Hud::HudObjectInstance>& hunter, std::int32_t slot)
    {
        RequireReference(hunter).PositionX = (posX - 40.0F) / 256.0F;
        RequireReference(hunter).PositionY = (posY - 13.0F) / 192.0F;
        RequireReference(_scene).DrawHudObject(hunter, 2);
        const std::int32_t stars = ManagedAt(GameState::Stars(), slot);
        _starsInst->PositionX = posX / 256.0F;
        _starsInst->PositionY = posY / 192.0F;
        _starsInst->SetIndex(stars * 2, RequireReference(_scene));
        RequireReference(_scene).DrawHudObject(_starsInst, 2);
        _starsInst->PositionX = (posX + 32.0F) / 256.0F;
        _starsInst->SetIndex(stars * 2 + 1, RequireReference(_scene));
        RequireReference(_scene).DrawHudObject(_starsInst, 2);
        DrawText2D(posX + 32.0F, posY - 9.0F, Hud::Align::Center, 0,
            ManagedAt(GameState::Nicknames(), slot), color, 1.0F, 8.0F);
    }

    void PlayerEntity::DrawHealthbars()
    {
        if (!ModHudHealthVisible()) return;
        const std::int32_t displayHealth = ModHudHealth();
        _healthbarMainMeter->TankAmount = Values().EnergyTank;
        _healthbarMainMeter->TankCount = _healthMax / Values().EnergyTank;
        DrawMeter(_hudObjects->HealthMainPosX + _objShiftX,
            _hudObjects->HealthMainPosY + _healthbarYOffset + _objShiftY,
            Values().EnergyTank - 1, displayHealth, _healthbarPalette, _healthbarMainMeter,
            true, GameState::SinglePlayer(), Features::HudOpacity());
        if (GameState::Multiplayer())
        {
            std::int32_t amount = displayHealth >= Values().EnergyTank ? displayHealth - Values().EnergyTank : 0;
            _healthbarSubMeter->TankAmount = Values().EnergyTank;
            _healthbarSubMeter->TankCount = _healthMax / Values().EnergyTank;
            DrawMeter(_hudObjects->HealthSubPosX + _objShiftX,
                _hudObjects->HealthSubPosY + _healthbarYOffset + _objShiftY,
                Values().EnergyTank - 1, amount, _healthbarPalette, _healthbarSubMeter,
                false, false, Features::HudOpacity());
        }
    }

    void PlayerEntity::DrawAmmoBar()
    {
        auto info = RequireReference(_equipInfo).Weapon;
        if (RequireReference(info).AmmoCost == 0 || !_ammoBarMeter->BarInst->Enabled) return;
        const std::int32_t ammoType = static_cast<std::int32_t>(RequireReference(info).AmmoType);
        _ammoBarMeter->TankAmount = ManagedAt(_ammoMax, ammoType) + 1;
        _ammoBarMeter->TankCount = 0;
        std::int32_t amount = ManagedAt(_ammo, ammoType);
        DrawMeter(_hudObjects->AmmoBarPosX + _objShiftX, _hudObjects->AmmoBarPosY + _objShiftY,
            amount, amount, _ammoBarPalette, _ammoBarMeter, false, false, Features::HudOpacity());
        amount /= RequireReference(info).AmmoCost;
        const std::string ammoText = FormatTwo(amount);
        float ammoTextX = _hudObjects->AmmoBarPosX + _ammoBarMeter->BarOffsetX + _objShiftX;
        const float ammoTextY = _hudObjects->AmmoBarPosY + _ammoBarMeter->BarOffsetY + _objShiftY;
        const std::u16string managedAmmoText = ToManagedChars(ammoText);
        ammoTextX = ModAmmoTextX(ammoTextX, ammoTextY, _ammoBarMeter->Align,
            std::u16string_view(managedAmmoText.data(), managedAmmoText.size()));
        DrawText2D(ammoTextX, ammoTextY, _ammoBarMeter->Align, _ammoBarPalette, ammoText,
            std::nullopt, Features::HudOpacity());
    }

    float PlayerEntity::HudAspectFix() const
    {
        if (RequireReference(_scene).Size().X <= 0 || RequireReference(_scene).Size().Y <= 0) return 1.0F;
        return RequireReference(_scene).Size().Y / 192.0F * (256.0F / RequireReference(_scene).Size().X);
    }

    void PlayerEntity::DrawWeaponList()
    {
        const float scale = std::clamp(Features::WeaponListScale(), 0.6F, 2.0F);
        const float aspectFix = HudAspectFix();
        const float panelX = 2.0F * aspectFix;
        const float rowHeight = 8.0F * scale;
        const float panelWidth = 26.0F * scale * aspectFix;
        const float iconBox = rowHeight - 1.0F * scale;
        const float iconBoxX = iconBox * aspectFix;
        const float ammoRightX = panelX + panelWidth - 1.5F * scale * aspectFix;
        float y = 46.0F;
        // Drawn in _weaponOrder, the cartridge's cycling order, not BeamType's
        // numeric order; every other table is still indexed by the beam.
        for (std::size_t row = 0; row < _weaponOrder.size(); ++row)
        {
            const BeamType beam = _weaponOrder[row];
            const auto i = static_cast<std::int32_t>(beam);
            if (!_availableWeapons[beam]) continue;
            const bool equipped = beam == _currentWeapon;
            const auto& info = RequireReference(ManagedAt(RequireReference(Weapons::Current), i));
            const ColorRgba tint = _weaponListColors[static_cast<std::size_t>(i)];
            const float rowBottom = y + rowHeight - 1.0F * scale;
            RequireReference(_scene).DrawHudFlatBox(panelX, y, panelX + panelWidth, rowBottom,
                equipped
                    ? OpenTK::Mathematics::Vector4(0.45F, 0.4F, 0.2F, 0.72F * Features::HudOpacity())
                    : OpenTK::Mathematics::Vector4(0, 0, 0, 0.42F * Features::HudOpacity()));
            const auto& icon = _weaponListIcons[static_cast<std::size_t>(i)];
            Mods::Render::SmoothHudIcon::Tint(icon, _weaponListSheetData, i, tint, RequireReference(_scene));
            const IconBounds bounds = _weaponListIconBounds[static_cast<std::size_t>(i)];
            const float iconFit = iconBox - 1.0F * scale;
            const float iconScale = iconFit
                / ::MphRead::NativeRuntime::MathMax(bounds.Width(), bounds.Height());
            RequireReference(icon).PositionX = (panelX + iconBoxX / 2.0F - bounds.CentreX() * iconScale * aspectFix) / 256.0F;
            RequireReference(icon).PositionY = (y + iconBox / 2.0F - bounds.CentreY() * iconScale) / 192.0F;
            RequireReference(icon).Alpha = Features::HudOpacity();
            RequireReference(_scene).DrawHudObject(icon, 1, iconScale);
            const std::int32_t ammoAmount = ManagedAt(_ammo, static_cast<std::int32_t>(info.AmmoType));
            const std::string ammo = info.AmmoCost > 0 && ammoAmount >= 0
                ? std::to_string(ammoAmount / info.AmmoCost) : "--";
            DrawText2D(ammoRightX, y + 1.6F * scale, Hud::Align::Right, 0, ammo,
                ColorRgba(230, 234, 242, 255), Features::HudOpacity(), -1.0F, -1, 0.42F * scale);
            y += rowHeight;
        }
    }

    void PlayerEntity::DrawBoostBombs()
    {
        const float posY = _boostBombsYOffset;
        if (TestFlag(_abilities, AbilityFlags::Bombs) && _hunter != Hunter::Kanden)
        {
            float posX = 244.0F;
            for (std::int32_t i = 3; i > 0; --i)
            {
                _bombInst->SetIndex(_bombAmmo < i ? 1 : 0, RequireReference(_scene));
                _bombInst->PositionX = (posX - static_cast<float>(_bombInst->Width / 2)) / 256.0F;
                _bombInst->PositionY = posY / 192.0F;
                RequireReference(_scene).DrawHudObject(_bombInst, 2);
                posX -= 14.0F;
            }
            DrawText2D(230, posY + 18, Hud::Align::Center, 0, Text::Strings::GetHudMessage(1));
        }
        if (TestFlag(_abilities, AbilityFlags::Boost))
        {
            if (_altAttackCooldown == 0) _boostInst->SetIndex(0, RequireReference(_scene));
            else if (_boostInst->Timer <= 1.0F / 30.0F) _boostInst->SetIndex(1, RequireReference(_scene));
            _boostInst->PositionX = (29.0F - static_cast<float>(_boostInst->Width / 2)) / 256.0F;
            _boostInst->PositionY = (posY - 16.0F) / 192.0F;
            RequireReference(_scene).DrawHudObject(_boostInst, 2);
            DrawText2D(29, posY + 18, Hud::Align::Center, 0, Text::Strings::GetHudMessage(2));
        }
    }

    void PlayerEntity::DrawMeter(float x, float y, std::int32_t baseAmount,
        std::int32_t curAmount, std::int32_t palette, const std::shared_ptr<Hud::HudMeter>& meter,
        bool drawText, bool drawTanks, float alpha)
    {
        auto& m = RequireReference(meter);
        std::int32_t filledTanks = 0;
        std::int32_t remaining = curAmount;
        if (drawTanks && m.TankCount > 0)
        {
            for (std::int32_t i = 0; i < m.TankCount; ++i)
            {
                if (remaining < m.TankAmount) break;
                ++filledTanks;
                remaining -= m.TankAmount;
            }
        }
        const std::int32_t barAmount = GameState::SinglePlayer()
            ? curAmount - filledTanks * m.TankAmount : std::min(baseAmount, curAmount);
        std::int32_t tiles = (m.Length + 7) / 8;
        std::int32_t filledTiles = 100000 * barAmount / (99000 * m.TankAmount / m.Length);
        if (filledTiles == 0 && barAmount > 0) filledTiles = 1;
        if (drawText)
        {
            const std::int32_t amount = GameState::Multiplayer() ? curAmount : barAmount;
            DrawText2D(x + m.BarOffsetX, y + m.BarOffsetY, m.Align, _healthbarPalette, FormatTwo(amount),
                std::nullopt, alpha);
            if (m.MessageId > 0)
            {
                DrawText2D(x + m.TextOffsetX, y + m.TextOffsetY, Hud::Align::Left, _healthbarPalette,
                    Text::Strings::GetHudMessage(m.MessageId), std::nullopt, alpha);
            }
            if (drawTanks && m.TankCount > 0)
            {
                assert(m.TankInst);
                float tankX = x + m.TankOffsetX;
                float tankY = y + m.TankOffsetY;
                for (std::int32_t i = 0; i < m.TankCount; ++i)
                {
                    m.TankInst->PositionX = tankX / 256.0F;
                    m.TankInst->PositionY = tankY / 192.0F;
                    m.TankInst->SetData(i < filledTanks ? 0 : 1, palette, RequireReference(_scene));
                    m.TankInst->Alpha = alpha;
                    RequireReference(_scene).DrawHudObject(m.TankInst);
                    if (m.Horizontal) tankX += m.TankSpacing;
                    else tankY -= m.TankSpacing;
                }
            }
        }
        const auto drawTile = [&](std::int32_t charFrame)
        {
            m.BarInst->PositionX = x / 256.0F;
            m.BarInst->PositionY = y / 192.0F;
            m.BarInst->SetData(charFrame, palette, RequireReference(_scene));
            m.BarInst->Alpha = alpha;
            RequireReference(_scene).DrawHudObject(m.BarInst, 2);
            if (m.Horizontal) x += 8.0F; else y -= 8.0F;
        };
        for (std::int32_t i = 0; i < filledTiles / 8; ++i) { drawTile(0); --tiles; }
        if (tiles > 0)
        {
            drawTile(8 - (filledTiles & 7));
            --tiles;
            if (tiles > 0) for (std::int32_t i = 0; i < tiles; ++i) drawTile(8);
        }
    }

    void PlayerEntity::ProcessModeHud()
    {
        _locatorInfo.clear();
        ProcessOpponent();
        const GameMode mode = GameState::Mode();
        if (mode == GameMode::Survival || mode == GameMode::SurvivalTeams) ProcessHudSurvival();
        else if (mode == GameMode::Bounty || mode == GameMode::BountyTeams) ProcessHudBounty();
        else if (mode == GameMode::Capture) ProcessHudCapture();
        else if (mode == GameMode::Defender || mode == GameMode::DefenderTeams) ProcessHudDefender();
        else if (mode == GameMode::Nodes || mode == GameMode::NodesTeams) ProcessHudNodes();
        else if (mode == GameMode::PrimeHunter) ProcessHudPrimeHunter();
    }

    void PlayerEntity::ProcessHudSurvival()
    {
        std::int32_t reveal = 0;
        for (auto it = RequireReference(_scene).GetPlayerEntities().GetEnumerator(); it.MoveNext(); )
        {
            auto player = it.Current();
            if (player->Health() == 0 || player->TeamIndex() == TeamIndex()) continue;
            float alpha = 1.0F;
            if (GameState::RadarPlayers())
            {
                const float past = std::fmod(RequireReference(_scene).ElapsedTime(), 120.0F / 30.0F);
                if (past > 32.0F / 30.0F) alpha = 0.0F;
                else
                {
                    const float pct = std::fmod(past / (32.0F / 30.0F), 1.0F);
                    alpha = pct <= 0.5F ? Lerp(0, 1, pct * 2) : Lerp(1, 0, (pct - 0.5F) * 2);
                }
            }
            else
            {
                if (!TestFlag(player->Flags2(), PlayerFlags2::RadarReveal)) continue;
                if (TestFlag(player->Flags2(), PlayerFlags2::RadarRevealPrevious)) reveal = 2;
                else if (reveal == 0) reveal = 1;
            }
            OpenTK::Mathematics::Vector3 pos = player->Position;
            if (!player->IsAltForm()) pos.Y += 0.75F;
            AddLocatorInfo(pos, _playerLocator, GameState::Teams()
                ? Mods::Multiplayer::TeamVisuals::Get(player->TeamIndex()).RadarColor() : ColorRgb(31, 31, 31), alpha);
        }
        if (reveal == 1)
        {
            _soundSource.QueueStream(VoiceId::VOICE_CAMPING, 1.0F);
            QueueHudMessage(128, 150, 60.0F / 30.0F, 0, 234);
        }
    }

    void PlayerEntity::ProcessHudBounty()
    {
        const ColorRgb goodColor(15, 15, 31);
        if (_octolithFlag)
        {
            for (auto it = RequireReference(_scene).GetFlagBaseEntities().GetEnumerator(); it.MoveNext(); )
            {
                auto flagBase = it.Current();
                AddLocatorInfo(flagBase->Position, _nodeLocator, goodColor);
            }
        }
        else
        {
            for (auto it = RequireReference(_scene).GetOctolithFlagEntities().GetEnumerator(); it.MoveNext(); )
            {
                auto flag = it.Current();
                ColorRgb color(31, 31, 31);
                if (flag->Carrier() && (RequireReference(_scene).FrameCount() & 8) != 0)
                    color = GameState::Teams() ? Mods::Multiplayer::TeamVisuals::Get(flag->Carrier()->TeamIndex()).ObjectiveColor
                        : flag->Carrier().get() == this ? goodColor : ColorRgb(31, 0, 0);
                AddLocatorInfo(flag->Position, _octolithLocator, color);
            }
        }
    }

    void PlayerEntity::ProcessHudCapture()
    {
        const ColorRgb goodColor(15, 15, 31);
        for (auto it = RequireReference(_scene).GetOctolithFlagEntities().GetEnumerator(); it.MoveNext(); )
        {
            auto flag = it.Current();
            if (flag->Carrier().get() != this)
            {
                ColorRgb color = ManagedAt(Metadata::TeamColors, flag->Data().TeamId);
                if (flag->Carrier() && (RequireReference(_scene).FrameCount() & 8) != 0)
                    color = GameState::Teams() ? Mods::Multiplayer::TeamVisuals::Get(flag->Carrier()->TeamIndex()).ObjectiveColor
                        : flag->Carrier().get() == this ? goodColor : ColorRgb(31, 0, 0);
                AddLocatorInfo(flag->Position, _octolithLocator, color);
                if (_octolithFlag && flag->Data().TeamId == TeamIndex()) AddLocatorInfo(flag->BasePosition(), _nodeLocator, goodColor);
            }
        }
    }

    void PlayerEntity::ProcessHudDefender()
    {
        for (auto it = RequireReference(_scene).GetNodeDefenseEntities().GetEnumerator(); it.MoveNext(); )
        {
            auto defense = it.Current();
            ColorRgb color;
            if (defense->CurrentTeam() == NodeDefenseEntity::NoTeam) color = ColorRgb(31, 31, 31);
            else if (GameState::Teams())
            {
                assert(static_cast<std::uint32_t>(defense->CurrentTeam()) < static_cast<std::uint32_t>(GameState::TeamCount()));
                color = ManagedAt(Metadata::TeamColors, defense->CurrentTeam());
            }
            else if (defense->CurrentTeam() == TeamIndex()) color = ColorRgb(15, 15, 31);
            else color = ColorRgb(31, 0, 0);
            AddLocatorInfo(defense->Position, _nodeLocator, color);
        }
    }

    void PlayerEntity::ProcessHudNodes()
    {
        _nodeBonusOpponent = -1;
        _mainNodeBonus = false;
        _teamNodeCounts.fill(0);
        bool showBar = false;
        for (auto it = RequireReference(_scene).GetNodeDefenseEntities().GetEnumerator(); it.MoveNext(); )
        {
            auto defense = it.Current();
            ColorRgb color;
            if (defense->CurrentTeam() == NodeDefenseEntity::NoTeam)
            {
                if (defense->Blinking())
                {
                    if (GameState::Teams())
                    {
                        assert(static_cast<std::uint32_t>(defense->OccupyingTeam()) < static_cast<std::uint32_t>(GameState::TeamCount()));
                        color = ManagedAt(Metadata::TeamColors, defense->OccupyingTeam());
                    }
                    else if (defense->OccupyingTeam() == TeamIndex()) color = ColorRgb(15, 15, 31);
                    else color = ColorRgb(31, 0, 0);
                }
                else color = ColorRgb(31, 31, 31);
            }
            else if (GameState::Teams())
                color = ManagedAt(Metadata::TeamColors, defense->Blinking() ? defense->OccupyingTeam() : defense->CurrentTeam());
            else if (defense->CurrentTeam() == TeamIndex())
                color = !defense->Blinking() || defense->OccupyingTeam() == TeamIndex() ? ColorRgb(15, 15, 31) : ColorRgb(31, 0, 0);
            else if (defense->Blinking() && defense->OccupyingTeam() == TeamIndex()) color = ColorRgb(15, 15, 31);
            else color = ColorRgb(31, 0, 0);
            AddLocatorInfo(defense->Position, _nodeLocator, color);
            if (defense->CurrentTeam() != NodeDefenseEntity::NoTeam && defense->OccupyingTeam() == NodeDefenseEntity::NoTeam)
            {
                const std::int32_t team = defense->CurrentTeam();
                const std::int32_t count = ++ManagedAt(_teamNodeCounts, team);
                if (count > 1)
                {
                    if (team == TeamIndex()) _mainNodeBonus = true;
                    else if (_nodeBonusOpponent == -1 || count > ManagedAt(_teamNodeCounts, _nodeBonusOpponent))
                        _nodeBonusOpponent = team;
                }
            }
            if (ManagedAt(RequireReference(defense->OccupiedBy()), SlotIndex()))
            {
                showBar = true;
                if (_nodesHudState == 0)
                {
                    QueueHudMessage(128, 133, 45.0F / 30.0F, 17, 205);
                    _nodesProgressAmount = 0;
                    _nodesHudState = 1;
                }
                else if (_nodesHudState == 1)
                    _nodesProgressAmount = static_cast<std::int32_t>(RoundToEven(Lerp(0, 40, defense->Progress() / (300.0F / 30.0F))));
            }
        }
        if (!showBar && _nodesHudState != 0)
        {
            ClearHudMessage(16);
            _nodesHudState = 0;
        }
    }

    void PlayerEntity::ProcessHudPrimeHunter()
    {
        if (GameState::PrimeHunter() == SlotIndex())
        {
            if (!_hudIsPrimeHunter)
            {
                _primeHunterInst->SetAnimation(0, 1, 20, true);
                _primeHunterTextTimer = 90.0F / 30.0F;
                _hudIsPrimeHunter = true;
            }
            if (_primeHunterTextTimer > 0) _primeHunterTextTimer -= RequireReference(_scene).FrameTime();
        }
        else
        {
            if (_hudIsPrimeHunter) _hudIsPrimeHunter = false;
            if (GameState::PrimeHunter() != -1)
            {
                auto prime = ManagedAt(Players(), GameState::PrimeHunter());
                OpenTK::Mathematics::Vector3 pos = RequireReference(prime).Position;
                if (!RequireReference(prime).IsAltForm()) pos.Y += 0.75F;
                AddLocatorInfo(pos, _playerLocator, ColorRgb(31, 0, 0));
            }
        }
        _primeHunterInst->ProcessAnimation(RequireReference(_scene));
    }

    void PlayerEntity::DrawModeHud()
    {
        const GameMode mode = GameState::Mode();
        if (mode == GameMode::SinglePlayer) DrawHudAdventure();
        else
        {
            if (mode == GameMode::Battle || mode == GameMode::BattleTeams) DrawHudBattle();
            else if (mode == GameMode::Survival || mode == GameMode::SurvivalTeams) DrawHudSurvival();
            else if (mode == GameMode::Bounty || mode == GameMode::BountyTeams) DrawHudBounty();
            else if (mode == GameMode::Capture) DrawHudCapture();
            else if (mode == GameMode::Defender || mode == GameMode::DefenderTeams) DrawHudDefender();
            else if (mode == GameMode::Nodes || mode == GameMode::NodesTeams) DrawHudNodes();
            else if (mode == GameMode::PrimeHunter) DrawHudPrimeHunter();
            DrawOpponent();
        }
    }

    void PlayerEntity::DrawHudAdventure()
    {
        if (_scanVisor)
        {
            if (_scanning) DrawScanProgress();
            DrawScanObjects();
            if (!GameState::DialogPause()) DrawVisorMessage();
            return;
        }
        if (RequireReference(_scene).RoomId() == 92)
        {
            for (auto it = RequireReference(_scene).GetEnemyInstanceEntities().GetEnumerator(); it.MoveNext(); )
            {
                auto enemy = it.Current();
                if (enemy->EnemyType() == EnemyType::GoreaSealSphere2)
                {
                    auto sphere = std::dynamic_pointer_cast<Enemies::Enemy32Entity>(enemy);
                    if (sphere && sphere->Damage() < sphere->HealthMax() && sphere->Targetable()) DrawTargetHealthbar(sphere.get());
                    break;
                }
            }
        }
        else if (_lastTarget)
        {
            if (!DrawTargetHealthbar(_lastTarget.get())) _lastTarget.reset();
        }
        DrawVisorMessage();
    }

    std::string PlayerEntity::FormatModeScore(std::int32_t slot) const
    {
        const GameMode mode = GameState::Mode();
        if (mode == GameMode::Battle || mode == GameMode::BattleTeams || mode == GameMode::Capture
            || mode == GameMode::Nodes || mode == GameMode::NodesTeams || mode == GameMode::Bounty || mode == GameMode::BountyTeams)
        {
            if (GameState::Teams())
            {
                const auto player = ManagedAt(Players(), slot);
                return std::to_string(ManagedAt(GameState::TeamPoints(), RequireReference(player).TeamIndex()))
                    + " / " + std::to_string(GameState::PointGoal());
            }
            return std::to_string(ManagedAt(GameState::Points(), slot)) + " / " + std::to_string(GameState::PointGoal());
        }
        if (mode == GameMode::Survival || mode == GameMode::SurvivalTeams)
        {
            const auto player = ManagedAt(Players(), slot);
            const std::int32_t lives = std::max(GameState::PointGoal()
                - ManagedAt(GameState::TeamDeaths(), RequireReference(player).TeamIndex()), 0);
            return std::to_string(lives);
        }
        if (mode == GameMode::Defender || mode == GameMode::DefenderTeams || mode == GameMode::PrimeHunter)
            return FormatTime(ManagedAt(GameState::Time(), slot)) + "/" + FormatTime(GameState::TimeGoal());
        return " ";
    }

    void PlayerEntity::DrawModeScore(std::int32_t messageId, const std::string& text)
    {
        if (Features::ProHud()) return;
        float posX = _hudObjects->ScorePosX + _objShiftX;
        float posY = _hudObjects->ScorePosY + _objShiftY;
        _textSpacingY = 8.0F;
        DrawText2D(posX, posY, _hudObjects->ScoreAlign, 0, Text::Strings::GetHudMessage(messageId));
        posY += 9.0F;
        DrawText2D(posX, posY, _hudObjects->ScoreAlign, 0, text);
        _textSpacingY = 0.0F;
    }

    void PlayerEntity::DrawHudBattle() { DrawModeScore(212, FormatModeScore(MainPlayerIndex())); }
    void PlayerEntity::DrawHudSurvival() { DrawModeScore(213, FormatModeScore(MainPlayerIndex())); }

    void PlayerEntity::DrawOctolithInst(std::int32_t frame)
    {
        bool drawIcon = false;
        if (_octolithFlag)
        {
            drawIcon = (RequireReference(_scene).FrameCount() & 32) != 0;
            _octolithInst->Alpha = 1.0F;
        }
        else if (GameState::Teams())
        {
            for (auto it = RequireReference(_scene).GetOctolithFlagEntities().GetEnumerator(); it.MoveNext(); )
            {
                auto flag = it.Current();
                if (flag->Carrier() && flag->Carrier()->TeamIndex() == TeamIndex())
                {
                    drawIcon = true;
                    _octolithInst->Alpha = 0.5F;
                    break;
                }
            }
        }
        if (drawIcon)
        {
            _octolithInst->PositionX = (_hudObjects->OctolithPosX + _objShiftX) / 256.0F;
            _octolithInst->PositionY = (_hudObjects->OctolithPosY + _objShiftY) / 192.0F;
            _octolithInst->SetIndex(frame, RequireReference(_scene));
            RequireReference(_scene).DrawHudObject(_octolithInst);
        }
    }

    void PlayerEntity::DrawHudBounty() { DrawModeScore(215, FormatModeScore(MainPlayerIndex())); DrawOctolithInst(0); }
    void PlayerEntity::DrawHudCapture() { DrawModeScore(216, FormatModeScore(MainPlayerIndex())); DrawOctolithInst(TeamIndex() == 0 ? 4 : 3); }
    void PlayerEntity::DrawHudDefender() { DrawModeScore(217, FormatModeScore(MainPlayerIndex())); }

    void PlayerEntity::DrawHudNodes()
    {
        DrawModeScore(218, FormatModeScore(MainPlayerIndex()));
        DrawNodesBonuses();
        DrawNodesIcons();
        if (_nodesHudState == 1 && !IsHudMessageQueued(16))
        {
            _nodeProgressMeter->TankAmount = 40;
            _nodeProgressMeter->TankCount = 0;
            DrawMeter(108, 143, _nodesProgressAmount, _nodesProgressAmount, 0, _nodeProgressMeter, false, false);
            DrawText2D(128, 133, Hud::Align::Center, 0, Text::Strings::GetHudMessage(204));
        }
    }

    void PlayerEntity::DrawNodesBonuses()
    {
        if (GameState::Teams())
        {
            float y = _hudObjects->NodeBonusPosY + _objShiftY;
            for (std::int32_t team = 0; team < GameState::TeamCount(); team++)
            {
                if (ManagedAt(_teamNodeCounts, team) < 2) continue;
                const Mods::Multiplayer::TeamPresentation& visual = Mods::Multiplayer::TeamVisuals::Get(team);
                static_cast<void>(DrawText2D(_hudObjects->NodeBonusPosX + _objShiftX, y, Hud::Align::Left, 0,
                    visual.Label + " x " + std::to_string(ManagedAt(_teamNodeCounts, team)),
                    visual.Color, 1.0F, -1.0F, -1, 0.8F));
                y += 10;
            }
            return;
        }
        const std::string message = Text::Strings::GetHudMessage(210);
        if (_mainNodeBonus)
        {
            _nodesInst->PositionX = (_hudObjects->NodeBonusPosX + _objShiftX) / 256.0F;
            _nodesInst->PositionY = (_hudObjects->NodeBonusPosY + _objShiftY) / 192.0F;
            _nodesInst->SetIndex(GameState::Teams() && TeamIndex() == 0 ? 2 : 4, RequireReference(_scene));
            RequireReference(_scene).DrawHudObject(_nodesInst);
            DrawText2D(_hudObjects->NodeBonusPosX + 12 + _objShiftX, _hudObjects->NodeBonusPosY + 2 + _objShiftY,
                Hud::Align::Left, 0, "x " + std::to_string(ManagedAt(_teamNodeCounts, TeamIndex())));
            DrawText2D(_hudObjects->NodeBonusPosX + _objShiftX, _hudObjects->NodeBonusPosY + 10 + _objShiftY,
                Hud::Align::Left, 0, message);
        }
        const float past = std::fmod(RequireReference(_scene).ElapsedTime(), 16.0F / 30.0F);
        if (_nodeBonusOpponent != -1 && past < 12.0F / 30.0F)
        {
            _nodesInst->PositionX = (_hudObjects->EnemyBonusPosX + _objShiftX) / 256.0F;
            _nodesInst->PositionY = (_hudObjects->EnemyBonusPosY + _objShiftY) / 192.0F;
            _nodesInst->SetIndex(GameState::Teams() && _nodeBonusOpponent == 1 ? 4 : 2, RequireReference(_scene));
            RequireReference(_scene).DrawHudObject(_nodesInst);
            DrawText2D(_hudObjects->EnemyBonusPosX + 12 + _objShiftX, _hudObjects->EnemyBonusPosY + 2 + _objShiftY,
                Hud::Align::Left, 2, "x " + std::to_string(ManagedAt(_teamNodeCounts, _nodeBonusOpponent)));
            DrawText2D(_hudObjects->EnemyBonusPosX + _objShiftX, _hudObjects->EnemyBonusPosY + 10 + _objShiftY,
                Hud::Align::Left, 2, message);
        }
    }

    void PlayerEntity::DrawNodesIcons()
    {
        float startX = 12.0F;
        std::int32_t nodeCount = 0;
        for (auto it = RequireReference(_scene).GetNodeDefenseEntities().GetEnumerator(); it.MoveNext(); )
            if (it.Current()->Type == EntityType::NodeDefense) ++nodeCount;
        if (nodeCount < 4) startX = 16.0F * static_cast<float>(nodeCount) / 2.0F - 12.0F;
        float posX = 0.0F;
        for (auto it = RequireReference(_scene).GetNodeDefenseEntities().GetEnumerator(); it.MoveNext(); )
        {
            auto defense = it.Current();
            if (GameState::Teams())
            {
                const std::int32_t owner = defense->Blinking() ? defense->OccupyingTeam() : defense->CurrentTeam();
                const Mods::Multiplayer::TeamPresentation& visual = Mods::Multiplayer::TeamVisuals::Get(owner);
                const float x = _hudObjects->NodeIconPosX + startX - posX + _objShiftX;
                const float y = _hudObjects->NodeIconPosY - 8 + _objShiftY;
                const OpenTK::Mathematics::Vector4 objective = visual.ObjectiveColor.AsVector4();
                RequireReference(_scene).DrawHudFlatBox(x, y, x + 12, y + 12, OpenTK::Mathematics::Vector4(
                    objective.X * (255.0F / 31), objective.Y * (255.0F / 31), objective.Z * (255.0F / 31), objective.W));
                static_cast<void>(DrawText2D(x + 2, y + 2, Hud::Align::Left, 0,
                    owner == NodeDefenseEntity::NoTeam ? std::string("-") : std::string(1, static_cast<char>('A' + owner)),
                    ColorRgba(0, 0, 0, 255)));
                posX += 16;
                continue;
            }
            std::int32_t frame;
            if (defense->CurrentTeam() == NodeDefenseEntity::NoTeam)
            {
                if (defense->Blinking())
                    frame = GameState::Teams() ? (defense->OccupyingTeam() == 0 ? 2 : 4)
                        : (defense->OccupyingTeam() == TeamIndex() ? 4 : 2);
                else frame = 0;
            }
            else if (GameState::Teams())
                frame = defense->Blinking() ? (defense->OccupyingTeam() == 0 ? 2 : 4)
                    : (defense->CurrentTeam() == 0 ? 2 : 4);
            else if (defense->CurrentTeam() == TeamIndex())
                frame = !defense->Blinking() || defense->OccupyingTeam() == TeamIndex() ? 4 : 2;
            else if (defense->Blinking() && defense->OccupyingTeam() == TeamIndex()) frame = 4;
            else frame = 2;
            _nodesInst->PositionX = (_hudObjects->NodeIconPosX + startX - posX + _objShiftX) / 256.0F;
            _nodesInst->PositionY = (_hudObjects->NodeIconPosY - 8 + _objShiftY) / 192.0F;
            _nodesInst->SetIndex(frame, RequireReference(_scene));
            RequireReference(_scene).DrawHudObject(_nodesInst);
            posX += 16.0F;
        }
        DrawText2D(_hudObjects->NodeTextPosX + _objShiftX, _hudObjects->NodeTextPosY + _objShiftY,
            Hud::Align::Center, 0, Text::Strings::GetHudMessage(8));
    }

    void PlayerEntity::DrawHudPrimeHunter()
    {
        if (_hudIsPrimeHunter)
        {
            const float posX = _hudObjects->PrimePosX + _objShiftX;
            const float posY = _hudObjects->PrimePosY + _objShiftY;
            _primeHunterInst->PositionX = (posX - 16) / 256.0F;
            _primeHunterInst->PositionY = (posY - 16) / 192.0F;
            RequireReference(_scene).DrawHudObject(_primeHunterInst);
            if (_primeHunterTextTimer > 0)
            {
                const float elapsed = 90.0F / 30.0F - _primeHunterTextTimer;
                const std::int32_t length = static_cast<std::int32_t>(std::ceil(elapsed / (1.0F / 30.0F)));
                _textSpacingY = 8.0F;
                DrawText2D(posX + _hudObjects->PrimeTextPosX, posY + _hudObjects->PrimeTextPosY,
                    _hudObjects->PrimeAlign, 0, Text::Strings::GetHudMessage(11), std::nullopt, 1, -1, length);
                _textSpacingY = 0;
            }
        }
        DrawModeScore(214, FormatModeScore(MainPlayerIndex()));
    }

    void PlayerEntity::UpdateDoubleDamageSpeed(std::int32_t speed)
    {
        _doubleDamageSpeed = speed;
        _doubleDamageIconTimer = 0;
        if (speed == 1) _doubleDamageTextTimer = 60.0F / 30.0F;
    }

    void PlayerEntity::ProcessDoubleDamageHud()
    {
        if (_doubleDmgTimer > 0)
        {
            if (_doubleDamageTextTimer > 0) _doubleDamageTextTimer -= RequireReference(_scene).FrameTime();
            _doubleDamageIconTimer += RequireReference(_scene).FrameTime();
        }
    }

    void PlayerEntity::DrawDoubleDamageHud()
    {
        if (_doubleDmgTimer <= 0) return;
        const float posX = _hudObjects->DblDmgPosX + _objShiftX;
        const float posY = _hudObjects->DblDmgPosY + _objShiftY;
        _doubleDamageInst->PositionX = (posX - 16) / 256.0F;
        _doubleDamageInst->PositionY = (posY - 16) / 192.0F;
        std::int32_t frame = 0;
        if (_doubleDamageSpeed == 1 && std::fmod(_doubleDamageIconTimer, 35.0F / 30.0F) >= 30.0F / 30.0F) frame = 1;
        else if (_doubleDamageSpeed == 2 && std::fmod(_doubleDamageIconTimer, 25.0F / 30.0F) >= 20.0F / 30.0F) frame = 1;
        else if (_doubleDamageSpeed == 3 && std::fmod(_doubleDamageIconTimer, 10.0F / 30.0F) >= 5.0F / 30.0F) frame = 1;
        _doubleDamageInst->SetIndex(frame, RequireReference(_scene));
        _doubleDamageInst->Alpha = 0.5F;
        RequireReference(_scene).DrawHudObject(_doubleDamageInst);
        if (_doubleDamageTextTimer > 0)
        {
            const float elapsed = 60.0F / 30.0F - _doubleDamageTextTimer;
            const std::int32_t length = static_cast<std::int32_t>(std::ceil(elapsed / (1.0F / 30.0F)));
            _textSpacingY = 10;
            DrawText2D(posX + _hudObjects->DblDmgTextPosX, posY + _hudObjects->DblDmgTextPosY,
                _hudObjects->DblDmgAlign, 0, Text::Strings::GetHudMessage(3), std::nullopt, 1, -1, length);
            _textSpacingY = 0;
        }
    }

    void PlayerEntity::ProcessCloakHud()
    {
        if (_cloakTimer > 0 && TestFlag(_flags2, PlayerFlags2::Cloaking))
        {
            if (!_hudCloaking) { _hudCloaking = true; _cloakTextTimer = 45.0F / 30.0F; }
            if (_cloakTextTimer > 0) _cloakTextTimer -= RequireReference(_scene).FrameTime();
        }
        else _hudCloaking = false;
    }

    void PlayerEntity::DrawCloakHud()
    {
        if (_cloakTimer <= 0 || !TestFlag(_flags2, PlayerFlags2::Cloaking)) return;
        const float posX = _hudObjects->CloakPosX + _objShiftX;
        const float posY = _hudObjects->CloakPosY + _objShiftY;
        _cloakInst->PositionX = (posX - 16) / 256.0F;
        _cloakInst->PositionY = (posY - 16) / 192.0F;
        _cloakInst->Alpha = 0.5F;
        RequireReference(_scene).DrawHudObject(_cloakInst);
        if (_cloakTextTimer > 0)
        {
            const float elapsed = 45.0F / 30.0F - _cloakTextTimer;
            const std::int32_t length = static_cast<std::int32_t>(std::ceil(elapsed / (1.0F / 30.0F)));
            DrawText2D(posX + _hudObjects->CloakTextPosX, posY + _hudObjects->CloakTextPosY,
                _hudObjects->CloakAlign, 0, Text::Strings::GetHudMessage(4), std::nullopt, 1, -1, length);
        }
    }

    bool PlayerEntity::DrawTargetHealthbar(EntityBase* target)
    {
        EntityBase& targetRef = RequireReference(target);
        std::int32_t max = 0;
        std::int32_t current = 0;
        std::optional<std::string> text{};
        std::int32_t lowHealth = 0;
        bool showHealth = true;
        if (targetRef.Type == EntityType::EnemyInstance)
        {
            auto& enemy = RequireReference(ManagedCast<EnemyInstanceEntity>(target));
            const EnemyType enemyType = enemy.EnemyType();
            if (enemyType != EnemyType::FireSpawn && enemyType != EnemyType::CretaphidCrystal
                && enemyType != EnemyType::Slench && enemyType != EnemyType::SlenchShield
                && enemyType != EnemyType::GoreaArm && enemyType != EnemyType::GoreaSealSphere1
                && enemyType != EnemyType::GoreaSealSphere2)
            {
                return true;
            }
            text = Text::Strings::GetMessage('E', enemy.HealthbarMessageId(),
                Text::StringTables::HudMessagesSP);
            max = enemy.HealthMax();
            current = enemy.Health();
            if (enemyType == EnemyType::SlenchShield)
            {
                auto& shield = RequireReference(ManagedCast<Enemies::Enemy42Entity>(target));
                EnemyInstanceEntity& slench = RequireReference(shield.Slench());
                max = slench.HealthMax();
                current = slench.Health();
            }
            else if (enemyType == EnemyType::GoreaArm)
            {
                auto& arm = RequireReference(ManagedCast<Enemies::Enemy26Entity>(target));
                current = max - arm.Damage;
            }
            else if (enemyType == EnemyType::GoreaSealSphere1)
            {
                auto& sphere = RequireReference(ManagedCast<Enemies::Enemy29Entity>(target));
                current = max - sphere.Damage();
            }
            else if (enemyType == EnemyType::GoreaSealSphere2)
            {
                auto& sphere = RequireReference(ManagedCast<Enemies::Enemy32Entity>(target));
                current = max - sphere.Damage();
            }
            lowHealth = max / 4;
        }
        else if (targetRef.Type == EntityType::Player)
        {
            auto& player = RequireReference(ManagedCast<PlayerEntity>(target));
            max = player.HealthMax();
            current = ModOpponentHudHealth(player);
            showHealth = Mods::Network::NetHudHealth::Visible(player.SlotIndex());
            text = ManagedAt(_hunterNames, static_cast<std::int32_t>(player.Hunter()));
            lowHealth = 25;
        }
        else if (targetRef.Type == EntityType::Halfturret)
        {
            auto& turret = RequireReference(ManagedCast<HalfturretEntity>(target));
            max = RequireReference(turret.Owner()).HealthMax() / 2;
            current = turret.Health();
            showHealth = Mods::Network::NetHudHealth::Visible(RequireReference(turret.Owner()).SlotIndex());
            text = ManagedAt(_altAttackNames, static_cast<std::int32_t>(Hunter::Weavel));
            lowHealth = 25;
        }
        const std::int32_t palette = !showHealth || current > lowHealth ? 0 : 2;
        RequireReference(_enemyHealthMeter).TankAmount = max;
        RequireReference(_enemyHealthMeter).TankCount = 0;
        RequireReference(_enemyHealthMeter).Length = RequireReference(
            ManagedAt(*Hud::HudElements::SubHealthbars, 0)).Length;
        if (showHealth)
        {
            DrawMeter(RequireReference(_hudObjects).EnemyHealthPosX + _objShiftX,
                RequireReference(_hudObjects).EnemyHealthPosY + _objShiftY,
                max, current, palette, _enemyHealthMeter, false, false);
        }
        const std::int32_t scanId = targetRef.GetScanId();
        if (scanId != 0 && GameState::SinglePlayer()
            && !RequireReference(GameState::StorySave).CheckLogbook(scanId))
        {
            text = Text::Strings::GetMessage('E', 6, Text::StringTables::HudMessagesSP);
        }
        if (text)
        {
            DrawText2D(RequireReference(_hudObjects).EnemyHealthTextPosX + _objShiftX,
                RequireReference(_hudObjects).EnemyHealthTextPosY + _objShiftY,
                Hud::Align::Center, palette, *text);
        }
        return current > 0;
    }

    void PlayerEntity::UpdateOpponent(std::int32_t slot)
    {
        if (GameState::Multiplayer() && slot != SlotIndex())
        {
            _opponentHealthbarTimer = 60.0F / 30.0F;
            _opponentIndex = slot;
        }
    }

    void PlayerEntity::ProcessOpponent()
    {
        if (_opponentIndex != -1 && _opponentHealthbarTimer > 0)
        {
            _opponentHealthbarTimer -= RequireReference(_scene).FrameTime();
            if (_opponentHealthbarTimer <= 0)
            {
                _opponentHealthbarTimer = 0;
                _opponentIndex = -1;
            }
        }
    }

    void PlayerEntity::DrawOpponent()
    {
        if (_opponentIndex == -1 || _opponentHealthbarTimer == 0
            || !Features::TopScreenTargetInfo())
        {
            return;
        }
        PlayerEntity& opponent = RequireReference(ManagedAt(Players(), _opponentIndex));
        float posX = 93;
        float posY = 182;
        if (Features::TargetInfoSway())
        {
            posX += _objShiftX;
            posY += _objShiftY;
        }
        std::string nickname = ManagedAt(GameState::Nicknames(), _opponentIndex);
        if (GameState::Teams())
        {
            nickname = Mods::Multiplayer::TeamVisuals::Get(opponent.TeamIndex()).Label + ": " + nickname;
        }
        static_cast<void>(DrawText2D(posX, posY, Hud::Align::Center, 0, nickname,
            GameState::Teams() ? std::optional<ColorRgba>(Mods::Multiplayer::TeamVisuals::Get(opponent.TeamIndex()).Color)
                : std::nullopt));
        const auto& portrait = ManagedAt(_hunterInsts,
            static_cast<std::int32_t>(opponent.Hunter()));
        RequireReference(portrait).PositionX = (posX - 16 * HudAspectFix()) / 256.0F;
        RequireReference(portrait).PositionY = (posY - 33) / 192.0F;
        RequireReference(_scene).DrawHudObject(portrait, 1);
        posX += 18;
        posY -= 26;
        if (Mods::Network::NetHudHealth::Visible(opponent.SlotIndex()))
        {
            const std::int32_t displayHealth = ModOpponentHudHealth(opponent);
            const std::int32_t remainingAmount = displayHealth >= Values().EnergyTank
                ? displayHealth - Values().EnergyTank : 0;
            RequireReference(_enemyHealthMeter).TankAmount = Values().EnergyTank;
            RequireReference(_enemyHealthMeter).TankCount = opponent.HealthMax() / Values().EnergyTank;
            RequireReference(_enemyHealthMeter).Length = 72;
            DrawMeter(posX, posY, Values().EnergyTank - 1, displayHealth, 0,
                _enemyHealthMeter, false, false);
            DrawMeter(posX, posY + 5, Values().EnergyTank - 1, remainingAmount, 0,
                _enemyHealthMeter, false, false);
        }
        DrawText2D(posX + 5, posY + 14, Hud::Align::Left, 0,
            FormatModeScore(opponent.SlotIndex()));
    }

    void PlayerEntity::DrawModeRules()
    {
        assert(_rulesLines[0].has_value());
        std::optional<ColorRgba> color = Paths::IsMphJapan() || Paths::IsMphKorea()
            ? std::nullopt : std::optional<ColorRgba>(ColorRgba(0x7FDE));
        DrawText2D(128, 10, Hud::Align::Center, 0, *_rulesLines[0], color);
        const std::int32_t totalCharacters = static_cast<std::int32_t>(
            RequireReference(_scene).ElapsedTime() / (1.0F / 30.0F));
        float posY = 28;
        _textSpacingY = 8;
        for (std::int32_t i = 1; i < RequireReference(_rulesInfo).Count(); ++i)
        {
            const std::int32_t prevLength = ManagedAt(_rulesLengths, i - 1).first;
            const std::int32_t characters = totalCharacters - prevLength;
            if (characters <= 0)
            {
                break;
            }
            const auto& line = ManagedAt(_rulesLines, i);
            assert(line.has_value());
            const float posX = static_cast<float>(
                ManagedAt(*RequireReference(_rulesInfo).Offsets(), i) + 12);
            color = Paths::IsMphJapan() || Paths::IsMphKorea()
                ? std::nullopt : std::optional<ColorRgba>(ColorRgba(0x7F5A));
            DrawText2D(posX, posY, Hud::Align::Left, 0, *line, color,
                1.0F, -1.0F, characters);
            posY += 13 + ManagedAt(_rulesLengths, i).second * 8;
        }
        if (totalCharacters > _prevScrollingChars
            && totalCharacters > _rulesLengths[0].first
            && totalCharacters <= ManagedAt(_rulesLengths,
                RequireReference(_rulesInfo).Count() - 1).first)
        {
            _soundSource.StopFreeSfx(SfxId::LETTER_BLIP);
            _soundSource.PlayFreeSfx(SfxId::LETTER_BLIP);
            _prevScrollingChars = totalCharacters;
        }
        _textSpacingY = 0;
    }

    std::int32_t PlayerEntity::GlyphIndex(const Text::Font& font, std::int32_t ch)
    {
        const auto& widths = RequireReference(font.Widths());
        const auto& offsets = RequireReference(font.Offsets());
        const std::int32_t index = ch - font.MinCharacter();
        if (index >= 0 && static_cast<std::size_t>(index) < widths.size()
            && static_cast<std::size_t>(index) < offsets.size())
        {
            return index;
        }
        const std::int32_t fallback = static_cast<std::int32_t>('?') - font.MinCharacter();
        return fallback >= 0 && static_cast<std::size_t>(fallback) < widths.size()
            && static_cast<std::size_t>(fallback) < offsets.size() ? fallback : 0;
    }

    const Text::Font& PlayerEntity::SetUpFont(char16_t firstChar, bool set)
    {
        const Text::Font* font = &RequireReference(Text::Font::Normal());
        if (Scene::Language() == Language::Japanese
            && (Paths::IsMphJapan() || Paths::IsMphKorea())
            && (firstChar & 0xA0) == 0xA0)
        {
            font = &RequireReference(Text::Font::Kanji());
            if (set && !_usingKanjiFont)
            {
                RequireReference(_textInst).SetCharacterData(
                    RequireReference(Text::Font::Kanji()).CharacterData(), 16, 16,
                    RequireReference(_scene));
                _usingKanjiFont = true;
            }
        }
        else if (set && _usingKanjiFont)
        {
            RequireReference(_textInst).SetCharacterData(
                RequireReference(Text::Font::Normal()).CharacterData(), 8, 8,
                RequireReference(_scene));
            _usingKanjiFont = false;
        }
        return *font;
    }

    void PlayerEntity::DrawFps()
    {
        // FramesPerSecond.TryFormat(buffer[12], out written, "0"): a number
        // too long for the buffer draws nothing.
        const std::string fps = NativeRuntime::ToString(RequireReference(_scene).FramesPerSecond(), "0");
        if (NativeRuntime::Utf16Length(fps) > 12)
        {
            return;
        }
        const ColorRgba color(0x3FEF);
        const auto unit = DrawText2D(256 - NumberMargin * HudAspectFix(),
            NumberY + (NumberScale - UnitScale) * 8, Hud::Align::Right,
            0, std::string("fps"), color, 1.0F, 8.0F, -1, UnitScale);
        DrawText2D(unit.X - HudAspectFix(), NumberY, Hud::Align::Right, 0,
            fps, color,
            1.0F, 8.0F, -1, NumberScale);
    }

    OpenTK::Mathematics::Vector2 PlayerEntity::DrawText2D(float x, float y,
        Hud::Align type, std::int32_t palette, const std::string& text,
        std::optional<ColorRgba> color, float alpha, float fontSpacing,
        std::int32_t maxLength, float scale)
    {
        const std::u16string managed = ToManagedChars(text);
        return DrawText2D(x, y, type, palette,
            std::span<const char16_t>(managed.data(), managed.size()), color,
            alpha, fontSpacing, maxLength, scale);
    }

    OpenTK::Mathematics::Vector2 PlayerEntity::DrawText2D(float x, float y,
        Hud::Align type, std::int32_t palette, std::span<const char16_t> text,
        std::int32_t maxLength)
    {
        return DrawText2D(x, y, type, palette, text, std::nullopt,
            1.0F, -1.0F, maxLength, 1.0F);
    }

    OpenTK::Mathematics::Vector2 PlayerEntity::DrawText2D(float x, float y,
        Hud::Align type, std::int32_t palette, std::span<const char16_t> text,
        std::optional<ColorRgba> color, float alpha, float fontSpacing,
        std::int32_t maxLength, float scale)
    {
        const std::int32_t padAfter = maxLength;
        if (type == Hud::Align::PadCenter)
        {
            maxLength = -1;
        }
        std::int32_t length = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == u'\0')
            {
                break;
            }
            ++length;
        }
        if (maxLength != -1)
        {
            length = std::min(length, maxLength);
        }
        if (length == 0)
        {
            return OpenTK::Mathematics::Vector2(x, y);
        }
        const Text::Font& font = SetUpFont(text[0], true);
        RequireReference(_textInst).Alpha = alpha;
        float aspectFix = 1;
        if (RequireReference(_scene).Size().X > 0 && RequireReference(_scene).Size().Y > 0)
        {
            aspectFix = RequireReference(_scene).Size().Y / 192.0F
                * (256.0F / RequireReference(_scene).Size().X);
        }
        float spacingY = _textSpacingY == 0
            ? (fontSpacing == -1 ? 12 : fontSpacing) : _textSpacingY;
        if (scale != 1)
        {
            spacingY *= scale;
        }
        const auto& widths = RequireReference(font.Widths());
        const auto& offsets = RequireReference(font.Offsets());
        auto drawGlyph = [&](std::int32_t index, float drawX, float drawY)
        {
            RequireReference(_textInst).PositionX = drawX / 256.0F;
            RequireReference(_textInst).PositionY = drawY / 192.0F;
            if (color)
            {
                RequireReference(_textInst).SetData(index, *color, RequireReference(_scene));
            }
            else
            {
                RequireReference(_textInst).SetData(index, palette, RequireReference(_scene));
            }
            RequireReference(_scene).DrawHudObject(_textInst, 1, scale);
        };

        if (type == Hud::Align::Left)
        {
            const float startX = x;
            for (std::int32_t i = 0; i < length; ++i)
            {
                std::int32_t ch = ManagedAt(text, i);
                const std::int32_t orig = ch;
                if ((ch & 0x80) != 0 && i + 1 < static_cast<std::int32_t>(text.size()))
                {
                    ch = ManagedAt(text, ++i) & 0x3F | ((ch & 0x1F) << 6);
                }
                if (orig == u'\n')
                {
                    x = startX;
                    y += spacingY;
                }
                else
                {
                    const std::int32_t index = GlyphIndex(font, ch);
                    const float offset = ManagedAt(offsets, index) * scale + y;
                    if (orig != u' ')
                    {
                        drawGlyph(index, x, offset);
                    }
                    x += ManagedAt(widths, index) * scale * aspectFix;
                }
            }
        }
        else if (type == Hud::Align::Right)
        {
            const float startX = x;
            std::int32_t start = 0;
            std::int32_t end = 0;
            do
            {
                end = -1;
                for (std::int32_t i = start; i < static_cast<std::int32_t>(text.size()); ++i)
                {
                    if (ManagedAt(text, i) == u'\n')
                    {
                        end = i;
                        break;
                    }
                }
                if (end == -1 || length < end)
                {
                    end = length;
                }
                x = startX;
                for (std::int32_t i = end - 1; i >= start; --i)
                {
                    std::int32_t ch = ManagedAt(text, i);
                    const std::int32_t orig = ch;
                    if ((ch & 0x80) != 0 && i + 1 < static_cast<std::int32_t>(text.size()))
                    {
                        ch = ManagedAt(text, ++i) & 0x3F | ((ch & 0x1F) << 6);
                    }
                    const std::int32_t index = GlyphIndex(font, ch);
                    x -= ManagedAt(widths, index) * scale * aspectFix;
                    const float offset = ManagedAt(offsets, index) * scale + y;
                    if (orig != u' ')
                    {
                        drawGlyph(index, x, offset);
                    }
                }
                if (end != length)
                {
                    do
                    {
                        ++end;
                        start = end;
                        y += spacingY;
                    }
                    while (ManagedAt(text, start) == u'\n');
                }
            }
            while (end < length);
        }
        else if (type == Hud::Align::Center || type == Hud::Align::PadCenter)
        {
            const float startX = x;
            std::int32_t start = 0;
            std::int32_t end = 0;
            do
            {
                end = -1;
                for (std::int32_t i = start; i < static_cast<std::int32_t>(text.size()); ++i)
                {
                    if (ManagedAt(text, i) == u'\n')
                    {
                        end = i;
                        break;
                    }
                }
                if (end == -1 || length < end)
                {
                    end = length;
                }
                x = startX;
                float width = 0;
                for (std::int32_t i = start; i < end; ++i)
                {
                    std::int32_t ch = ManagedAt(text, i);
                    if ((ch & 0x80) != 0 && i + 1 < static_cast<std::int32_t>(text.size()))
                    {
                        ch = ManagedAt(text, ++i) & 0x3F | ((ch & 0x1F) << 6);
                    }
                    const std::int32_t index = GlyphIndex(font, ch);
                    width += ManagedAt(widths, index) * scale;
                }
                x = startX - std::floor(width / 2) * aspectFix;
                for (std::int32_t i = start; i < end; ++i)
                {
                    std::int32_t ch = ManagedAt(text, i);
                    const std::int32_t orig = ch;
                    if ((ch & 0x80) != 0 && i + 1 < static_cast<std::int32_t>(text.size()))
                    {
                        ch = ManagedAt(text, ++i) & 0x3F | ((ch & 0x1F) << 6);
                    }
                    const std::int32_t index = GlyphIndex(font, ch);
                    const float offset = ManagedAt(offsets, index) * scale + y;
                    if (orig != u' ')
                    {
                        if (type != Hud::Align::PadCenter || i < padAfter)
                        {
                            drawGlyph(index, x, offset);
                        }
                    }
                    x += ManagedAt(widths, index) * scale * aspectFix;
                }
                if (end != length)
                {
                    do
                    {
                        ++end;
                        start = end;
                        y += spacingY;
                    }
                    while (ManagedAt(text, start) == u'\n');
                }
            }
            while (end < length);
        }
        return OpenTK::Mathematics::Vector2(x, y);
    }

    std::int32_t PlayerEntity::WrapText(const std::string& text,
        std::int32_t maxWidth, std::span<char16_t> dest, std::int32_t maxTiles)
    {
        const std::u16string managed = ToManagedChars(text);
        return WrapText(std::span<const char16_t>(managed.data(), managed.size()),
            maxWidth, dest, maxTiles);
    }

    std::int32_t PlayerEntity::WrapText(std::span<const char16_t> text,
        std::int32_t maxWidth, std::span<char16_t> dest, std::int32_t maxTiles)
    {
        static_cast<void>(maxTiles);
        std::int32_t lines = 1;
        if (maxWidth <= 0)
        {
            return lines;
        }
        std::int32_t lineWidth = 0;
        std::int32_t widthAfterBreak = 0;
        std::int32_t breakPos = 0;
        std::int32_t c = 0;
        if (text.empty())
        {
            return 1;
        }
        const Text::Font& font = SetUpFont(text[0], false);
        const auto& widths = RequireReference(font.Widths());
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(text.size()); ++i)
        {
            const char16_t ch = ManagedAt(text, i);
            ManagedAt(dest, c) = ch;
            if (ch == u'\n')
            {
                lineWidth = 0;
                breakPos = 0;
                widthAfterBreak = 0;
                ++lines;
            }
            else
            {
                if (ch == u' ')
                {
                    breakPos = c;
                    widthAfterBreak = 0;
                }
                if (ch >= u' ')
                {
                    std::int32_t index = ch;
                    if ((ch & 0x80) != 0 && i + 1 < static_cast<std::int32_t>(text.size()))
                    {
                        const char16_t next = ManagedAt(text, ++i);
                        ManagedAt(dest, ++c) = next;
                        index = (next & 0x3F) | ((ch & 0x1F) << 6);
                    }
                    // The same clamp DrawText2D uses: a string table in another
                    // language than the cartridge's (Japanese on an EU ROM) holds
                    // characters this font has no width for.
                    const std::int32_t width = ManagedAt(widths, GlyphIndex(font, index));
                    lineWidth += width;
                    if (ch != u' ')
                    {
                        widthAfterBreak += width;
                    }
                }
                if (i + 1 < static_cast<std::int32_t>(text.size()) && lineWidth > maxWidth)
                {
                    if (breakPos == 0 && maxWidth >= 8)
                    {
                        breakPos = c++ + 1;
                        widthAfterBreak = 0;
                    }
                    if (breakPos > 0)
                    {
                        ManagedAt(dest, breakPos) = u'\n';
                        lineWidth = widthAfterBreak;
                        widthAfterBreak = 0;
                        breakPos = 0;
                        ++lines;
                    }
                }
            }
            ++c;
        }
        ManagedAt(dest, c) = u'\0';
        return lines;
    }

    void PlayerEntity::QueueHudMessage(float x, float y, float duration,
        std::uint8_t category, std::int32_t messageId, bool dialogHide)
    {
        QueueHudMessage(x, y, Hud::Align::Center, 256, 8, ColorRgba(0x3FEF),
            1, duration, category, Text::Strings::GetHudMessage(messageId), dialogHide);
    }

    void PlayerEntity::QueueHudMessage(float x, float y, std::int32_t maxWidth,
        float duration, std::uint8_t category, std::int32_t messageId, bool dialogHide)
    {
        QueueHudMessage(x, y, Hud::Align::Center, maxWidth, 8, ColorRgba(0x3FEF),
            1, duration, category, Text::Strings::GetHudMessage(messageId), dialogHide);
    }

    void PlayerEntity::QueueHudMessage(float x, float y, float duration,
        std::uint8_t category, const std::string& text, bool dialogHide)
    {
        QueueHudMessage(x, y, Hud::Align::Center, 256, 8, ColorRgba(0x3FEF),
            1, duration, category, text, dialogHide);
    }

    void PlayerEntity::QueueHudMessage(float x, float y, std::int32_t maxWidth,
        float duration, std::uint8_t category, const std::string& text, bool dialogHide)
    {
        QueueHudMessage(x, y, Hud::Align::Center, maxWidth, 8, ColorRgba(0x3FEF),
            1, duration, category, text, dialogHide);
    }

    void PlayerEntity::QueueHudMessage(float x, float y, Hud::Align align,
        std::int32_t maxWidth, float fontSize, ColorRgba color, float alpha,
        float duration, std::uint8_t category, const std::string& text,
        bool dialogHide)
    {
        const std::u16string managed = ToManagedChars(text);
        assert(managed.size() < 256);
        std::array<char16_t, 512> buffer{};
        const std::int32_t lineCount = WrapText(
            std::span<const char16_t>(managed.data(), managed.size()), maxWidth, buffer);
        float minDuration = std::numeric_limits<float>::max();
        std::shared_ptr<HudMessage> message{};
        for (const auto& existing : _hudMessageQueue)
        {
            HudMessage& item = RequireReference(existing);
            if (item.Lifetime > 0)
            {
                if ((category & item.Category & 14) != 0)
                {
                    item.Position.Y -= lineCount * item.FontSize;
                }
                else if (item.Position.Y == y)
                {
                    item.Lifetime = 0;
                }
            }
            if (item.Lifetime < minDuration)
            {
                minDuration = item.Lifetime;
                message = existing;
            }
        }
        assert(message != nullptr);
        RequireReference(message).Text.fill(u'\0');
        std::copy_n(buffer.begin(), RequireReference(message).Text.size(),
            RequireReference(message).Text.begin());
        if ((category & 14) != 0)
        {
            y -= (lineCount - 1) * fontSize;
        }
        RequireReference(message).Position = OpenTK::Mathematics::Vector2(x, y);
        RequireReference(message).MaxWidth = maxWidth;
        RequireReference(message).FontSize = fontSize;
        RequireReference(message).Color = color;
        RequireReference(message).Alpha = alpha;
        RequireReference(message).Align = align;
        RequireReference(message).Category = category;
        RequireReference(message).Lifetime = duration;
        RequireReference(message).DialogHide = dialogHide;
    }

    void PlayerEntity::ClearHudMessage(std::int32_t mask)
    {
        for (const auto& item : _hudMessageQueue)
        {
            HudMessage& message = RequireReference(item);
            if ((mask & message.Category) != 0)
            {
                message.Lifetime = 0;
            }
        }
    }

    bool PlayerEntity::IsHudMessageQueued(std::int32_t mask) const
    {
        for (const auto& item : _hudMessageQueue)
        {
            const HudMessage& message = RequireReference(item);
            if ((mask & message.Category) != 0 && message.Lifetime > 0)
            {
                return true;
            }
        }
        return false;
    }

    void PlayerEntity::ProcessHudMessageQueue()
    {
        for (const auto& item : _hudMessageQueue)
        {
            HudMessage& message = RequireReference(item);
            if (message.Lifetime > 0)
            {
                message.Lifetime -= RequireReference(_scene).FrameTime();
                if (message.Lifetime < 0)
                {
                    message.Lifetime = 0;
                }
            }
        }
    }

    void PlayerEntity::DrawQueuedHudMessages()
    {
        if (!GameState::MenuPause())
        {
            for (const auto& item : _hudMessageQueue)
            {
                const HudMessage& message = RequireReference(item);
                if (message.Lifetime > 0
                    && ((message.Category & 1) == 0
                        || (RequireReference(_scene).FrameCount() & (7 * 2)) <= 3 * 2)
                    && (!GameState::DialogPause() || !message.DialogHide))
                {
                    DrawText2D(message.Position.X, message.Position.Y, message.Align, 0,
                        std::span<const char16_t>(message.Text.data(), message.Text.size()),
                        message.Color, message.Alpha, message.FontSize);
                }
            }
        }
    }
}
