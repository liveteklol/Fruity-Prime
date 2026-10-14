#include "PlayerAi.hpp"

#include "../../NativeRuntime/System/Runtime.hpp"
#include "HalfturretEntity.hpp"
#include "../RoomEntity.hpp"
#include "../../Formats/CollisionDetection.hpp"
#include "../../GameState.hpp"
#include "../../Metadata/Metadata.hpp"
#include "../../Metadata/Weapons.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Encoding.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"
#include "../../NativeRuntime/System/Console.hpp"

#include <bit>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

using ::MphRead::NativeRuntime::HasFlag;

namespace MphRead::Entities
{
    std::int32_t PlayerEntity::PlayerAiData::_globalField0 = 0;
    std::int32_t PlayerEntity::PlayerAiData::_globalField2 = 0;
    std::vector<PlayerEntity::PlayerAiData::AiGlobals> PlayerEntity::PlayerAiData::_globalObjs(
        static_cast<std::size_t>(PlayerEntity::SlotCapacity));
    std::vector<std::vector<bool>> PlayerEntity::PlayerAiData::_playerVisibility(
        static_cast<std::size_t>(PlayerEntity::SlotCapacity),
        std::vector<bool>(static_cast<std::size_t>(PlayerEntity::SlotCapacity), false));
    std::uint8_t PlayerEntity::PlayerAiData::_visIndex1 = 0;
    std::uint8_t PlayerEntity::PlayerAiData::_visIndex2 = 0;

    const std::array<std::array<std::uint32_t, 8>, 4> PlayerEntity::PlayerAiData::_botLevelRandomValues1{{
        {{45,45,90,45,60,45,45,45}},
        {{15,15,10,10,10,10,10,10}},
        {{7,7,2,2,2,2,2,2}},
        {{1,1,1,1,1,1,1,1}}
    }};
    const std::array<std::uint32_t, 4> PlayerEntity::PlayerAiData::_botLevelRandomValues2{{150,45,10,1}};
    const std::array<float, 4> PlayerEntity::PlayerAiData::_dotValues{{255.0F / 256.0F, 3956.0F / 4096.0F, 3849.0F / 4096.0F, -1.0F}};
    const std::array<float, 4> PlayerEntity::PlayerAiData::_aimValues{{5.0F, 15.0F, 20.0F, 180.0F}};
    const std::array<std::int32_t, 3> PlayerEntity::PlayerAiData::_func4Ids{{1,2,3}};

    void PlayerEntity::PlayerAiData::Reset()
    {
        _nodeData.reset();
        _forceDisable = false;
        Flags1 = false;
        Flags2 = AiFlags2::None;
        Flags3 = AiFlags3::None;
        HealthThreshold = 0;
        DamageFromHalfturret = 0;
        Field118 = 0;
        std::fill(_slotHits.begin(), _slotHits.end(), 0);
        std::fill(_slotDamage.begin(), _slotDamage.end(), 0);
        _node3C.reset(); _node40.reset(); _node44.reset(); _node48.reset();
        _field4C.fill(nullptr);
        _field78 = 0;
        _field7A.fill(0);
        _targetPlayer.reset(); _targetHalfturret.reset(); _itemSpawnC4.reset(); _itemC8.reset();
        _octolithFlagCC.reset(); _octolithFlagD4.reset(); _octolithFlagDC.reset();
        _flagBaseD0.reset(); _flagBaseD8.reset(); _flagBaseE0.reset();
        _targetDefense.reset(); _targetDoor.reset();
        _nodeDataSetIndex = 0;
        _nodeList = nullptr;
        _nodeTypeIndex.fill(0);
        _field102C = 0; _field102E = 0; _field1030 = 0; _field116 = 0; _field1020 = 0; _field1032 = 0;
        _fieldA0 = {}; _fieldAC = {}; _fieldB8 = {}; _field1038 = {}; _field1048 = {}; _field1054 = {};
        _field30 = 0;
        Field118 = 0;
        _field90 = {};
        _field9C = 0.0F;
        _queuedFindEntityAction = AiQueuedEnt::None;
        _weapon1 = 0; _weapon2 = 0; _shotDelay = 0;
        for (auto& context : _executionTree)
        {
            if (!context) context = std::make_shared<AiContext>();
            else context->Clear();
        }
        _playerAggroCount = 0;
        for (AiPlayerAggro& aggro : _playerAggro) aggro.Clear();
    }

    void PlayerEntity::PlayerAiData::InitializeAtLoad()
    {
        InitializeMain();
        ClearInput();
        InitializeSub();
        UpdateExecutionPath(Personality, 0);
    }

    void PlayerEntity::PlayerAiData::InitializeAtSpawn()
    {
        InitializeMain();
        ClearInput();
        UpdateExecutionPath(Personality, 0);
        ApplyInstagibWeapons();
    }

    void PlayerEntity::PlayerAiData::InitializeGlobals()
    {
        _globalField0 = 0;
        _globalField2 = 0;
        for (AiGlobals& globals : _globalObjs)
        {
            globals.Player.reset();
            globals.Field4 = 0;
            globals.NodeDataIndex = 0;
            globals.NodeData = nullptr;
        }
        for (std::int32_t i = 0; i < PlayerEntity::SlotCapacity; ++i)
            for (std::int32_t j = 0; j < PlayerEntity::SlotCapacity; ++j)
                _playerVisibility[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = false;
        _visIndex1 = 1;
        _visIndex2 = 0;
    }

    void PlayerEntity::PlayerAiData::UpdateVisibilityAndGlobals(Scene& scene)
    {
        UpdateVisibility(scene);
        UpdateGlobals(scene);
    }

    void PlayerEntity::PlayerAiData::UpdateVisibility(Scene& scene)
    {
        _playerVisibility[_visIndex1][_visIndex2] = false;
        _playerVisibility[_visIndex2][_visIndex1] = false;
        std::shared_ptr<PlayerEntity> player1 = PlayerEntity::Players()[_visIndex1];
        std::shared_ptr<PlayerEntity> player2 = PlayerEntity::Players()[_visIndex2];
        if (player1->Health() != 0 && HasFlag(player1->LoadFlags(), LoadFlags::Active)
            && player2->Health() != 0 && HasFlag(player2->LoadFlags(), LoadFlags::Active)
            && (player1->IsBot() || player2->IsBot()))
        {
            Vector3 pos1 = player1->CameraInfo()->Position;
            Vector3 pos2 = player2->CameraInfo()->Position;
            if (VectorEqual(pos1, pos2)) { pos1 = player1->Position; pos2 = player2->Position; }
            Formats::CollisionResult discard{};
            if (!Formats::CollisionDetection::CheckBetweenPoints(pos1, pos2, Formats::TestFlags::None, &scene, discard))
            {
                _playerVisibility[_visIndex1][_visIndex2] = true;
                _playerVisibility[_visIndex2][_visIndex1] = true;
            }
        }
        std::int32_t slots = std::clamp(PlayerEntity::MaxPlayers(), 2, PlayerEntity::SlotCapacity);
        if (++_visIndex1 >= slots)
        {
            if (++_visIndex2 >= slots - 1) _visIndex2 = 0;
            _visIndex1 = static_cast<std::uint8_t>(_visIndex2 + 1);
        }
    }

    void PlayerEntity::PlayerAiData::UpdateGlobals(Scene& scene)
    {
        if (_globalField2 == 0) return;
        if (_globalField0 >= _globalField2) _globalField0 = 0;
        AiGlobals& global = _globalObjs[static_cast<std::size_t>(_globalField0)];
        std::shared_ptr<PlayerEntity> player = global.Player;
        std::shared_ptr<Formats::NodeData3> node = (*global.NodeData)[static_cast<std::size_t>(global.NodeDataIndex)];
        Vector3 pos1 = AddY(player->Position, player->IsAltForm() ? 0.5F : 1.0F);
        Vector3 pos2 = AddY(node->Position, 0.5F);
        Formats::CollisionResult discard{};
        if (Formats::CollisionDetection::CheckBetweenPoints(pos1, pos2, Formats::TestFlags::None, &scene, discard))
        {
            ++global.NodeDataIndex;
            --global.Field4;
            if (global.Field4 == 0)
            {
                player->AiData->Flags2 &= ~AiFlags2::Bit10;
                RemovePlayerFromGlobals(player);
            }
        }
        else
        {
            player->AiData->_node40 = node;
            player->AiData->_entityRefs.Field1 = node;
            player->AiData->Flags2 &= ~AiFlags2::Bit10;
            RemovePlayerFromGlobals(player);
        }
        ++_globalField0;
    }

    void PlayerEntity::PlayerAiData::ClearInput()
    {
        for (AiButton* button : _buttons.AllButtons) button->Clear();
        _touchAimX = 0; _touchAimY = 0; _hasTouch = false; _framesWithTouch = 0; _framesWithoutTouch = 0;
        _buttonAimX = 0.0F; _buttonAimY = 0.0F;
        for (AiButton* button : _touchButtons.AllButtons) button->Clear();
        _nodeDataSelOff = 0;
        _nodeDataSelOn = 0;
    }

    void PlayerEntity::PlayerAiData::InitializeSub()
    {
        if (GameState::SinglePlayer() && GameState::EncounterState()[_player->SlotIndex()] != 0)
        {
            _field102C = 0;
            _field1030 = 0;
        }
        else
        {
            std::int32_t index = std::clamp(_player->BotLevel(), 0, 3);
            _field102C = _botLevelRandomValues1[static_cast<std::size_t>(index)][static_cast<std::size_t>(_player->Hunter())] * 2;
            _field1030 = _botLevelRandomValues2[static_cast<std::size_t>(index)] * 2;
        }
    }

    void PlayerEntity::PlayerAiData::Process()
    {
        if (!_nodeData || _forceDisable) return;
        if ((Flags2 & AiFlags2::TargetItem) != AiFlags2::None && _itemC8 && _itemC8->DespawnTimer() == 0)
            Flags2 &= ~AiFlags2::TargetItem;
        Flags2 &= ~AiFlags2::Bit18;
        Flags2 &= ~AiFlags2::Bit19;
        Flags2 &= ~AiFlags2::Bit20;
        Flags4 &= ~AiFlags4::Bit2;
        Func2134594();
        Func2148ABC();
        Execute(*_executionTree[0]);
        std::fill(_slotHits.begin(), _slotHits.end(), 0);
        std::fill(_slotDamage.begin(), _slotDamage.end(), 0);
        DamageFromHalfturret = 0;
        Flags2 &= ~AiFlags2::AiStart;
        Flags2 &= ~AiFlags2::Bit16;
        Flags2 &= ~AiFlags2::Bit17;
        Flags2 &= ~AiFlags2::Bit21;
    }

    void PlayerEntity::PlayerAiData::Func2148ABC()
    {
        UpdateAggroExpiration();
        Flags4 &= ~AiFlags4::Bit0;
        if (Vector3::Dot(_field1038, _player->CameraInfo()->Facing) >= 255.0F / 256.0F) Flags4 |= AiFlags4::Bit0;
        if (_field1020 > 0) --_field1020;
    }

    void PlayerEntity::PlayerAiData::InitializeMain()
    {
        const std::shared_ptr<RoomEntity> room = _scene.Room();
        if (room == nullptr || room->NodeData() == nullptr) return;
        _nodeData = _scene.Room()->NodeData();
        SetClosestNodeList(_player->Position);
        if (GameState::Mode() == GameMode::Capture)
        {
            for (auto entityEnumerator = _scene.Entities().GetEnumerator(); entityEnumerator.MoveNext(); )
            if (const auto entity = entityEnumerator.Current(); true)
            {
                if (entity->Type == EntityType::OctolithFlag)
                {
                    auto octoFlag = std::static_pointer_cast<OctolithFlagEntity>(entity);
                    if (octoFlag->Data().TeamId == _player->TeamIndex()) _octolithFlagCC = _octolithFlagD4 = octoFlag;
                    else _octolithFlagDC = octoFlag;
                }
                else if (entity->Type == EntityType::FlagBase)
                {
                    auto flagBase = std::static_pointer_cast<FlagBaseEntity>(entity);
                    if (flagBase->Data().TeamId == _player->TeamIndex()) _flagBaseD0 = _flagBaseD8 = flagBase;
                    else _flagBaseE0 = flagBase;
                }
            }
            if (!_octolithFlagCC || !_octolithFlagD4 || !_octolithFlagDC) _forceDisable = true;
        }
        else if (GameState::Mode() == GameMode::Bounty || GameState::Mode() == GameMode::BountyTeams)
        {
            for (auto entityEnumerator = _scene.Entities().GetEnumerator(); entityEnumerator.MoveNext(); )
            if (const auto entity = entityEnumerator.Current(); true)
            {
                if (entity->Type == EntityType::OctolithFlag && !_octolithFlagCC)
                    _octolithFlagCC = _octolithFlagD4 = _octolithFlagDC = std::static_pointer_cast<OctolithFlagEntity>(entity);
                else if (entity->Type == EntityType::FlagBase && !_flagBaseD0)
                    _flagBaseD0 = _flagBaseD8 = _flagBaseE0 = std::static_pointer_cast<FlagBaseEntity>(entity);
            }
            if (!_octolithFlagCC || !_octolithFlagD4 || !_octolithFlagDC) _forceDisable = true;
        }
    }

    void PlayerEntity::PlayerAiData::ProcessInput()
    {
        if (!_nodeData) return;
        Flags3 |= AiFlags3::NoInput;
        for (AiButton* button : _buttons.AllButtons)
        {
            Keybind* control = nullptr;
            if (button == &_buttons.Up) control = _player->IsAltForm() ? &_player->Controls().RollUp() : &_player->Controls().AimUp();
            else if (button == &_buttons.Down) control = _player->IsAltForm() ? &_player->Controls().RollDown() : &_player->Controls().AimDown();
            else if (button == &_buttons.Left) control = _player->IsAltForm() ? &_player->Controls().RolltLeft() : &_player->Controls().AimLeft();
            else if (button == &_buttons.Right) control = _player->IsAltForm() ? &_player->Controls().RollRight() : &_player->Controls().AimRight();
            else if (button == &_buttons.A) control = _player->IsAltForm() && _player->Values().AltFormStrafe == 0 ? &_player->Controls().AimRight() : &_player->Controls().MoveRight();
            else if (button == &_buttons.B) control = _player->IsAltForm() && _player->Values().AltFormStrafe == 0 ? &_player->Controls().AimDown() : &_player->Controls().MoveDown();
            else if (button == &_buttons.X) control = _player->IsAltForm() && _player->Values().AltFormStrafe == 0 ? &_player->Controls().AimUp() : &_player->Controls().MoveUp();
            else if (button == &_buttons.Y) control = _player->IsAltForm() && _player->Values().AltFormStrafe == 0 ? &_player->Controls().AimLeft() : &_player->Controls().MoveLeft();
            else if (button == &_buttons.L) control = _player->IsAltForm() ? &_player->Controls().AltAttack() : &_player->Controls().Jump();
            else if (button == &_buttons.R) control = _player->IsAltForm() ? &_player->Controls().Boost() : &_player->Controls().Shoot();
            else if (button == &_buttons.Start) control = &_player->Controls().Pause();
            else if (button == &_buttons.Select) control = &_player->Controls().Zoom();
            else throw std::runtime_error("Unreachable AI button.");
            bool prevDown = control->IsDown();
            if (button->IsDown)
            {
                Flags3 &= ~AiFlags3::NoInput;
                button->FramesUp = 0;
                if (button->FramesDown < 6000) ++button->FramesDown;
                button->IsDown = false;
                control->SetIsDown(true);
                control->SetIsPressed(!prevDown);
                control->SetIsReleased(false);
            }
            else
            {
                button->FramesDown = 0;
                if (button->FramesUp < 6000) ++button->FramesUp;
                control->SetIsDown(false);
                control->SetIsPressed(false);
                control->SetIsReleased(prevDown);
            }
        }
        if (_hasTouch)
        {
            ::MphRead::NativeRuntime::DebuggerBreak();
            Flags3 &= ~AiFlags3::NoInput;
            _framesWithoutTouch = 0;
            if (_framesWithTouch < 6000) ++_framesWithTouch;
            _hasTouch = false;
        }
        else
        {
            _framesWithTouch = 0;
            if (_framesWithoutTouch < 6000) ++_framesWithoutTouch;
        }
        for (AiButton* button : _touchButtons.AllButtons)
        {
            if (button->IsDown)
            {
                Flags3 &= ~AiFlags3::NoInput;
                if (button == &_touchButtons.Morph || button == &_touchButtons.Unmorph) _player->TrySwitchForms();
                else if (button == &_touchButtons.PowerBeam) _player->TryEquipWeapon(BeamType::PowerBeam);
                else if (button == &_touchButtons.Missile) _player->TryEquipWeapon(BeamType::Missile);
                else if (button == &_touchButtons.VoltDriver) { _player->UpdateAffinityWeaponSlot(BeamType::VoltDriver); _player->TryEquipWeapon(BeamType::VoltDriver); }
                else if (button == &_touchButtons.Battlehammer) { _player->UpdateAffinityWeaponSlot(BeamType::Battlehammer); _player->TryEquipWeapon(BeamType::Battlehammer); }
                else if (button == &_touchButtons.Imperialist) { _player->UpdateAffinityWeaponSlot(BeamType::Imperialist); _player->TryEquipWeapon(BeamType::Imperialist); }
                else if (button == &_touchButtons.Judicator) { _player->UpdateAffinityWeaponSlot(BeamType::Judicator); _player->TryEquipWeapon(BeamType::Judicator); }
                else if (button == &_touchButtons.Magmaul) { _player->UpdateAffinityWeaponSlot(BeamType::Magmaul); _player->TryEquipWeapon(BeamType::Magmaul); }
                else if (button == &_touchButtons.ShockCoil) { _player->UpdateAffinityWeaponSlot(BeamType::ShockCoil); _player->TryEquipWeapon(BeamType::ShockCoil); }
                else if (button == &_touchButtons.OmegaCannon) { _player->UpdateAffinityWeaponSlot(BeamType::OmegaCannon); _player->TryEquipWeapon(BeamType::OmegaCannon); }
                button->FramesUp = 0;
                if (button->FramesDown < 6000) ++button->FramesDown;
                button->IsDown = false;
            }
            else
            {
                button->FramesDown = 0;
                if (button->FramesUp < 6000) ++button->FramesUp;
            }
        }
        _player->_buttonAimX = 0.0F;
        _player->_buttonAimY = 0.0F;
        if (_buttonAimX != 0.0F) { Flags3 &= ~AiFlags3::NoInput; _player->_buttonAimX = _buttonAimX; }
        if (_buttonAimY != 0.0F) { Flags3 &= ~AiFlags3::NoInput; _player->_buttonAimY = _buttonAimY; }
        _buttonAimX = 0.0F;
        _buttonAimY = 0.0F;
        UpdateNodeDataSetSelection();
        _nodeDataSelOff = 0;
        _nodeDataSelOn = 0;
    }

    void PlayerEntity::PlayerAiData::Func2134594()
    {
        _entityRefs.Clear();
        UpdateAggro();
        if (GameState::Mode() == GameMode::PrimeHunter && GameState::PrimeHunter() == _player->SlotIndex()
            && (Flags2 & AiFlags2::TargetItem) != AiFlags2::None && _itemC8
            && (_itemC8->ItemType() == ItemType::HealthSmall || _itemC8->ItemType() == ItemType::HealthMedium
                || _itemC8->ItemType() == ItemType::HealthBig))
            Flags2 &= ~AiFlags2::TargetItem;
    }

    bool PlayerEntity::PlayerAiData::IsPlayerVisible(const PlayerEntity& player, const PlayerEntity& other) const
    {
        return _playerVisibility[static_cast<std::size_t>(other.SlotIndex())][static_cast<std::size_t>(player.SlotIndex())];
    }

    void PlayerEntity::PlayerAiData::UpdateAggro()
    {
        float fov = DegreesToRadians(_player->CameraInfo()->Fov > 0 ? _player->CameraInfo()->Fov : 78.0F);
        OpenTK::Mathematics::Matrix4 perspectiveMatrix = _scene.GetPerspectiveMatrix(fov);
        for (auto _enumerator0 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator0.MoveNext(); )
        if (const auto other = _enumerator0.Current(); true)
        {
            if (other == _player || other->Health() == 0 || !IsPlayerVisible(*_player, *other)) continue;
            OpenTK::Mathematics::Vector2 proj{};
            float w = Matrix::ProjectPosition(other->Position, _player->CameraInfo()->ViewMatrix, perspectiveMatrix, proj);
            if (w < 0) return;
            if (proj.X >= 1 || proj.Y >= 1) continue;
            if (other->CurAlpha() >= 1 || HasFlag(other->Flags2(), PlayerFlags2::RadarReveal) || GameState::RadarPlayers()
                || other->OctolithFlag() != nullptr || GameState::PrimeHunter() == other->SlotIndex())
            {
                AggroFunc214864C(6, 1, 2, nullptr, other, 0, 30, 10, 3);
            }
            else
            {
                Vector3 between = other->Position - _player->CameraInfo()->Position;
                between = AddY(between, other->IsAltForm() ? Fixed::ToFloat(other->Values().AltColYPos) : 0.5F);
                std::int32_t rand = static_cast<std::int32_t>(LengthSquared(between) * 4096.0F);
                std::int32_t alpha = static_cast<std::int32_t>(other->CurAlpha() * 31.0F);
                if (alpha > 2) rand /= alpha * alpha * 853;
                else rand /= 2 * 2 * 853;
                std::int32_t div = 1;
                if (AggroFunc214857C(4, 2, 1, other, nullptr)) div = 4;
                if (Rng::GetRandomInt2((31 - alpha + rand) / div) != 0) return;
                alpha = alpha <= 2 ? 1 : alpha - 2;
                AggroFunc214864C(6, 1, 2, nullptr, other, 0, alpha, 10, 3);
            }
            float otherFov = DegreesToRadians(other->CameraInfo()->Fov > 0 ? other->CameraInfo()->Fov : 78.0F);
            OpenTK::Mathematics::Matrix4 otherPerspective = _scene.GetPerspectiveMatrix(otherFov);
            w = Matrix::ProjectPosition(_player->Position, other->CameraInfo()->ViewMatrix, otherPerspective, proj);
            if (w < 0) return;
            if (proj.X < 1 && proj.Y < 1) AggroFunc214864C(6, 2, 1, other, nullptr, 0, 30, 10, 3);
        }
    }

    void PlayerEntity::PlayerAiData::UpdateExecutionPath(
        const std::shared_ptr<Formats::AiPersonalityData1>& data1, std::int32_t depth)
    {
        assert(depth < _maxContextDepth);
        AiContext& context = *_executionTree[static_cast<std::size_t>(depth)];
        context.Data1 = data1;
        context.Depth = depth;
        context.CallCount = 0;
        std::fill_n(context.Weights.begin(), data1->Data1.size(), 0);
        ExecuteFuncs1(data1->Data3a);
        context.Func24Id = data1->Func24Id;
        context.Field10 = 0;
        _field1020 = 0;
        _node44.reset();
        Flags4 &= ~AiFlags4::Bit3;
        Flags2 &= ~AiFlags2::Bit10;
        RemovePlayerFromGlobals(_player);
        if (std::find(_func4Ids.begin(), _func4Ids.end(), context.Func24Id) != _func4Ids.end()
            && HasFlag(_player->Flags1(), PlayerFlags1::Grounded))
        {
            Flags2 &= ~AiFlags2::Bit7;
        }
        ExecuteFuncs4(context);
        if (!data1->Data1.empty() && depth < _maxContextDepth - 1)
            UpdateExecutionPath(data1->Data1[0], depth + 1);
    }

    void PlayerEntity::PlayerAiData::Execute(AiContext& context)
    {
        ExecuteFuncs1(context.Data1->Data3b);
        ExecuteFuncs2(context);
        if (context.Func24Id != 0 && HasFlag(_player->EquipWeapon().Flags, WeaponFlags::CanZoom)
            && _buttons.Select.FramesUp > 5 * 2
            && ((!_player->EquipInfo()->Zoomed && (Flags4 & AiFlags4::Bit2) != AiFlags4::None)
                || (_player->EquipInfo()->Zoomed && (Flags4 & AiFlags4::Bit2) == AiFlags4::None)))
            _buttons.Select.IsDown = true;
        if (_player->Hunter() == Hunter::Spire && !_player->IsAltForm()) _field116 = 0;
        if (context.CallCount < std::numeric_limits<std::int32_t>::max()) ++context.CallCount;
        if (!context.Data1->Data1.empty() && context.Depth < _maxContextDepth - 1)
        {
            Execute(*_executionTree[static_cast<std::size_t>(context.Depth + 1)]);
            std::int32_t newChildIndex = UpdatePathWeights(context);
            if (newChildIndex != -1)
            {
                assert(newChildIndex < static_cast<std::int32_t>(context.Data1->Data1.size()));
                UpdateExecutionPath(context.Data1->Data1[static_cast<std::size_t>(newChildIndex)], context.Depth + 1);
            }
        }
    }

    std::int32_t PlayerEntity::PlayerAiData::UpdatePathWeights(AiContext& context)
    {
        const std::shared_ptr<Formats::AiPersonalityData1>& nextData1 =
            _executionTree[static_cast<std::size_t>(context.Depth + 1)]->Data1;
        if (nextData1->Data2.empty()) return -1;
        std::int32_t result = -1;
        for (const auto& data2Value : nextData1->Data2)
        {
            const Formats::AiPersonalityData2& data2 = *data2Value;
            bool noUpdate = false;
            for (const auto& data4Value : data2.Data4)
            {
                const Formats::AiPersonalityData4& data4 = *data4Value;
                if (ExecuteFuncs3(context, data4.Func3Id, *data4.Parameters) == 0)
                {
                    noUpdate = true;
                    break;
                }
            }
            if (noUpdate) continue;
            std::int32_t weightIndex = data2.Data1SelectIndex;
            if (weightIndex >= 20) weightIndex = static_cast<std::int32_t>(context.Data1->Data1.size());
            if ((data2.Weight >= 100000 && data2.Func3Id != 210) || _scene.FrameCount() % 2 == 0)
            {
                context.Weights[static_cast<std::size_t>(weightIndex)] +=
                    ExecuteFuncs3(context, data2.Func3Id, *data2.Parameters) * data2.Weight;
                if (context.Weights[static_cast<std::size_t>(weightIndex)] >= 100000)
                {
                    result = data2.Data1SelectIndex;
                    context.Weights[static_cast<std::size_t>(weightIndex)] = 0;
                    break;
                }
            }
        }
        if (result < 20) return result;
        std::fill_n(context.Weights.begin(), context.Data1->Data1.size(), 0);
        return -1;
    }

    void PlayerEntity::PlayerAiData::ExecuteFuncs1(const std::vector<std::int32_t>& funcsIds)
    {
        for (std::int32_t id : funcsIds)
        {
            switch (id)
            {
            case 0: Func1_214A39C(); break;
            case 1: Func1_214A098(); break;
            case 2: Func1_2149D3C(); break;
            case 3: Func1_2149C98(); break;
            case 4: Func1_2149C80(); break;
            case 5: Func1_2149C68(); break;
            case 6: Func1_2149C50(); break;
            case 7: Func1_2149C38(); break;
            case 8: Func1_2149C20(); break;
            case 9: Func1_2149C08(); break;
            case 10: Func1_2149BF0(); break;
            case 11: Func1_2149BD8(); break;
            case 12: Func1_2149BC0(); break;
            case 13: Func1_2149BA8(); break;
            case 14: Func1_2149B98(); break;
            case 15: Func1_2149AD8(); break;
            case 16: Func1_2149AC8(); break;
            case 17: Func1_2149ABC(); break;
            case 18: Func1_2149AB0(); break;
            case 19: Func1_2149AA4(); break;
            case 20: Func1_2149A98(); break;
            case 21: Func1_2149A64(); break;
            case 22: Func1_2149824(); break;
            case 23: Func1_21497F0(); break;
            case 24: Func1_2149570(); break;
            case 25: Func1_21494FC(); break;
            case 26: Func1_2149488(); break;
            case 27: Func1_2149414(); break;
            case 28: Func1_21493A0(); break;
            case 29: Func1_214932C(); break;
            case 30: Func1_21495A4(); break;
            case 31: Func1_2149530(); break;
            case 32: Func1_21494BC(); break;
            case 33: Func1_2149448(); break;
            case 34: Func1_21493D4(); break;
            case 35: Func1_2149360(); break;
            case 36: Func1_21492EC(); break;
            case 37: Func1_21492DC(); break;
            case 38: Func1_21492CC(); break;
            case 39: Func1_21492BC(); break;
            case 40: Func1_21492AC(); break;
            case 41: Func1_214929C(); break;
            case 42: Func1_214928C(); break;
            case 43: Func1_214927C(); break;
            case 44: Func1_214926C(); break;
            case 45: Func1_214925C(); break;
            case 46: Func1_214924C(); break;
            case 47: Func1_214923C(); break;
            case 48: Func1_214922C(); break;
            case 49: Func1_214921C(); break;
            case 50: Func1_214920C(); break;
            case 51: Func1_21491FC(); break;
            case 52: Func1_21491E4(); break;
            case 53: Func1_21491CC(); break;
            case 54: Func1_21491B4(); break;
            case 55: Func1_214919C(); break;
            case 56: Func1_2149184(); break;
            case 57: Func1_214916C(); break;
            case 58: Func1_2149154(); break;
            case 59: Func1_214913C(); break;
            case 60: Func1_2149124(); break;
            case 61: Func1_214910C(); break;
            case 62: Func1_21490F4(); break;
            case 63: Func1_21490DC(); break;
            case 64: Func1_21490C4(); break;
            case 65: Func1_21490AC(); break;
            case 66: Func1_2149094(); break;
            case 67: Func1_2149088(); break;
            case 68: Func1_2149034(); break;
            case 69: Func1_2148F10(); break;
            case 70: Func1_2148EDC(); break;
            case 71: Func1_2148ECC(); break;
            case 72: Func1_2148EB8(); break;
            case 73: Func1_2148EA8(); break;
            case 74: Func1_2148E98(); break;
            case 75: Func1_2148E88(); break;
            case 76: Func1_2148E74(); break;
            case 77: Func1_2148E64(); break;
            case 78: Func1_2148E54(); break;
            case 79: Func1_2148DF8(); break;
            case 80: Func1_2148DE8(); break;
            case 81: Func1_2148D50(); break;
            case 82: Func1_UnlockEchoHallForceField(); break;
            case 83: Func1_SetInvulnerable(); break;
            default: throw std::runtime_error("Invalid AI func 1.");
            }
        }
        ApplyInstagibWeapons();
    }

    void PlayerEntity::PlayerAiData::ApplyInstagibWeapons()
    {
        // Instagib: the Imperialist is the only weapon a hunter holds. The
        // personality scripts are told it is the Power Beam: their Imperialist
        // branches are a sniper's (zoom, hold a vantage point), which left the
        // bots standing still, while a Power Beam bot keeps moving. The shot
        // itself is the Imperialist's (Func2143A40). No charge and no zoom, and
        // no weapon to go and find, since nothing on the map can be picked up.
        if (!InstagibRule())
        {
            return;
        }
        _weapon1 = 0;
        _weapon2 = 0;
        _findWeaponIndex = 4;
        Flags4 &= ~(AiFlags4::Bit1 | AiFlags4::Bit2);
    }

    void PlayerEntity::PlayerAiData::ExecuteFuncs2(AiContext& context)
    {
        const std::int32_t id = context.Func24Id;
        switch (id)
        {
        case 0: case 125: break;
        case 1: Func2_213EA10(context); break;
        case 45: Func2_213DDCC(context); break;
        case 47: Func2_213DA88(context); break;
        case 49: Func2_213E148(context); break;
        case 79: Func2_213E9C8(context); break;
        case 80: Func2_213E984(context); break;
        case 81: Func2_213E934(context); break;
        case 99: Func2_213E904(context); break;
        case 100: Func2_213E684(context); break;
        case 102: Func2_213E3C4(context); break;
        case 104: Func2_213E31C(context); break;
        case 105: Func2_213E274(context); break;
        case 114: Func2_213E1CC(context); break;
        case 123: Func2_213D9B8(context); break;
        case 124: Func2_213D96C(context); break;
        default:
            if ((id >= 2 && id <= 44) || id == 46 || id == 48 || (id >= 50 && id <= 78)
                || (id >= 82 && id <= 98) || id == 101 || id == 103 || (id >= 106 && id <= 113)
                || (id >= 115 && id <= 122))
            {
                Func2_213EA48(context);
                break;
            }
            throw std::runtime_error("Invalid AI func 2.");
        }
    }

    std::int32_t PlayerEntity::PlayerAiData::ExecuteFuncs3(AiContext& context, std::int32_t funcId,
        const Formats::AiPersonalityData5& param)
    {
        switch (funcId)
        {
        case 0: return Func3_213D87C(context, param);
        case 1: return Func3_213D83C(context, param);
        case 2: return Func3_213D814(context, param);
        case 3: return Func3_213D7F0(context, param);
        case 4: return Func3_213D800(context, param);
        case 5: return Func3_213D7E8(context, param);
        case 6: return Func3_213D7D0(context, param);
        case 7: return Func3_213D7B8(context, param);
        case 8: return Func3_213D7A0(context, param);
        case 9: return Func3_213D77C(context, param);
        case 10: return Func3_213D758(context, param);
        case 11: return Func3_213D734(context, param);
        case 12: return Func3_213D710(context, param);
        case 13: return Func3_213D6D0(context, param);
        case 14: return Func3_213D6AC(context, param);
        case 15: return Func3_213D624(context, param);
        case 16: return Func3_213D608(context, param);
        case 17: return Func3_213D564(context, param);
        case 18: return Func3_213D540(context, param);
        case 19: return Func3_213D530(context, param);
        case 20: return Func3_213D514(context, param);
        case 21: return Func3_213D4C0(context, param);
        case 22: return Func3_213D49C(context, param);
        case 23: return Func3_213D43C(context, param);
        case 24: return Func3_213D418(context, param);
        case 25: return Func3_213D388(context, param);
        case 26: return Func3_213D36C(context, param);
        case 27: return Func3_213D2C0(context, param);
        case 28: return Func3_213D2A4(context, param);
        case 29: return Func3_213D234(context, param);
        case 30: return Func3_213D218(context, param);
        case 31: return Func3_213D178(context, param);
        case 32: return Func3_213D15C(context, param);
        case 33: return Func3_213D128(context, param);
        case 34: return Func3_213D0F4(context, param);
        case 35: return Func3_213D0C4(context, param);
        case 36: return Func3_213D0A8(context, param);
        case 37: return Func3_213D078(context, param);
        case 38: return Func3_213D05C(context, param);
        case 39: return Func3_213D044(context, param);
        case 40: return Func3_213D028(context, param);
        case 41: return Func3_213D010(context, param);
        case 42: return Func3_213CFF4(context, param);
        case 43: return Func3_213CFDC(context, param);
        case 44: return Func3_213CFC0(context, param);
        case 45: return Func3_213CFA4(context, param);
        case 46: return Func3_213CF0C(context, param);
        case 47: return Func3_213CEE8(context, param);
        case 48: return Func3_213CDB8(context, param);
        case 49: return Func3_213CF94(context, param);
        case 50: return Func3_213CF7C(context, param);
        case 51: return Func3_213CDA4(context, param);
        case 52: return Func3_213CD74(context, param);
        case 53: return Func3_213CD58(context, param);
        case 54: return Func3_213CD34(context, param);
        case 55: return Func3_213CD18(context, param);
        case 56: return Func3_213CCF4(context, param);
        case 57: return Func3_213CCD8(context, param);
        case 58: return Func3_213CCBC(context, param);
        case 59: return Func3_213CCB0(context, param);
        case 60: return Func3_213CC94(context, param);
        case 61: return Func3_213CBE4(context, param);
        case 62: return Func3_213CBC0(context, param);
        case 63: return Func3_213CBB0(context, param);
        case 64: return Func3_213CB8C(context, param);
        case 65: return Func3_213CADC(context, param);
        case 66: return Func3_213CAA8(context, param);
        case 67: return Func3_213CA84(context, param);
        case 68: return Func3_213CA70(context, param);
        case 69: return Func3_213CA58(context, param);
        case 70: return Func3_213CA2C(context, param);
        case 71: return Func3_213CA00(context, param);
        case 72: return Func3_213C9D4(context, param);
        case 73: return Func3_213C9C4(context, param);
        case 74: return Func3_213C89C(context, param);
        case 75: return Func3_213C88C(context, param);
        case 76: return Func3_213C764(context, param);
        case 77: return Func3_213C75C(context, param);
        case 78: return Func3_213C698(context, param);
        case 79: return Func3_213C64C(context, param);
        case 80: return Func3_213C600(context, param);
        case 81: return Func3_213C52C(context, param);
        case 82: return Func3_213C48C(context, param);
        case 83: return Func3_213C470(context, param);
        case 84: return Func3_213C334(context, param);
        case 85: return Func3_213C310(context, param);
        case 86: return Func3_213C0D0(context, param);
        case 87: return Func3_213C078(context, param);
        case 88: return Func3_213C054(context, param);
        case 89: return Func3_213BFFC(context, param);
        case 90: return Func3_213BFD8(context, param);
        case 91: return Func3_213BED8(context, param);
        case 92: return Func3_213BEBC(context, param);
        case 93: return Func3_213BEA0(context, param);
        case 94: return Func3_213BE48(context, param);
        case 95: return Func3_213BE10(context, param);
        case 96: return Func3_213BDF4(context, param);
        case 97: return Func3_213BD7C(context, param);
        case 98: return Func3_213BCE8(context, param);
        case 99: return Func3_213BCC4(context, param);
        case 100: return Func3_213BCB0(context, param);
        case 101: return Func3_213BC8C(context, param);
        case 102: return Func3_213BC70(context, param);
        case 103: return Func3_213BC4C(context, param);
        case 104: return Func3_213BC0C(context, param);
        case 105: return Func3_213BBE8(context, param);
        case 106: return Func3_213BBA0(context, param);
        case 107: return Func3_213BB7C(context, param);
        case 108: return Func3_213BAF4(context, param);
        case 109: return Func3_213BAD0(context, param);
        case 110: return Func3_213BA68(context, param);
        case 111: return Func3_213BA44(context, param);
        case 112: return Func3_213BA28(context, param);
        case 113: return Func3_213BA04(context, param);
        case 114: return Func3_213B99C(context, param);
        case 115: return Func3_213B978(context, param);
        case 116: return Func3_213B8B0(context, param);
        case 117: return Func3_213B88C(context, param);
        case 118: return Func3_213B7A0(context, param);
        case 119: return Func3_213B77C(context, param);
        case 120: return Func3_213B690(context, param);
        case 121: return Func3_213B5DC(context, param);
        case 122: return Func3_213B528(context, param);
        case 123: return Func3_213B4E4(context, param);
        case 124: return Func3_213B4A0(context, param);
        case 125: return Func3_213B45C(context, param);
        case 126: return Func3_213B3F0(context, param);
        case 127: return Func3_213B3A0(context, param);
        case 128: return Func3_213B37C(context, param);
        case 129: return Func3_213B34C(context, param);
        case 130: return Func3_213B328(context, param);
        case 131: return Func3_213B284(context, param);
        case 132: return Func3_213B260(context, param);
        case 133: return Func3_213B1F0(context, param);
        case 134: return Func3_213B1D8(context, param);
        case 135: return Func3_213B1C0(context, param);
        case 136: return Func3_213B1A8(context, param);
        case 137: return Func3_213B190(context, param);
        case 138: return Func3_213B178(context, param);
        case 139: return Func3_213B160(context, param);
        case 140: return Func3_213B148(context, param);
        case 141: return Func3_213B130(context, param);
        case 142: return Func3_213B118(context, param);
        case 143: return Func3_213B100(context, param);
        case 144: return Func3_213B0E8(context, param);
        case 145: return Func3_213B0D0(context, param);
        case 146: return Func3_213B0B8(context, param);
        case 147: return Func3_213B0A0(context, param);
        case 148: return Func3_213B088(context, param);
        case 149: return Func3_213B070(context, param);
        case 150: return Func3_213B058(context, param);
        case 151: return Func3_213B040(context, param);
        case 152: return Func3_213B020(context, param);
        case 153: return Func3_213B000(context, param);
        case 154: return Func3_213AFE0(context, param);
        case 155: return Func3_213AFC0(context, param);
        case 156: return Func3_213AFA0(context, param);
        case 157: return Func3_213AF80(context, param);
        case 158: return Func3_213AF68(context, param);
        case 159: return Func3_213AF50(context, param);
        case 160: return Func3_213AF38(context, param);
        case 161: return Func3_213AF20(context, param);
        case 162: return Func3_213AF08(context, param);
        case 163: return Func3_213AEF0(context, param);
        case 164: return Func3_213AED8(context, param);
        case 165: return Func3_213AEC0(context, param);
        case 166: return Func3_213AEA8(context, param);
        case 167: return Func3_213AE90(context, param);
        case 168: return Func3_213AE78(context, param);
        case 169: return Func3_213AE60(context, param);
        case 170: return Func3_213AE48(context, param);
        case 171: return Func3_213AE30(context, param);
        case 172: return Func3_213AE14(context, param);
        case 173: return Func3_213ADF8(context, param);
        case 174: return Func3_213ADC4(context, param);
        case 175: return Func3_213ADA0(context, param);
        case 176: return Func3_213AD88(context, param);
        case 177: return Func3_213AD64(context, param);
        case 178: return Func3_213ACE8(context, param);
        case 179: return Func3_213ACCC(context, param);
        case 180: return Func3_213ACA8(context, param);
        case 181: return Func3_213AC8C(context, param);
        case 182: return Func3_213AC70(context, param);
        case 183: return Func3_213AC54(context, param);
        case 184: return Func3_213AC38(context, param);
        case 185: return Func3_213AC04(context, param);
        case 186: return Func3_213ABC0(context, param);
        case 187: return Func3_213AB8C(context, param);
        case 188: return Func3_213AB58(context, param);
        case 189: return Func3_213AB24(context, param);
        case 190: return Func3_213AAF0(context, param);
        case 191: return Func3_213AA64(context, param);
        case 192: return Func3_213AA20(context, param);
        case 193: return Func3_213A9B8(context, param);
        case 194: return Func3_213A94C(context, param);
        case 195: return Func3_213A938(context, param);
        case 196: return Func3_213A91C(context, param);
        case 197: return Func3_213A900(context, param);
        case 198: return Func3_213A8DC(context, param);
        case 199: return Func3_213A8A8(context, param);
        case 200: return Func3_213A884(context, param);
        case 201: return Func3_213A868(context, param);
        case 202: return Func3_213A844(context, param);
        case 203: return Func3_213A828(context, param);
        case 204: return Func3_213A804(context, param);
        case 205: return Func3_213A798(context, param);
        case 206: return Func3_213A72C(context, param);
        case 207: return Func3_213A714(context, param);
        case 208: return Func3_213A698(context, param);
        case 209: return Func3_213A688(context, param);
        case 210: return Func3_213A660(context, param);
        case 211: return Func3_213A650(context, param);
        default: throw std::runtime_error("Invalid AI func 3.");
        }
    }

    void PlayerEntity::PlayerAiData::ExecuteFuncs4(AiContext& context)
    {
        const std::int32_t id = context.Func24Id;
        switch (id)
        {
        case 0: case 1: case 47: case 49: case 99: break;
        case 45: Func4_2145EB0(context); break;
        case 79: Func4_21462AC(context); break;
        case 80: Func4_2146284(context); break;
        case 81: Func4_21461EC(context); break;
        case 100: Func4_214612C(context); break;
        case 102: Func4_2145F78(context); break;
        case 104: Func4_2145F50(context); break;
        case 105: Func4_2145F28(context); break;
        case 114: Func4_2145F00(context); break;
        case 123: Func4_2145E54(context); break;
        case 124: Func4_2145E40(context); break;
        case 125: Func4_SetDespawned(context); break;
        default:
            if ((id >= 2 && id <= 44) || id == 46 || id == 48 || (id >= 50 && id <= 78)
                || (id >= 82 && id <= 98) || id == 101 || id == 103 || (id >= 106 && id <= 113)
                || (id >= 115 && id <= 122))
            {
                Func4_21462DC(context);
                break;
            }
            throw std::runtime_error("Invalid AI func 4.");
        }
    }

    void PlayerEntity::PlayerAiData::OnTakeDamage(std::int32_t damage, EntityBase& source,
        const std::shared_ptr<PlayerEntity>& attacker)
    {
        for (auto _enumerator1 = _scene.GetPlayerEntities().GetEnumerator(); _enumerator1.MoveNext(); )
        if (const auto player = _enumerator1.Current(); true)
        {
            if (!player->IsBot())
            {
                continue;
            }
            ++player->AiData->_slotHits[static_cast<std::size_t>(_player->SlotIndex())];
            player->AiData->_slotDamage[static_cast<std::size_t>(_player->SlotIndex())] += damage;
            if (attacker)
            {
                if (player == _player)
                {
                    if (source.Type == EntityType::BeamProjectile
                        && attacker->Hunter() == Hunter::Weavel && attacker->IsAltForm())
                    {
                        AggroFunc214864C(5, 2, 1, attacker, nullptr, damage, damage, 2, 2);
                    }
                    else
                    {
                        AggroFunc214864C(4, 2, 1, attacker, nullptr, damage, damage, 2, 2);
                        if (source.Type == EntityType::BeamProjectile
                            && static_cast<BeamProjectileEntity&>(source).Beam() == BeamType::ShockCoil
                            && attacker->ShockCoilTimer() > 10 * 2)
                        {
                            player->AiData->Flags2 |= AiFlags2::Bit21;
                        }
                    }
                }
                else if (source.Type == EntityType::BeamProjectile
                    && attacker->Hunter() == Hunter::Weavel && attacker->IsAltForm())
                {
                    AggroFunc214864C(5, 2, 2, attacker, _player,
                        damage, damage, 2, 2);
                }
                else
                {
                    AggroFunc214864C(4, 2, 2, attacker, _player,
                        damage, damage, 2, 2);
                }
            }
        }
    }

    void PlayerEntity::PlayerAiData::GetOuptut(std::string& sb)
    {
        std::ostringstream out;
        for (std::size_t i = 0; i < _executionTree.size(); ++i)
        {
            const std::shared_ptr<AiContext>& item = _executionTree[i];
            if (i == 0)
            {
                out << '\n';
                for (std::size_t j = 0; j < _executionTree.size(); ++j)
                {
                    const std::shared_ptr<AiContext>& node = _executionTree[j];
                    if (j != 0)
                    {
                        out << " -> ";
                    }
                    out << node->Data1->Label;
                    if (node->Data1->Data1.empty())
                    {
                        break;
                    }
                }
                out << '\n';
            }
            else
            {
                out << item->Data1->Label << '\n';
                assert(item->Data1->Parent);
                const auto& level = item->Data1->Parent->Data1;
                if (level.size() > 1)
                {
                    for (const std::shared_ptr<Formats::AiPersonalityData2>& data2 : item->Data1->Data2)
                    {
                        std::int32_t index = data2->Data1SelectIndex;
                        std::string target;
                        if (index >= 20)
                        {
                            index = static_cast<std::int32_t>(level.size());
                            target = "reset";
                        }
                        else
                        {
                            target = level[static_cast<std::size_t>(index)]->Label;
                        }
                        std::int32_t weight = _executionTree[i - 1]->Weights[static_cast<std::size_t>(index)];
                        float pct = weight / 100000.0F * 100.0F;
                        out << "w: " << NativeRuntime::StringPadLeft(NativeRuntime::ToString(weight), 6) << " / 100000 ("
                            << NativeRuntime::StringPadLeft(NativeRuntime::ToString(pct, "f1"), 5) << "%) -> " << target
                            << NativeRuntime::EnvironmentNewLine();
                        if (data2->Func3Id >= 70 && data2->Func3Id <= 72)
                        {
                            std::int32_t param1 = data2->Parameters->Param1 * 2;
                            const std::string paramText = NativeRuntime::ToString(param1);
                            const std::string calls = NativeRuntime::StringPadLeft(
                                NativeRuntime::ToString(item->CallCount), NativeRuntime::Utf16Length(paramText));
                            float callPct = item->CallCount / static_cast<float>(param1) * 100.0F;
                            out << "c: " << calls << " / " << paramText << " ("
                                << NativeRuntime::StringPadLeft(NativeRuntime::ToString(callPct, "f1"), 5) << "%) -> " << target
                                << NativeRuntime::EnvironmentNewLine();
                        }
                    }
                }
            }
            if (item->Data1->Data1.empty())
            {
                break;
            }
            out << '\n';
        }
        sb += out.str();
    }

    std::vector<std::string> PlayerEntity::PlayerAiData::GetFuncs1Names(const std::vector<std::int32_t>& ids)
    {
        std::vector<std::string> result;
        result.reserve(ids.size());
        for (std::int32_t id : ids)
        {
            result.push_back(GetFuncs1Name(id));
        }
        return result;
    }

    std::string PlayerEntity::PlayerAiData::GetFuncs1Name(std::int32_t id)
    {
        switch (id)
        {
        case 0: return "Func1_214A39C";
        case 1: return "Func1_214A098";
        case 2: return "Func1_2149D3C";
        case 3: return "Func1_2149C98";
        case 4: return "Func1_2149C80";
        case 5: return "Func1_2149C68";
        case 6: return "Func1_2149C50";
        case 7: return "Func1_2149C38";
        case 8: return "Func1_2149C20";
        case 9: return "Func1_2149C08";
        case 10: return "Func1_2149BF0";
        case 11: return "Func1_2149BD8";
        case 12: return "Func1_2149BC0";
        case 13: return "Func1_2149BA8";
        case 14: return "Func1_2149B98";
        case 15: return "Func1_2149AD8";
        case 16: return "Func1_2149AC8";
        case 17: return "Func1_2149ABC";
        case 18: return "Func1_2149AB0";
        case 19: return "Func1_2149AA4";
        case 20: return "Func1_2149A98";
        case 21: return "Func1_2149A64";
        case 22: return "Func1_2149824";
        case 23: return "Func1_21497F0";
        case 24: return "Func1_2149570";
        case 25: return "Func1_21494FC";
        case 26: return "Func1_2149488";
        case 27: return "Func1_2149414";
        case 28: return "Func1_21493A0";
        case 29: return "Func1_214932C";
        case 30: return "Func1_21495A4";
        case 31: return "Func1_2149530";
        case 32: return "Func1_21494BC";
        case 33: return "Func1_2149448";
        case 34: return "Func1_21493D4";
        case 35: return "Func1_2149360";
        case 36: return "Func1_21492EC";
        case 37: return "Func1_21492DC";
        case 38: return "Func1_21492CC";
        case 39: return "Func1_21492BC";
        case 40: return "Func1_21492AC";
        case 41: return "Func1_214929C";
        case 42: return "Func1_214928C";
        case 43: return "Func1_214927C";
        case 44: return "Func1_214926C";
        case 45: return "Func1_214925C";
        case 46: return "Func1_214924C";
        case 47: return "Func1_214923C";
        case 48: return "Func1_214922C";
        case 49: return "Func1_214921C";
        case 50: return "Func1_214920C";
        case 51: return "Func1_21491FC";
        case 52: return "Func1_21491E4";
        case 53: return "Func1_21491CC";
        case 54: return "Func1_21491B4";
        case 55: return "Func1_214919C";
        case 56: return "Func1_2149184";
        case 57: return "Func1_214916C";
        case 58: return "Func1_2149154";
        case 59: return "Func1_214913C";
        case 60: return "Func1_2149124";
        case 61: return "Func1_214910C";
        case 62: return "Func1_21490F4";
        case 63: return "Func1_21490DC";
        case 64: return "Func1_21490C4";
        case 65: return "Func1_21490AC";
        case 66: return "Func1_2149094";
        case 67: return "Func1_2149088";
        case 68: return "Func1_2149034";
        case 69: return "Func1_2148F10";
        case 70: return "Func1_2148EDC";
        case 71: return "Func1_2148ECC";
        case 72: return "Func1_2148EB8";
        case 73: return "Func1_2148EA8";
        case 74: return "Func1_2148E98";
        case 75: return "Func1_2148E88";
        case 76: return "Func1_2148E74";
        case 77: return "Func1_2148E64";
        case 78: return "Func1_2148E54";
        case 79: return "Func1_2148DF8";
        case 80: return "Func1_2148DE8";
        case 81: return "Func1_2148D50";
        case 82: return "Func1_UnlockEchoHallForceField";
        case 83: return "Func1_SetInvulnerable";
        default: throw std::runtime_error("Invalid AI func 1.");
        }
    }

    std::vector<std::string> PlayerEntity::PlayerAiData::GetFuncs3Names(const std::vector<std::int32_t>& ids)
    {
        std::vector<std::string> result;
        result.reserve(ids.size());
        for (std::int32_t id : ids)
        {
            result.push_back(GetFuncs3Name(id));
        }
        return result;
    }

    std::string PlayerEntity::PlayerAiData::GetFuncs3Name(std::int32_t id)
    {
        switch (id)
        {
        case 0: return "Func3_213D87C";
        case 1: return "Func3_213D83C";
        case 2: return "Func3_213D814";
        case 3: return "Func3_213D7F0";
        case 4: return "Func3_213D800";
        case 5: return "Func3_213D7E8";
        case 6: return "Func3_213D7D0";
        case 7: return "Func3_213D7B8";
        case 8: return "Func3_213D7A0";
        case 9: return "Func3_213D77C";
        case 10: return "Func3_213D758";
        case 11: return "Func3_213D734";
        case 12: return "Func3_213D710";
        case 13: return "Func3_213D6D0";
        case 14: return "Func3_213D6AC";
        case 15: return "Func3_213D624";
        case 16: return "Func3_213D608";
        case 17: return "Func3_213D564";
        case 18: return "Func3_213D540";
        case 19: return "Func3_213D530";
        case 20: return "Func3_213D514";
        case 21: return "Func3_213D4C0";
        case 22: return "Func3_213D49C";
        case 23: return "Func3_213D43C";
        case 24: return "Func3_213D418";
        case 25: return "Func3_213D388";
        case 26: return "Func3_213D36C";
        case 27: return "Func3_213D2C0";
        case 28: return "Func3_213D2A4";
        case 29: return "Func3_213D234";
        case 30: return "Func3_213D218";
        case 31: return "Func3_213D178";
        case 32: return "Func3_213D15C";
        case 33: return "Func3_213D128";
        case 34: return "Func3_213D0F4";
        case 35: return "Func3_213D0C4";
        case 36: return "Func3_213D0A8";
        case 37: return "Func3_213D078";
        case 38: return "Func3_213D05C";
        case 39: return "Func3_213D044";
        case 40: return "Func3_213D028";
        case 41: return "Func3_213D010";
        case 42: return "Func3_213CFF4";
        case 43: return "Func3_213CFDC";
        case 44: return "Func3_213CFC0";
        case 45: return "Func3_213CFA4";
        case 46: return "Func3_213CF0C";
        case 47: return "Func3_213CEE8";
        case 48: return "Func3_213CDB8";
        case 49: return "Func3_213CF94";
        case 50: return "Func3_213CF7C";
        case 51: return "Func3_213CDA4";
        case 52: return "Func3_213CD74";
        case 53: return "Func3_213CD58";
        case 54: return "Func3_213CD34";
        case 55: return "Func3_213CD18";
        case 56: return "Func3_213CCF4";
        case 57: return "Func3_213CCD8";
        case 58: return "Func3_213CCBC";
        case 59: return "Func3_213CCB0";
        case 60: return "Func3_213CC94";
        case 61: return "Func3_213CBE4";
        case 62: return "Func3_213CBC0";
        case 63: return "Func3_213CBB0";
        case 64: return "Func3_213CB8C";
        case 65: return "Func3_213CADC";
        case 66: return "Func3_213CAA8";
        case 67: return "Func3_213CA84";
        case 68: return "Func3_213CA70";
        case 69: return "Func3_213CA58";
        case 70: return "Func3_213CA2C";
        case 71: return "Func3_213CA00";
        case 72: return "Func3_213C9D4";
        case 73: return "Func3_213C9C4";
        case 74: return "Func3_213C89C";
        case 75: return "Func3_213C88C";
        case 76: return "Func3_213C764";
        case 77: return "Func3_213C75C";
        case 78: return "Func3_213C698";
        case 79: return "Func3_213C64C";
        case 80: return "Func3_213C600";
        case 81: return "Func3_213C52C";
        case 82: return "Func3_213C48C";
        case 83: return "Func3_213C470";
        case 84: return "Func3_213C334";
        case 85: return "Func3_213C310";
        case 86: return "Func3_213C0D0";
        case 87: return "Func3_213C078";
        case 88: return "Func3_213C054";
        case 89: return "Func3_213BFFC";
        case 90: return "Func3_213BFD8";
        case 91: return "Func3_213BED8";
        case 92: return "Func3_213BEBC";
        case 93: return "Func3_213BEA0";
        case 94: return "Func3_213BE48";
        case 95: return "Func3_213BE10";
        case 96: return "Func3_213BDF4";
        case 97: return "Func3_213BD7C";
        case 98: return "Func3_213BCE8";
        case 99: return "Func3_213BCC4";
        case 100: return "Func3_213BCB0";
        case 101: return "Func3_213BC8C";
        case 102: return "Func3_213BC70";
        case 103: return "Func3_213BC4C";
        case 104: return "Func3_213BC0C";
        case 105: return "Func3_213BBE8";
        case 106: return "Func3_213BBA0";
        case 107: return "Func3_213BB7C";
        case 108: return "Func3_213BAF4";
        case 109: return "Func3_213BAD0";
        case 110: return "Func3_213BA68";
        case 111: return "Func3_213BA44";
        case 112: return "Func3_213BA28";
        case 113: return "Func3_213BA04";
        case 114: return "Func3_213B99C";
        case 115: return "Func3_213B978";
        case 116: return "Func3_213B8B0";
        case 117: return "Func3_213B88C";
        case 118: return "Func3_213B7A0";
        case 119: return "Func3_213B77C";
        case 120: return "Func3_213B690";
        case 121: return "Func3_213B5DC";
        case 122: return "Func3_213B528";
        case 123: return "Func3_213B4E4";
        case 124: return "Func3_213B4A0";
        case 125: return "Func3_213B45C";
        case 126: return "Func3_213B3F0";
        case 127: return "Func3_213B3A0";
        case 128: return "Func3_213B37C";
        case 129: return "Func3_213B34C";
        case 130: return "Func3_213B328";
        case 131: return "Func3_213B284";
        case 132: return "Func3_213B260";
        case 133: return "Func3_213B1F0";
        case 134: return "Func3_213B1D8";
        case 135: return "Func3_213B1C0";
        case 136: return "Func3_213B1A8";
        case 137: return "Func3_213B190";
        case 138: return "Func3_213B178";
        case 139: return "Func3_213B160";
        case 140: return "Func3_213B148";
        case 141: return "Func3_213B130";
        case 142: return "Func3_213B118";
        case 143: return "Func3_213B100";
        case 144: return "Func3_213B0E8";
        case 145: return "Func3_213B0D0";
        case 146: return "Func3_213B0B8";
        case 147: return "Func3_213B0A0";
        case 148: return "Func3_213B088";
        case 149: return "Func3_213B070";
        case 150: return "Func3_213B058";
        case 151: return "Func3_213B040";
        case 152: return "Func3_213B020";
        case 153: return "Func3_213B000";
        case 154: return "Func3_213AFE0";
        case 155: return "Func3_213AFC0";
        case 156: return "Func3_213AFA0";
        case 157: return "Func3_213AF80";
        case 158: return "Func3_213AF68";
        case 159: return "Func3_213AF50";
        case 160: return "Func3_213AF38";
        case 161: return "Func3_213AF20";
        case 162: return "Func3_213AF08";
        case 163: return "Func3_213AEF0";
        case 164: return "Func3_213AED8";
        case 165: return "Func3_213AEC0";
        case 166: return "Func3_213AEA8";
        case 167: return "Func3_213AE90";
        case 168: return "Func3_213AE78";
        case 169: return "Func3_213AE60";
        case 170: return "Func3_213AE48";
        case 171: return "Func3_213AE30";
        case 172: return "Func3_213AE14";
        case 173: return "Func3_213ADF8";
        case 174: return "Func3_213ADC4";
        case 175: return "Func3_213ADA0";
        case 176: return "Func3_213AD88";
        case 177: return "Func3_213AD64";
        case 178: return "Func3_213ACE8";
        case 179: return "Func3_213ACCC";
        case 180: return "Func3_213ACA8";
        case 181: return "Func3_213AC8C";
        case 182: return "Func3_213AC70";
        case 183: return "Func3_213AC54";
        case 184: return "Func3_213AC38";
        case 185: return "Func3_213AC04";
        case 186: return "Func3_213ABC0";
        case 187: return "Func3_213AB8C";
        case 188: return "Func3_213AB58";
        case 189: return "Func3_213AB24";
        case 190: return "Func3_213AAF0";
        case 191: return "Func3_213AA64";
        case 192: return "Func3_213AA20";
        case 193: return "Func3_213A9B8";
        case 194: return "Func3_213A94C";
        case 195: return "Func3_213A938";
        case 196: return "Func3_213A91C";
        case 197: return "Func3_213A900";
        case 198: return "Func3_213A8DC";
        case 199: return "Func3_213A8A8";
        case 200: return "Func3_213A884";
        case 201: return "Func3_213A868";
        case 202: return "Func3_213A844";
        case 203: return "Func3_213A828";
        case 204: return "Func3_213A804";
        case 205: return "Func3_213A798";
        case 206: return "Func3_213A72C";
        case 207: return "Func3_213A714";
        case 208: return "Func3_213A698";
        case 209: return "Func3_213A688";
        case 210: return "Func3_213A660";
        case 211: return "Func3_213A650";
        default: throw std::runtime_error("Invalid AI func 3.");
        }
    }

    std::string PlayerEntity::PlayerAiData::GetFuncs4Name(std::int32_t id)
    {
        if (id == 0 || id == 1 || id == 47 || id == 49 || id == 99) return "empty";
        if ((id >= 2 && id <= 44) || id == 46 || id == 48 || (id >= 50 && id <= 78)
            || (id >= 82 && id <= 98) || id == 101 || id == 103 || (id >= 106 && id <= 113)
            || (id >= 115 && id <= 122)) return "Func4_21462DC*";
        switch (id)
        {
        case 45: return "Func4_2145EB0";
        case 79: return "Func4_21462AC";
        case 80: return "Func4_2146284";
        case 81: return "Func4_21461EC";
        case 100: return "Func4_214612C";
        case 102: return "Func4_2145F78";
        case 104: return "Func4_2145F50";
        case 105: return "Func4_2145F28";
        case 114: return "Func4_2145F00";
        case 123: return "Func4_2145E54";
        case 124: return "Func4_2145E40";
        case 125: return "Func4_SetDespawned";
        default: throw std::runtime_error("Invalid AI func 4.");
        }
    }

    std::string PlayerEntity::PlayerAiData::GetFuncs2Name(std::int32_t id)
    {
        if (id == 0 || id == 125) return "empty";
        if ((id >= 2 && id <= 44) || id == 46 || id == 48 || (id >= 50 && id <= 78)
            || (id >= 82 && id <= 98) || id == 101 || id == 103 || (id >= 106 && id <= 113)
            || (id >= 115 && id <= 122)) return "Func2_213EA48*";
        switch (id)
        {
        case 1: return "Func2_213EA10";
        case 45: return "Func2_213DDCC";
        case 47: return "Func2_213DA88";
        case 49: return "Func2_213E148";
        case 79: return "Func2_213E9C8";
        case 80: return "Func2_213E984";
        case 81: return "Func2_213E934";
        case 99: return "Func2_213E904";
        case 100: return "Func2_213E684";
        case 102: return "Func2_213E3C4";
        case 104: return "Func2_213E31C";
        case 105: return "Func2_213E274";
        case 114: return "Func2_213E1CC";
        case 123: return "Func2_213D9B8";
        case 124: return "Func2_213D96C";
        default: throw std::runtime_error("Invalid AI func 2.");
        }
    }
}
