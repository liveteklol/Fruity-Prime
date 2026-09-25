#pragma once

#include "Collision.h"
#include "audio/Sfx.h"
#include "formats/Animation.h"
#include "formats/Mat4.h"
#include "formats/Metadata.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace fp {

class Effects;
class Model;
class PlayerAi;
struct NodeData3;
struct OctolithFlag;

// GameState.Teams, TeamCount and FriendlyFire: the match's teams, set by the World.
struct TeamRules {
    bool teams = false;
    int teamCount = 2;
    bool friendlyFire = false;
};
TeamRules& teamRules();
// TeamRules.AreAllies: on the same team, in a team mode.
inline bool areAllies(int first, int second)
{
    const TeamRules& rules = teamRules();
    return rules.teams && first >= 0 && first < rules.teamCount && first == second;
}

enum class Hunter : int { Samus = 0, Kanden = 1, Trace = 2, Sylux = 3, Noxus = 4, Spire = 5, Weavel = 6, Guardian = 7 };

// Metadata.HunterSfx, BeamSfx, TerrainSfx: a sound id, -1 for none (or no sound).
int hunterSfx(Hunter hunter, HunterSfx sfx);
int beamSfx(int beam, BeamSfx sfx);
int terrainSfx(int terrain, TerrainSfx sfx);

struct PlayerInput {
    bool forward = false, back = false, left = false, right = false;
    bool jumpPressed = false;   // this tick only
    bool morphPressed = false;  // this tick only
    bool boostHeld = false;     // alt form: charge while held, release to boost
    bool altAttackPressed = false;
    bool altAttackHeld = false;
    bool shootPressed = false; // this tick only
    bool shootHeld = false;
    bool zoomPressed = false;  // this tick only
    bool hasInput = false;     // any control this tick (Input.HasInput: the gun's idle sway waits for none)
    // The bots' button aim (PlayerInput._buttonAimX/Y): degrees to turn left and up this tick.
    float buttonAimX = 0, buttonAimY = 0;
};

// DamageFlags
namespace DamageFlags {
constexpr uint32_t NoDmgInvuln = 0x1;
constexpr uint32_t IgnoreInvuln = 0x2;
constexpr uint32_t Death = 0x4;
constexpr uint32_t Halfturret = 0x8;
constexpr uint32_t Headshot = 0x10;
constexpr uint32_t Deathalt = 0x20;
constexpr uint32_t Burn = 0x40;
constexpr uint32_t NoSfx = 0x80;
constexpr uint32_t FromAlt = 0x100;
} // namespace DamageFlags

// BeamSpawnFlags and BeamResultFlags
namespace BeamSpawnFlags {
constexpr int DoubleDamage = 0x1;
constexpr int Charged = 0x2;
constexpr int NoMuzzle = 0x4;
constexpr int PrimeHunter = 0x8;
constexpr int FromHalfturret = 0x100; // not the game's: the beam's owner is the Halfturret (no ammo, its own kill message)
} // namespace BeamSpawnFlags
namespace BeamResult {
constexpr int NoSpawn = 0x0;
constexpr int Spawned = 0x1;
constexpr int Homing = 0x2;
} // namespace BeamResult

// GunAnimation
namespace GunAnim {
constexpr int FullCharge = 0, ChargeShot = 1, Charging = 2, Idle = 3, Switch = 4, FullChargeMissile = 5, ChargingMissile = 6,
              MissileClose = 7, MissileOpen = 8, Unknown9 = 9, MissileShot = 10, UpDown = 11, Shot = 12;
} // namespace GunAnim

// EquipInfo: what a player's gun fires, with its charge. Charge counts 60 Hz ticks.
struct EquipInfo {
    const WeaponInfo* weapon = nullptr;
    int chargeLevel = 0;
    int smokeLevel = 0;
    bool zoomed = false;
};

// Where a hit came from, for TakeDamage.
struct DamageSource {
    class Player* attacker = nullptr;
    int beam = -1;       // BeamType, -1 for none
    int afflictions = 0; // Affliction bits of the beam
    Vec3 beamVelocity{}; // hit direction when the beam's own is zero
    int bomb = -1;       // BombType, -1 for none
    bool fromHalfturret = false;
    int damage = 0;        // filled in by TakeDamage: what the hit took
    int turretDamage = -1; // and the Halfturret's share, when it was the one hit
    Vec3 impulse{};        // and the knockback it was given (zero for none)
    // BeamProjectileEntity.ModLaunchFrame and Age: the world the shot was
    // fired in (0 for none), and how long it flew before it landed, in seconds.
    uint32_t launchFrame = 0;
    float flight = 0;
    bool claimed = false; // a hit claim's damage, final as it stands (NetHitClaims.ApplyOne)
};

// The network's say in a hit, where TakeDamage asks it (NetDamage.Suppress,
// NetHitPrediction.NoteHit, NetHitClaims.AlreadyRescued).
class NetDamageHooks {
public:
    virtual ~NetDamageHooks() = default;
    // A replica's hit resolved on this machine, which decides nothing: true
    // when it is shown anyway -- this machine's own shot on somebody else, its
    // own splash on itself, its own fall.
    virtual bool predicts(const Player& victim, const DamageSource& source, uint32_t flags) = 0;
    // The damage is final and the death not decided yet: the prediction is
    // recorded (and a killing blow on somebody else clamped to leave them standing).
    virtual void noteHit(Player& victim, uint32_t& flags, int& damage, const DamageSource& source) = 0;
    // On the machine that runs the match: a hit that must not land (a hit
    // claim has already made that shot real).
    virtual bool suppress(const Player& victim, const DamageSource& source) = 0;
    // The Shock Coil's drain: `gained` health this player just took off somebody.
    virtual void noteDrain(const Player& healer, int gained) = 0;
};

// HalfturretEntity: Weavel's legs, left behind as a turret while the upper
// body goes about in alt form. Owned by its Player.
struct Halfturret {
    bool active = false; // PlayerFlags2.Halfturret
    int health = 0;
    Vec3 position{}, facing{0, 0, 1}, aimVector{0, 0, 1};
    float ySpeed = 0;
    bool grounded = false;
    int target = -1; // a player slot
    int targetTimer = 0, cooldownTimer = 0;
    float cooldownFactor = 1.5f;
    int timeSinceDamage = 0xFFFF, timeSinceFrozen = 0, freezeTimer = 0, burnTimer = 0;
    int animation = -1; // set this tick: 1 deploying, 0 firing; the World plays it
    EquipInfo equip;
    const struct NodeData3* closestNode = nullptr; // the bots' node for it
};

// BombType
namespace BombType {
constexpr int MorphBall = 0, Stinglarva = 1, Lockjaw = 2;
} // namespace BombType

// What CheckHitByBomb needs of a bomb.
struct BombHit {
    class Player* owner = nullptr;
    Vec3 position{};
    float radius = 0;
    int damage = 0;
    int type = BombType::MorphBall;
    bool exploding = false; // BombFlags.Exploding or Exploded
};

// Doors and force fields push the player like faces do (PlayerCollision.CheckCollision).
struct DoorObstacle {
    Vec3 lockPosition;
    Vec3 facing;
    float radiusSquared;
};

struct ForceFieldObstacle {
    Vec4 plane;
    Vec3 position, up, right;
    float width, height;
};

struct CameraPose {
    Vec3 position, target, up;
};

enum class ItemType : int {
    HealthMedium = 0, HealthSmall = 1, HealthBig = 2, DoubleDamage = 3, EnergyTank = 4, VoltDriver = 5,
    MissileExpansion = 6, Battlehammer = 7, Imperialist = 8, Judicator = 9, Magmaul = 10, ShockCoil = 11,
    OmegaCannon = 12, UASmall = 13, UABig = 14, MissileSmall = 15, MissileBig = 16, Cloak = 17, UAExpansion = 18,
    ArtifactKey = 19, Deathalt = 20, AffinityWeapon = 21, PickWpnMissile = 22,
};

// What the Player needs from its models: animation lengths and the alt form's shape.
struct PlayerModels {
    const AnimationSet* biped = nullptr;
    const AnimationSet* alt = nullptr;
    const AnimationSet* gun = nullptr;
    const Model* altModel = nullptr; // Spire's rocks are nodes of it
    std::array<float, 4> kandenNodeDistances{}; // PlayerEntity.KandenAltNodeDistances
};

// PlayerEntity for one local player, one 60 Hz tick at a time: ProcessBiped,
// ProcessAlt, ProcessMovement, PlayerCollision, the form switch, jump pads,
// pickups, and PlayerCamera.
class Player {
    friend class PlayerAi; // the bots read and drive the player's state the way PlayerAiData does
public:
    Player(int hunter, const RoomCollision* collision);

    // Spawns a beam for this player's shot (BeamProjectileEntity.Spawn); returns BeamResult bits.
    using FireCallback = std::function<int(Player& owner, EquipInfo& equip, const Vec3& position, const Vec3& direction, int spawnFlags)>;
    void setFireCallback(FireCallback callback) { m_fire = std::move(callback); }
    // Lays a bomb at `transform` (BombEntity.Spawn), or sets Sylux's three off;
    // returns false when no bomb was laid.
    using BombCallback = std::function<bool(Player& owner, const Mat4& transform)>;
    void setBombCallback(BombCallback callback) { m_spawnBomb = std::move(callback); }
    // SyluxBombCount: Lockjaw bombs out, kept by the World.
    int syluxBombCount() const { return m_syluxBombCount; }
    void setSyluxBombCount(int count) { m_syluxBombCount = count; }
    // PlayerEntity.CheckHitByBomb: the owner's own bomb jumps it, anyone else's hurts.
    bool checkHitByBomb(const BombHit& bomb);
    int bombAmmo() const { return m_bombAmmo; }
    const Halfturret& halfturret() const { return m_halfturret; }
    // Message.Impact: one of this player's shots hit `targetSlot`; the Shock
    // Coil's damage grows while it stays on the same target.
    void onImpact(int targetSlot, bool shockCoil);
    int shockCoilTarget() const { return m_shockCoilTarget; }
    int shockCoilTimer() const { return m_shockCoilTimer; }
    // HalfturretEntity.Process, after every player's tick; clears the animation request.
    void processHalfturret(float killHeight);
    void takeHalfturretAnimation() { m_halfturret.animation = -1; }
    // CheckHitByBomb against the Halfturret.
    bool checkHalfturretHitByBomb(const BombHit& bomb);
    int slot() const { return m_slot; }
    void setSlot(int slot) { m_slot = slot; }
    // Told about every hit that lands: the match's scoring and the HUD's messages.
    using DamageListener = std::function<void(Player& victim, const DamageSource& source, bool died, uint32_t flags)>;
    void setDamageListener(DamageListener listener) { m_damageListener = std::move(listener); }
    // Everybody in the match, for bumping into each other and alt attacks (CheckPlayerCollision).
    void setPlayers(const std::vector<Player*>* players) { m_players = players; }
    // Where the gun's muzzle flash and charge glow go (the Scene's effects).
    void setEffects(Effects* effects) { m_effects = effects; }

    void setModels(const PlayerModels& models) { m_models = models; }
    // PlayerEntity.Spawn (multiplayer): full health, Power Beam and Missiles.
    void spawn(const Vec3& position, const Vec3& facing, bool respawn = false);
    // Back to the state PlayerEntity.Setup leaves a player in before its
    // first spawn: no health, waiting to be spawned.
    void despawn();
    // PlayerEntity.Teleport: moved to `position` facing `facing` (Reposition),
    // the alt form's camera brought along at once, and the horizontal speed dropped.
    void teleport(const Vec3& position, const Vec3& facing);
    // PlayerEntity._soundSource and the main player's own sounds (PlayerSound.cs).
    void setMainPlayer(bool main) { m_isMain = main; }
    bool isMainPlayer() const { return m_isMain; }
    SoundSource& soundSource() { return m_soundSource; }
    void stopContinuousBeamSfx(int beam);
    void startFlagCarrySfx() { m_flagCarrySfxOn = true; }
    void stopFlagCarrySfx();
    // UpdateTimedSounds: the power-ups' countdowns and the Octolith's hum.
    void updateTimedSounds();
    void stopAllSfx();
    // MorphCameraEntity: the fixed camera watching the alt form while it is
    // in the entity's volume (the id), -1 for none. Setting one keeps the
    // roll directions as they are until the stick is let go (RefreshExternalCamera);
    // clearing it brings the player's own camera back (ResumeOwnCamera).
    int morphCamera() const { return m_morphCameraId; }
    void setMorphCamera(int id, const Vec3& position);
    void clearMorphCamera(bool refresh);
    void tick(const PlayerInput& input);

    void setAim(float yawDegrees, float pitchDegrees);
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }

    void setObstacles(std::vector<DoorObstacle> doors, std::vector<ForceFieldObstacle> forceFields);
    void activateJumpPad(const Vec3& vector, uint16_t lockTime);
    bool inPickupRange(const Vec3& itemPosition) const;
    // Message.Gravity from an area volume (Fuel Stack's fans): this gravity,
    // in fixed point, for the next tick while airborne.
    void applyGravity(int param1);
    bool pickUp(ItemType type);

    const Vec3& position() const { return m_position; }
    const Vec3& prevPosition() const { return m_prevPosition; }
    const Vec3& speed() const { return m_speed; }
    // Field70 and Field74: which way the player faces, on the ground plane (x, z).
    float facingX() const { return m_field70; }
    float facingZ() const { return m_field74; }
    CameraPose camera() const;
    // PlayerCamera.UpdateMatchEndCamera, for this player as the winner: in
    // front of it and looking back at it, drifting sideways `seconds` after the match ended.
    CameraPose matchEndCamera(float seconds) const;
    bool standing() const { return m_standing; }
    bool isAltForm() const { return m_altForm; }
    bool isMorphing() const { return m_morphing; }
    bool isUnmorphing() const { return m_unmorphing; }
    bool firstPerson() const;
    Hunter hunter() const { return m_hunter; }
    const PlayerValues& values() const { return v; }

    // The alt model's draw transform, with position (PlayerDraw), and its animation.
    Mat4 altModelTransform() const;
    const AnimationState& altAnimation() const { return m_altAnim; }
    // First-person gun (UpdateAimVecs + PlayerDraw) and third-person biped (PlayerDraw).
    Mat4 gunTransform() const;
    Mat4 bipedTransform() const;
    // The biped's two animation slots: 1 up to the spine (legs), 2 above it (torso and gun arm).
    const AnimationState& bipedAnimation() const { return m_bipedAnim; }
    const AnimationState& bipedAnimation2() const { return m_bipedAnim2; }
    // PlayerDraw: the angle the spine leans by to aim up or down, in radians.
    float spineAngle() const;
    // PlayerDraw's drawBiped: third person, or still switching cameras.
    bool drawBiped() const;
    // Kanden's stinglarva: one matrix per node.
    const std::array<Mat4, 5>& kandenSegments() const { return m_kandenSegMtx; }
    int boostCharge() const { return m_boostCharge; }
    int altAttackCooldown() const { return m_altAttackCooldown; }

    int health() const { return m_health; }
    int healthMax() const { return m_healthMax; }
    int ammo(int slot) const { return m_ammo[slot]; }
    void setAmmo(int slot, int value) { m_ammo[slot] = value; }
    int ammoMax(int slot) const { return m_ammoMax[slot]; }
    int currentWeapon() const { return m_currentWeapon; }
    // TryEquipWeapon: `beam` if the player has it and its ammo, with the switch animation.
    bool selectWeapon(int beam) { return tryEquipWeapon(beam, false); }
    // The next or previous weapon the player can switch to, in the weapon wheel's order.
    void cycleWeapon(int direction);
    // Cheats.FreeWeaponSelect: every weapon, full ammo.
    void giveAllWeapons();
    const EquipInfo& equip() const { return m_equip; }
    // PlayerEntity.TakeDamage; returns true when this hit killed.
    bool takeDamage(int damage, uint32_t flags, const Vec3* direction, const DamageSource& source);
    void gainHealth(int health);
    bool dead() const { return m_health == 0; }
    int respawnTimer() const { return m_respawnTimer; }
    int timeSinceDead() const { return m_timeSinceDead; }
    int timeSinceDamage() const { return m_timeSinceDamage; }
    // PlayerHud._damageIndicatorTimers: north, ne, east, se, south, sw, west, nw (main player).
    const std::array<int, 8>& damageIndicators() const { return m_damageIndicatorTimers; }
    // CameraInfo.Shake: how far the camera shakes after a hit or a landing.
    float cameraShake() const { return m_cam.shake; }
    int frozenTimer() const { return m_frozenTimer; }
    int disruptedTimer() const { return m_disruptedTimer; }
    int burnTimer() const { return m_burnTimer; }
    bool headshotMessage() const { return m_headshotTimer > 0; }
    // The collision sphere beams test (Volume: PlayerVolumes[hunter, biped 0 / alt 2]).
    Vec3 volumeCenter() const { return sphereCenter(); }
    float volumeRadius() const { return m_altForm ? fx(v.AltColRadius) : fx(v.BipedColRadius); }
    const std::array<Vec3, 5>& kandenSegPositions() const { return m_kandenSegPos; }
    // The gun's two animation slots: 0 nodes (by gun animation), 1 materials (by gun animation and beam).
    const AnimationState& gunAnimation() const { return m_gunAnim; }
    const AnimationState& gunMaterialAnimation() const { return m_gunAnim2; }
    // CameraInfo.Fov over the hunter's normal FOV: below 1 while zoomed.
    float fovScale() const { return m_fov / (fx(v.NormalFov) * 2); }
    bool zoomed() const { return m_equip.zoomed; }
    bool hasWeapon(int beam) const { return m_availableWeapons[beam]; }
    int weaponSlot(int i) const { return m_weaponSlots[i]; }
    int doubleDamageTimer() const { return m_doubleDmgTimer; }
    int cloakTimer() const { return m_cloakTimer; }
    bool cloaking() const { return m_cloaking; }
    // PlayerEntity.CurAlpha: how visible the player is (cloaked, Trace standing still).
    float alpha() const { return m_curAlpha; }
    // A bot, played by a PlayerAi; the level picks its aim and reaction times (0-3).
    bool isBot() const { return m_isBot; }
    void setBot(bool bot, int level)
    {
        m_isBot = bot;
        m_botLevel = level;
    }
    int botLevel() const { return m_botLevel; }
    // TeamIndex: every player is on its own team in a battle.
    int teamIndex() const { return m_teamIndex; }
    void setTeamIndex(int team) { m_teamIndex = team; }
    // AbilityFlags for multiplayer: what the hunter's alt form can do.
    int abilities() const;
    // PlayerEntity.ClosestNode: the bots' node for this player, found on demand each tick.
    const NodeData3* closestNode = nullptr;
    // Survival, kept by the World: PlayerFlags2.RadarReveal (this tick and
    // the last) for a player hiding near where it spawned, and _hidingTimer.
    bool radarReveal = false, radarRevealPrevious = false;
    int hidingTimer = 0;
    // Prime Hunter, kept by the World (IsPrimeHunter): faster, higher jumps,
    // half again the damage, no health pickups, fading while standing still.
    bool primeHunter = false;
    // PlayerEntity.OctolithFlag: the Octolith this player carries, kept by the World.
    OctolithFlag* octolithFlag = nullptr;
    // PlayerEntity.IdlePosition: where it last spawned.
    const Vec3& idlePosition() const { return m_idlePosition; }

    // Online (NetPlayerBridge): a player on a machine that does not run the
    // match. Its damage is the authority's -- replayed from the snapshots
    // inside a DamageReplay -- and nothing this machine resolves may hurt it.
    void setNetReplica(bool replica) { m_netReplica = replica; }
    void setNetDamageHooks(NetDamageHooks* hooks) { m_netHooks = hooks; }
    // PlayerFlags2.Spectating (SpectatorMode): watching rather than playing --
    // hidden, not solid, no target for anybody's shots or bots.
    bool spectating() const { return m_spectating; }
    void netSetSpectating(bool spectating) { m_spectating = spectating; }
    // ModInPlay: alive and not sitting the match out.
    bool inPlay() const { return m_health > 0 && !m_spectating; }
    bool netReplica() const { return m_netReplica; }
    struct DamageReplay {
        DamageReplay() { s_damageReplay++; }
        ~DamageReplay() { s_damageReplay--; }
        DamageReplay(const DamageReplay&) = delete;
        DamageReplay& operator=(const DamageReplay&) = delete;
    };
    // Where the authority put it: position (and the previous one, so the
    // next collision sweep starts there), speed, health.
    void netPlace(const Vec3& position);
    void netSetSpeed(const Vec3& speed) { m_speed = speed; }
    void netSetHealth(int health) { m_health = std::clamp(health, 0, 999); }
    // ModSetFacing: the aim along a direction.
    void netSetFacing(const Vec3& facing);
    // ModSetWeapon: holding what the authority says, granted if need be.
    void netSetWeapon(int beam);
    void netSetZoom(bool zoomed);
    // ModStartFormSwitch and ModForceForm, for a form that disagrees with the authority's.
    void netStartFormSwitch();
    void netForceForm(bool altForm);
    // The afflictions the authority says it has (the freeze, the disruption, the fire).
    void netSetAfflictions(bool frozen, bool disrupted, bool burning);
    // PlayerEntity.ModSetShotState: what the owner says its next shot is
    // worth (the charge, the ram's damage, Double Damage), on the machine
    // that resolves it.
    void netSetShotState(int chargeLevel, int boostDamage, bool doubleDamage);
    // How many times this player has spawned: a new life each time.
    int spawnCount() const { return m_spawnCount; }
    // What the intent carries: the charge, the ram, where the gun points.
    int chargeLevel() const { return m_equip.chargeLevel; }
    int boostDamage() const { return m_boostDamage; }
    Vec3 aimVector() const { return facingVector(); }
    bool spawnedAlive() const { return m_health > 0; }
    // PlayerVolumes[hunter, biped].SpherePosition - [hunter, alt]: how far a
    // position moves between the two forms' frames of reference.
    Vec3 formOffset() const { return {0, fx(v.MinPickupHeight) + fx(v.BipedColRadius) - fx(v.AltColYPos), 0}; }

private:
    enum class CameraType { First, Third1, Third2 };
    struct CameraInfo {
        Vec3 position{0, 0, 1}, prevPosition{0, 0, 1}, target{}, up{0, 1, 0}, facing{0, 0, -1};
        float field48 = 0, field4C = -1, field50 = -1, field54 = 0;
        float shake = 0;
        bool shakeThisTick = true; // CameraInfo._shake: every other update
        void setShake(float value) { shake = std::max(shake, value); }
        void update();
    };

    void processBiped(const PlayerInput& input);
    void updateCloak();
    // PlayerInput.UpdateAimX/UpdateAimY for the bots' button aim: degrees left and up.
    void applyButtonAim(float aimX, float aimY);
    // Returns the torso animation the shot asks for (PlayerAnimation), -1 for none.
    int processWeapon(const PlayerInput& input, uint16_t& animFlags2);
    void checkPlayerCollision();
    static void checkAltAttackHit1(Player& attacker, Player& target, bool halfturret = false);
    static void checkAltAttackHit2(Player& attacker, Player& target, bool halfturret = false);
    void updateSpireRocks();
    bool notifyDamage(const DamageSource& source, bool died, uint32_t flags, int damage, int turretDamage = -1);
    bool tryFireWeapon(bool pressed);
    bool tryEquipWeapon(int beam, bool silent);
    void updateAffinityWeaponSlot(int beam);
    void unequipOmegaCannon();
    void spawnBomb();
    void updateWeaponEffects();
    void updateEffects();
    void clearWeaponEffects();
    void createBurnEffect();
    int spawnEffect(int effectId, const Vec3& facing, const Vec3& up, const Vec3& position, bool entry);
    void createHalfturret();
    void halfturretDie();
    void halfturretOnTakeDamage(Player* attacker, int damage);
    void checkHalfturretCollision(Player& other);
    void setGunAnimation(int anim, uint16_t flags);
    void updateGunAnimation();
    void updateAimVecs();
    void updateZoom(bool zoom);
    const WeaponInfo& weaponInfo(int beam) const { return weaponsMP().at(beam); }
    void processAlt(const PlayerInput& input);
    void processMovement();
    void checkCollision();
    void handleCollision(CollisionResult result);
    void updateBob();
    void pickUpWeapon(ItemType type);
    bool trySwitchForms();
    void enterAltForm();
    void exitAltForm();
    void endAltAttack();
    void updateForm(bool altForm);
    void initAltTransform();
    void updateAltTransform();
    void updateStinglarvaSegments();
    void switchCamera(CameraType type, const Vec3& facing);
    void updateCamera();
    void updateCameraFirst();
    void updateCameraThird1();
    void updateCameraThird2();
    // SetBipedAnimation: both slots by default; setIfMorphing false leaves them during a form switch.
    void setBipedAnimation(int index, uint16_t flags, bool setBiped1 = true, bool setBiped2 = true, bool setIfMorphing = true);
    void setBiped1Animation(int index, uint16_t flags) { setBipedAnimation(index, flags, true, false, false); }
    void setBiped2Animation(int index, uint16_t flags) { setBipedAnimation(index, flags, false, true, false); }
    void setAltAnimation(int index, uint16_t flags);
    bool checkBetweenPoints(const Vec3& a, const Vec3& b, CollisionResult& result) const;
    Vec3 sphereCenter() const; // Volume.SpherePosition
    Vec3 facingVector() const;
    Vec3 gunVec2() const { return {m_field74, 0, -m_field70}; }
    float fx(int32_t raw) const { return raw / 4096.0f; }

    Hunter m_hunter;
    const PlayerValues& v;
    const RoomCollision* m_collision;
    Vec3 m_idlePosition{};
    PlayerModels m_models;
    std::vector<DoorObstacle> m_doors;
    std::vector<ForceFieldObstacle> m_forceFields;
    long long m_frameCount = 0;

    Vec3 m_position{}, m_prevPosition{}, m_speed{}, m_prevSpeed{};
    float m_yaw = 0, m_pitch = 0;
    float m_field70 = 0, m_field74 = -1; // horizontal facing
    float m_field78 = 0, m_field7C = 0;  // _gunVec2: (field74, -field70)
    float m_field80 = 0, m_field84 = -1; // strafing alt forms' body facing
    float m_hSpeedCap = 0, m_hSpeedMag = 0, m_gravity = 0;
    int m_slipperiness = 0;
    int m_timeSinceGrounded = 0;
    int m_timeStanding = 0xFFFF;
    bool m_standing = false, m_standingPrevious = false, m_grounded = false;
    bool m_walking = false, m_strafing = false, m_movingBiped = false;
    bool m_groundedPrevious = false;
    bool m_onLava = false, m_onAcid = false; // PlayerFlags1: standing on lava, on a damaging face
    bool m_usedJump = false, m_collidingLateral = false;
    bool m_altFormGravity = false;
    bool m_gravityOverride = false; // PlayerFlags2.GravityOverride
    bool m_noUnmorph = false, m_noUnmorphPrevious = false;
    bool m_spireClimbing = false;
    Vec3 m_fieldC0{0, 1, 0};

    // Forms
    bool m_altForm = false, m_morphing = false, m_unmorphing = false;
    AnimationState m_bipedAnim, m_bipedAnim2, m_altAnim;
    int m_timeIdle = 0;
    Mat4 m_modelTransform = Mat4::identity();
    float m_altRollFbX = 0, m_altRollFbZ = -1, m_altRollLrX = -1, m_altRollLrZ = 0;
    // PlayerSound
    SoundSource m_soundSource;
    bool m_isMain = false;
    int m_missileSfxHandle = -1, m_healthSfxHandle = -1;
    float m_walkSfxTimer = 0;
    int m_walkSfxIndex = 0;
    float m_burnSfxAmount = 0, m_moveSfxAmount = 0, m_damageSfxTimer = 0;
    int m_timeBeforeLanding = 0;
    int m_dblDamageSfxHandle = -1, m_dblDamageSfxId = -1;
    int m_cloakSfxHandle = -1, m_cloakSfxId = -1;
    bool m_flagCarrySfxOn = false;
    int m_flagCarrySfxHandle = -1;
    void playHunterSfx(HunterSfx sfx);
    int playMissileSfx(HunterSfx sfx);
    void playRandomDamageSfx();
    void playBeamEmptySfx(int beam);
    void playBeamShotSfx(int beam, bool charged, bool continuous, bool homing, float amountA);
    int beamChargeSfx(int beam) const;
    void playBeamChargeSfx(int beam);
    void stopBeamChargeSfx(int beam);
    void updateHealthSfx(int health);
    void updateWalkingSfx();
    int altMovementSfx() const;
    void updateAltMovementSfx();
    void updateSlidingSfx(float amount);
    void updateMovementSfxAmount(float amount);
    void stopTerrainSfx(int previousTerrain);
    void stopAltFormSfx();
    void playLandingSfx();
    void updateBurningSfx(bool burning);
    void updateDoubleDamageSfx(int index, bool play);
    void updateCloakSfx(int index, bool play);
    int m_morphCameraId = -1;
    Vec3 m_morphCameraPosition{};
    bool m_altDirOverride = false;          // PlayerFlags1.AltDirOverride
    int m_timeSinceMorphCamera = 0xFFFF;
    uint8_t m_rollDirsHeld = 0;             // last tick's forward/back/left/right, for "pressed"
    void resumeOwnCamera();
    int m_boostCharge = 0;
    bool m_boosting = false, m_altAttack = false;
    int m_altAttackCooldown = 0, m_altAttackTime = 0;
    int m_accelerationTimer = 0;
    Vec3 m_acceleration{};
    float m_altSpinSpeed = 0, m_altTiltX = 0, m_altTiltZ = 0, m_altSpinRot = 0, m_altWobble = 0;
    std::array<Vec3, 16> m_spireAltVecs{};
    std::array<Vec3, 5> m_kandenSegPos{};
    std::array<Mat4, 5> m_kandenSegMtx{};

    // Jump pads
    bool m_usedJumpPad = false;
    Vec3 m_jumpPadAccel{};
    int m_jumpPadControlLock = 0, m_jumpPadControlLockMin = 0;
    int m_timeSinceJumpPad = 0xFFFF;

    // Camera
    CameraType m_cameraType = CameraType::First;
    CameraInfo m_cam;
    int m_camSwitchTimer = 0xFFFF;
    Vec3 m_field544{};
    int m_field551 = 0, m_field552 = 0, m_field553 = 0;
    float m_field554 = 0, m_field558 = 0, m_field68C = 0, m_field690 = 0;
    float m_gunViewBob = 0, m_walkViewBob = 0;
    float m_viewTiltAngleH = 0, m_viewTiltAngleV = 0;
    float m_field44C = 0;

    // Inventory (multiplayer rules)
    int m_health = 0, m_healthMax = 0;
    std::array<int, 2> m_ammo{}, m_ammoMax{};
    std::array<bool, 9> m_availableWeapons{};
    std::array<int, 3> m_weaponSlots{0, 2, -1};
    int m_currentWeapon = 0;
    int m_doubleDmgTimer = 0, m_cloakTimer = 0, m_deathaltTimer = 0;
    bool m_cloaking = false; // PlayerFlags2.Cloaking: the Cloak pickup's timer runs
    float m_curAlpha = 1, m_targetAlpha = 1;
    bool m_isBot = false;
    int m_botLevel = 0, m_teamIndex = 0;
    int m_horizColTimer = 0;  // ticks colliding sideways in a row
    int m_standTerrain = 0;   // Terrain of the face stood on

    // Weapons
    FireCallback m_fire;
    int m_slot = 0;
    EquipInfo m_equip;
    int m_previousWeapon = 0;
    int m_timeSinceShot = 255, m_autofireCooldown = 0, m_powerBeamAutofire = 0;
    bool m_shooting = false, m_noShotsFired = true;                      // PlayerFlags2.Shooting, NoShotsFired
    bool m_shotUncharged = false, m_shotCharged = false, m_shotMissile = false; // PlayerFlags1, this tick
    bool m_gunOpenAnimation = false;
    int m_gunAnimation = GunAnim::Idle;
    AnimationState m_gunAnim, m_gunAnim2;
    int m_timeSinceInput = 0;
    Vec3 m_muzzlePos{}, m_aimVec{0, 0, -1}, m_aimPosition{};
    float m_fov = 0;

    // Effects: the muzzle flash and the charge glow (EffectEntry handles)
    Effects* m_effects = nullptr;
    int m_muzzleEffect = -1, m_chargeEffect = -1;
    int m_deathaltEffect = -1, m_burnEffect = -1, m_doubleDmgEffect = -1, m_boostEffect = -1, m_furlEffect = -1;
    int m_turretBurnEffect = -1;
    bool m_chargeLoopEffect = false; // PlayerFlags2.ChargeEffect

    // Bombs
    BombCallback m_spawnBomb;
    int m_bombCooldown = 0, m_bombRefillTimer = 0, m_bombAmmo = 3, m_bombOveruse = 0;
    int m_syluxBombCount = 0;
    Halfturret m_halfturret;
    int m_shockCoilTarget = -1, m_shockCoilTimer = 0, m_timeSinceHitTarget = 0xFFFF;

    // Other players
    DamageListener m_damageListener;
    NetDamageHooks* m_netHooks = nullptr;
    bool m_spectating = false;
    const std::vector<Player*>* m_players = nullptr;
    int m_boostDamage = 0;
    Vec3 m_spireRockL{}, m_spireRockR{}, m_spireAltFacing{0, 0, 1}, m_spireAltUp{0, 1, 0};
    int m_spireRockNodes[2] = {-2, -2}; // -2: not looked up yet

    // Damage
    int m_respawnTimer = 0, m_timeSinceDead = 0, m_timeSinceDamage = 255;
    int m_damageInvulnTimer = 0, m_spawnInvulnTimer = 0;
    int m_frozenTimer = 0, m_timeSinceFrozen = 255, m_disruptedTimer = 0;
    int m_burnTimer = 0;
    Player* m_burnedBy = nullptr;
    int m_headshotTimer = 0;
    std::array<int, 8> m_damageIndicatorTimers{};
    std::optional<Vec3> m_deathLookAt;
    Vec3 m_pendingImpulse{}; // the knockback of the hit being taken, for its listener
    int m_spawnCount = 0;
    bool m_netReplica = false;
    static inline int s_damageReplay = 0;
};

} // namespace fp
