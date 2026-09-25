// PlayerAiData: state, input, aggro and the behavior tree's execution.
#include "PlayerAi.h"

#include "PlayerAiUtil.h"
#include "World.h"

#include <QtGlobal>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace fp {

using namespace ai;

namespace {

constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

// The controls a bot's buttons stand for (Keybind), to tell pressed from held.
enum Control { MoveUp, MoveDown, MoveLeft, MoveRight, AimUp, AimDown, AimLeft, AimRight, RollUp, RollDown, RollLeft, RollRight,
    Jump, AltAttack, Boost, Shoot, Pause, Zoom, ControlCount };

} // namespace

PlayerAi::PlayerAi(Player& player, World& world)
    : _player(&player)
    , _world(world)
{
    Reset();
}

void PlayerAi::Reset()
{
    _nodeData = nullptr;
    _forceDisable = false;
    Flags1 = false;
    Flags2 = 0;
    Flags3 = 0;
    HealthThreshold = 0;
    DamageFromHalfturret = 0;
    Field118 = 0;
    _slotHits.fill(0);
    _slotDamage.fill(0);
    _node3C = _node40 = _node44 = _node48 = nullptr;
    _field4C.fill(nullptr);
    _field78 = 0;
    _field7A.fill(0);
    _targetPlayer = nullptr;
    _targetHalfturret = nullptr;
    _itemSpawnC4 = nullptr;
    _itemC8 = nullptr;
    _octolithFlagCC = _octolithFlagD4 = _octolithFlagDC = nullptr;
    _flagBaseD0 = _flagBaseD8 = _flagBaseE0 = nullptr;
    _targetDefense = nullptr;
    _targetDoor = nullptr;
    _nodeDataSetIndex = 0;
    _nodeList = nullptr;
    _nodeTypeIndex.fill(0);
    _field102C = _field102E = _field1030 = 0;
    _field116 = 0;
    _field1020 = 0;
    _field1032 = 0;
    _fieldA0 = _fieldAC = _fieldB8 = _field1038 = _field1048 = _field1054 = {};
    _field30 = 0;
    _field90 = {};
    _field9C = 0;
    _queuedFindEntityAction = AiQueuedEntNone;
    _weapon1 = 0;
    _weapon2 = 0;
    _shotDelay = 0;
    for (AiContext& context : _executionTree) {
        context.Clear();
    }
    _playerAggroCount = 0;
    for (AiPlayerAggro& aggro : _playerAggro) {
        aggro.Clear();
    }
}

void PlayerAi::InitializeAtLoad()
{
    InitializeMain();
    ClearInput();
    InitializeSub();
    if (Personality != nullptr) {
        UpdateExecutionPath(Personality, 0);
    }
}

void PlayerAi::InitializeAtSpawn()
{
    InitializeMain();
    ClearInput();
    if (Personality != nullptr) {
        UpdateExecutionPath(Personality, 0);
    }
}

// ---- match-wide state ---------------------------------------------------------

void PlayerAi::InitializeGlobals(AiGlobalState& state)
{
    state = AiGlobalState{};
    state.visIndex1 = 1;
    state.visIndex2 = 0;
}

void PlayerAi::UpdateVisibilityAndGlobals(World& world)
{
    UpdateVisibility(world);
    UpdateGlobals(world);
}

void PlayerAi::UpdateVisibility(World& world)
{
    // One pair of players a tick: whether they can see each other.
    AiGlobalState& g = world.m_aiGlobals;
    const int slots = std::clamp(static_cast<int>(world.m_slots.size()), 2, 16);
    g.playerVisibility[g.visIndex1][g.visIndex2] = false;
    g.playerVisibility[g.visIndex2][g.visIndex1] = false;
    if (g.visIndex1 < static_cast<int>(world.m_slots.size()) && g.visIndex2 < static_cast<int>(world.m_slots.size())) {
        Player* player1 = world.m_slots[g.visIndex1].player.get();
        Player* player2 = world.m_slots[g.visIndex2].player.get();
        if (player1->m_health != 0 && player2->m_health != 0 && (player1->m_isBot || player2->m_isBot)) {
            Vec3 pos1 = player1->m_cam.position;
            Vec3 pos2 = player2->m_cam.position;
            if (pos1 == pos2) {
                pos1 = player1->m_position;
                pos2 = player2->m_position;
            }
            CollisionResult discard;
            if (world.m_collision == nullptr || !world.m_collision->checkBetweenPoints(pos1, pos2, 0, discard)) {
                g.playerVisibility[g.visIndex1][g.visIndex2] = true;
                g.playerVisibility[g.visIndex2][g.visIndex1] = true;
            }
        }
    }
    if (++g.visIndex1 >= slots) {
        if (++g.visIndex2 >= slots - 1) {
            g.visIndex2 = 0;
        }
        g.visIndex1 = g.visIndex2 + 1;
    }
}

void PlayerAi::UpdateGlobals(World& world)
{
    // One bot's path a tick: the first node on it the bot can see becomes its target.
    AiGlobalState& g = world.m_aiGlobals;
    if (g.globalField2 == 0) {
        return;
    }
    if (g.globalField0 >= g.globalField2) {
        g.globalField0 = 0;
    }
    AiGlobalState::Global& global = g.globalObjs[g.globalField0];
    Player* player = global.player;
    PlayerAi* ai = world.m_slots[player->m_slot].ai.get();
    const NodeData3* node = (*global.nodeData)[global.nodeDataIndex];
    const Vec3 pos1 = addY(player->m_position, player->m_altForm ? 0.5f : 1);
    const Vec3 pos2 = addY(node->Position, 0.5f);
    CollisionResult discard;
    if (world.m_collision != nullptr && world.m_collision->checkBetweenPoints(pos1, pos2, 0, discard)) {
        global.nodeDataIndex++;
        global.field4--;
        if (global.field4 == 0) {
            ai->Flags2 &= ~AiFlags2::Bit10;
            RemovePlayerFromGlobals(g, player);
        }
    } else {
        ai->_node40 = node;
        ai->_entityRefs.Nodes[1] = node;
        ai->Flags2 &= ~AiFlags2::Bit10;
        RemovePlayerFromGlobals(g, player);
    }
    g.globalField0++;
}

void PlayerAi::RemovePlayerFromGlobals(AiGlobalState& g, Player* player)
{
    if (g.globalField2 == 0) {
        return;
    }
    int i;
    for (i = 0; i < g.globalField2; i++) {
        if (g.globalObjs[i].player == player) {
            break;
        }
    }
    if (i == g.globalField2) {
        return;
    }
    for (; i < g.globalField2 - 1; i++) {
        g.globalObjs[i] = g.globalObjs[i + 1];
    }
    g.globalField2--;
}

// ---- setup -------------------------------------------------------------------

void PlayerAi::InitializeMain()
{
    if (_world.m_nodeData == nullptr) {
        return;
    }
    _nodeData = _world.m_nodeData.get();
    SetClosestNodeList(_player->m_position);
    if (_world.m_mode == GameMode::Capture) {
        // Its own team's Octolith and base, and the other team's.
        for (OctolithFlag& flag : _world.m_octolithFlags) {
            if (flag.teamId == _player->m_teamIndex) {
                _octolithFlagCC = _octolithFlagD4 = &flag;
            } else {
                _octolithFlagDC = &flag;
            }
        }
        for (FlagBase& base : _world.m_flagBases) {
            if (base.teamId == _player->m_teamIndex) {
                _flagBaseD0 = _flagBaseD8 = &base;
            } else {
                _flagBaseE0 = &base;
            }
        }
        if (_octolithFlagCC == nullptr || _octolithFlagD4 == nullptr || _octolithFlagDC == nullptr) {
            // A room without them in this mode: no AI rather than null references.
            _forceDisable = true;
        }
    } else if (_world.m_mode == GameMode::Bounty || _world.m_mode == GameMode::BountyTeams) {
        // The first Octolith and the first base serve for everything.
        if (!_world.m_octolithFlags.empty()) {
            _octolithFlagCC = _octolithFlagD4 = _octolithFlagDC = &_world.m_octolithFlags[0];
        }
        if (!_world.m_flagBases.empty()) {
            _flagBaseD0 = _flagBaseD8 = _flagBaseE0 = &_world.m_flagBases[0];
        }
        if (_octolithFlagCC == nullptr) {
            _forceDisable = true;
        }
    }
}

void PlayerAi::ClearInput()
{
    for (AiButton* button : {&_buttons.Up, &_buttons.Down, &_buttons.Left, &_buttons.Right, &_buttons.A, &_buttons.B, &_buttons.X,
             &_buttons.Y, &_buttons.L, &_buttons.R, &_buttons.Start, &_buttons.Select}) {
        button->Clear();
    }
    _hasTouch = false;
    _framesWithTouch = 0;
    _framesWithoutTouch = 0;
    _buttonAimX = 0;
    _buttonAimY = 0;
    for (AiButton* button : touchButtons()) {
        button->Clear();
    }
    _nodeDataSelOff = 0;
    _nodeDataSelOn = 0;
}

std::array<PlayerAi::AiButton*, 11> PlayerAi::touchButtons()
{
    return {&_touchButtons.Morph, &_touchButtons.Unmorph, &_touchButtons.PowerBeam, &_touchButtons.Missile, &_touchButtons.VoltDriver,
        &_touchButtons.Battlehammer, &_touchButtons.Imperialist, &_touchButtons.Judicator, &_touchButtons.Magmaul,
        &_touchButtons.ShockCoil, &_touchButtons.OmegaCannon};
}

void PlayerAi::InitializeSub()
{
    // Reaction times by bot level (Insane is the port's fourth tier), per hunter.
    static constexpr uint32_t randomValues1[4][8] = {
        {45, 45, 90, 45, 60, 45, 45, 45},
        {15, 15, 10, 10, 10, 10, 10, 10},
        {7, 7, 2, 2, 2, 2, 2, 2},
        {1, 1, 1, 1, 1, 1, 1, 1},
    };
    static constexpr uint32_t randomValues2[4] = {150, 45, 10, 1};
    const int index = std::clamp(_player->m_botLevel, 0, 3);
    _field102C = randomValues1[index][static_cast<int>(_player->m_hunter)] * 2;
    _field1030 = randomValues2[index] * 2;
}

// ---- input ---------------------------------------------------------------------

PlayerInput PlayerAi::ProcessInput()
{
    // The buttons pressed last tick become the player's controls: the same
    // button is a different control in biped and in alt form.
    PlayerInput input;
    if (_nodeData == nullptr) {
        return input;
    }
    Flags3 |= AiFlags3::NoInput;
    const bool alt = _player->m_altForm;
    const bool noStrafe = alt && _player->v.AltFormStrafe == 0;
    std::array<bool, ControlCount> down{};
    struct Mapping {
        AiButton* button;
        Control control;
    };
    const Mapping mappings[] = {
        {&_buttons.Up, alt ? RollUp : AimUp},
        {&_buttons.Down, alt ? RollDown : AimDown},
        {&_buttons.Left, alt ? RollLeft : AimLeft},
        {&_buttons.Right, alt ? RollRight : AimRight},
        {&_buttons.A, noStrafe ? AimRight : MoveRight},
        {&_buttons.B, noStrafe ? AimDown : MoveDown},
        {&_buttons.X, noStrafe ? AimUp : MoveUp},
        {&_buttons.Y, noStrafe ? AimLeft : MoveLeft},
        {&_buttons.L, alt ? AltAttack : Jump},
        {&_buttons.R, alt ? Boost : Shoot},
        {&_buttons.Start, Pause},
        {&_buttons.Select, Zoom},
    };
    for (const Mapping& m : mappings) {
        AiButton& button = *m.button;
        if (button.IsDown) {
            Flags3 &= ~AiFlags3::NoInput;
            button.FramesUp = 0;
            if (button.FramesDown < 6000) {
                button.FramesDown++;
            }
            button.IsDown = false;
            down[m.control] = true;
        } else {
            button.FramesDown = 0;
            if (button.FramesUp < 6000) {
                button.FramesUp++;
            }
        }
    }
    auto pressed = [&](Control c) { return down[c] && !_controlsDown[c]; };
    // Rolling forms roll with Up/Down/Left/Right; the strafing ones and the biped move with X/B/Y/A.
    if (alt && _player->v.AltFormStrafe == 0) {
        input.forward = down[RollUp];
        input.back = down[RollDown];
        input.left = down[RollLeft];
        input.right = down[RollRight];
    } else {
        input.forward = down[MoveUp];
        input.back = down[MoveDown];
        input.left = down[MoveLeft];
        input.right = down[MoveRight];
    }
    input.jumpPressed = pressed(Jump);
    input.altAttackPressed = pressed(AltAttack);
    input.altAttackHeld = down[AltAttack];
    input.boostHeld = down[Boost];
    input.shootHeld = down[Shoot];
    input.shootPressed = pressed(Shoot);
    input.zoomPressed = pressed(Zoom);
    _controlsDown = down;
    _framesWithTouch = 0;
    if (_framesWithoutTouch < 6000) {
        _framesWithoutTouch++;
    }
    const std::array<AiButton*, 11> touch = touchButtons();
    for (size_t i = 0; i < touch.size(); i++) {
        AiButton& button = *touch[i];
        if (button.IsDown) {
            Flags3 &= ~AiFlags3::NoInput;
            if (i < 2) {
                _player->trySwitchForms();
            } else {
                // PowerBeam, Missile, then the affinity weapons in beam order.
                static constexpr int beams[9] = {Beam::PowerBeam, Beam::Missile, Beam::VoltDriver, Beam::Battlehammer, Beam::Imperialist,
                    Beam::Judicator, Beam::Magmaul, Beam::ShockCoil, Beam::OmegaCannon};
                const int beam = beams[i - 2];
                if (i >= 4) {
                    _player->updateAffinityWeaponSlot(beam);
                }
                _player->tryEquipWeapon(beam, false);
            }
            button.FramesUp = 0;
            if (button.FramesDown < 6000) {
                button.FramesDown++;
            }
            button.IsDown = false;
        } else {
            button.FramesDown = 0;
            if (button.FramesUp < 6000) {
                button.FramesUp++;
            }
        }
    }
    if (_buttonAimX != 0) {
        Flags3 &= ~AiFlags3::NoInput;
        input.buttonAimX = _buttonAimX;
    }
    if (_buttonAimY != 0) {
        Flags3 &= ~AiFlags3::NoInput;
        input.buttonAimY = _buttonAimY;
    }
    _buttonAimX = 0;
    _buttonAimY = 0;
    UpdateNodeDataSetSelection();
    _nodeDataSelOff = 0;
    _nodeDataSelOn = 0;
    input.hasInput = !(Flags3 & AiFlags3::NoInput);
    return input;
}

// ---- process -------------------------------------------------------------------

void PlayerAi::Process()
{
    if (_nodeData == nullptr || _forceDisable || Personality == nullptr) {
        return;
    }
    if ((Flags2 & AiFlags2::TargetItem) && _itemC8 != nullptr && _itemC8->despawnTimer == 0) {
        Flags2 &= ~AiFlags2::TargetItem;
    }
    Flags2 &= ~(AiFlags2::Bit18 | AiFlags2::Bit19 | AiFlags2::Bit20);
    Flags4 &= ~AiFlags4::Bit2;
    Func2134594();
    Func2148ABC();
    Execute(_executionTree[0]);
    _slotHits.fill(0);
    _slotDamage.fill(0);
    DamageFromHalfturret = 0;
    Flags2 &= ~(AiFlags2::AiStart | AiFlags2::Bit16 | AiFlags2::Bit17 | AiFlags2::Bit21);
}

void PlayerAi::Func2134594()
{
    _entityRefs.Clear();
    UpdateAggro();
    // The Prime Hunter cannot pick health up.
    if (_world.m_mode == GameMode::PrimeHunter && _world.m_primeHunter == _player->slot() && (Flags2 & AiFlags2::TargetItem)
        && _itemC8 != nullptr && IsHealth(_itemC8->type)) {
        Flags2 &= ~AiFlags2::TargetItem;
    }
}

bool PlayerAi::ProjectPosition(const Player* viewer, const Vec3& position, float& projX, float& projY) const
{
    // Matrix.ProjectPosition with the viewer's camera (LookAt) and its field
    // of view, on the DS's 4:3 screen. Returns false when w < 0 (behind).
    const Vec3 eye = viewer->m_cam.position;
    const Vec3 toTarget = viewer->m_cam.target - eye;
    Vec3 up = cross(toTarget, cross(viewer->m_cam.up, toTarget));
    Vec3 z = normalized(-toTarget);
    Vec3 x = normalized(cross(up, z));
    const Vec3 y = cross(z, x);
    const Vec3 rel = position - eye;
    const float vx = dot(rel, x), vy = dot(rel, y), vz = dot(rel, z);
    const float fovDegrees = viewer->m_fov > 0 ? viewer->m_fov : 78;
    const float f = 1 / std::tan(fovDegrees * kDegToRad / 2);
    const float w = -vz;
    if (w <= 0) {
        projX = projY = 0;
        return w == 0;
    }
    const float ndcX = vx * f / (4 / 3.0f) / w;
    const float ndcY = vy * f / w;
    projX = (ndcX + 1) / 2;
    projY = (1 - ndcY) / 2;
    return true;
}

void PlayerAi::UpdateAggro()
{
    for (auto& slot : _world.m_slots) {
        Player* other = slot.player.get();
        if (other == _player || other->m_health == 0 || !IsPlayerVisible(_player, other)) {
            continue;
        }
        float projX, projY;
        if (!ProjectPosition(_player, other->m_position, projX, projY)) {
            return; // (the C#'s "return", kept: see its note)
        }
        if (projX >= 1 || projY >= 1) {
            continue;
        }
        if (other->m_curAlpha >= 1 || other->radarReveal || _world.m_radarPlayers || other->octolithFlag != nullptr
            || _world.m_primeHunter == other->slot()) {
            AggroFunc214864C(6, 1, 2, nullptr, other, 0, 30, 10, 3);
        } else {
            Vec3 between = other->m_position - _player->m_cam.position;
            between = addY(between, other->m_altForm ? fx(other->v.AltColYPos) : 0.5f);
            int rand = static_cast<int>(lengthSquared(between) * 4096);
            int alpha = static_cast<int>(other->m_curAlpha * 31);
            if (alpha > 2) {
                rand /= alpha * alpha * 853;
            } else {
                rand /= 2 * 2 * 853;
            }
            int div = 1;
            if (AggroFunc214857C(4, 2, 1, other, nullptr)) {
                div = 4;
            }
            if (randomInt2(static_cast<uint32_t>((31 - alpha + rand) / div)) != 0) {
                return;
            }
            alpha = alpha <= 2 ? 1 : (alpha - 2);
            AggroFunc214864C(6, 1, 2, nullptr, other, 0, alpha, 10, 3);
        }
        if (!ProjectPosition(other, _player->m_position, projX, projY)) {
            return;
        }
        if (projX < 1 && projY < 1) {
            AggroFunc214864C(6, 2, 1, other, nullptr, 0, 30, 10, 3);
        }
    }
}

bool PlayerAi::IsPlayerVisible(const Player* player, const Player* other) const
{
    return _world.m_aiGlobals.playerVisibility[other->m_slot][player->m_slot];
}

namespace {
template <typename Aggro>
bool aggroMatches(const Aggro& aggro, int a2, int a3, int a4, const Player* player1, const Player* player2)
{
    return (a2 == aggro.VarA2 || a2 == 7) && (a3 == aggro.VarA3 || a3 == 7) && (a4 == aggro.VarA4 || a4 == 7)
        && (player1 == aggro.Player1 || a3 != 2) && (player2 == aggro.Player2 || a4 != 2);
}
} // namespace

int PlayerAi::AggroFunc2148394(int a2, int a3, int a4, Player* player1, Player* player2)
{
    // the "priority" of the matching entries, for finding entities
    int result = 0;
    for (int i = 0; i < _playerAggroCount; i++) {
        if (aggroMatches(_playerAggro[i], a2, a3, a4, player1, player2)) {
            result += _playerAggro[i].VarA7;
        }
    }
    return result;
}

PlayerAi::AiPlayerAggro* PlayerAi::AggroFunc214847C(int a2, int a3, int a4, Player* player1, Player* player2)
{
    AiPlayerAggro* result = nullptr;
    int maxField4 = -1;
    for (int i = 0; i < _playerAggroCount; i++) {
        AiPlayerAggro& aggro = _playerAggro[i];
        if (aggroMatches(aggro, a2, a3, a4, player1, player2) && aggro.Staleness > maxField4) {
            result = &aggro;
            maxField4 = aggro.Staleness;
        }
    }
    return result;
}

bool PlayerAi::AggroFunc214857C(int a2, int a3, int a4, Player* player1, Player* player2)
{
    for (int i = 0; i < _playerAggroCount; i++) {
        if (aggroMatches(_playerAggro[i], a2, a3, a4, player1, player2)) {
            return true;
        }
    }
    return false;
}

void PlayerAi::AggroFunc214864C(int a2, int a3, int a4, Player* player1, Player* player2, int a7, int a8, int a9, int a10)
{
    if (a10 == 2) {
        // update, when taking damage (amount in a7)
        if (AiPlayerAggro* aggro = AggroFunc21489B4(a2, a3, a4, player1, player2)) {
            aggro->Expiration = static_cast<uint16_t>(std::min(aggro->Expiration + a8, 54000));
            if (a9 > aggro->VarA9) {
                aggro->VarA9 = static_cast<uint8_t>(a9 & 0xF);
            }
            aggro->VarA7 = static_cast<uint16_t>(std::min(aggro->VarA7 + a7, 4000));
            return;
        }
    } else if (a10 == 3) {
        // replace, at the start of the processing loop
        if (AiPlayerAggro* aggro = AggroFunc21489B4(a2, a3, a4, player1, player2)) {
            aggro->VarA2 = static_cast<uint8_t>(a2 & 0xF);
            aggro->VarA9 = static_cast<uint8_t>(a9 & 0xF);
            aggro->VarA3 = static_cast<uint8_t>(a3 & 0xF);
            aggro->VarA4 = static_cast<uint8_t>(a4 & 0xF);
            aggro->VarA10 = 3;
            aggro->VarA7 = static_cast<uint8_t>(a7 & 0xF);
            aggro->Staleness = 0;
            aggro->Expiration = static_cast<uint16_t>(a8);
            aggro->Player1 = player1;
            aggro->Player2 = player2;
            return;
        }
    }
    int index = _playerAggroCount;
    const int capacity = static_cast<int>(_playerAggro.size());
    if (index >= capacity) {
        int min = a9;
        for (int i = 0; i < capacity; i++) {
            if (_playerAggro[i].VarA9 < min) {
                index = i;
                min = _playerAggro[i].VarA9;
            }
        }
    } else {
        _playerAggroCount++;
    }
    if (index < capacity) {
        AiPlayerAggro& aggro = _playerAggro[index];
        aggro.VarA2 = static_cast<uint8_t>(a2 & 0xF);
        aggro.VarA9 = static_cast<uint8_t>(a9 & 0xF);
        aggro.VarA3 = static_cast<uint8_t>(a3 & 0xF);
        aggro.VarA4 = static_cast<uint8_t>(a4 & 0xF);
        aggro.VarA10 = static_cast<uint8_t>(a10 & 0xF);
        aggro.VarA7 = static_cast<uint8_t>(a7 & 0xF);
        aggro.Staleness = 0;
        aggro.Expiration = static_cast<uint16_t>(a8);
        aggro.Player1 = player1;
        aggro.Player2 = player2;
    }
}

PlayerAi::AiPlayerAggro* PlayerAi::AggroFunc21489B4(int a2, int a3, int a4, Player* player1, Player* player2)
{
    for (int i = 0; i < _playerAggroCount; i++) {
        AiPlayerAggro& aggro = _playerAggro[i];
        if (aggro.VarA10 != 1 && aggroMatches(aggro, a2, a3, a4, player1, player2)) {
            return &aggro;
        }
    }
    return nullptr;
}

void PlayerAi::UpdateAggroExpiration()
{
    int i = 0;
    while (i < _playerAggroCount) {
        AiPlayerAggro& aggro = _playerAggro[i];
        aggro.Staleness++;
        if (aggro.Staleness > aggro.Expiration * 2) {
            const AiPlayerAggro& last = _playerAggro[_playerAggroCount - 1];
            aggro.VarA2 = last.VarA2;
            aggro.VarA9 = last.VarA9;
            aggro.VarA3 = last.VarA3;
            aggro.VarA4 = last.VarA4;
            aggro.Staleness = last.Staleness;
            aggro.Expiration = last.Expiration;
            aggro.Player1 = last.Player1;
            aggro.Player2 = last.Player2;
            _playerAggroCount--;
        } else {
            i++;
        }
    }
}

void PlayerAi::Func2148ABC()
{
    UpdateAggroExpiration();
    Flags4 &= ~AiFlags4::Bit0;
    if (dot(_field1038, _player->m_cam.facing) >= 255 / 256.0f) {
        Flags4 |= AiFlags4::Bit0;
    }
    if (_field1020 > 0) {
        _field1020--;
    }
}

// ---- the behavior tree ---------------------------------------------------------
// One path through the tree runs every tick: each node calls its funcs_1
// (data3b) and funcs_2 (field0). Returning up, each level weighs whether to
// switch its child: funcs_3 checks (data4 all true, then data2 weighted) add
// up per candidate child, and the first to reach 100,000 is switched to,
// running its funcs_1 (data3a) and funcs_4 (field0) and those of the
// leftmost children below it. An index of 20 or more resets the weights.

void PlayerAi::UpdateExecutionPath(const AiPersonalityData1* data1, int depth)
{
    assert(depth < _maxContextDepth);
    AiContext& context = _executionTree[depth];
    context.Data1 = data1;
    context.Depth = depth;
    context.CallCount = 0;
    std::fill_n(context.Weights.begin(), std::min<size_t>(data1->Data1.size(), context.Weights.size()), 0);
    ExecuteFuncs1(context.Data1->Data3a);
    context.Func24Id = context.Data1->Func24Id;
    context.Field10 = 0;
    _field1020 = 0;
    _node44 = nullptr;
    Flags4 &= ~AiFlags4::Bit3;
    Flags2 &= ~AiFlags2::Bit10;
    RemovePlayerFromGlobals(_world.m_aiGlobals, _player);
    if ((context.Func24Id == 1 || context.Func24Id == 2 || context.Func24Id == 3) && _player->m_grounded) {
        Flags2 &= ~AiFlags2::Bit7;
    }
    ExecuteFuncs4(context);
    if (!data1->Data1.empty() && depth < _maxContextDepth - 1) {
        UpdateExecutionPath(data1->Data1[0], depth + 1);
    }
}

void PlayerAi::Execute(AiContext& context)
{
    ExecuteFuncs1(context.Data1->Data3b);
    ExecuteFuncs2(context);
    const WeaponInfo* weapon = _player->m_equip.weapon;
    if (context.Func24Id != 0 && weapon != nullptr && (weapon->flags & WeaponFlags::CanZoom) && _buttons.Select.FramesUp > 5 * 2
        && ((!_player->m_equip.zoomed && (Flags4 & AiFlags4::Bit2)) || (_player->m_equip.zoomed && !(Flags4 & AiFlags4::Bit2)))) {
        _buttons.Select.IsDown = true;
    }
    if (_player->m_hunter == Hunter::Spire && !_player->m_altForm) {
        _field116 = 0;
    }
    if (context.CallCount < INT32_MAX) {
        context.CallCount++;
    }
    if (!context.Data1->Data1.empty() && context.Depth < _maxContextDepth - 1) {
        Execute(_executionTree[context.Depth + 1]);
        const int newChildIndex = UpdatePathWeights(context);
        if (newChildIndex != -1 && newChildIndex < static_cast<int>(context.Data1->Data1.size())) {
            UpdateExecutionPath(context.Data1->Data1[newChildIndex], context.Depth + 1);
        }
    }
}

int PlayerAi::UpdatePathWeights(AiContext& context)
{
    const AiPersonalityData1* nextData1 = _executionTree[context.Depth + 1].Data1;
    if (nextData1->Data2.empty()) {
        return -1;
    }
    int result = -1;
    for (const AiPersonalityData2* data2 : nextData1->Data2) {
        bool noUpdate = false;
        for (const AiPersonalityData4* data4 : data2->Data4) {
            if (ExecuteFuncs3(context, data4->Func3Id, *data4->Parameters) == 0) {
                noUpdate = true;
                break;
            }
        }
        if (noUpdate) {
            continue;
        }
        int weightIndex = data2->Data1SelectIndex;
        if (weightIndex >= 20) {
            weightIndex = static_cast<int>(context.Data1->Data1.size());
        }
        // The checks run every other tick (the game's 30 Hz), except the ones
        // heavy enough to switch at once.
        if ((data2->Weight >= 100000 && data2->Func3Id != 210) || _world.m_ticks % 2 == 0) {
            context.Weights[weightIndex] += ExecuteFuncs3(context, data2->Func3Id, *data2->Parameters) * data2->Weight;
            if (context.Weights[weightIndex] >= 100000) {
                result = data2->Data1SelectIndex;
                context.Weights[weightIndex] = 0;
                break;
            }
        }
    }
    if (result < 20) {
        return result;
    }
    std::fill_n(context.Weights.begin(), std::min<size_t>(context.Data1->Data1.size(), context.Weights.size()), 0);
    return -1;
}

Vec3 PlayerAi::ExecuteVectorFunc(int index, bool clearY, bool normalize)
{
    Vec3 result{};
    switch (index) {
    case 0: result = Func213A470(); break;
    case 1: result = Func213A458(); break;
    case 2: result = Func213A3DC(); break;
    case 3: result = Func213A3C0(); break;
    case 4: result = Func213A3A8(); break;
    case 5: result = Func213A37C(); break;
    case 6: result = Func213A35C(); break;
    case 7: result = Func213A31C(); break;
    default: break;
    }
    if (clearY) {
        result[1] = 0;
    }
    if (normalize) {
        result = !isZero(result) ? normalized(result) : UnitX;
    }
    return result;
}

Vec3 PlayerAi::Func213A470()
{
    assert(_targetPlayer != nullptr);
    return _targetPlayer->m_position - _player->m_position;
}

Vec3 PlayerAi::Func213A458() { return _player->facingVector(); }

Vec3 PlayerAi::Func213A3DC()
{
    assert(_targetPlayer != nullptr);
    return TargetPosition(_targetPlayer) - _player->m_muzzlePos;
}

Vec3 PlayerAi::Func213A3C0() { return _player->m_aimPosition - _player->m_muzzlePos; }

Vec3 PlayerAi::Func213A3A8()
{
    assert(_targetPlayer != nullptr);
    return _targetPlayer->facingVector();
}

Vec3 PlayerAi::Func213A37C()
{
    assert(_node40 != nullptr);
    return _node40->Position - _player->m_position;
}

Vec3 PlayerAi::Func213A35C() { return _player->m_cam.facing; }

Vec3 PlayerAi::Func213A31C()
{
    FindEntityRef(26);
    Player* player = _entityRefs.Players[0];
    return player != nullptr ? player->m_position - _player->m_position : Vec3{};
}

Vec3 PlayerAi::TargetPosition(const Player* player)
{
    // GetPosition, raised to the middle of the body.
    return addY(player->m_position, player->m_altForm ? fx(player->v.AltColYPos) : 0.5f);
}

// ---- damage ----------------------------------------------------------------------

void PlayerAi::OnTakeDamage(int damage, bool fromBeam, int beam, bool fromWeavelAlt, Player* attacker)
{
    // Every bot counts the hit; this one also remembers who did it.
    for (auto& slot : _world.m_slots) {
        PlayerAi* other = slot.ai.get();
        if (other == nullptr) {
            continue;
        }
        other->_slotHits[_player->m_slot]++;
        other->_slotDamage[_player->m_slot] += damage;
        if (attacker == nullptr) {
            continue;
        }
        const bool weavelAlt = fromBeam && fromWeavelAlt;
        if (other == this) {
            if (weavelAlt) {
                AggroFunc214864C(5, 2, 1, attacker, nullptr, damage, damage, 2, 2);
            } else {
                AggroFunc214864C(4, 2, 1, attacker, nullptr, damage, damage, 2, 2);
                if (fromBeam && beam == Beam::ShockCoil && attacker->m_shockCoilTimer > 10 * 2) {
                    other->Flags2 |= AiFlags2::Bit21;
                }
            }
        } else if (weavelAlt) {
            AggroFunc214864C(5, 2, 2, attacker, _player, damage, damage, 2, 2);
        } else {
            AggroFunc214864C(4, 2, 2, attacker, _player, damage, damage, 2, 2);
        }
    }
}

bool PlayerAi::AiEntityRefs::IsPopulated(int index) const
{
    if (index < 26) {
        return Nodes[index] != nullptr;
    }
    if (index < 33) {
        return Players[index - 26] != nullptr;
    }
    if (index == 33) {
        return Field33 != nullptr;
    }
    if (index < 54) {
        return ItemSpawns[index - 34] != nullptr;
    }
    if (index < 74) {
        return Items[index - 54] != nullptr;
    }
    if (index < 77) {
        return Defenses[index - 74] != nullptr;
    }
    return Field77 != nullptr;
}

std::string PlayerAi::pathDescription() const
{
    std::string text;
    for (int i = 0; i < _maxContextDepth; i++) {
        const AiContext& node = _executionTree[i];
        if (node.Data1 == nullptr) {
            break;
        }
        if (i != 0) {
            text += " -> ";
        }
        text += node.Data1->Label + "(" + std::to_string(node.Func24Id) + ")";
        if (node.Data1->Data1.empty()) {
            // The leaf's movement: Field4 (33 straight at, 37 along nodes), Field9 (what).
            text += " [" + std::to_string(node.Field4) + "/" + std::to_string(node.Field9) + "]";
            break;
        }
    }
    return text;
}

// ---- dispatch ------------------------------------------------------------------------

void PlayerAi::ExecuteFuncs1(const std::vector<int>& funcIds)
{
    using Func = void (PlayerAi::*)();
    static constexpr Func funcs[84] = {
        &PlayerAi::Func1_214A39C, &PlayerAi::Func1_214A098, &PlayerAi::Func1_2149D3C, &PlayerAi::Func1_2149C98,
        &PlayerAi::Func1_2149C80, &PlayerAi::Func1_2149C68, &PlayerAi::Func1_2149C50, &PlayerAi::Func1_2149C38,
        &PlayerAi::Func1_2149C20, &PlayerAi::Func1_2149C08, &PlayerAi::Func1_2149BF0, &PlayerAi::Func1_2149BD8,
        &PlayerAi::Func1_2149BC0, &PlayerAi::Func1_2149BA8, &PlayerAi::Func1_2149B98, &PlayerAi::Func1_2149AD8,
        &PlayerAi::Func1_2149AC8, &PlayerAi::Func1_2149ABC, &PlayerAi::Func1_2149AB0, &PlayerAi::Func1_2149AA4,
        &PlayerAi::Func1_2149A98, &PlayerAi::Func1_2149A64, &PlayerAi::Func1_2149824, &PlayerAi::Func1_21497F0,
        &PlayerAi::Func1_2149570, &PlayerAi::Func1_21494FC, &PlayerAi::Func1_2149488, &PlayerAi::Func1_2149414,
        &PlayerAi::Func1_21493A0, &PlayerAi::Func1_214932C, &PlayerAi::Func1_21495A4, &PlayerAi::Func1_2149530,
        &PlayerAi::Func1_21494BC, &PlayerAi::Func1_2149448, &PlayerAi::Func1_21493D4, &PlayerAi::Func1_2149360,
        &PlayerAi::Func1_21492EC, &PlayerAi::Func1_21492DC, &PlayerAi::Func1_21492CC, &PlayerAi::Func1_21492BC,
        &PlayerAi::Func1_21492AC, &PlayerAi::Func1_214929C, &PlayerAi::Func1_214928C, &PlayerAi::Func1_214927C,
        &PlayerAi::Func1_214926C, &PlayerAi::Func1_214925C, &PlayerAi::Func1_214924C, &PlayerAi::Func1_214923C,
        &PlayerAi::Func1_214922C, &PlayerAi::Func1_214921C, &PlayerAi::Func1_214920C, &PlayerAi::Func1_21491FC,
        &PlayerAi::Func1_21491E4, &PlayerAi::Func1_21491CC, &PlayerAi::Func1_21491B4, &PlayerAi::Func1_214919C,
        &PlayerAi::Func1_2149184, &PlayerAi::Func1_214916C, &PlayerAi::Func1_2149154, &PlayerAi::Func1_214913C,
        &PlayerAi::Func1_2149124, &PlayerAi::Func1_214910C, &PlayerAi::Func1_21490F4, &PlayerAi::Func1_21490DC,
        &PlayerAi::Func1_21490C4, &PlayerAi::Func1_21490AC, &PlayerAi::Func1_2149094, &PlayerAi::Func1_2149088,
        &PlayerAi::Func1_2149034, &PlayerAi::Func1_2148F10, &PlayerAi::Func1_2148EDC, &PlayerAi::Func1_2148ECC,
        &PlayerAi::Func1_2148EB8, &PlayerAi::Func1_2148EA8, &PlayerAi::Func1_2148E98, &PlayerAi::Func1_2148E88,
        &PlayerAi::Func1_2148E74, &PlayerAi::Func1_2148E64, &PlayerAi::Func1_2148E54, &PlayerAi::Func1_2148DF8,
        &PlayerAi::Func1_2148DE8, &PlayerAi::Func1_2148D50, &PlayerAi::Func1_UnlockEchoHallForceField,
        &PlayerAi::Func1_SetInvulnerable,
    };
    for (int id : funcIds) {
        if (id >= 0 && id < 84) {
            (this->*funcs[id])();
        } else {
            qWarning("AI: invalid func 1 %d", id);
        }
    }
}

void PlayerAi::ExecuteFuncs2(AiContext& context)
{
    const int id = context.Func24Id;
    switch (id) {
    case 0:
    case 125: break;
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
        if ((id >= 2 && id <= 44) || id == 46 || id == 48 || (id >= 50 && id <= 78) || (id >= 82 && id <= 98) || id == 101 || id == 103
            || (id >= 106 && id <= 113) || (id >= 115 && id <= 122)) {
            Func2_213EA48(context);
        } else {
            qWarning("AI: invalid func 2 %d", id);
        }
        break;
    }
}

void PlayerAi::ExecuteFuncs4(AiContext& context)
{
    const int id = context.Func24Id;
    switch (id) {
    case 0:
    case 1:
    case 47:
    case 49:
    case 99: break;
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
        if ((id >= 2 && id <= 44) || id == 46 || id == 48 || (id >= 50 && id <= 78) || (id >= 82 && id <= 98) || id == 101 || id == 103
            || (id >= 106 && id <= 113) || (id >= 115 && id <= 122)) {
            Func4_21462DC(context);
        } else {
            qWarning("AI: invalid func 4 %d", id);
        }
        break;
    }
}

} // namespace fp
