#include "Player.h"

#include "Effects.h"

#include "formats/Model.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fp {

namespace {

constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

Vec3 normalized(Vec3 v)
{
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? v * (1.0f / len) : v;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

Vec3 row(const Mat4& m, int r) { return {m.m[r][0], m.m[r][1], m.m[r][2]}; }

void setRow(Mat4& m, int r, const Vec3& v)
{
    m.m[r][0] = v[0];
    m.m[r][1] = v[1];
    m.m[r][2] = v[2];
}

// OpenTK Matrix4.CreateFromAxisAngle
Mat4 axisAngle(Vec3 axis, float angle)
{
    axis = normalized(axis);
    const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    const float x = axis[0], y = axis[1], z = axis[2];
    Mat4 m = Mat4::identity();
    m.m[0][0] = t * x * x + c;
    m.m[0][1] = t * x * y + s * z;
    m.m[0][2] = t * x * z - s * y;
    m.m[1][0] = t * x * y - s * z;
    m.m[1][1] = t * y * y + c;
    m.m[1][2] = t * y * z + s * x;
    m.m[2][0] = t * x * z + s * y;
    m.m[2][1] = t * y * z - s * x;
    m.m[2][2] = t * z * z + c;
    return m;
}

// Matrix.Vec3MultMtx3
Vec3 mult3(const Vec3& v, const Mat4& m)
{
    return {v[0] * m.m[0][0] + v[1] * m.m[1][0] + v[2] * m.m[2][0], v[0] * m.m[0][1] + v[1] * m.m[1][1] + v[2] * m.m[2][1],
        v[0] * m.m[0][2] + v[1] * m.m[1][2] + v[2] * m.m[2][2]};
}

// EntityBase.GetTransformMatrix(facing, up)
Mat4 transformMatrix(const Vec3& facing, const Vec3& up) { return Mat4::fromVectors(facing, up, {0, 0, 0}); }

constexpr std::array<std::array<int, 3>, 16> kSpireAltVectors{{{262, -2465, -1417}, {724, 3129, 192}, {2772, -770, -311},
    {-2289, -700, 1884}, {1224, -2617, 774}, {-450, 2183, 2150}, {2469, 1224, -966}, {-2236, -1044, -1540}, {-1519, -2510, 512},
    {-937, 2838, -950}, {2355, 892, 1273}, {-2695, 1212, -262}, {733, -1290, 2633}, {-516, 602, 2875}, {663, 1290, -2723},
    {-536, -737, -3047}}};

namespace Anim {
// PlayerAnimation
constexpr int BipedMorph = 0, Flourish = 1, WalkForward = 2, BipedUnmorph = 3, DamageBack = 4, DamageFront = 5, DamageLeft = 6,
              DamageRight = 7, Idle = 8, LandNeutral = 9, LandLeft = 10, LandRight = 11, JumpNeutral = 12, JumpBack = 13,
              JumpForward = 14, JumpLeft = 15, JumpRight = 16, WalkBackward = 18, Spawn = 19, WalkLeft = 20, WalkRight = 21,
              Charge = 23, ChargeShoot = 24, Shoot = 25; // (22 Turn: the mouse turn, not ported)
constexpr int TraceAttack = 1, WeavelAttack = 1, WeavelIdle = 0, TraceIdle = 0, SyluxIdle = 0, KandenIdle = 0;
constexpr int NoxusExtend = 0, SpireAttack = 0;
constexpr int KandenTailOut = 1, KandenTailIn = 2;
} // namespace Anim

} // namespace

void Player::CameraInfo::update()
{
    // CameraInfo.Update
    if (shake > 0 && shakeThisTick) {
        const Vec3 toTarget = target - position;
        const auto jitter = [this] { return randomInt2(static_cast<uint32_t>(shake * 4096)) / 4096.0f - shake / 2; };
        target[0] += jitter();
        target[1] += jitter();
        target[2] += jitter();
        if (toTarget[0] * (target[0] - position[0]) + toTarget[2] * (target[2] - position[2]) < 0) {
            target[0] = position[0] + toTarget[0] / 2;
            target[2] = position[2] + toTarget[2] / 2;
        }
        shake *= 0.85f;
        if (shake < 0.01f) {
            shake = 0;
        }
    }
    shakeThisTick = !shakeThisTick;
    facing = target - position;
    const float fx = facing[0], fz = facing[2];
    const float hMag = std::sqrt(fx * fx + fz * fz);
    facing = normalized(facing);
    if (hMag > 0) {
        field48 = fx / hMag;
        field4C = fz / hMag;
    }
    field50 = field4C;
    field54 = -field48;
}

Player::Player(int hunter, const RoomCollision* collision)
    : m_hunter(static_cast<Hunter>(hunter))
    , v(playerValuesTable().at(hunter))
    , m_collision(collision)
{
}

void Player::setObstacles(std::vector<DoorObstacle> doors, std::vector<ForceFieldObstacle> forceFields)
{
    m_doors = std::move(doors);
    m_forceFields = std::move(forceFields);
}

Vec3 Player::sphereCenter() const
{
    // _volume = PlayerVolumes[hunter, biped 0 / alt 2] moved to Position.
    if (m_altForm) {
        return {m_position[0], m_position[1] + fx(v.AltColYPos), m_position[2]};
    }
    return {m_position[0], m_position[1] + fx(v.MinPickupHeight) + fx(v.BipedColRadius), m_position[2]};
}

Vec3 Player::facingVector() const
{
    const float cp = std::cos(m_pitch * kDegToRad);
    return {-std::sin(m_yaw * kDegToRad) * cp, std::sin(m_pitch * kDegToRad), -std::cos(m_yaw * kDegToRad) * cp};
}

bool Player::firstPerson() const
{
    return m_cameraType == CameraType::First && m_camSwitchTimer >= v.CamSwitchTime * 2;
}

void Player::spawn(const Vec3& position, const Vec3& facing, bool respawn)
{
    m_spawnCount++;
    m_position = position;
    m_prevPosition = position;
    m_idlePosition = position;
    hidingTimer = 0;
    radarReveal = radarRevealPrevious = false;
    octolithFlag = nullptr;
    m_gravityOverride = false;
    m_speed = {};
    m_prevSpeed = {};
    const float hMag = std::sqrt(facing[0] * facing[0] + facing[2] * facing[2]);
    m_yaw = hMag > 0 ? std::atan2(-facing[0], -facing[2]) / kDegToRad : 0;
    m_pitch = 0;
    setAim(m_yaw, 0);
    m_field80 = m_field70;
    m_field84 = m_field74;
    m_hSpeedCap = fx(v.WalkSpeedCap);
    m_standing = m_standingPrevious = m_grounded = false;
    m_usedJump = false;
    m_timeSinceGrounded = 0;
    m_usedJumpPad = false;
    m_jumpPadAccel = {};
    m_jumpPadControlLock = m_jumpPadControlLockMin = 0;
    m_gunViewBob = m_walkViewBob = 0;
    m_viewTiltAngleH = m_viewTiltAngleV = 0;
    m_timeStanding = 0xFFFF;
    m_altForm = m_morphing = m_unmorphing = false;
    m_boostCharge = 0;
    m_boosting = m_altAttack = false;
    m_altAttackCooldown = m_altAttackTime = m_accelerationTimer = 0;
    m_bombCooldown = m_bombOveruse = m_bombRefillTimer = 0;
    m_bombAmmo = 3;
    m_syluxBombCount = 0;
    m_halfturret = Halfturret{};
    m_shockCoilTarget = -1;
    m_shockCoilTimer = 0;
    m_timeSinceHitTarget = 0xFFFF;
    clearWeaponEffects();
    if (respawn) {
        // spawnEffectMP or spawnEffect
        spawnEffect(m_players != nullptr && m_players->size() > 2 ? 33 : 31, {1, 0, 0}, {0, 1, 0}, position, false);
    }
    m_bipedAnim = {};
    m_bipedAnim2 = {};
    m_altAnim = {};
    m_timeIdle = 0;
    m_damageIndicatorTimers.fill(0);
    m_cam.shake = 0;
    m_cameraType = CameraType::First;
    m_camSwitchTimer = v.CamSwitchTime * 2;
    m_cam.position = m_cam.prevPosition = {m_position[0], m_position[1] + fx(v.AimYOffset), m_position[2]};
    m_cam.target = m_cam.position + facingVector();
    m_cam.update();
    // PlayerEntity: multiplayer health and ammo, InitializeWeapon.
    m_healthMax = 2 * v.EnergyTank - 1;
    m_health = v.EnergyTank - 1;
    m_ammoMax = {v.MpAmmoCap, v.MpAmmoCap};
    m_availableWeapons.fill(false);
    m_availableWeapons[0] = true; // Power Beam
    m_availableWeapons[2] = true; // Missile
    m_weaponSlots = {0, 2, -1};
    const WeaponInfo& missile = weaponsMP()[2];
    m_ammo = {0, 0};
    m_ammo[missile.ammoType] = 10 * missile.ammoCost;
    m_currentWeapon = 0;
    m_doubleDmgTimer = m_cloakTimer = m_deathaltTimer = 0;
    m_cloaking = false;
    m_curAlpha = m_targetAlpha = 1;
    m_horizColTimer = 0;
    // TryEquipWeapon(PowerBeam, silent) and the timers Spawn resets.
    m_equip = {};
    m_previousWeapon = 0;
    tryEquipWeapon(0, true);
    m_shooting = false;
    m_noShotsFired = true;
    m_shotUncharged = m_shotCharged = m_shotMissile = false;
    m_timeSinceShot = 255;
    m_timeSinceDamage = 255;
    m_timeSinceInput = 0;
    m_autofireCooldown = m_powerBeamAutofire = 0;
    m_respawnTimer = 0;
    m_timeSinceDead = 0;
    m_damageInvulnTimer = 0;
    m_spawnInvulnTimer = respawn ? v.SpawnInvulnerability * 2 : 0;
    m_frozenTimer = m_disruptedTimer = m_burnTimer = 0;
    m_timeSinceFrozen = 255;
    m_burnedBy = nullptr;
    m_headshotTimer = 0;
    m_deathLookAt.reset();
    m_morphCameraId = -1;
    m_altDirOverride = false;
    m_timeSinceMorphCamera = 0xFFFF;
    m_fov = fx(v.NormalFov) * 2;
    setGunAnimation(GunAnim::Idle, AnimFlags::NoLoop);
    setBipedAnimation(Anim::Spawn, 0);
    updateAimVecs();
    // PlayerEntity.Spawn: the last life's sounds stop, the new one's start here.
    m_soundSource.stopAllSfx();
    m_soundSource.update(m_position, m_isMain ? -1 : 1);
    m_missileSfxHandle = -1;
    m_burnSfxAmount = m_moveSfxAmount = 0;
    m_walkSfxTimer = 0;
    m_walkSfxIndex = 0;
    if (m_isMain) {
        updateDoubleDamageSfx(0, false);
        updateCloakSfx(0, false);
    }
    if (respawn) {
        playHunterSfx(HunterSfx::Spawn);
    }
}

void Player::setAim(float yawDegrees, float pitchDegrees)
{
    m_yaw = yawDegrees;
    m_pitch = std::clamp(pitchDegrees, -75.0f, 75.0f); // ProcessMovement resets _aimY past 75
    m_field70 = -std::sin(m_yaw * kDegToRad);
    m_field74 = -std::cos(m_yaw * kDegToRad);
    m_field78 = m_field74;
    m_field7C = -m_field70;
}

CameraPose Player::camera() const
{
    if (m_health == 0 && m_deathLookAt) {
        // SwitchCamera(CameraType.Free, ...): the dead look at their killer, or ahead.
        return {m_cam.position, *m_deathLookAt, {0, 1, 0}};
    }
    return {m_cam.position, m_cam.target, m_cam.up};
}

CameraPose Player::matchEndCamera(float seconds) const
{
    const Vec3 facing = facingVector();
    const Vec3 gunVec = gunVec2();
    Vec3 target = m_position;
    Vec3 position;
    if (m_altForm || m_morphing || m_unmorphing) {
        position = m_position + Vec3{-(2.75f * m_field70), 3, -(2.75f * m_field74)};
    } else {
        target = target + facing * 10;
        // The C# offsets both X and Z by half of _gunVec2.X.
        position = m_position + Vec3{-(1.5f * m_field70 + gunVec[0] / 2), 1.75f, -(1.5f * m_field74 + gunVec[0] / 2)};
    }
    if (facing[1] < 0) {
        position = position + Vec3{-(2.15f * facing[1] * m_field70), -(facing[1] / 2), -(2.15f * facing[1] * m_field74)};
    } else {
        position = position + Vec3{0.75f * facing[1] * m_field70, -(facing[1] * 2), 0.75f * facing[1] * m_field74};
    }
    const float factor = fx(15) * seconds * 30;
    position = position + Vec3{gunVec[0] * factor, 0, gunVec[2] * factor};
    CollisionResult result;
    if (m_collision != nullptr && m_collision->checkBetweenPoints(m_position, position, TestFlags::Players, result)) {
        const Vec3 between = position - m_position;
        position = m_position + between * result.distance + Vec3{result.plane[0], result.plane[1], result.plane[2]} * 0.05f;
    }
    return {position, target, {0, 1, 0}};
}

bool Player::drawBiped() const
{
    return !m_altForm && (m_cameraType != CameraType::First || m_camSwitchTimer < v.CamSwitchTime * 2);
}

Mat4 Player::gunTransform() const
{
    // PlayerProcess.UpdateAimVecs: the gun sits off the camera and drifts half
    // way toward the point being aimed at.
    const Vec3 facing = facingVector();
    const Vec3 right = gunVec2();
    const Vec3 up = normalized(cross(facing, right));
    Vec3 gunDrawPos = facing * fx(v.FieldB8) + m_cam.position + right * fx(v.FieldB0) + up * fx(v.FieldB4);
    gunDrawPos[1] += fx(20) * std::cos(m_gunViewBob * kDegToRad);
    const Vec3 aimPosition = facing * fx(v.AimDistance) + m_cam.position;
    Vec3 aimVec = aimPosition - gunDrawPos;
    const Vec3 along = facing * dot(aimVec, facing);
    aimVec = normalized(aimVec + (along - aimVec) * 0.5f);
    return Mat4::fromVectors(aimVec, up, gunDrawPos);
}

Mat4 Player::bipedTransform() const
{
    // PlayerDraw: the biped stands on the ground under Position, scaled per hunter.
    static constexpr float scales[8] = {1.0f, 0x10F5 / 4096.0f, 1.0f, 1.0f, 1.0f, 0x123D / 4096.0f, 1.0f, 1.0f};
    const float scale = scales[static_cast<int>(m_hunter)];
    const float bottom = fx(v.MinPickupHeight);
    const Vec3 lateral{m_field70, 0, m_field74};
    const Vec3 right = gunVec2();
    Mat4 t = Mat4::identity();
    setRow(t, 0, right * -scale);
    setRow(t, 1, cross(lateral, right) * scale);
    setRow(t, 2, lateral * -scale);
    t.m[3][0] = m_position[0];
    t.m[3][1] = m_position[1] + bottom + bottom * (1 - scale);
    t.m[3][2] = m_position[2];
    return t;
}

Mat4 Player::altModelTransform() const
{
    Mat4 m = m_modelTransform;
    m.m[3][0] = m_position[0];
    m.m[3][1] = m_position[1];
    m.m[3][2] = m_position[2];
    return m;
}

void Player::setBipedAnimation(int index, uint16_t flags, bool setBiped1, bool setBiped2, bool setIfMorphing)
{
    if (m_models.biped == nullptr || (!setIfMorphing && m_morphing)) {
        return;
    }
    if (setBiped2 && (setIfMorphing || !m_unmorphing)) {
        m_bipedAnim2.set(*m_models.biped, index, flags);
    }
    if (setBiped1) {
        m_bipedAnim.set(*m_models.biped, index, flags);
    }
}

float Player::spineAngle() const
{
    // PlayerDraw: the torso leans with the facing vector's pitch, up to about 45 degrees.
    const Vec3 facing = facingVector();
    const float limit = fx(2896);
    float cos = std::sqrt(std::max(0.0f, 1 - facing[1] * facing[1]));
    float sin = facing[1];
    if (std::fabs(facing[1]) > limit) {
        cos = limit;
        sin = facing[1] <= 0 ? -limit : limit;
    }
    return std::atan2(sin, cos);
}

void Player::setAltAnimation(int index, uint16_t flags)
{
    if (m_models.alt != nullptr) {
        m_altAnim.set(*m_models.alt, index, flags);
    }
}

void Player::despawn()
{
    if (m_halfturret.active) {
        halfturretDie();
    }
    clearWeaponEffects();
    updateZoom(false);
    m_health = 0;
    m_speed = {};
    m_equip.chargeLevel = 0;
    m_shooting = false;
    m_respawnTimer = 0;
    m_timeSinceDead = 0;
    m_deathLookAt.reset();
}

void Player::teleport(const Vec3& position, const Vec3& facing)
{
    m_soundSource.playSfx(SfxId::TELEPORT_OUT, false, true);
    m_position = position;
    const float hMag = std::sqrt(facing[0] * facing[0] + facing[2] * facing[2]);
    const float yaw = hMag > 0 ? std::atan2(-facing[0], -facing[2]) / kDegToRad : m_yaw;
    setAim(yaw, std::atan2(facing[1], hMag) / kDegToRad);
    if (m_altForm || m_morphing || m_unmorphing) {
        resumeOwnCamera();
        m_cam.update();
    }
    // TeleporterEntity.Process
    m_speed = {0, m_speed[1], 0};
}

void Player::resumeOwnCamera()
{
    // PlayerCamera.ResumeOwnCamera
    if (m_cameraType == CameraType::Third1) {
        m_cam.target = m_position;
        m_cam.position = m_cam.target - Vec3{m_field80 * fx(v.Field78), 0, m_field84 * fx(v.Field78)};
        m_field544 = m_cam.position;
        m_cam.target[1] += fx(v.Field80);
    } else {
        m_field68C = fx(v.Field80);
        m_field690 = fx(v.Field78);
        m_cam.target = m_position + Vec3{0, m_field68C + fx(v.AltColYPos), 0};
        m_cam.position = m_cam.target - facingVector() * m_field690;
        m_field544 = m_cam.position;
    }
}

void Player::setMorphCamera(int id, const Vec3& position)
{
    m_morphCameraId = id;
    m_morphCameraPosition = position;
    // RefreshExternalCamera
    m_altDirOverride = true;
    m_timeSinceMorphCamera = 0;
}

void Player::clearMorphCamera(bool refresh)
{
    m_morphCameraId = -1;
    resumeOwnCamera();
    if (refresh) {
        m_altDirOverride = true;
        m_timeSinceMorphCamera = 0;
    }
}

// ---- tick ----------------------------------------------------------------

void Player::tick(const PlayerInput& input)
{
    m_frameCount++;
    // PlayerEntity.ProcessPlayer: timers
    m_shotUncharged = m_shotCharged = m_shotMissile = false;
    if (m_respawnTimer > 0) {
        m_respawnTimer--;
    }
    if (m_damageInvulnTimer > 0) {
        m_damageInvulnTimer--;
    }
    if (m_spawnInvulnTimer > 0) {
        m_spawnInvulnTimer--;
    }
    if (m_disruptedTimer > 0) {
        m_disruptedTimer--;
    }
    if (m_headshotTimer > 0) {
        m_headshotTimer--;
    }
    for (int& timer : m_damageIndicatorTimers) {
        if (timer > 0) {
            timer--;
        }
    }
    if (m_timeSinceShot < 0xFFFF) {
        m_timeSinceShot++;
    }
    if (m_timeSinceDamage < 0xFFFF) {
        m_timeSinceDamage++;
    }
    if (m_timeSinceHitTarget != 0xFFFF && ++m_timeSinceHitTarget > 1) {
        // A tick without a hit ends the Shock Coil's hold.
        m_shockCoilTimer = 0;
        m_shockCoilTarget = -1;
    }
    m_timeSinceInput = input.hasInput ? 0 : m_timeSinceInput + 1;
    updateCloak();
    if (m_health == 0) {
        if (m_timeSinceDead < 0xFFFF) {
            m_timeSinceDead++;
        }
        const int switchTime = v.CamSwitchTime * 2;
        if (m_deathLookAt && m_camSwitchTimer < switchTime) {
            // UpdateCameraFree: back away from the killer, behind the body
            m_camSwitchTimer++;
            const float pct = m_camSwitchTimer / static_cast<float>(switchTime);
            Vec3 camVec = m_cam.position - *m_deathLookAt;
            const float len = std::sqrt(dot(camVec, camVec));
            camVec = len > 0 ? camVec * (1 / len) : facingVector();
            const Vec3 posVec = sphereCenter() + camVec * fx(v.Field78);
            m_cam.position = m_field544 + (posVec - m_field544) * pct;
        }
        m_prevPosition = m_position;
        return;
    }
    if (m_timeSinceMorphCamera != 0xFFFF) {
        m_timeSinceMorphCamera++;
    }
    if (m_altAttackCooldown > 0) {
        m_altAttackCooldown--;
    }
    if (m_jumpPadControlLock > 0) {
        m_jumpPadControlLock--;
    }
    if (m_jumpPadControlLockMin > 0) {
        m_jumpPadControlLockMin--;
    }
    if (m_timeSinceJumpPad < 0xFFFF) {
        m_timeSinceJumpPad++;
    }
    // PlayerProcess: bomb ammo refills; Sylux's is the Lockjaw bombs not out.
    if (m_hunter == Hunter::Samus) {
        if (m_bombRefillTimer > 0) {
            m_bombRefillTimer--;
        } else {
            m_bombAmmo = 3;
        }
    } else if (m_hunter == Hunter::Sylux) {
        m_bombAmmo = m_bombCooldown > 0 ? 0 : 3 - m_syluxBombCount;
    } else {
        m_bombAmmo = 1;
    }
    if (m_bombCooldown > 0) {
        m_bombCooldown--;
        if (m_hunter == Hunter::Kanden && m_bombCooldown == 10 * 2) {
            setAltAnimation(Anim::KandenTailIn, AnimFlags::NoLoop);
        }
    }
    if (m_hunter == Hunter::Sylux && m_bombOveruse > 0) {
        m_bombOveruse--;
    }
    for (int* timer : {&m_doubleDmgTimer, &m_deathaltTimer}) {
        if (*timer > 0) {
            (*timer)--;
        }
    }
    if (m_isMain) {
        // The Double Damage countdown's three stages.
        if (m_doubleDmgTimer == 0) {
            if (m_dblDamageSfxId != -1) {
                updateDoubleDamageSfx(0, false);
            }
        } else if (m_doubleDmgTimer == 210 * 2) {
            updateDoubleDamageSfx(1, true);
        } else if (m_doubleDmgTimer == 120 * 2) {
            updateDoubleDamageSfx(2, true);
        }
    }
    m_prevPosition = m_position;
    m_prevSpeed = m_speed;
    m_movingBiped = false;
    if (m_boosting && m_hSpeedMag <= fx(v.AltMinHSpeed)) {
        m_boosting = false;
    }
    // The equipped weapon is out of ammo: the best slot that is not.
    if (m_equip.weapon != nullptr) {
        const int ammo = m_ammo[m_equip.weapon->ammoType];
        if (ammo >= 0 && ammo < m_equip.weapon->ammoCost) {
            int slot = 0;
            int priority = 0;
            for (int i = 0; i < 3; i++) {
                if (m_weaponSlots[i] >= 0) {
                    const WeaponInfo& info = weaponInfo(m_weaponSlots[i]);
                    if (info.priority > priority && m_ammo[info.ammoType] >= info.ammoCost) {
                        priority = info.priority;
                        slot = i;
                    }
                }
            }
            tryEquipWeapon(m_weaponSlots[slot], false);
        }
    }
    // PlayerInput.ProcessInput
    m_walking = false;
    m_strafing = false;
    if (m_frozenTimer > 0) {
        m_frozenTimer--;
        m_timeSinceFrozen = 0;
        if (m_frozenTimer == 0) {
            m_soundSource.playSfx(SfxId::SHOTGUN_BREAK_FREEZE);
        }
    }
    if (m_timeSinceFrozen < 0xFFFF) {
        m_timeSinceFrozen++;
    }
    if (m_altForm || m_morphing) {
        processAlt(input);
    } else {
        processBiped(input);
    }
    // PlayerProcess: the sounds follow the player; the main player's own, in
    // first person, are heard at the listener.
    m_soundSource.update(m_position, m_isMain && !m_altForm ? -1 : 1);
    if (m_isMain) {
        updateHealthSfx(m_health);
    }
    if (m_health > 0) {
        if (m_grounded && !m_groundedPrevious) {
            playLandingSfx();
        }
        if (m_altForm) {
            updateAltMovementSfx();
        }
    }
    updateBob();
    if (!m_equip.zoomed) {
        // CameraInfo.Fov eases back to the normal FOV.
        const float normalFov = fx(v.NormalFov) * 2;
        const float diff = normalFov - m_fov;
        m_fov = std::fabs(diff) >= 0.1f * 2 ? m_fov + diff / 4 : normalFov;
    }
    updateGunAnimation();
    updateWeaponEffects();
    updateEffects();
    // EntityBase.UpdateAnimFrames: every other tick (the Shock Coil's shot every tick).
    if (m_frameCount % 2 == 0) {
        if (m_frozenTimer == 0) {
            m_bipedAnim.advance();
            m_bipedAnim2.advance();
        }
        m_altAnim.advance();
    }
    if (m_frameCount % 2 == 0 || (m_currentWeapon == Beam::ShockCoil && m_gunAnimation == GunAnim::Shot)) {
        m_gunAnim.advance();
        m_gunAnim2.advance();
    }
    if (m_burnTimer > 0) {
        m_burnTimer--;
        if (m_burnTimer % (8 * 2) == 0) {
            DamageSource source;
            source.attacker = m_burnedBy;
            takeDamage(1, DamageFlags::NoSfx | DamageFlags::Burn | DamageFlags::NoDmgInvuln, nullptr, source);
        }
    }
    if (m_isMain && m_gunAnim.frame == 15 && m_frameCount % 2 == 0
        && (m_gunAnimation == GunAnim::Unknown9 || m_gunAnimation == GunAnim::MissileShot)) {
        Sfx::instance().stopSoundByHandle(m_missileSfxHandle);
        m_missileSfxHandle = playMissileSfx(HunterSfx::MissileOpen);
    }
    // Acid, and lava for everybody but Spire: a point every eight ticks standing on it.
    if (m_health > 0 && m_standing && (m_timeStanding == 0 || m_frameCount % (4 * 2) == 0)
        && (m_onAcid || (m_onLava && m_hunter != Hunter::Spire))) {
        takeDamage(1, DamageFlags::IgnoreInvuln | (m_onLava ? DamageFlags::NoSfx : 0), nullptr, {});
    }
    // The form switch completes when the biped's morph or unmorph animation ends.
    const bool bipedEnded = m_bipedAnim.index < 0 || (m_bipedAnim.flags & AnimFlags::Ended);
    if (bipedEnded) {
        if (m_morphing) {
            m_morphing = false;
            updateForm(true);
        } else if (m_unmorphing) {
            m_unmorphing = false;
        }
    }
}

void Player::processBiped(const PlayerInput& input)
{
    // PlayerInput.ProcessBiped: walking and strafing build a speed delta that is
    // applied after this tick's movement; a jump sets the vertical speed now.
    Vec3 speedDelta{};
    int anim1 = -1, anim2 = -1;
    uint16_t animFlags1 = 0, animFlags2 = 0;
    auto moveRightLeft = [&](int walkAnim, int sign) {
        m_strafing = true;
        m_movingBiped = true;
        m_walking = m_standing;
        float traction = fx(v.StrafeBipedTraction);
        if (m_jumpPadControlLockMin > 0) {
            traction *= fx(v.JumpPadSlideFactor);
        } else if (m_standing && m_slipperiness != 0) {
            traction *= tractionFactors()[m_slipperiness];
        }
        speedDelta[0] -= m_field78 * traction * sign;
        speedDelta[2] -= m_field7C * traction * sign;
        if (!input.forward && !input.back && m_grounded && m_timeSinceJumpPad > 7 * 2) {
            anim1 = walkAnim;
        }
        m_viewTiltAngleH = std::clamp(m_viewTiltAngleH + fx(v.ViewTiltIncrement) * sign / 2, -180.0f, 180.0f);
    };
    auto moveForwardBack = [&](int walkAnim, int sign) {
        m_movingBiped = true;
        m_walking = m_standing;
        float traction = fx(v.WalkBipedTraction);
        if (m_jumpPadControlLockMin > 0) {
            traction *= fx(v.JumpPadSlideFactor);
        } else if (m_standing && m_slipperiness != 0) {
            traction *= tractionFactors()[m_slipperiness];
        }
        speedDelta[0] += m_field70 * traction * sign;
        speedDelta[2] += m_field74 * traction * sign;
        if (m_grounded && m_timeSinceJumpPad > 7 * 2) {
            anim1 = walkAnim;
        }
        m_viewTiltAngleV = std::clamp(m_viewTiltAngleV + fx(v.ViewTiltIncrement) * sign / 2, -180.0f, 180.0f);
    };
    auto decayTilt = [](float& angle) {
        if (angle < 500 / 4096.0f && angle > -500 / 4096.0f) {
            angle = 0;
        } else {
            angle *= 0.9f;
        }
    };
    const bool canMove = m_frozenTimer == 0;
    if (canMove && m_isBot) {
        applyButtonAim(input.buttonAimX, input.buttonAimY);
    }
    if (canMove && input.right) {
        moveRightLeft(Anim::WalkRight, 1);
    } else if (canMove && input.left) {
        moveRightLeft(Anim::WalkLeft, -1);
    }
    decayTilt(m_viewTiltAngleH);
    if (canMove && input.forward) {
        moveForwardBack(Anim::WalkForward, 1);
    } else if (canMove && input.back) {
        moveForwardBack(Anim::WalkBackward, -1);
    }
    decayTilt(m_viewTiltAngleV);
    bool jumping = false;
    if (canMove && m_jumpPadControlLockMin == 0 && input.jumpPressed && !m_usedJump) {
        jumping = true;
        m_usedJump = true; // no Space Jump in multiplayer
        m_speed[1] = primeHunter ? 0.35f : fx(v.JumpSpeed);
        m_timeSinceGrounded = 8 * 2;
        playHunterSfx(HunterSfx::Jump);
    }
    if (canMove && (jumping || m_timeSinceJumpPad == 1)) {
        animFlags1 = AnimFlags::NoLoop;
        if (input.forward) {
            anim1 = Anim::JumpForward;
        } else if (input.back) {
            anim1 = Anim::JumpBack;
        } else if (input.left) {
            anim1 = Anim::JumpLeft;
        } else if (input.right) {
            anim1 = Anim::JumpRight;
        } else {
            anim1 = Anim::JumpNeutral;
        }
    }

    processMovement();
    updateCamera();
    updateAimVecs();

    if (m_frozenTimer == 0 && !m_unmorphing) {
        anim2 = processWeapon(input, animFlags2);
        if (input.morphPressed) {
            trySwitchForms();
            anim1 = anim2 = Anim::BipedMorph;
        }
    }
    const float magBefore = std::sqrt(m_speed[0] * m_speed[0] + m_speed[2] * m_speed[2]);
    m_speed = m_speed + speedDelta;
    const float magAfter = std::sqrt(m_speed[0] * m_speed[0] + m_speed[2] * m_speed[2]);
    if (magAfter > magBefore && magAfter > m_hSpeedCap) {
        const float factor = magBefore <= m_hSpeedCap ? m_hSpeedCap / magAfter : magBefore / magAfter;
        m_speed[0] *= factor;
        m_speed[2] *= factor;
    }
    if (m_frozenTimer > 0) {
        return;
    }
    // The legs play what the movement asked for, else settle back to idle;
    // the torso follows the legs unless a shot has its own.
    if (anim1 < 0) {
        if (m_grounded) {
            if (m_bipedAnim.index == Anim::Idle) {
                if (++m_timeIdle > 300 * 2 && m_timeSinceInput > 300 * 2) {
                    setBiped1Animation(Anim::Flourish, AnimFlags::NoLoop);
                }
            } else if (!(m_bipedAnim.flags & AnimFlags::NoLoop) || (m_bipedAnim.flags & AnimFlags::Ended)) {
                m_timeIdle = 0;
                setBiped1Animation(Anim::Idle, 0);
            }
        }
    } else if (anim1 != m_bipedAnim.index || (anim1 >= Anim::JumpNeutral && anim1 <= Anim::JumpRight)) {
        setBiped1Animation(anim1, animFlags1);
    }
    if (anim2 < 0) {
        if ((!(m_bipedAnim2.flags & AnimFlags::NoLoop) || (m_bipedAnim2.flags & AnimFlags::Ended))
            && m_bipedAnim2.index != m_bipedAnim.index) {
            setBiped2Animation(m_bipedAnim.index, m_bipedAnim.flags);
            if (!m_morphing) {
                m_bipedAnim2.frame = m_bipedAnim.frame;
            }
        }
    } else if (anim2 != m_bipedAnim2.index) {
        setBiped2Animation(anim2, animFlags2);
    }
}

void Player::processAlt(const PlayerInput& input)
{
    // PlayerInput.ProcessAlt
    Vec3 speedDelta{};
    int animId = -1;
    uint16_t animFlags = 0;
    m_usedJump = true;
    // The roll directions follow the camera, but are kept as they were when a
    // morph camera takes over or lets go, until the directions are let go or
    // a new one is pressed.
    const uint8_t dirs = (input.forward ? 1 : 0) | (input.back ? 2 : 0) | (input.left ? 4 : 0) | (input.right ? 8 : 0);
    if (dirs == 0 || (dirs & ~m_rollDirsHeld) != 0) {
        m_altDirOverride = false;
    }
    m_rollDirsHeld = dirs;
    if (m_timeSinceMorphCamera > 10 * 2 && !m_altDirOverride
        && (std::fabs(m_cam.field48) >= 1 / 4096.0f || std::fabs(m_cam.field4C) >= 1 / 4096.0f)) {
        m_altRollFbX = m_cam.field48;
        m_altRollFbZ = m_cam.field4C;
        m_altRollLrX = m_cam.field50;
        m_altRollLrZ = m_cam.field54;
    }
    const bool strafeAlt = v.AltFormStrafe != 0;
    const bool traceOrWeavel = m_hunter == Hunter::Trace || m_hunter == Hunter::Weavel;
    if (strafeAlt) {
        // Trace, Sylux, Weavel: move like the biped, relative to the aim.
        if (m_isBot && m_frozenTimer == 0) {
            applyButtonAim(input.buttonAimX, input.buttonAimY);
        }
        if (!(m_hunter == Hunter::Trace && m_altAttack)) {
            auto moveRightLeft = [&](int walkAnim, int sign) {
                m_strafing = true;
                m_movingBiped = true;
                m_walking = m_standing;
                float traction = fx(v.StrafeBipedTraction);
                if (m_jumpPadControlLockMin > 0) {
                    traction *= fx(v.JumpPadSlideFactor);
                }
                speedDelta[0] -= m_field78 * traction * sign;
                speedDelta[2] -= m_field7C * traction * sign;
                if (traceOrWeavel) {
                    animId = walkAnim;
                    animFlags = 0;
                }
            };
            auto moveForwardBack = [&](int walkAnim, int sign) {
                m_movingBiped = true;
                m_walking = m_standing;
                float traction = fx(v.WalkBipedTraction);
                if (m_jumpPadControlLockMin > 0) {
                    traction *= fx(v.JumpPadSlideFactor);
                } else if (m_standing && m_slipperiness != 0) {
                    traction *= tractionFactors()[m_slipperiness];
                }
                speedDelta[0] += m_field70 * traction * sign;
                speedDelta[2] += m_field74 * traction * sign;
                if (traceOrWeavel) {
                    animId = walkAnim;
                    animFlags = 0;
                }
            };
            if (input.right) {
                moveRightLeft(4, 1);
            } else if (input.left) {
                moveRightLeft(2, -1);
            }
            if (input.forward) {
                moveForwardBack(3, 1);
            } else if (input.back) {
                moveForwardBack(m_hunter == Hunter::Weavel ? 6 : 5, -1);
            }
        }
    } else {
        // Samus, Kanden, Spire, Noxus: roll relative to the camera.
        float traction = fx(v.RollAltTraction);
        if (m_jumpPadControlLockMin > 0) {
            traction *= fx(v.JumpPadSlideFactor);
        }
        if (input.forward) {
            speedDelta[0] += m_altRollFbX * traction;
            speedDelta[2] += m_altRollFbZ * traction;
        } else if (input.back) {
            speedDelta[0] -= m_altRollFbX * traction;
            speedDelta[2] -= m_altRollFbZ * traction;
        }
        if (input.left) {
            speedDelta[0] += m_altRollLrX * traction;
            speedDelta[2] += m_altRollLrZ * traction;
        } else if (input.right) {
            speedDelta[0] -= m_altRollLrX * traction;
            speedDelta[2] -= m_altRollLrZ * traction;
        }
    }
    if (!m_morphing) {
        const bool hasBombs = m_hunter == Hunter::Samus || m_hunter == Hunter::Kanden || m_hunter == Hunter::Sylux;
        if (hasBombs && input.altAttackPressed && m_bombAmmo > 0 && m_bombCooldown == 0) {
            spawnBomb();
        }
        if (m_hunter == Hunter::Noxus) {
            if (input.altAttackHeld) {
                if (input.altAttackPressed) {
                    m_altAttackTime = 1;
                    setAltAnimation(Anim::NoxusExtend, AnimFlags::NoLoop);
                } else if (m_altAttackTime > 0) {
                    m_altAttackTime++;
                    const int startupTime = v.AltAttackStartup * 2;
                    if (m_altAttackTime == 7 * 2) {
                        m_soundSource.playSfx(SfxId::NOX_TOP_ATTACK1);
                    } else if (m_altAttackTime == startupTime / 2) {
                        m_soundSource.playSfx(SfxId::NOX_TOP_ATTACK2, true);
                    } else if (m_altAttackTime >= startupTime) {
                        m_altAttackTime = startupTime;
                        m_altAttack = true;
                    }
                }
                if (v.AltAttackStartup > 0) {
                    m_altAnim.frame = (m_altAttackTime / 2 * m_altAnim.frameCount - 1) / v.AltAttackStartup;
                }
            } else {
                endAltAttack();
            }
        }
        if (m_hunter == Hunter::Spire) {
            if (m_altAttack) {
                if (m_altAnim.flags & AnimFlags::Ended) {
                    endAltAttack();
                }
            } else if (input.altAttackPressed) {
                m_altAttack = true;
                setAltAnimation(Anim::SpireAttack, AnimFlags::NoLoop);
                m_soundSource.playSfx(SfxId::SPIRE_ALT_ATTACK);
                m_spireRockL = m_spireRockR = m_position;
                m_spireAltUp = m_fieldC0;
                m_spireAltFacing = normalized(cross(m_spireAltUp, cross(facingVector(), m_spireAltUp)));
            }
        }
        if (traceOrWeavel) {
            if (m_altAttack || m_altAttackCooldown > 0) {
                if (m_standing) {
                    endAltAttack();
                }
            } else if (input.altAttackPressed) {
                // The lunge: along the aim, up at LungeVSpeed.
                m_altAttack = true;
                const float attackHSpeed = fx(v.LungeHSpeed);
                const float attackVSpeed = fx(v.LungeVSpeed);
                const float accelX = m_field70 * attackHSpeed;
                const float accelZ = m_field74 * attackHSpeed;
                if (m_field70 * m_speed[0] + m_field74 * m_speed[2] < attackHSpeed) {
                    m_speed[0] = accelX;
                    m_speed[2] = accelZ;
                }
                if (m_hunter == Hunter::Trace) {
                    m_accelerationTimer = 6 * 2;
                    m_acceleration = {accelX, 0, accelZ};
                }
                if (m_speed[1] < attackVSpeed) {
                    m_speed[1] = std::min(m_speed[1] + attackVSpeed, attackVSpeed);
                }
                animId = m_hunter == Hunter::Trace ? Anim::TraceAttack : Anim::WeavelAttack;
                animFlags = AnimFlags::NoLoop;
                m_soundSource.playSfx(m_hunter == Hunter::Trace ? SfxId::TRACE_ALT_ATTACK : SfxId::WEAVEL_ALT_ATTACK);
            }
        }
        if (m_hunter == Hunter::Samus) {
            // Boost: hold to charge, release to go.
            if (input.boostHeld) {
                if (m_boostCharge < v.BoostChargeMax * 2) {
                    m_boostCharge++;
                }
            } else {
                if (m_boostCharge > v.BoostChargeMin * 2) {
                    m_soundSource.playSfx(hunterSfx(m_hunter, HunterSfx::Boost));
                    const float boostHCap = fx(v.BoostSpeedCap) * m_boostCharge / (v.BoostChargeMax * 2);
                    if (m_hSpeedCap < boostHCap) {
                        m_hSpeedCap = boostHCap;
                    }
                    const float factor = fx(v.BoostSpeedMin)
                        + m_boostCharge * (fx(v.BoostSpeedMax) - fx(v.BoostSpeedMin)) / (v.BoostChargeMax * 2);
                    speedDelta[0] += m_field70 * factor;
                    speedDelta[2] += m_field74 * factor;
                    m_altAttackCooldown = v.AltAttackCooldown * 2;
                    m_boosting = true;
                    if (m_effects != nullptr) {
                        m_effects->unlink(m_boostEffect);
                        m_boostEffect = spawnEffect(136, gunVec2(), facingVector(), m_position, true); // samusDash
                        m_effects->setElementExtension(m_boostEffect, true);
                    }
                    m_boostDamage = v.AltAttackDamage * m_boostCharge / (v.BoostChargeMax * 2);
                }
                m_boostCharge = 0;
            }
        }
    }
    const float magBefore = std::sqrt(m_speed[0] * m_speed[0] + m_speed[2] * m_speed[2]);
    m_speed = m_speed + speedDelta;
    const float magAfter = std::sqrt(m_speed[0] * m_speed[0] + m_speed[2] * m_speed[2]);
    if (magAfter > magBefore && magAfter > m_hSpeedCap) {
        const float factor = magBefore <= m_hSpeedCap ? m_hSpeedCap / magAfter : magBefore / magAfter;
        m_speed[0] *= factor;
        m_speed[2] *= factor;
    }
    if (input.morphPressed) {
        trySwitchForms();
    }
    if (traceOrWeavel) {
        if (animId != -1) {
            if ((m_altAnim.index != 1 || (m_altAnim.flags & AnimFlags::Ended)) && animId != m_altAnim.index) {
                setAltAnimation(animId, animFlags);
            }
        } else if (m_altAnim.index != 0 && (!(m_altAnim.flags & AnimFlags::NoLoop) || (m_altAnim.flags & AnimFlags::Ended))) {
            setAltAnimation(0, 0);
        }
    }
    processMovement();
    updateCamera();
}

namespace {
// Metadata.MuzzleEffectIds, ChargeEffectIds, ChargeLoopEffectIds (by beam)
constexpr int kMuzzleEffectIds[9] = {62, 57, 62, 60, 63, 59, 61, 58, 62};
constexpr int kChargeEffectIds[9] = {169, 165, 170, 169, 169, 166, 168, 169, 169};
constexpr int kChargeLoopEffectIds[9] = {198, 194, 197, 198, 198, 195, 196, 198, 198};
} // namespace

void Player::updateWeaponEffects()
{
    // PlayerProcess: the muzzle flash follows the gun until it is done (the
    // Shock Coil's until it stops firing); the charge glow starts at the
    // minimum charge and loops from the full one.
    if (m_effects == nullptr) {
        return;
    }
    const Mat4 orientation = Mat4::fromVectors(gunVec2(), facingVector(), {0, 0, 0});
    if (m_muzzleEffect >= 0) {
        if (m_effects->isFinished(m_muzzleEffect)
            || (m_effects->effectId(m_muzzleEffect) == kMuzzleEffectIds[Beam::ShockCoil] && !m_shotUncharged)) {
            m_effects->unlink(m_muzzleEffect);
            m_muzzleEffect = -1;
        } else {
            m_effects->setTransform(m_muzzleEffect, m_muzzlePos, orientation);
        }
    }
    if (m_equip.weapon == nullptr) {
        return;
    }
    const WeaponInfo& weapon = *m_equip.weapon;
    if (m_equip.chargeLevel < weapon.minCharge * 2) {
        if (m_chargeEffect >= 0) {
            m_effects->unlink(m_chargeEffect);
            m_chargeEffect = -1;
        }
        return;
    }
    const int beam = std::clamp(m_currentWeapon, 0, 8);
    if (m_equip.chargeLevel == weapon.minCharge * 2) {
        m_effects->unlink(m_chargeEffect);
        m_chargeEffect = m_effects->spawnEntry(kChargeEffectIds[beam], orientation * Mat4::translation(m_muzzlePos[0], m_muzzlePos[1], m_muzzlePos[2]));
        m_chargeLoopEffect = false;
    } else if (m_equip.chargeLevel == weapon.fullCharge * 2 && !m_chargeLoopEffect) {
        m_effects->unlink(m_chargeEffect);
        m_chargeEffect = m_effects->spawnEntry(kChargeLoopEffectIds[beam], orientation * Mat4::translation(m_muzzlePos[0], m_muzzlePos[1], m_muzzlePos[2]));
        if (m_chargeEffect >= 0) {
            m_effects->setElementExtension(m_chargeEffect, true);
            m_chargeLoopEffect = true;
        }
    }
    if (m_equip.chargeLevel == weapon.fullCharge * 2) {
        m_cam.setShake(0.023f);
    }
    if (m_chargeEffect >= 0) {
        m_effects->setTransform(m_chargeEffect, m_muzzlePos, orientation);
    }
}

void Player::clearWeaponEffects()
{
    // Death and respawn end every effect the player carries.
    if (m_effects != nullptr) {
        for (int handle : {m_muzzleEffect, m_chargeEffect, m_deathaltEffect, m_burnEffect, m_doubleDmgEffect, m_boostEffect, m_furlEffect,
                 m_turretBurnEffect}) {
            m_effects->unlink(handle);
        }
    }
    m_muzzleEffect = m_chargeEffect = m_deathaltEffect = m_burnEffect = m_doubleDmgEffect = m_boostEffect = m_furlEffect = -1;
    m_turretBurnEffect = -1;
    m_chargeLoopEffect = false;
}

int Player::spawnEffect(int effectId, const Vec3& facing, const Vec3& up, const Vec3& position, bool entry)
{
    if (m_effects == nullptr) {
        return -1;
    }
    const Mat4 transform = Mat4::fromVectors(facing, up, position);
    if (!entry) {
        m_effects->spawn(effectId, transform);
        return -1;
    }
    return m_effects->spawnEntry(effectId, transform);
}

void Player::createBurnEffect()
{
    // PlayerProcess.CreateBurnEffect: on the gun in first person, else on the body.
    if (m_effects == nullptr) {
        return;
    }
    m_effects->unlink(m_burnEffect);
    m_burnEffect = -1;
    if (m_unmorphing) {
        return;
    }
    if (m_altForm || m_morphing || m_slot != 0) {
        m_burnEffect = spawnEffect(m_altForm || m_morphing ? 187 : 189, {m_field70, 0, m_field74}, {0, 1, 0}, sphereCenter(), true);
    } else {
        m_burnEffect = spawnEffect(188, {0, 1, 0}, facingVector(), m_muzzlePos, true); // flamingGun
    }
    m_effects->setElementExtension(m_burnEffect, true);
}

void Player::updateEffects()
{
    // PlayerProcess: the effects that follow the player (Deathalt's ball, the
    // flames, the double damage glow on the gun, Samus's boost and furl).
    if (m_effects == nullptr) {
        return;
    }
    if (m_deathaltTimer > 0) {
        if (m_deathaltEffect < 0) {
            m_deathaltEffect = spawnEffect(181, {1, 0, 0}, {0, 1, 0}, sphereCenter(), true); // deathBall
            m_effects->setElementExtension(m_deathaltEffect, true);
        } else {
            m_effects->setTransform(m_deathaltEffect, sphereCenter(), Mat4::fromVectors({0, 1, 0}, {1, 0, 0}, {0, 0, 0}));
        }
    } else if (m_deathaltEffect >= 0) {
        m_effects->unlink(m_deathaltEffect);
        m_deathaltEffect = -1;
    }
    if (m_doubleDmgTimer == 0 || m_altForm || m_morphing || m_unmorphing) {
        m_effects->unlink(m_doubleDmgEffect);
        m_doubleDmgEffect = -1;
    } else if (m_doubleDmgEffect >= 0) {
        m_effects->setTransform(m_doubleDmgEffect, m_muzzlePos, Mat4::fromVectors({0, 1, 0}, facingVector(), {0, 0, 0}));
    } else {
        m_doubleDmgEffect = spawnEffect(244, {0, 1, 0}, facingVector(), m_muzzlePos, true); // doubleDamageGun
        m_effects->setElementExtension(m_doubleDmgEffect, true);
    }
    if (m_burnTimer > 0) {
        if (m_burnEffect >= 0) {
            if (m_slot != 0 || m_altForm || m_morphing) {
                m_effects->setTransform(m_burnEffect, sphereCenter(), Mat4::fromVectors({m_field70, 0, m_field74}, {0, 1, 0}, {0, 0, 0}));
            } else {
                m_effects->setTransform(m_burnEffect, m_muzzlePos, Mat4::fromVectors({0, 1, 0}, facingVector(), {0, 0, 0}));
            }
        }
    } else if (m_burnEffect >= 0) {
        m_effects->unlink(m_burnEffect);
        m_burnEffect = -1;
    }
    const Mat4 rolling = Mat4::fromVectors(gunVec2(), facingVector(), {0, 0, 0});
    if (m_boostEffect >= 0) {
        m_effects->setTransform(m_boostEffect, m_position, rolling);
        if (!m_altForm && !m_morphing) {
            m_effects->unlink(m_boostEffect);
            m_boostEffect = -1;
        } else if (!m_boosting) {
            if (m_effects->isFinished(m_boostEffect)) {
                m_effects->unlink(m_boostEffect);
                m_boostEffect = -1;
            } else {
                m_effects->setElementExtension(m_boostEffect, false);
            }
        }
    }
    if (m_furlEffect >= 0) {
        m_effects->setTransform(m_furlEffect, m_position, rolling);
        if ((!m_altForm && !m_morphing) || m_effects->isFinished(m_furlEffect)) {
            m_effects->unlink(m_furlEffect);
            m_furlEffect = -1;
        }
    }
    // HalfturretEntity: its flames.
    if (m_halfturret.active && m_halfturret.burnTimer > 0) {
        const Vec3 facing{m_halfturret.facing[0], 0, m_halfturret.facing[2]};
        if (m_turretBurnEffect < 0) {
            m_turretBurnEffect = spawnEffect(187, facing, {0, 1, 0}, m_halfturret.position, true); // flamingAltForm
            m_effects->setElementExtension(m_turretBurnEffect, true);
        } else {
            m_effects->setTransform(m_turretBurnEffect, m_halfturret.position, Mat4::fromVectors(facing, {0, 1, 0}, {0, 0, 0}));
        }
    } else if (m_turretBurnEffect >= 0) {
        m_effects->unlink(m_turretBurnEffect);
        m_turretBurnEffect = -1;
    }
}

void Player::onImpact(int targetSlot, bool shockCoil)
{
    // PlayerEntity.HandleMessage(Impact)
    if (targetSlot == m_slot) {
        return;
    }
    m_timeSinceHitTarget = 0;
    if (shockCoil) {
        if (targetSlot == m_shockCoilTarget) {
            m_shockCoilTimer++;
        } else {
            m_shockCoilTimer = 0;
            m_shockCoilTarget = targetSlot;
        }
    }
}

void Player::spawnBomb()
{
    // PlayerInput.SpawnBomb: Kanden's stinglarva leaves from the tail's last
    // segment; the others' bombs sit just under the ball's position.
    if (!m_spawnBomb) {
        return;
    }
    Mat4 transform;
    if (m_hunter == Hunter::Kanden) {
        const Mat4& seg = m_kandenSegMtx[4];
        transform = Mat4::fromVectors({seg.m[2][0], seg.m[2][1], seg.m[2][2]}, {seg.m[1][0], seg.m[1][1], seg.m[1][2]}, m_kandenSegPos[4]);
    } else {
        transform = Mat4::fromVectors({0, 0, 1}, {0, 1, 0}, {m_position[0], m_position[1] + fx(-1000), m_position[2]});
    }
    if (!m_spawnBomb(*this, transform)) {
        return; // Sylux set off the three already out
    }
    if (m_bombAmmo >= 2) {
        m_bombRefillTimer = static_cast<uint16_t>(v.BombRefillTime * 2);
    }
    m_bombAmmo--;
    // The C# keeps these as ushort: BombCooldown's upper half is another field.
    m_bombCooldown = static_cast<uint16_t>(v.BombCooldown * 2);
    if (m_hunter == Hunter::Kanden) {
        setAltAnimation(Anim::KandenTailOut, AnimFlags::NoLoop);
    } else if (m_hunter == Hunter::Sylux && m_syluxBombCount == 3) {
        m_bombOveruse += 27 * 2;
        if (m_bombOveruse >= 100 * 2) {
            m_bombCooldown = 150 * 2;
        }
    }
}

bool Player::checkHitByBomb(const BombHit& bomb)
{
    // PlayerEntity.CheckHitByBomb
    if (bomb.owner == this && !bomb.exploding) {
        return false;
    }
    const Vec3 between = sphereCenter() - bomb.position;
    const float distSqr = dot(between, between);
    if (bomb.owner == this) {
        if (distSqr <= fx(v.BombSelfRadiusSquared) && between[1] > -volumeRadius()) {
            const float ySpeed = fx(v.BombJumpSpeed);
            if (m_speed[1] < ySpeed) {
                m_speed[1] = ySpeed;
            }
            return true;
        }
        return false;
    }
    const float hitRadiusSqr = bomb.radius * bomb.radius;
    if (distSqr <= hitRadiusSqr) {
        DamageSource source;
        source.attacker = bomb.owner;
        source.bomb = bomb.type;
        takeDamage(bomb.damage, DamageFlags::NoDmgInvuln, nullptr, source);
        m_cam.setShake((hitRadiusSqr - distSqr) / hitRadiusSqr * 0.1f);
        return true;
    }
    return false;
}

void Player::endAltAttack()
{
    // PlayerInput.EndAltAttack
    if (m_hunter == Hunter::Samus) {
        m_boosting = false;
    } else if (m_hunter == Hunter::Trace || m_hunter == Hunter::Weavel) {
        if (m_altAttack) {
            m_altAttackCooldown = v.AltAttackCooldown * 2;
        }
    } else if (m_hunter == Hunter::Noxus) {
        if (m_altAttackTime > 0) {
            m_soundSource.stopSfx(SfxId::NOX_TOP_ATTACK1);
            m_soundSource.stopSfx(SfxId::NOX_TOP_ATTACK2);
            if (m_altAttackTime >= v.AltAttackStartup / 2 * 2) {
                m_soundSource.playSfx(SfxId::NOX_TOP_ATTACK3);
            }
            setAltAnimation(Anim::NoxusExtend, AnimFlags::Paused);
            m_altAttackTime = 0;
        }
    }
    m_altAttack = false;
}

// ---- movement ------------------------------------------------------------

void Player::processMovement()
{
    // PlayerInput.ProcessMovement
    if (m_accelerationTimer > 0) {
        m_accelerationTimer--;
        m_speed = m_speed + m_acceleration * 0.5f;
    }
    const float hSpeedMag = std::sqrt(m_speed[0] * m_speed[0] + m_speed[2] * m_speed[2]);
    if (hSpeedMag == 0) {
        m_hSpeedMag = 0;
    } else {
        const float hx = m_speed[0] / hSpeedMag, hz = m_speed[2] / hSpeedMag;
        if (v.AltFormStrafe == 0) {
            if (hSpeedMag > fx(v.Field5C)) {
                m_field80 = hx;
                m_field84 = hz;
            }
            if ((m_altForm || m_morphing) && hSpeedMag > fx(v.Field58)) {
                // Rolling forms face where they roll.
                setAim(std::atan2(-hx, -hz) / kDegToRad, m_pitch);
            }
        }
        if (m_altForm) {
            const float altMin = fx(v.AltMinHSpeed);
            if (m_hSpeedCap <= altMin) {
                m_hSpeedCap = altMin;
            } else if (hSpeedMag >= m_hSpeedCap) {
                m_hSpeedCap -= fx(v.AltHSpeedCapIncrement) / 2;
            } else {
                m_hSpeedCap = hSpeedMag;
            }
        } else {
            m_hSpeedCap = fx(m_strafing ? v.StrafeSpeedCap : v.WalkSpeedCap);
        }
        if (primeHunter && !m_altForm) {
            m_hSpeedCap = 0.4f;
        }
        m_hSpeedMag = hSpeedMag;
    }
    if (v.AltFormStrafe != 0) {
        m_field80 = m_field70;
        m_field84 = m_field74;
    }

    if (m_usedJumpPad) {
        // The pad's speed is left out of the friction below, then put back.
        const float prevX = m_speed[0];
        m_speed[0] -= m_jumpPadAccel[0];
        if ((prevX <= 0 && m_speed[0] > 0) || (prevX > 0 && m_speed[0] < 0)) {
            m_jumpPadAccel[0] += m_speed[0] / 2;
            m_speed[0] = 0;
        }
        const float prevZ = m_speed[2];
        m_speed[2] -= m_jumpPadAccel[2];
        if ((prevZ <= 0 && m_speed[2] > 0) || (prevZ > 0 && m_speed[2] < 0)) {
            m_jumpPadAccel[2] += m_speed[2] / 2;
            m_speed[2] = 0;
        }
    }
    float speedFactor;
    if (m_altForm || m_morphing) {
        if (m_altAttack && (m_hunter == Hunter::Trace || m_hunter == Hunter::Weavel)) {
            speedFactor = 0.96f;
        } else {
            speedFactor = fx(m_standing ? v.AltGroundSpeedFactor : v.AirSpeedFactor);
        }
    } else if (m_standing) {
        speedFactor = fx(m_strafing ? v.StrafeSpeedFactor : m_walking ? v.WalkSpeedFactor : v.StandSpeedFactor);
    } else {
        speedFactor = fx(v.AirSpeedFactor);
    }
    float slideSfxAmount = 0;
    if (m_standing && m_slipperiness != 0) {
        speedFactor += (1 - speedFactor) * slipSpeedFactors()[m_slipperiness];
        if (!m_movingBiped) {
            slideSfxAmount = 0xFFFF * m_hSpeedMag / fx(v.WalkSpeedCap);
        }
    }
    updateSlidingSfx(slideSfxAmount);
    m_speed[0] += (m_speed[0] * speedFactor - m_speed[0]) / 2;
    m_speed[2] += (m_speed[2] * speedFactor - m_speed[2]) / 2;
    if (m_usedJumpPad) {
        m_speed[0] += m_jumpPadAccel[0];
        m_speed[2] += m_jumpPadAccel[2];
    }
    if (m_standing && m_timeSinceJumpPad > 5 * 2) {
        m_usedJumpPad = false;
        m_jumpPadControlLock = 0;
        m_jumpPadControlLockMin = 0;
    }
    if (m_altForm) {
        m_altFormGravity = true;
    } else if (m_speed[1] <= 0.01f) {
        m_altFormGravity = false;
    }
    if (m_health > 0) {
        if (m_jumpPadControlLock == 0) {
            if (m_gravityOverride) {
                m_gravityOverride = false; // an area volume set this tick's
            } else if (m_altForm || m_altFormGravity) {
                if (m_standing && m_slipperiness == 0 && v.AltFormStrafe != 0) {
                    m_gravity = 0;
                } else {
                    m_gravity = fx(m_standing ? v.AltGroundGravity : v.AltAirGravity);
                }
            } else {
                m_gravity = m_standing && m_slipperiness == 0 ? 0 : fx(v.BipedGravity);
            }
            m_speed[1] += m_gravity / 2;
        }
        m_position = m_position + m_speed * 0.5f;
        checkPlayerCollision();
    }

    const int prevTerrain = m_standTerrain;
    const bool standingPrev = m_standing;
    const bool noUnmorphPrev = m_noUnmorph;
    m_onLava = m_onAcid = false;
    m_standing = false;
    m_standingPrevious = standingPrev;
    m_noUnmorph = false;
    m_noUnmorphPrevious = noUnmorphPrev;
    m_collidingLateral = false;
    m_spireClimbing = false;
    m_fieldC0 = {};
    checkCollision();
    m_fieldC0 = dot(m_fieldC0, m_fieldC0) > 0 ? normalized(m_fieldC0) : Vec3{0, 1, 0};
    if (m_standTerrain != prevTerrain) {
        stopTerrainSfx(prevTerrain);
    }
    if (!m_collidingLateral) {
        m_horizColTimer = 0;
    } else if (m_horizColTimer < 0xFFFF) {
        m_horizColTimer++;
    }

    if (m_standing && !m_standingPrevious) {
        // Landing: the camera dips by an amount that grows with the fall.
        m_timeStanding = 0;
        m_field44C = m_prevSpeed[1] >= 0 ? 0 : std::min(-m_prevSpeed[1] * 0.35f, fx(800));
        if (m_prevSpeed[1] < -0.65f) {
            m_cam.setShake(fx(204));
        }
    } else if (m_timeStanding < 0xFFFF) {
        m_timeStanding++;
    }
    if (m_altForm) {
        updateAltTransform();
    }
    m_groundedPrevious = m_grounded;
    if (m_standing || m_spireClimbing) {
        m_timeBeforeLanding = m_timeSinceGrounded;
        m_timeSinceGrounded = 0;
        m_grounded = true;
    } else if (m_timeSinceGrounded < 90 * 2) {
        m_timeSinceGrounded++;
        if (m_timeSinceGrounded >= 8 * 2) {
            m_grounded = false;
            m_walkSfxTimer = 0;
            m_walkSfxIndex = 0;
        }
    }
    const bool burning = m_health > 0 && (m_burnTimer > 0 || (m_hunter != Hunter::Spire && m_onLava && m_grounded));
    updateBurningSfx(burning);
    if ((!m_altForm || m_hunter == Hunter::Weavel) && m_grounded) {
        updateWalkingSfx();
    }
}

void Player::checkCollision()
{
    // PlayerCollision.CheckCollision
    if (m_collision == nullptr) {
        return;
    }
    CollisionResult results[40];
    auto limits = [](const Vec3& a, const Vec3& b, float margin, Vec3& lo, Vec3& hi) {
        lo = {std::min(a[0], b[0]) - margin, std::min(a[1], b[1]) - margin, std::min(a[2], b[2]) - margin};
        hi = {std::max(a[0], b[0]) + margin, std::max(a[1], b[1]) + margin, std::max(a[2], b[2]) + margin};
    };
    Vec3 limitMin, limitMax;
    if (m_altForm) {
        const Vec3 center{0, fx(v.AltColYPos), 0};
        const Vec3 point1 = m_prevPosition + center;
        const Vec3 point2 = m_position + center;
        const float altRadius = fx(v.AltColRadius);
        limits(point1, point2, altRadius + 0.4f, limitMin, limitMax);
        limitMin[1] = std::min(limitMin[1], m_position[1] + fx(v.MaxPickupHeight));
        const std::vector<CollisionEntry> candidates = m_collision->candidates(limitMin, limitMax);
        const float radius = altRadius + (m_hunter == Hunter::Spire || m_hunter == Hunter::Sylux ? 0.5f : 0.35f);
        int count = m_collision->checkSphereBetweenPoints(candidates, point1, point2, radius, 40, true, TestFlags::Players, results);
        for (int i = 0; i < count; i++) {
            handleCollision(results[i]);
        }
        // Room to stand back up?
        const float bipedRadius = fx(v.BipedColRadius) - 0.15f;
        const Vec3 up1{m_position[0], m_position[1] + bipedRadius, m_position[2]};
        const float yOffset = fx(v.AltColYPos) + fx(v.MaxPickupHeight) - fx(v.MinPickupHeight) - altRadius - bipedRadius;
        const Vec3 up2{m_position[0], m_position[1] + yOffset, m_position[2]};
        count = m_collision->checkSphereBetweenPoints(candidates, up1, up2, bipedRadius, 1, false, TestFlags::Players, results);
        if (count > 0) {
            m_noUnmorph = true;
        }
        if (m_hunter == Hunter::Kanden) {
            for (size_t i = 1; i < m_kandenSegPos.size(); i++) {
                const Vec3 p = m_kandenSegPos[i] + Vec3{0, fx(v.AltColYPos), 0};
                count = m_collision->checkSphereBetweenPoints(candidates, p, p, altRadius, 40, true, TestFlags::Players, results);
                for (int j = 0; j < count; j++) {
                    const CollisionResult& r = results[j];
                    if (r.field0 == 1) {
                        const Vec3 edge = r.edgePoint2 - r.edgePoint1;
                        const float div = std::clamp(dot(p - r.edgePoint1, edge) / dot(edge, edge), 0.0f, 1.0f);
                        const Vec3 between = p - (r.edgePoint1 + edge * div);
                        const float magSqr = dot(between, between);
                        if (magSqr > 0 && magSqr < altRadius * altRadius) {
                            const float mag = std::sqrt(magSqr);
                            m_kandenSegPos[i][1] += between[1] / mag * (altRadius - mag);
                        }
                    } else {
                        const float d = altRadius + r.plane[3] - dot(p, xyz(r.plane));
                        if (d > 0) {
                            m_kandenSegPos[i] = m_kandenSegPos[i] + xyz(r.plane) * d;
                        }
                    }
                }
            }
        }
    } else {
        const float midpoint = (fx(v.MaxPickupHeight) + fx(v.MinPickupHeight)) / 2;
        const Vec3 point1{m_prevPosition[0], m_prevPosition[1] + midpoint, m_prevPosition[2]};
        const Vec3 point2{m_position[0], m_position[1] + midpoint, m_position[2]};
        const float radius = (fx(v.MaxPickupHeight) - fx(v.MinPickupHeight)) / 2;
        limits(point1, point2, radius + 0.4f, limitMin, limitMax);
        const std::vector<CollisionEntry> candidates = m_collision->candidates(limitMin, limitMax);
        const int count = m_collision->checkSphereBetweenPoints(candidates, point1, point2, radius, 40, true, TestFlags::Players, results);
        for (int i = 0; i < count; i++) {
            handleCollision(results[i]);
        }
    }
    for (const DoorObstacle& door : m_doors) {
        Vec3 between = m_position - door.lockPosition;
        const float d = dot(between, door.facing);
        if (d <= 1.25f && d >= -1.25f) {
            between = between - door.facing * d;
            if (dot(between, between) < door.radiusSquared) {
                CollisionResult result;
                Vec3 n = door.facing;
                if (dot(m_prevPosition - door.lockPosition, door.facing) < 0) {
                    n = n * -1.0f;
                }
                const float w = n[0] * (door.lockPosition[0] + 0.4f * n[0]) + n[1] * (door.lockPosition[1] + 0.4f * n[1])
                    + n[2] * (door.lockPosition[2] + 0.4f * n[2]);
                result.plane = {n[0], n[1], n[2], w};
                handleCollision(result);
            }
        }
    }
    for (const ForceFieldObstacle& ff : m_forceFields) {
        const float dot1 = dot(m_position, xyz(ff.plane)) - ff.plane[3];
        const float dot2 = dot(m_prevPosition, xyz(ff.plane)) - ff.plane[3];
        if ((dot1 < 1 && dot1 > -1) || (dot2 < 1 && dot2 > -1)) {
            const Vec3 between = sphereCenter() - ff.position;
            const float dotH = dot(between, ff.up);
            const float dotW = dot(between, ff.right);
            if (dotH <= ff.height && dotH >= -ff.height && dotW <= ff.width && dotW >= -ff.width) {
                CollisionResult result;
                result.plane = dot2 < 0 ? Vec4{-ff.plane[0], -ff.plane[1], -ff.plane[2], -ff.plane[3]} : ff.plane;
                handleCollision(result);
            }
        }
    }
}

void Player::handleCollision(CollisionResult result)
{
    // PlayerCollision.HandleCollision
    bool v163 = false; // pushed by the biped's upper sphere: a wall or ceiling, not the floor
    bool v164 = false;
    bool v165 = false;
    bool v166 = false;
    float v2 = 0;
    const Vec3 pos = m_position;
    if (m_altForm) {
        const float altRad = fx(v.AltColRadius);
        const Vec3 altPos{pos[0], pos[1] + fx(v.AltColYPos), pos[2]};
        if (result.field0 == 0) {
            v2 = altRad + result.plane[3] - dot(altPos, xyz(result.plane));
        } else if (result.field0 == 1) {
            const Vec3 edge = result.edgePoint2 - result.edgePoint1;
            const float div = std::clamp(dot(altPos - result.edgePoint1, edge) / dot(edge, edge), 0.0f, 1.0f);
            Vec3 between = altPos - (result.edgePoint1 + edge * div);
            const float magSqr = dot(between, between);
            if (magSqr >= altRad * altRad || magSqr <= 0) {
                return;
            }
            const float mag = std::sqrt(magSqr);
            between = between * (1.0f / mag);
            const float d = dot(between, xyz(result.plane)) * (altRad - mag);
            v2 = d * d;
        } else {
            return;
        }
    } else if (result.field0 == 0) {
        const float radius = fx(v.BipedColRadius);
        const Vec3 vec1{pos[0], pos[1] + fx(v.MaxPickupHeight) - radius, pos[2]};
        const Vec3 vec2{pos[0], pos[1] + fx(v.MinPickupHeight) + radius, pos[2]};
        const float dot1 = radius + result.plane[3] - dot(vec1, xyz(result.plane));
        const float dot2 = radius + result.plane[3] - dot(vec2, xyz(result.plane));
        if (dot2 <= dot1) {
            v2 = dot1;
            v163 = true;
        } else {
            v2 = dot2;
        }
    } else if (result.field0 == 1) {
        float v11 = 0;
        float v162 = 1;
        bool v169 = false;
        const Vec3 edge = result.edgePoint2 - result.edgePoint1;
        const float yTop = pos[1] + fx(v.MaxPickupHeight);
        const float yBot = pos[1] + fx(v.MinPickupHeight);
        const float yBotAdd = yBot + 0.5f;
        if (result.edgePoint1[1] >= yBot) {
            if (result.edgePoint1[1] > yTop) {
                if (edge[1] == 0) {
                    return;
                }
                v11 = (yTop - result.edgePoint1[1]) / edge[1];
            }
        } else if (edge[1] != 0) {
            v11 = (yBot - result.edgePoint1[1]) / edge[1];
        } else {
            if (result.edgePoint1[1] <= yBotAdd) {
                return;
            }
            const float bx = pos[0] - result.edgePoint1[0];
            const float bz = pos[2] - result.edgePoint1[2];
            const float div = std::clamp((bx * edge[0] + bz * edge[2]) / (edge[0] * edge[0] + edge[2] * edge[2]), 0.0f, 1.0f);
            const Vec3 between{pos[0] - result.edgePoint1[0] + edge[0] * div, yBotAdd - result.edgePoint1[1],
                pos[2] - result.edgePoint1[2] + edge[2] * div};
            const float magSqr = dot(between, between);
            if (magSqr >= 0.25f) {
                return;
            }
            const float mag = std::sqrt(magSqr);
            const Vec3 n = between * (1.0f / mag);
            result.plane = {n[0], n[1], n[2], result.plane[3]};
            v2 = 0.5f - mag;
            v169 = true;
        }
        if (!v169) {
            if (result.edgePoint2[1] >= yBot) {
                if (result.edgePoint2[1] > yTop) {
                    v162 = (yTop - result.edgePoint1[1]) / edge[1];
                }
            } else {
                v162 = (yBot - result.edgePoint1[1]) / edge[1];
            }
            if (std::fabs(v11 - v162) < 1 / 4096.0f) {
                return;
            }
            const Vec3 between{pos[0] - result.edgePoint1[0], yTop - result.edgePoint1[1], pos[2] - result.edgePoint1[2]};
            const float div = dot(between, edge) / dot(edge, edge);
            if (div >= v11) {
                v11 = div <= v162 ? div : v162;
            }
            const float betweenY = result.edgePoint1[1] + edge[1] * v11;
            if (betweenY > yTop + fx(2) || betweenY < yBot - fx(2)) {
                return;
            }
            if (betweenY <= yBotAdd) {
                const Vec3 b{pos[0] - (result.edgePoint1[0] + edge[0] * v11), yBotAdd - betweenY,
                    pos[2] - (result.edgePoint1[2] + edge[2] * v11)};
                const float magSqr = dot(b, b);
                if (magSqr >= 0.25f) {
                    return;
                }
                const float mag = std::sqrt(magSqr);
                const Vec3 n = b * (1.0f / mag);
                result.plane = {n[0], n[1], n[2], result.plane[3]};
                v2 = 0.5f - mag;
            } else {
                const float radius = fx(v.BipedColRadius);
                const float bx = pos[0] - (result.edgePoint1[0] + edge[0] * v11);
                const float bz = pos[2] - (result.edgePoint1[2] + edge[2] * v11);
                const float v31 = bx * bx + bz * bz;
                if (v31 >= radius * radius) {
                    return;
                }
                const float v32 = std::sqrt(v31);
                result.plane = {bx / v32, 0, bz / v32, result.plane[3]};
                v2 = radius - v32;
                if (betweenY > pos[1]) {
                    v163 = true;
                }
            }
        }
    } else {
        return;
    }

    Vec3 position = m_position;
    if (v2 >= fx(-5)) {
        v164 = true;
    }
    const Vec3 normal = xyz(result.plane);
    if (v2 > 0) {
        if (normal[1] < 0.1f && normal[1] > -0.1f) {
            v165 = true;
            m_collidingLateral = true;
        }
        position[0] += normal[0] * v2;
        position[2] += normal[2] * v2;
        if (!v163 || !m_standingPrevious) {
            // The C#'s 60 Hz stand-in for the game's 30 Hz response, clamped to the radius.
            float factor = 1;
            if (normal[1] > 0 && normal[1] < 0.9f) {
                factor = 0.5f / 2;
            } else if (normal[1] < 0) {
                factor = 2 * 2;
            }
            const float reach = fx(m_altForm ? v.AltColRadius : v.BipedColRadius);
            position[1] += std::clamp(normal[1] * v2 * factor, -reach, reach);
        }
        const float d = dot(m_speed, normal);
        if (d < 0) {
            if (m_hunter == Hunter::Noxus && m_altForm && v165) {
                // The Vhoscythe bounces off walls, tilting and wobbling.
                const float magSqr = m_prevSpeed[0] * normal[0] + m_prevSpeed[2] * normal[2];
                if (magSqr < 0) {
                    const float tilt = fx(v.AltBounceTilt) * -magSqr;
                    m_altTiltX += normal[0] * tilt;
                    m_altTiltZ += normal[2] * tilt;
                    m_altWobble += fx(v.AltBounceWobble) * -magSqr;
                    m_altSpinSpeed -= fx(v.AltBounceSpin) * -magSqr;
                    const Vec3 axis = mult3(normal, Mat4::rotationY(40 * kDegToRad));
                    m_speed[0] += axis[0] * (-magSqr / 2);
                    m_speed[2] += axis[2] * (-magSqr / 2);
                }
            }
            m_speed = m_speed + normal * -d;
            if (!v163 && !m_altForm && result.field0 != 1) {
                const float hMagSqr = m_speed[0] * m_speed[0] + m_speed[2] * m_speed[2];
                if (hMagSqr > 0) {
                    const float div = (normal[0] * m_speed[0] + normal[2] * m_speed[2]) / std::sqrt(hMagSqr);
                    if (div < 0) {
                        m_speed = m_speed * (div + 1);
                    }
                }
            }
        }
    }
    if (m_altForm) {
        const bool climbing = m_hunter == Hunter::Spire && result.field0 == 0;
        if (climbing) {
            // The Dialanche sticks to walls.
            for (const Vec3& spireVec : m_spireAltVecs) {
                Vec3 vec = m_position + spireVec;
                const float d = result.plane[3] - dot(vec, normal);
                if (d >= 0) {
                    v164 = true;
                    m_position = m_position + normal * d;
                    vec = normalized(Vec3{m_position[0] - vec[0], 0, m_position[2] - vec[2]});
                    vec[0] *= d / 4 * (m_hSpeedMag + 0.1f);
                    vec[2] *= d / 4 * (m_hSpeedMag + 0.1f);
                    m_speed = m_speed + vec * 0.5f;
                    if (normal[1] > fx(-357) && m_speed[1] < 0.15f) {
                        v166 = true;
                        float yFactor = m_hSpeedMag / 2;
                        if (m_speed[1] < 0.01f) {
                            yFactor += 0.3f;
                        }
                        m_speed[1] = std::min(m_speed[1] + 4 * d * yFactor / 2, 0.15f);
                    }
                }
            }
            position = m_position + (position - pos);
        } else if (m_hunter == Hunter::Sylux && result.field0 == 0 && normal[1] > fx(3138) && !m_noUnmorphPrevious) {
            // The Lockjaw hovers above the floor.
            const Vec3 p{m_position[0], m_position[1] + fx(v.AltColYPos) - fx(v.AltColRadius) - 0.3f, m_position[2]};
            const float d = result.plane[3] - dot(p, normal);
            if (d >= 0) {
                v164 = true;
                m_speed[1] += fx(v.AltAirGravity);
                if (m_speed[1] < 0.25f) {
                    if (m_speed[1] < 0) {
                        m_speed[1] *= fx(4034);
                    }
                    m_speed[1] = std::min(m_speed[1] + d * 0.2f, 0.25f);
                }
            }
        }
    }
    if (v164) {
        if (normal[1] >= fx(1401)) {
            m_fieldC0 = m_fieldC0 + normal;
        }
        if (!v163) {
            m_slipperiness = result.slipperiness();
            m_standTerrain = result.terrain();
            m_onAcid = m_onAcid || (result.flags & CollisionFlags::Damaging) != 0;
            m_onLava = m_onLava || m_standTerrain == 8; // Terrain.Lava
            if (result.field0 == 0 && normal[1] > 0.5f) {
                m_standing = true;
                if (!m_standingPrevious) {
                    m_usedJump = false;
                }
                if (m_health > 0 && !m_altForm && !m_grounded) {
                    if (m_bipedAnim.index == Anim::JumpLeft) {
                        setBiped1Animation(Anim::LandLeft, AnimFlags::NoLoop);
                    } else if (m_bipedAnim.index == Anim::JumpRight) {
                        setBiped1Animation(Anim::LandRight, AnimFlags::NoLoop);
                    } else {
                        setBiped1Animation(Anim::LandNeutral, AnimFlags::NoLoop);
                    }
                }
            }
        }
    }
    if (v166 && !m_standing) {
        m_spireClimbing = true;
    }
    m_position = position;
}

// ---- forms ---------------------------------------------------------------

bool Player::trySwitchForms()
{
    // PlayerProcess.TrySwitchForms
    if (m_morphing || m_unmorphing || m_deathaltTimer > 0 || m_hunter == Hunter::Guardian || (m_altForm && m_morphCameraId >= 0)) {
        if (m_isMain) {
            Sfx::instance().playFreeSfx(SfxId::BEAM_SWITCH_FAIL);
        }
        return false;
    }
    if (!m_altForm) {
        enterAltForm();
        return true;
    }
    if (!m_noUnmorph) {
        exitAltForm();
        return true;
    }
    if (m_isMain) {
        Sfx::instance().playFreeSfx(SfxId::BEAM_SWITCH_FAIL);
    }
    return false;
}

void Player::enterAltForm()
{
    m_altRollFbX = m_field70;
    m_altRollFbZ = m_field74;
    m_altRollLrX = m_field78;
    m_altRollLrZ = m_field7C;
    m_morphing = true;
    if (m_equip.chargeLevel > 0 && m_equip.weapon != nullptr) {
        stopBeamChargeSfx(m_currentWeapon);
    }
    m_equip.chargeLevel = 0;
    updateZoom(false);
    switchCamera(v.AltFormStrafe != 0 ? CameraType::Third2 : CameraType::Third1, {m_field70, 0, m_field74});
    initAltTransform();
    switch (m_hunter) {
    case Hunter::Spire:
        m_spireAltVecs.fill({});
        setAltAnimation(Anim::SpireAttack, AnimFlags::Paused);
        break;
    case Hunter::Noxus:
        m_altSpinSpeed = fx(v.AltMinSpinAccel);
        m_altTiltX = m_altTiltZ = m_altSpinRot = m_altWobble = 0;
        setAltAnimation(Anim::NoxusExtend, AnimFlags::Paused);
        break;
    case Hunter::Weavel:
        setAltAnimation(Anim::WeavelIdle, 0);
        createHalfturret();
        break;
    case Hunter::Kanden:
        setAltAnimation(Anim::KandenIdle, AnimFlags::Paused);
        break;
    case Hunter::Trace:
        setAltAnimation(Anim::TraceIdle, 0);
        break;
    case Hunter::Sylux:
        setAltAnimation(Anim::SyluxIdle, 0);
        break;
    default:
        setAltAnimation(0, 0);
        break;
    }
    if (m_hunter == Hunter::Samus && m_effects != nullptr) {
        m_effects->unlink(m_furlEffect);
        m_furlEffect = spawnEffect(30, gunVec2(), facingVector(), m_position, true); // samusFurl
    }
    setBipedAnimation(Anim::BipedMorph, AnimFlags::NoLoop);
    playHunterSfx(HunterSfx::Morph);
}

void Player::exitAltForm()
{
    if (m_halfturret.active) {
        // What the turret has left comes back.
        m_halfturret.active = false;
        if (m_halfturret.health > 0) {
            gainHealth(m_halfturret.health);
        }
        halfturretDie();
    }
    m_morphing = false;
    m_unmorphing = true;
    switchCamera(CameraType::First, facingVector());
    m_boostCharge = 0;
    setBipedAnimation(Anim::BipedUnmorph, AnimFlags::NoLoop);
    playHunterSfx(HunterSfx::Unmorph);
    if (m_altAttack) {
        endAltAttack();
    }
    if (m_altForm) {
        updateForm(false);
    }
}

void Player::updateForm(bool altForm)
{
    // PlayerProcess.UpdateForm: the collision sphere keeps its place across the switch.
    const Vec3 before = sphereCenter();
    m_altForm = altForm;
    m_position = m_position + (before - sphereCenter());
    if (altForm && m_hunter == Hunter::Spire) {
        // The slam: whoever stands within 4 units is bounced up.
        spawnEffect(37, {1, 0, 0}, {0, 1, 0}, m_position, false); // spireAltSlam
        m_cam.setShake(0.3f);
        if (m_players != nullptr) {
            for (Player* other : *m_players) {
                const Vec3 between = m_position - other->m_position;
                if (other != this && other->m_standing && dot(between, between) < 16) {
                    other->m_cam.setShake(0.3f);
                    if (other->m_speed[1] < 0.15f) {
                        other->m_speed[1] = 0.15f;
                    }
                }
            }
        }
    }
    if (altForm) {
        initAltTransform();
        m_field80 = m_field70;
        m_field84 = m_field74;
        if (m_hunter == Hunter::Kanden) {
            const Vec3 facing{m_field70, 0, m_field74};
            m_kandenSegPos[0] = m_position;
            m_kandenSegMtx[0] = Mat4::fromVectors(facing, {0, 1, 0}, m_position);
            for (size_t i = 1; i < m_kandenSegPos.size(); i++) {
                m_kandenSegPos[i] = m_kandenSegPos[i - 1] + facing * -m_models.kandenNodeDistances[i - 1];
                Mat4 matrix = m_kandenSegMtx[0];
                matrix.m[3][0] = m_kandenSegPos[i][0];
                matrix.m[3][1] = m_kandenSegPos[i][1];
                matrix.m[3][2] = m_kandenSegPos[i][2];
                m_kandenSegMtx[i] = matrix;
            }
        }
    }
    stopAltFormSfx();
}

void Player::initAltTransform()
{
    const Vec3 right = gunVec2();
    Mat4 m = Mat4::identity();
    setRow(m, 0, {right[0], 0, right[2]});
    setRow(m, 1, {0, 1, 0});
    setRow(m, 2, cross(row(m, 0), row(m, 1)));
    setRow(m, 1, cross(row(m, 2), row(m, 0)));
    setRow(m, 0, normalized(row(m, 0)));
    setRow(m, 1, normalized(row(m, 1)));
    setRow(m, 2, normalized(row(m, 2)));
    m_modelTransform = m;
}

void Player::updateAltTransform()
{
    // PlayerProcess.UpdateAltTransform
    if (m_hunter == Hunter::Noxus) {
        m_altWobble = std::clamp(m_altWobble + (5 - m_altWobble) / 32 / 2, fx(v.AltMinWobble), fx(v.AltMaxWobble));
        m_altTiltX -= m_altTiltX / 8 / 2;
        m_altTiltZ -= m_altTiltZ / 8 / 2;
        m_altTiltX += -(m_altTiltX + fx(25) * (m_speed[0] - m_prevSpeed[0])) / 32 / 2;
        m_altTiltZ += -(m_altTiltZ + fx(25) * (m_speed[2] - m_prevSpeed[2])) / 32 / 2;
        const float minSpinAccel = fx(v.AltMinSpinAccel), maxSpinAccel = fx(v.AltMaxSpinAccel);
        m_altSpinSpeed += (minSpinAccel + (m_altAttackTime * (maxSpinAccel - minSpinAccel) / (v.AltAttackStartup * 2)) - m_altSpinSpeed) / 32 / 2;
        m_altSpinSpeed = std::clamp(m_altSpinSpeed, fx(v.AltMinSpinSpeed), fx(v.AltMaxSpinSpeed));
        m_altSpinRot += m_altSpinSpeed / 2;
        while (m_altSpinRot > 360) {
            m_altSpinRot -= 360;
        }
        Mat4 transform = Mat4::rotationX(m_altWobble * kDegToRad) * Mat4::rotationY(m_altSpinRot * kDegToRad);
        const float mag = std::sqrt(m_altTiltX * m_altTiltX + m_altTiltZ * m_altTiltZ);
        if (mag != 0) {
            const Vec3 axis{m_altTiltZ / mag, 0, -m_altTiltX / mag};
            const float angle = std::min(mag * fx(v.AltTiltAngleMax), fx(v.AltTiltAngleCap));
            transform *= axisAngle(axis, angle * kDegToRad);
        }
        m_modelTransform = transform;
    } else if (m_hunter == Hunter::Kanden) {
        updateStinglarvaSegments();
    } else if (m_hunter == Hunter::Samus || m_hunter == Hunter::Spire) {
        // Rolling: turn about the axis across the motion by distance / radius.
        const float altRadius = fx(v.AltColRadius);
        Vec3 axis{};
        if (m_hunter == Hunter::Spire) {
            axis[0] = altRadius * (m_speed[2] / 2);
            axis[2] = -altRadius * (m_speed[0] / 2);
        } else {
            axis[0] = altRadius * (m_position[2] - m_prevPosition[2]);
            axis[2] = -altRadius * (m_position[0] - m_prevPosition[0]);
        }
        const float mag = length(axis);
        if (mag > 0) {
            axis = axis * (1.0f / mag);
            const float angle = mag / (altRadius * altRadius);
            Mat4 transform = m_modelTransform * axisAngle(axis, angle);
            if (m_hunter == Hunter::Samus) {
                // The Morph Ball's band leans back toward level.
                if (dot(row(transform, 0), axis) < 0) {
                    axis = axis * -1.0f;
                }
                axis = cross(row(transform, 0), axis);
                float mbAngle = length(axis);
                if (mbAngle > 0) {
                    if (mbAngle > 0.125f) {
                        mbAngle *= std::min(angle / fx(3216), 1.0f) / 8;
                    }
                    transform *= axisAngle(axis, mbAngle);
                }
            }
            setRow(transform, 2, cross(row(transform, 0), row(transform, 1)));
            setRow(transform, 1, cross(row(transform, 2), row(transform, 0)));
            setRow(transform, 0, normalized(row(transform, 0)));
            setRow(transform, 1, normalized(row(transform, 1)));
            setRow(transform, 2, normalized(row(transform, 2)));
            if (m_hunter == Hunter::Spire) {
                for (size_t i = 0; i < m_spireAltVecs.size(); i++) {
                    const auto& raw = kSpireAltVectors[i];
                    m_spireAltVecs[i] = mult3({fx(raw[0]), fx(raw[1]), fx(raw[2])}, transform);
                }
            }
            m_modelTransform = transform;
        }
    } else {
        m_modelTransform = transformMatrix({m_field80, 0, m_field84}, {0, 1, 0});
    }
}

void Player::updateStinglarvaSegments()
{
    // PlayerProcess.UpdateStinglarvaSegments: the head weaves, the body follows.
    constexpr int cycle = 13 * 2;
    float angle = 359.0f * (m_frameCount % cycle) / (cycle - 1);
    float factor = 0.3f * std::sin(angle * kDegToRad) * m_hSpeedMag;
    m_kandenSegPos[0] = m_position + Vec3{m_field78 * factor, 0, m_field7C * factor};
    Vec3 dir = dot(m_speed, m_speed) > 0.02f ? Vec3{m_speed[0] + m_field80 / 4, m_speed[1], m_speed[2] + m_field84 / 4}
                                             : Vec3{m_field80, 0, m_field84};
    dir = normalized(dir);
    if (dot(dir, row(m_kandenSegMtx[0], 2)) < fx(-5)) {
        dir[0] += fx(5);
    }
    dir = normalized(row(m_kandenSegMtx[0], 2) + (dir - row(m_kandenSegMtx[0], 2)) * 0.3f);
    if (dir[0] != 0 || dir[2] != 0) {
        m_kandenSegMtx[0] = transformMatrix(dir, {0, 1, 0});
    } else {
        m_kandenSegMtx[0] = transformMatrix({-m_field70, 0, -m_field74}, {dir[0], dir[1], 0});
    }
    m_kandenSegMtx[0].m[3][0] = m_kandenSegPos[0][0];
    m_kandenSegMtx[0].m[3][1] = m_kandenSegPos[0][1];
    m_kandenSegMtx[0].m[3][2] = m_kandenSegPos[0][2];
    for (size_t i = 1; i < m_kandenSegPos.size(); i++) {
        angle += 85;
        while (angle >= 360) {
            angle -= 360;
        }
        factor = 0.12f * std::sin(angle * kDegToRad) * m_hSpeedMag;
        const Vec3 segPos = m_kandenSegPos[i] + Vec3{m_field78 * factor, 0, m_field7C * factor};
        m_kandenSegPos[i] = segPos;
        dir = normalized(m_kandenSegPos[i - 1] - segPos);
        const Vec3 prevFacing = row(m_kandenSegMtx[i - 1], 2);
        const float d = dot(dir, prevFacing);
        if (d < fx(2896)) {
            Vec3 axis = cross(dir, prevFacing);
            const float mag = length(axis);
            if (mag > 0) {
                axis = axis * (1.0f / mag);
                const float atan = std::atan2(mag, d) - 45 * kDegToRad;
                dir = mult3(dir, axisAngle(axis, atan));
            }
        }
        m_kandenSegMtx[i] = transformMatrix(dir, row(m_kandenSegMtx[0], 1));
        m_kandenSegPos[i] = m_kandenSegPos[i - 1] - dir * m_models.kandenNodeDistances[i - 1];
        m_kandenSegMtx[i].m[3][0] = m_kandenSegPos[i][0];
        m_kandenSegMtx[i].m[3][1] = m_kandenSegPos[i][1];
        m_kandenSegMtx[i].m[3][2] = m_kandenSegPos[i][2];
    }
}

// ---- camera --------------------------------------------------------------

void Player::switchCamera(CameraType type, const Vec3& facing)
{
    if (type == CameraType::Third1) {
        m_cam.target = m_cam.position + facing;
        m_cam.position = m_cam.position - facing * (1.0f / 64);
    }
    m_cameraType = type;
    m_field544 = m_cam.position;
    m_camSwitchTimer = std::max(0, v.CamSwitchTime * 2 - std::min(m_camSwitchTimer, v.CamSwitchTime * 2));
}

void Player::updateCamera()
{
    m_cam.prevPosition = m_cam.position;
    if (m_camSwitchTimer < v.CamSwitchTime * 2) {
        m_camSwitchTimer++;
    }
    switch (m_cameraType) {
    case CameraType::Third1:
        updateCameraThird1();
        break;
    case CameraType::Third2:
        updateCameraThird2();
        break;
    default:
        updateCameraFirst();
        break;
    }
    m_cam.update();
}

void Player::updateCameraFirst()
{
    // PlayerCamera.UpdateCameraFirst
    Vec3 position = m_position;
    position[1] += fx(v.AimYOffset) + std::cos(m_gunViewBob * kDegToRad) * m_walkViewBob;
    if (m_timeStanding < 9 * 2) {
        const float angle = 360.0f * m_timeStanding / (9 * 2) * kDegToRad;
        position[1] += std::cos(angle) * m_field44C - m_field44C;
    }
    const Vec3 facing = facingVector();
    const float switchTime = v.CamSwitchTime * 2.0f;
    if (m_camSwitchTimer < switchTime) {
        const float pct = m_camSwitchTimer / switchTime;
        m_cam.position = m_field544 + (position - m_field544) * pct;
        const Vec3 target = m_cam.position + facing;
        m_cam.target = m_position + (target - m_position) * pct;
    } else {
        m_cam.position = position;
        m_cam.target = m_cam.position + facing;
    }
    m_cam.target[1] += fx(v.ViewTiltFactor) * std::sin(m_viewTiltAngleV * kDegToRad);
    if (std::fabs(m_viewTiltAngleH) >= 1 / 4096.0f) {
        const Vec3 toTarget = m_cam.target - m_cam.position;
        const Vec3 upVec = normalized({-toTarget[2], 0, toTarget[0]});
        const float factor = fx(v.ViewTiltFactor) * std::sin(m_viewTiltAngleH * kDegToRad);
        m_cam.up = {upVec[0] * factor, 1, upVec[2] * factor};
    } else {
        m_cam.up = {0, 1, 0};
    }
}

bool Player::checkBetweenPoints(const Vec3& a, const Vec3& b, CollisionResult& result) const
{
    return m_collision != nullptr && m_collision->checkBetweenPoints(a, b, TestFlags::Players, result);
}

void Player::updateCameraThird1()
{
    // PlayerCamera.UpdateCameraThird1: the camera trails the rolling form.
    const float v5 = m_noUnmorph ? 1.5f : fx(v.Field78);
    const float v6 = m_noUnmorph ? 0.7f : fx(v.Field7C);
    const float v7 = m_noUnmorph ? 0.5f : fx(v.Field80);
    const Vec3 sphere = sphereCenter();
    m_cam.target[0] = sphere[0];
    m_cam.target[1] -= v7;
    m_cam.target[1] += (sphere[1] - m_cam.target[1]) / 2;
    m_cam.target[2] = sphere[2];
    if (m_morphCameraId >= 0) {
        m_cam.position = m_morphCameraPosition;
        return;
    }
    Vec3 posVec;
    if (m_jumpPadControlLock > 0) {
        Vec3 camVec = m_cam.position - m_cam.target;
        camVec = normalized({camVec[0], 0, camVec[2]});
        posVec = {m_cam.target[0] + camVec[0], m_cam.target[1] + v6, m_cam.target[2] + camVec[2]};
    } else if (m_field551 <= 1) {
        const Vec3 camVec = normalized(m_cam.position - m_cam.target);
        posVec = {m_cam.target[0] + camVec[0] * v5, m_cam.position[1], m_cam.target[2] + camVec[2] * v5};
    } else {
        Vec3 camVec;
        if (m_camSwitchTimer >= v.CamSwitchTime * 2) {
            camVec = m_cam.position - m_cam.target;
            camVec[1] = 0;
        } else {
            camVec = {-m_cam.facing[0], 0, -m_cam.facing[2]};
            m_field544 = m_field544 + (m_position - m_prevPosition) * 0.5f;
        }
        camVec = normalized(camVec);
        posVec = {m_cam.target[0] + camVec[0] * v5, m_cam.target[1] + v6, m_cam.target[2] + camVec[2] * v5};
    }
    m_cam.target[1] += v7;
    if (m_camSwitchTimer < v.CamSwitchTime * 2) {
        const float pct = m_camSwitchTimer / (v.CamSwitchTime * 2.0f);
        m_cam.position = m_field544 + (posVec - m_field544) * pct;
        const Vec3 facingVec = m_cam.position + m_cam.facing;
        m_cam.target = facingVec + (m_cam.target - facingVec) * pct;
    } else {
        m_cam.position = m_cam.position + (posVec - m_cam.position) * fx(v.Field84);
    }
    if (m_field553 > 0) {
        m_field553--;
    }
    const Vec3 toSphere = m_cam.position - sphere;
    if (dot(toSphere, toSphere) >= 6 * 6) {
        m_field551 = 255;
    } else {
        const Vec3 half = m_speed * 0.5f;
        if (dot(half, half) > fx(36) && m_field551 != 255) {
            m_field551++;
        }
        // The side and vertical probes the C# makes here test degenerate
        // segments (point to itself) and never report a hit, so the swing they
        // would drive stays at rest: _field558 and _field554 are always 0.
        m_field558 = 0;
        m_field554 = 0;
        if (m_collision != nullptr) {
            // Keep the camera sphere out of walls.
            const Vec3 point1 = m_cam.prevPosition, point2 = m_cam.position;
            const float margin = fx(v.Field90);
            const Vec3 lo{std::min(point1[0], point2[0]) - margin, std::min(point1[1], point2[1]) - margin,
                std::min(point1[2], point2[2]) - margin};
            const Vec3 hi{std::max(point1[0], point2[0]) + margin, std::max(point1[1], point2[1]) + margin,
                std::max(point1[2], point2[2]) + margin};
            CollisionResult results[8];
            const int count = m_collision->checkSphereBetweenPoints(
                m_collision->candidates(lo, hi), point1, point2, margin, 8, true, TestFlags::Players, results);
            bool pushed = false;
            for (int i = 0; i < count; i++) {
                if (results[i].field0 == 0) {
                    const float d = -(dot(m_cam.position, xyz(results[i].plane)) - results[i].plane[3] - margin);
                    if (d > 0) {
                        m_cam.position = m_cam.position + xyz(results[i].plane) * d;
                        pushed = true;
                    }
                }
            }
            if (pushed) {
                const Vec3 toTarget = m_cam.target - m_cam.position;
                for (int i = 0; i < count; i++) {
                    if (results[i].field0 == 1 && dot(toTarget, xyz(results[i].plane)) >= 0) {
                        const float d = -(dot(m_cam.position, xyz(results[i].plane)) - results[i].plane[3] - margin);
                        if (d > 0) {
                            m_cam.position = m_cam.position + xyz(results[i].plane) * d;
                        }
                    }
                }
            }
        }
        m_field552 = 255;
    }
    // Something between the ball and the camera: after half a second, come in front of it.
    CollisionResult targResult;
    if (checkBetweenPoints(m_cam.target, m_cam.position, targResult)) {
        if (m_field552 < 15 * 2) {
            m_field552++;
        } else {
            m_cam.position = m_cam.target + (m_cam.position - m_cam.target) * targResult.distance;
        }
    } else {
        m_field552 = 0;
    }
}

void Player::updateCameraThird2()
{
    // PlayerCamera.UpdateCameraThird2: behind the strafing form, along the aim.
    const float switchTime = v.CamSwitchTime * 2.0f;
    if (m_camSwitchTimer < switchTime) {
        m_field68C = fx(v.Field80);
        m_field690 = fx(v.Field78);
    } else if (m_noUnmorph) {
        m_field68C += -0.2f * m_field68C / 2;
        m_field690 += 0.2f * (1.5f - m_field690) / 2;
    } else {
        m_field68C += 0.2f * (fx(v.Field80) - m_field68C) / 2;
        m_field690 += 0.2f * (fx(v.Field78) - m_field690) / 2;
    }
    m_cam.target = sphereCenter();
    const Vec3 camTarget = m_cam.target;
    m_cam.target[1] += m_field68C;
    if (m_morphCameraId >= 0) {
        m_cam.position = m_morphCameraPosition;
        return;
    }
    const Vec3 facing = facingVector();
    const Vec3 camVec = m_noUnmorph ? Vec3{m_field70 * m_field690, 0, m_field74 * m_field690} : facing * m_field690;
    const Vec3 posVec = m_cam.target - camVec;
    if (m_camSwitchTimer < switchTime) {
        const float pct = m_camSwitchTimer / switchTime;
        m_cam.position = m_field544 + (posVec - m_field544) * pct;
        const Vec3 facingVec = m_cam.position + facing;
        m_cam.target = facingVec + (m_cam.target - facingVec) * pct;
    } else {
        m_cam.position = m_cam.position + (posVec - m_cam.position) * fx(v.Field84);
    }
    CollisionResult result;
    if (checkBetweenPoints(camTarget, m_cam.position, result)) {
        m_cam.position = camTarget + (m_cam.position - camTarget) * result.distance;
        m_cam.position = m_cam.position + xyz(result.plane) * 0.15f;
    }
}

void Player::updateBob()
{
    // PlayerEntity.ProcessPlayer, after input.
    const float bobMax = fx(v.WalkBobMax);
    if (m_walking && !m_altForm) {
        m_gunViewBob += 14 / 2.0f;
        if (m_gunViewBob > 450) {
            m_gunViewBob -= 180;
        }
        if (m_walkViewBob < bobMax) {
            m_walkViewBob = std::min(m_walkViewBob + 1 / 2.0f, bobMax);
        }
    } else {
        if (m_walkViewBob > 0) {
            m_walkViewBob = std::max(0.0f, m_walkViewBob - bobMax / 32 / 2.0f);
        }
        if (m_gunViewBob >= 360) {
            m_gunViewBob = std::min(m_gunViewBob + 14 / 2.0f, 450.0f);
        } else {
            m_gunViewBob = std::max(m_gunViewBob - 14 / 2.0f, 270.0f);
        }
    }
}

// ---- pickups and jump pads -------------------------------------------------

void Player::activateJumpPad(const Vec3& vector, uint16_t lockTime)
{
    if (m_timeSinceJumpPad > 5 * 2) {
        m_soundSource.playSfx(SfxId::JUMP_PAD);
    }
    m_speed = vector;
    m_jumpPadAccel = vector;
    m_usedJumpPad = true;
    const int lock = lockTime * 2;
    m_jumpPadControlLock = lock;
    m_jumpPadControlLockMin = std::max(lock, 5 * 2);
    m_timeSinceJumpPad = 0;
    m_usedJump = false;
    m_standing = true;
    if (m_altForm) {
        // The alt form falls differently: keep control locked as long as the biped would be.
        const float accelY = m_jumpPadAccel[1];
        const float altGrav = fx(v.AltAirGravity), bipedGrav = fx(v.BipedGravity);
        if (accelY != 0 && altGrav != 0 && bipedGrav != 0) {
            const float altFactor = -accelY / altGrav, bipedFactor = -accelY / bipedGrav;
            const float lockInc = ((accelY * bipedFactor) + (bipedGrav * (bipedFactor * bipedFactor) / 2)
                                      - ((accelY * altFactor) + (altGrav * (altFactor * altFactor) / 2)))
                    / accelY
                + 2;
            m_jumpPadControlLock += static_cast<int>(lockInc * 2);
        }
    }
}

void Player::applyGravity(int param1)
{
    // HandleMessage(Message.Gravity)
    const float gravity = fx(param1);
    if (!m_standing && !m_altAttack && gravity != 0 && m_jumpPadControlLock == 0) {
        m_gravityOverride = true;
        m_gravity = gravity;
    }
}

bool Player::inPickupRange(const Vec3& itemPosition) const
{
    if (m_altForm) {
        const float radius = fx(v.AltColRadius) + 0.45f;
        const Vec3 between = itemPosition - sphereCenter();
        return dot(between, between) < radius * radius;
    }
    const float radius = fx(v.BipedColRadius) + 0.45f;
    const Vec3 between = itemPosition - m_position;
    return between[0] * between[0] + between[2] * between[2] < radius * radius && between[1] >= fx(v.MinPickupHeight)
        && between[1] <= fx(v.MaxPickupHeight);
}

bool Player::pickUp(ItemType type)
{
    if (m_health == 0) {
        return false;
    }
    static constexpr int healthAmounts[3] = {30, 60, 100};
    // PickUpItems' PlaySfx: the main player's, unless the timed sounds are muted.
    auto pickupSfx = [&](SfxId id) {
        if (m_isMain && Sfx::instance().timedSfxMute == 0) {
            Sfx::instance().playFreeSfx(id);
        }
    };
    switch (type) {
    case ItemType::HealthMedium:
    case ItemType::HealthSmall:
    case ItemType::HealthBig:
        if (primeHunter) {
            return false; // the Prime Hunter only heals by killing
        }
        gainHealth(healthAmounts[static_cast<int>(type)]);
        pickupSfx(type == ItemType::HealthSmall ? SfxId::POWER_UP1 : SfxId::POWER_UP2);
        return true;
    case ItemType::UASmall:
    case ItemType::UABig:
    case ItemType::MissileSmall:
    case ItemType::MissileBig: {
        const int slot = type == ItemType::UASmall || type == ItemType::UABig ? 0 : 1;
        const bool big = type == ItemType::UABig || type == ItemType::MissileBig;
        pickupSfx(big ? SfxId::AMMO_POWER_UP2 : SfxId::AMMO_POWER_UP1);
        m_ammo[slot] = std::min(m_ammo[slot] + (big ? 100 : 50), m_ammoMax[slot]);
        return true;
    }
    case ItemType::VoltDriver:
    case ItemType::Battlehammer:
    case ItemType::Imperialist:
    case ItemType::Judicator:
    case ItemType::Magmaul:
    case ItemType::ShockCoil:
    case ItemType::OmegaCannon:
    case ItemType::AffinityWeapon:
        pickUpWeapon(type);
        return true;
    case ItemType::DoubleDamage:
        m_doubleDmgTimer = 900 * 2;
        if (m_isMain) {
            Sfx::instance().playFreeSfx(SfxId::DOUBLE_DAMAGE_POWER_UP);
            updateDoubleDamageSfx(0, true);
        }
        return true;
    case ItemType::Cloak:
        m_cloakTimer = 900 * 2;
        m_cloaking = true;
        if (m_isMain) {
            Sfx::instance().playFreeSfx(SfxId::CLOAK_POWER_UP);
            updateCloakSfx(0, true);
        }
        return true;
    case ItemType::Deathalt:
        m_deathaltTimer = 900 * 2;
        if (!m_altForm && !m_morphing) {
            enterAltForm();
        }
        if (m_isMain) {
            Sfx::instance().playFreeSfx(SfxId::DOUBLE_DAMAGE_POWER_UP);
        }
        return true;
    default:
        return true;
    }
}

void Player::pickUpWeapon(ItemType type)
{
    // PlayerEntity.PickUpWeapon
    const int hunter = static_cast<int>(m_hunter);
    int weapon = -1;
    switch (type) {
    case ItemType::AffinityWeapon:
        if (m_hunter == Hunter::Samus || m_hunter == Hunter::Guardian) {
            m_ammo[1] = std::min(m_ammo[1] + 50, m_ammoMax[1]);
            if (m_isMain && Sfx::instance().timedSfxMute == 0) {
                Sfx::instance().playFreeSfx(SfxId::AMMO_POWER_UP1);
            }
            return;
        }
        weapon = affinityWeapons().at(hunter);
        break;
    case ItemType::VoltDriver: weapon = Beam::VoltDriver; break;
    case ItemType::Battlehammer: weapon = Beam::Battlehammer; break;
    case ItemType::Imperialist: weapon = Beam::Imperialist; break;
    case ItemType::Judicator: weapon = Beam::Judicator; break;
    case ItemType::Magmaul: weapon = Beam::Magmaul; break;
    case ItemType::ShockCoil: weapon = Beam::ShockCoil; break;
    case ItemType::OmegaCannon: weapon = Beam::OmegaCannon; break;
    default: return;
    }
    const WeaponInfo& info = weaponInfo(weapon);
    if (m_ammo[info.ammoType] < 60) {
        m_ammo[info.ammoType] = std::min(m_ammo[info.ammoType] + 60, 60);
    }
    const bool pickupSfx = m_isMain && Sfx::instance().timedSfxMute == 0;
    if (m_availableWeapons[weapon]) {
        if (pickupSfx) {
            Sfx::instance().playFreeSfx(SfxId::AMMO_POWER_UP1);
        }
        return;
    }
    if (pickupSfx) {
        Sfx::instance().playFreeSfx(SfxId::WEAPON_POWER_UP);
    }
    m_availableWeapons[weapon] = true;
    const int slot2 = m_weaponSlots[2];
    const int affinity = affinityWeapons().at(hunter);
    if (slot2 < 0 || weapon == Beam::OmegaCannon
        || ((info.priority > weaponInfo(slot2).priority || weapon == affinity) && (!m_shooting || m_currentWeapon != slot2))) {
        if ((info.priority > m_equip.weapon->priority || weapon == Beam::OmegaCannon || weapon == affinity) && !m_shooting) {
            if (!tryEquipWeapon(weapon, false)) {
                updateAffinityWeaponSlot(weapon);
            }
        } else if (!m_shooting || m_currentWeapon != slot2) {
            updateAffinityWeaponSlot(weapon);
        }
    }
}

void Player::gainHealth(int health)
{
    // PlayerProcess.GainHealth: with the Halfturret out, the two share it.
    if (m_health > 0) {
        if (m_halfturret.active) {
            if (m_health <= m_halfturret.health) {
                m_health += health - health / 2;
                m_halfturret.health += health / 2;
            } else {
                m_health += health / 2;
                m_halfturret.health += health - health / 2;
            }
            m_halfturret.health = std::min(m_halfturret.health, 100);
        } else {
            m_health += health;
        }
        m_health = std::min(m_health, m_healthMax);
    }
}

// ---- weapons ---------------------------------------------------------------

bool Player::tryEquipWeapon(int beam, bool silent)
{
    // PlayerEntity.TryEquipWeapon
    if (beam < 0 || beam >= 9) {
        return false;
    }
    const WeaponInfo& info = weaponInfo(beam);
    const int ammo = m_ammo[info.ammoType];
    const bool hasAmmo = beam == Beam::PowerBeam || ammo >= info.ammoCost || ammo == -1;
    if (!silent && (!hasAmmo || !m_availableWeapons[beam] || m_gunAnimation == GunAnim::UpDown)) {
        if (m_isMain) {
            Sfx::instance().playFreeSfx(SfxId::BEAM_SWITCH_FAIL);
        }
        return false;
    }
    stopBeamChargeSfx(m_currentWeapon);
    updateZoom(false);
    m_previousWeapon = m_currentWeapon;
    m_currentWeapon = beam;
    // The affinity weapon fires its hunter's version (Weapons.Current[beam + 9]).
    m_equip.weapon = beam == affinityWeapons().at(static_cast<int>(m_hunter)) ? &weaponsMP().at(beam + 9) : &info;
    m_equip.chargeLevel = 0;
    m_equip.smokeLevel = 0;
    m_timeSinceInput = 0;
    if (!silent) {
        if (m_isMain && !m_altForm && beam != Beam::Missile) {
            Sfx::instance().playFreeSfx(hunterSfx(m_hunter, HunterSfx::BeamSwitch));
        }
        if (beam == Beam::Missile) {
            setGunAnimation(GunAnim::MissileOpen, AnimFlags::NoLoop);
        } else if (m_previousWeapon == Beam::Missile) {
            setGunAnimation(GunAnim::MissileClose, AnimFlags::NoLoop);
        } else {
            // The switch animation's materials are the old beam's.
            m_currentWeapon = m_previousWeapon;
            setGunAnimation(GunAnim::Switch, AnimFlags::NoLoop);
            m_currentWeapon = beam;
        }
    }
    if (beam != Beam::PowerBeam && beam != Beam::Missile) {
        updateAffinityWeaponSlot(beam);
    }
    return true;
}

void Player::updateAffinityWeaponSlot(int beam)
{
    if (m_weaponSlots[2] == Beam::OmegaCannon && beam != Beam::OmegaCannon) {
        m_availableWeapons[Beam::OmegaCannon] = false;
    }
    m_weaponSlots[2] = beam;
}

void Player::unequipOmegaCannon()
{
    // The Omega Cannon is gone after one shot in multiplayer.
    if (m_currentWeapon != Beam::OmegaCannon) {
        return;
    }
    m_availableWeapons[Beam::OmegaCannon] = false;
    int priority = 0;
    int next = -1;
    for (int i = 1; i < 9; i++) {
        if (i != Beam::Missile && m_availableWeapons[i]) {
            const WeaponInfo& info = weaponInfo(i);
            if (info.priority > priority && m_ammo[info.ammoType] >= info.ammoCost) {
                priority = info.priority;
                next = i;
            }
        }
    }
    updateAffinityWeaponSlot(next);
    tryEquipWeapon(next < 0 ? Beam::PowerBeam : next, false);
}

void Player::cycleWeapon(int direction)
{
    // PlayerInput._weaponOrder, skipping what CanCycleToWeapon refuses.
    static constexpr int order[9] = {Beam::PowerBeam, Beam::Missile, Beam::VoltDriver, Beam::Battlehammer, Beam::Imperialist,
        Beam::Judicator, Beam::Magmaul, Beam::ShockCoil, Beam::OmegaCannon};
    if (m_health == 0 || m_altForm || m_morphing) {
        return;
    }
    int index = 0;
    while (index < 9 && order[index] != m_currentWeapon) {
        index++;
    }
    for (int step = 1; step < 9; step++) {
        const int beam = order[((index + direction * step) % 9 + 9) % 9];
        const WeaponInfo& info = weaponInfo(beam);
        const int ammo = m_ammo[info.ammoType];
        if (m_availableWeapons[beam] && (beam == Beam::PowerBeam || ammo == -1 || ammo >= info.ammoCost)) {
            tryEquipWeapon(beam, false);
            return;
        }
    }
}

void Player::giveAllWeapons()
{
    m_availableWeapons.fill(true);
    m_ammo = m_ammoMax;
}

void Player::updateZoom(bool zoom)
{
    if (m_isMain && m_equip.zoomed != zoom) {
        Sfx::instance().playFreeSfx(zoom ? SfxId::SNIPER_ZOOM_IN : SfxId::SNIPER_ZOOM_OUT);
    }
    m_equip.zoomed = zoom;
}

void Player::updateAimVecs()
{
    // PlayerProcess.UpdateAimVecs: the muzzle is on the gun, which drifts half
    // way toward the point being aimed at.
    const Vec3 facing = facingVector();
    const Vec3 right = gunVec2();
    const Vec3 up = normalized(cross(facing, right));
    Vec3 gunDrawPos = facing * fx(v.FieldB8) + m_cam.position + right * fx(v.FieldB0) + up * fx(v.FieldB4);
    gunDrawPos[1] += fx(20) * std::cos(m_gunViewBob * kDegToRad);
    m_aimPosition = facing * fx(v.AimDistance) + m_cam.position;
    Vec3 aimVec = m_aimPosition - gunDrawPos;
    const Vec3 along = facing * dot(aimVec, facing);
    m_aimVec = normalized(aimVec + (along - aimVec) * 0.5f);
    m_muzzlePos = gunDrawPos + m_aimVec * fx(v.MuzzleOffset);
}

int Player::processWeapon(const PlayerInput& input, uint16_t& animFlags2)
{
    // PlayerInput.ProcessBiped: holding fire, charging, zoom, and the shot.
    int anim2 = -1;
    if (!m_equip.weapon) {
        return anim2;
    }
    const WeaponInfo& weapon = *m_equip.weapon;
    const uint32_t flags = static_cast<uint32_t>(weapon.flags);
    if (!input.shootHeld) {
        m_shooting = false;
    } else if (input.shootPressed || !m_noShotsFired) {
        m_shooting = true;
        m_noShotsFired = false;
    }
    if (!(flags & WeaponFlags::CanCharge)) {
        m_equip.chargeLevel = 0;
    } else {
        bool release = false;
        if (!m_shooting || m_ammo[weapon.ammoType] < weapon.chargeCost) {
            release = true;
        } else {
            if (m_equip.chargeLevel > 0 && m_gunAnimation != GunAnim::MissileClose) {
                // The C#'s own condition: the Power Beam's hum only once it is a charge.
                if (m_currentWeapon != Beam::PowerBeam || m_equip.chargeLevel >= weapon.minCharge * 2) {
                    playBeamChargeSfx(m_currentWeapon);
                }
                if ((m_bipedAnim2.flags & AnimFlags::Ended) || m_bipedAnim2.index == Anim::Charge
                    || (m_bipedAnim2.index == Anim::Shoot && m_bipedAnim2.frame > 8)) {
                    anim2 = Anim::Charge;
                }
            }
            if (m_equip.chargeLevel >= weapon.fullCharge * 2) {
                m_equip.smokeLevel = std::min(m_equip.smokeLevel + weapon.smokeChargeAmount, weapon.smokeStart * 2);
            } else {
                m_equip.chargeLevel++;
                const int minCharge = weapon.minCharge * 2;
                if (m_equip.chargeLevel > minCharge) {
                    const int fullCharge = weapon.fullCharge * 2;
                    const int chargeCost = weapon.chargeCost * 2;
                    const int minCost = weapon.minChargeCost * 2;
                    const int cost = minCost + (chargeCost - minCost) * (m_equip.chargeLevel - minCharge) / (fullCharge - minCharge);
                    if (m_ammo[weapon.ammoType] < cost / 2) {
                        m_equip.chargeLevel--;
                    }
                }
            }
        }
        if (release) {
            stopBeamChargeSfx(m_currentWeapon);
            if (m_equip.chargeLevel >= weapon.minCharge * 2) {
                tryFireWeapon(input.shootPressed);
                anim2 = Anim::ChargeShoot;
                animFlags2 = AnimFlags::NoLoop;
            }
            m_equip.chargeLevel = 0;
        }
    }
    if ((flags & WeaponFlags::CanZoom) && input.zoomPressed) {
        updateZoom(!m_equip.zoomed);
    }
    if (m_equip.zoomed) {
        // Eases toward the weapon's zoom FOV, 2 degrees a tick.
        const float zoomFov = fx(weapon.zoomFov) * 2;
        if (zoomFov > m_fov) {
            m_fov = std::min(m_fov + 2 * 2, zoomFov);
        } else if (zoomFov < m_fov) {
            m_fov = std::max(m_fov - 2 * 2, zoomFov);
        }
    }
    // A weapon can have changed above (the Omega Cannon after its shot).
    const WeaponInfo& current = *m_equip.weapon;
    const uint32_t currentFlags = static_cast<uint32_t>(current.flags);
    if ((input.shootPressed && m_equip.chargeLevel <= 1 * 2)
        || ((currentFlags & WeaponFlags::RepeatFire) && m_shooting
            && (!(currentFlags & WeaponFlags::CanCharge) || m_equip.chargeLevel < current.minCharge * 2))) {
        if (tryFireWeapon(input.shootPressed)) {
            anim2 = Anim::Shoot;
            animFlags2 |= AnimFlags::NoLoop;
            if (m_bipedAnim2.index == Anim::Shoot) {
                setBiped2Animation(Anim::Shoot, m_bipedAnim2.flags);
            }
        }
    }
    return anim2;
}

void Player::applyButtonAim(float aimX, float aimY)
{
    if (aimX == 0 && aimY == 0) {
        return;
    }
    // Zoomed, the turn slows with the field of view.
    float sensitivity = 1;
    const float normalFov = fx(v.NormalFov) * 2;
    if (m_equip.zoomed && normalFov != 0) {
        sensitivity = m_fov / normalFov;
    }
    float pitch = m_pitch + aimY * sensitivity;
    pitch = m_altForm ? std::clamp(pitch, -25.0f, 5.0f) : pitch;
    setAim(m_yaw + aimX * sensitivity, pitch);
    updateAimVecs();
}

void Player::updateCloak()
{
    // PlayerProcess: the Cloak pickup's timer, Trace (and the Prime Hunter)
    // fading while standing still with the Imperialist or in alt form, and
    // the alpha easing toward its target.
    if (m_health > 0) {
        if (m_cloaking) {
            if (--m_cloakTimer > 0) {
                m_targetAlpha = 3 / 31.0f;
                if (m_isMain && m_cloakTimer == 210 * 2) {
                    updateCloakSfx(1, true);
                } else if (m_isMain && m_cloakTimer == 120 * 2) {
                    updateCloakSfx(2, true);
                }
            } else {
                m_cloaking = false;
                m_targetAlpha = 1;
                Sfx::instance().playFreeSfx(SfxId::CLOAK_OFF);
                if (m_isMain) {
                    updateCloakSfx(0, false);
                }
            }
        } else {
            m_targetAlpha = 1;
            if ((m_hunter == Hunter::Trace || primeHunter) && m_hSpeedMag < 0.05f && m_speed[1] < 0.05f && m_speed[1] > -0.05f) {
                if (m_cloakTimer >= 30 * 2) {
                    if (m_hunter == Hunter::Trace && m_altForm) {
                        m_targetAlpha = 1 / 31.0f;
                    } else if (m_currentWeapon == Beam::Imperialist) {
                        m_targetAlpha = 5 / 31.0f;
                    }
                } else {
                    m_cloakTimer++;
                }
            } else {
                m_cloakTimer = 0;
            }
        }
        if (m_cloaking || !m_altAttack) {
            if (m_curAlpha < m_targetAlpha) {
                m_curAlpha = std::min(m_curAlpha + 2 / 31.0f / 2, m_targetAlpha);
            } else if (m_curAlpha > m_targetAlpha) {
                m_curAlpha = std::max(m_curAlpha - 1 / 31.0f / 2, m_targetAlpha);
            }
        } else {
            m_cloakTimer = 0;
            m_curAlpha = m_targetAlpha = 1;
        }
    } else if (m_altForm || m_morphing) {
        m_curAlpha = std::max(m_curAlpha - 2 / 31.0f / 2, 0.0f);
    }
}

int Player::abilities() const
{
    // PlayerEntity.Spawn: AbilityFlags for multiplayer.
    int flags = 0x1; // AltForm
    switch (m_hunter) {
    case Hunter::Samus: flags |= 0x4 | 0x40; break; // Bombs, Boost
    case Hunter::Kanden:
    case Hunter::Sylux: flags |= 0x4; break;
    case Hunter::Trace: flags |= 0x400; break;
    case Hunter::Noxus: flags |= 0x100; break;
    case Hunter::Spire: flags |= 0x200; break;
    case Hunter::Weavel: flags |= 0x1000; break;
    default: break;
    }
    return flags;
}

bool Player::tryFireWeapon(bool pressed)
{
    if (!m_cloaking) {
        m_cloakTimer = 0;
    }
    // PlayerInput.TryFireWeapon
    if (!m_fire || !m_equip.weapon) {
        return false;
    }
    m_cloakTimer = 0;
    const WeaponInfo& weapon = *m_equip.weapon;
    if (pressed || m_currentWeapon != Beam::PowerBeam) {
        m_autofireCooldown = weapon.autofireCooldown * 2;
        m_powerBeamAutofire = 0;
    } else {
        if (m_powerBeamAutofire < 0xFFFF) {
            m_powerBeamAutofire++;
        }
        // Holding the Power Beam slows it down, by up to 15 frames.
        const int pbAuto = static_cast<int>(std::min(m_powerBeamAutofire / 2, 90) * 15 / 90.0f);
        m_autofireCooldown = (pbAuto + weapon.autofireCooldown) * 2;
    }
    if (m_timeSinceShot < weapon.shotCooldown * 2 || (!pressed && m_timeSinceShot < m_autofireCooldown)) {
        return false;
    }
    if (m_gunAnimation == GunAnim::UpDown) {
        return false;
    }
    Vec3 shotVec = m_aimPosition - m_muzzlePos;
    if (m_disruptedTimer > 0) {
        // Battlehammer's disruption: up to 3 units off in each axis.
        for (float& c : shotVec) {
            c += (static_cast<int>(std::rand() % 24576) - 12288) / 4096.0f;
        }
    }
    shotVec = normalized(shotVec);
    // The Prime Hunter fires every weapon's affinity version, for the shot only.
    const WeaponInfo* curWeapon = m_equip.weapon;
    if (primeHunter) {
        m_equip.weapon = &weaponsMP().at(m_currentWeapon + 9);
    }
    int flags = BeamSpawnFlags::NoMuzzle;
    if (m_doubleDmgTimer > 0) {
        flags |= BeamSpawnFlags::DoubleDamage;
    } else if (primeHunter) {
        flags |= BeamSpawnFlags::PrimeHunter;
    }
    const int result = m_fire(*this, m_equip, m_muzzlePos, shotVec, flags);
    if (result == BeamResult::NoSpawn) {
        m_equip.weapon = curWeapon;
        playBeamEmptySfx(m_equip.weapon->beam);
        return false;
    }
    {
        // The shot's sound, of the weapon it was fired as (the Prime Hunter's affinity version).
        const WeaponInfo& fired = *m_equip.weapon;
        const uint32_t firedFlags = static_cast<uint32_t>(fired.flags);
        const bool charged = (firedFlags & WeaponFlags::PartialCharge) ? m_equip.chargeLevel >= fired.minCharge * 2
                                                                        : m_equip.chargeLevel >= fired.fullCharge * 2;
        const float amountA = 0x3FFF * m_shockCoilTimer / (30.0f * 2);
        playBeamShotSfx(fired.beam, charged, (firedFlags & WeaponFlags::Continuous) != 0, (result & BeamResult::Homing) != 0, amountA);
        if (fired.beam == Beam::Imperialist && m_ammo[fired.ammoType] >= fired.ammoCost) {
            m_soundSource.playSfx(SfxId::SNIPER_RELOAD);
        }
    }
    m_equip.weapon = curWeapon;
    m_timeSinceShot = 0;
    if (m_effects != nullptr && (m_muzzleEffect < 0 || !(static_cast<uint32_t>(weapon.flags) & WeaponFlags::Continuous))) {
        // The muzzle flash (a continuous weapon keeps the one it has).
        m_effects->unlink(m_muzzleEffect);
        m_muzzleEffect = m_effects->spawnEntry(kMuzzleEffectIds[std::clamp(m_currentWeapon, 0, 8)],
            Mat4::fromVectors(gunVec2(), facingVector(), m_muzzlePos));
    }
    if (m_currentWeapon == Beam::Missile) {
        m_shotMissile = true;
    }
    if (m_equip.chargeLevel < weapon.minCharge * 2) {
        m_shotUncharged = true;
    } else {
        m_shotCharged = true;
    }
    unequipOmegaCannon();
    return true;
}

void Player::setGunAnimation(int anim, uint16_t flags)
{
    // PlayerEntity.SetGunAnimation: slot 0 is the hunter's gun motion, slot 1
    // the beam's materials over it.
    m_gunAnimation = anim;
    const int hunter = static_cast<int>(m_hunter);
    if (m_models.gun != nullptr) {
        const int id = gunAnimationId(hunter, anim, 0);
        if (id >= 0) {
            m_gunAnim.set(*m_models.gun, id, flags);
        }
        const int materialId = gunAnimationId(hunter, anim, m_currentWeapon + 1);
        if (materialId >= 0) {
            m_gunAnim2.set(*m_models.gun, materialId, flags);
        }
    }
    if (m_isMain) {
        // The missile hatch closing, or opening for a switch.
        if (anim == GunAnim::MissileClose) {
            Sfx::instance().stopSoundByHandle(m_missileSfxHandle);
            m_missileSfxHandle = -1;
            if (!m_altForm) {
                playMissileSfx(HunterSfx::MissileClose);
            }
        } else if (anim == GunAnim::MissileOpen && m_equip.chargeLevel == 0) {
            Sfx::instance().stopSoundByHandle(m_missileSfxHandle);
            if (!m_altForm && m_health > 0) {
                m_missileSfxHandle = playMissileSfx(HunterSfx::MissileSwitch);
            }
        }
    }
    m_gunOpenAnimation = anim == GunAnim::FullChargeMissile || anim == GunAnim::ChargingMissile || anim == GunAnim::MissileClose
        || anim == GunAnim::MissileOpen || anim == GunAnim::Unknown9 || anim == GunAnim::MissileShot;
}

void Player::updateGunAnimation()
{
    // PlayerEntity.UpdateGunAnimation
    if (m_equip.weapon == nullptr) {
        return;
    }
    const bool ended = m_gunAnim.index < 0 || (m_gunAnim.flags & AnimFlags::Ended);
    if (m_timeSinceInput == 0) {
        if (m_gunAnimation == GunAnim::UpDown && (m_gunAnim.flags & AnimFlags::Reverse)) {
            m_gunAnim.flags &= static_cast<uint16_t>(~(AnimFlags::Reverse | AnimFlags::Ended));
        }
    } else if (m_timeSinceInput >= v.GunIdleTime * 2) {
        if (m_gunAnimation != GunAnim::UpDown) {
            if (m_currentWeapon != Beam::Missile || m_gunAnimation == GunAnim::MissileClose) {
                setGunAnimation(GunAnim::UpDown, AnimFlags::NoLoop | AnimFlags::Reverse);
            } else {
                setGunAnimation(GunAnim::MissileClose, AnimFlags::NoLoop);
            }
        }
        return;
    }
    if (m_gunAnimation == GunAnim::UpDown) {
        if (!ended) {
            return;
        }
        if (m_currentWeapon == Beam::Missile) {
            setGunAnimation(GunAnim::MissileOpen, AnimFlags::NoLoop);
        }
    }
    if (m_shotUncharged) {
        if (!m_shotMissile) {
            setGunAnimation(GunAnim::Shot, AnimFlags::NoLoop);
        } else if (!m_gunOpenAnimation) {
            setGunAnimation(GunAnim::Unknown9, AnimFlags::NoLoop);
        } else {
            setGunAnimation(GunAnim::MissileShot, AnimFlags::NoLoop);
        }
        return;
    }
    if (m_shotCharged) {
        setGunAnimation(m_shotMissile ? GunAnim::MissileShot : GunAnim::ChargeShot, AnimFlags::NoLoop);
        return;
    }
    const WeaponInfo& weapon = *m_equip.weapon;
    if (m_equip.chargeLevel >= weapon.minCharge * 2) {
        if ((m_gunAnimation == GunAnim::Charging || m_gunAnimation == GunAnim::ChargingMissile) && m_frameCount % 2 == 0
            && weapon.fullCharge > weapon.minCharge) {
            // The charge animation follows the charge.
            m_gunAnim.frame = std::clamp((m_gunAnim.frameCount - 1) * (m_equip.chargeLevel / 2 - weapon.minCharge)
                    / (weapon.fullCharge - weapon.minCharge), 0, std::max(0, m_gunAnim.frameCount - 1));
        }
        if (m_currentWeapon == Beam::Missile) {
            if (m_gunAnimation != GunAnim::ChargingMissile && m_gunAnimation != GunAnim::FullChargeMissile) {
                setGunAnimation(GunAnim::ChargingMissile, AnimFlags::NoLoop);
            } else if (m_gunAnimation != GunAnim::FullChargeMissile && ended) {
                setGunAnimation(GunAnim::FullChargeMissile, 0);
            }
        } else if (m_gunAnimation != GunAnim::Charging && m_gunAnimation != GunAnim::FullCharge) {
            setGunAnimation(GunAnim::Charging, AnimFlags::NoLoop);
        } else if (m_gunAnimation != GunAnim::FullCharge && ended) {
            setGunAnimation(GunAnim::FullCharge, 0);
        }
        return;
    }
    if ((m_gunAnimation == GunAnim::Charging || m_gunAnimation == GunAnim::ChargingMissile) && !(m_gunAnim.flags & AnimFlags::Reverse)) {
        m_gunAnim.flags |= AnimFlags::Reverse;
        return;
    }
    if (m_gunAnimation == GunAnim::FullCharge) {
        setGunAnimation(GunAnim::Idle, AnimFlags::NoLoop);
        return;
    }
    if (m_gunAnimation == GunAnim::FullChargeMissile) {
        setGunAnimation(GunAnim::MissileOpen, AnimFlags::NoLoop);
        m_gunAnim.frame = std::max(0, m_gunAnim.frameCount - 1);
        return;
    }
    if (ended) {
        // (The C# also closes the missile launcher while the weapon menu points
        // elsewhere; there is no weapon menu here.)
        if (!m_gunOpenAnimation || m_gunAnimation == GunAnim::MissileClose) {
            setGunAnimation(GunAnim::Idle, AnimFlags::NoLoop);
        }
    }
}

// ---- damage ----------------------------------------------------------------

TeamRules& teamRules()
{
    static TeamRules rules;
    return rules;
}

bool Player::takeDamage(int damage, uint32_t flags, const Vec3* direction, const DamageSource& source)
{
    // PlayerEntity.TakeDamage, for a multiplayer battle: every beam is of
    // normal effectiveness and the damage level is medium (x1).
    if (m_health == 0) {
        return false;
    }
    // NetDamage.Suppress: on a replica the authority has already decided,
    // except for what this machine predicts (NetHitPrediction).
    bool predicting = false;
    if (m_netReplica && s_damageReplay == 0) {
        if (m_netHooks == nullptr || !m_netHooks->predicts(*this, source, flags)) {
            return false;
        }
        predicting = true;
    } else if (m_netHooks != nullptr && s_damageReplay == 0 && m_netHooks->suppress(*this, source)) {
        return false;
    }
    m_pendingImpulse = direction != nullptr ? *direction : Vec3{};
    if (m_spawnInvulnTimer > 0 && !(flags & (DamageFlags::Death | DamageFlags::IgnoreInvuln))) {
        return false;
    }
    if (!(flags & (DamageFlags::Death | DamageFlags::IgnoreInvuln | DamageFlags::NoDmgInvuln))) {
        if (m_damageInvulnTimer > 0) {
            return false;
        }
        m_damageInvulnTimer = v.DamageInvuln * 2;
    }
    Player* attacker = source.attacker;
    if (!source.claimed && source.beam < 0 && source.bomb < 0 && attacker != nullptr && attacker != this && attacker->m_doubleDmgTimer > 0) {
        damage *= 2; // a direct hit from a player (alt attacks), not a beam
    }
    // A teammate's hit does nothing without friendly fire (it still counts as a hit).
    const bool ignoreDamage = teamRules().teams && !teamRules().friendlyFire && attacker != nullptr && attacker != this
        && areAllies(attacker->m_teamIndex, m_teamIndex);
    if (ignoreDamage) {
        damage = 0;
    }
    if (!ignoreDamage && (flags & DamageFlags::Headshot) && attacker != nullptr && attacker != this) {
        attacker->m_headshotTimer = 20 * 2; // HEADSHOT!
    }
    if (m_halfturret.active && attacker != nullptr && !ignoreDamage) {
        halfturretOnTakeDamage(attacker, damage);
    }
    int turretDamageDone = -1;
    if ((flags & DamageFlags::Halfturret) && !ignoreDamage) {
        // A hit on the turret: it takes its share, and never kills Weavel himself.
        const int turretDamage = m_health > m_halfturret.health ? damage - damage / 2 : damage / 2;
        if (m_halfturret.health <= turretDamage) {
            halfturretDie();
        } else {
            m_halfturret.health -= turretDamage;
        }
        damage -= turretDamage;
        turretDamageDone = turretDamage;
        if (m_health <= damage) {
            damage = m_health - 1;
        }
        m_halfturret.timeSinceDamage = 0;
    }
    if (predicting) {
        m_netHooks->noteHit(*this, flags, damage, source);
    }
    const bool dead = m_health <= damage || (flags & DamageFlags::Death);
    m_timeSinceDamage = 0;
    if (dead) {
        // The death cry; everything else this player was playing stops.
        m_soundSource.stopAllSfx(true);
        if (m_isMain) {
            updateDoubleDamageSfx(0, false);
            updateCloakSfx(0, false);
            Sfx::instance().stopFreeSfxScripts();
        }
        playHunterSfx(HunterSfx::Death);
        if (m_equip.weapon != nullptr) {
            stopBeamChargeSfx(m_currentWeapon);
        }
        m_health = 0;
        m_equip.chargeLevel = 0;
        m_doubleDmgTimer = m_deathaltTimer = m_cloakTimer = 0;
        m_cloaking = false;
        m_frozenTimer = m_disruptedTimer = m_burnTimer = 0;
        m_boostCharge = 0;
        if (m_halfturret.active) {
            halfturretDie();
        }
        clearWeaponEffects();
        if (m_altForm || m_morphing) {
            spawnEffect(216, {1, 0, 0}, {0, 1, 0}, m_position, false); // deathAlt
        }
        updateZoom(false);
        m_speed = {};
        m_respawnTimer = 90 * 2; // PlayerEntity.RespawnTime
        m_timeSinceDead = 0;
        m_shooting = false;
        m_noShotsFired = true;
        // SwitchCamera(CameraType.Free, attacker.Position), or ahead after a suicide;
        // UpdateCameraFree then backs the camera away from it behind the body.
        m_deathLookAt = attacker != nullptr && attacker != this ? attacker->position() : m_cam.position + m_cam.facing;
        const Vec3 camVec = m_cam.position - *m_deathLookAt;
        const float camLen = std::sqrt(dot(camVec, camVec));
        m_cam.position = m_cam.position + (camLen > 0 ? camVec * (1 / camLen) : Vec3{1, 0, 0}) * (1 / 64.0f);
        m_field544 = m_cam.position;
        m_camSwitchTimer = 0;
        return notifyDamage(source, true, flags, damage, turretDamageDone);
    }
    m_health -= damage;
    const bool turret = flags & DamageFlags::Halfturret;
    bool skipSfx = false;
    if (source.beam >= 0 && !ignoreDamage) {
        if (source.afflictions & Affliction::Freeze) {
            m_soundSource.playSfx(SfxId::SHOTGUN_FREEZE);
            if (turret) {
                // HalfturretEntity.OnFrozen
                if (m_halfturret.timeSinceFrozen > 60 * 2) {
                    m_halfturret.freezeTimer = 75 * 2;
                } else if (m_halfturret.freezeTimer < 15 * 2) {
                    m_halfturret.freezeTimer = 15 * 2;
                }
            } else if (m_frozenTimer == 0) {
                if (m_timeSinceFrozen > 60 * 2) {
                    m_frozenTimer = 75 * 2;
                } else if (m_frozenTimer < 15 * 2) {
                    m_frozenTimer = 15 * 2;
                }
                endAltAttack();
            }
        }
        if ((source.afflictions & Affliction::Disrupt) && !turret) {
            m_disruptedTimer = 60 * 2;
            if (m_isMain) {
                skipSfx = true;
                m_soundSource.playSfx(SfxId::LOB_DISRUPT);
            }
        }
        if (source.afflictions & Affliction::Burn) {
            if (turret) {
                m_halfturret.burnTimer = 150 * 2; // HalfturretEntity.OnSetOnFire
            } else {
                m_burnedBy = attacker;
                m_burnTimer = 150 * 2;
                createBurnEffect();
            }
        }
    }
    if (!skipSfx && !(flags & DamageFlags::NoSfx)) {
        playHunterSfx(HunterSfx::Damage);
    }
    if (m_isMain && !m_altForm) {
        playRandomDamageSfx();
    }
    std::optional<Vec3> hitDirection;
    if (m_frozenTimer == 0 && direction != nullptr) {
        // The hit pushes: horizontally in full, upward only to 0.25 a tick.
        const Vec3& dir = *direction;
        if (!m_altForm) {
            if (!turret) {
                Vec3 speed = m_speed + Vec3{dir[0], 0, dir[2]};
                if (dir[1] <= 0) {
                    speed[1] += dir[1];
                } else if (speed[1] < 0.25f) {
                    speed[1] = std::min(speed[1] + dir[1], 0.25f);
                }
                m_speed = speed;
            }
            if (dir[0] != 0 || dir[1] != 0 || dir[2] != 0) {
                hitDirection = dir;
            } else if (source.beam >= 0) {
                hitDirection = source.beamVelocity;
            }
        } else if (!turret) {
            m_speed = m_speed + Vec3{dir[0] * 0.4f, 0, dir[2] * 0.4f};
        }
    } else if (m_frozenTimer == 0 && attacker != nullptr) {
        hitDirection = m_position - attacker->m_position;
    }
    if (hitDirection && !m_altForm) {
        // The side the hit came from: the torso flinches that way, and the
        // HUD's arrow on that side blinks for two seconds.
        const float hitZ = (*hitDirection)[2];
        const float hitX = -(*hitDirection)[0];
        const Vec3 gunVec = gunVec2();
        const float dirLeftRight = hitX * gunVec[0] - hitZ * gunVec[2];
        const float dirUpDown = hitX * m_field70 - hitZ * m_field74;
        const float dirHorizontal = std::fabs(dirLeftRight);
        const float dirVertical = std::fabs(dirUpDown);
        int anim;
        int indicator;
        if (dirVertical <= dirHorizontal) {
            anim = dirLeftRight <= 0 ? Anim::DamageRight : Anim::DamageLeft;
            indicator = dirLeftRight <= 0 ? 2 : 6;
        } else if (dirUpDown <= 0) {
            anim = Anim::DamageBack;
            indicator = 4;
        } else {
            anim = Anim::DamageFront;
            indicator = 0;
        }
        m_damageIndicatorTimers[indicator] = 63 * 2;
        setBipedAnimation(anim, AnimFlags::NoLoop, false, true, false);
    }
    if (!m_altForm) {
        m_cam.setShake((flags & DamageFlags::Burn) ? 0.03f : std::max(damage * 0.01f, 0.05f));
    }
    return notifyDamage(source, false, flags, damage, turretDamageDone);
}

bool Player::notifyDamage(const DamageSource& source, bool died, uint32_t flags, int damage, int turretDamage)
{
    if (m_damageListener) {
        DamageSource hit = source;
        hit.damage = damage;
        hit.turretDamage = turretDamage;
        hit.impulse = m_pendingImpulse;
        m_damageListener(*this, hit, died, flags);
    }
    return died;
}

// ---- other players -----------------------------------------------------------

void Player::checkPlayerCollision()
{
    // PlayerCollision.CheckPlayerCollision: players push each other apart;
    // a boost, Deathalt and the alt attacks hurt.
    if (m_players == nullptr) {
        return;
    }
    if (m_hunter == Hunter::Spire && m_altAttack) {
        updateSpireRocks();
    }
    if (m_spectating) {
        return;
    }
    for (Player* other : *m_players) {
        if (other->m_health == 0 || other->m_spectating) {
            continue;
        }
        if (m_halfturret.active) {
            checkHalfturretCollision(*other);
        }
        if (other == this) {
            continue;
        }
        Vec3 between = sphereCenter() - other->sphereCenter();
        float distSqr = dot(between, between);
        if (distSqr == 0) {
            between = {1, 0, 0};
            distSqr = 1;
        }
        const float radii = volumeRadius() + other->volumeRadius();
        if (distSqr < radii * radii) {
            const float dist = std::sqrt(distSqr);
            between = between * (1 / dist);
            m_speed = m_speed - between * dot(m_speed, between);
            m_position = m_position + between * (radii - dist);
            const float kbAccel = fx(v.AltAttackKnockbackAccel);
            if (m_hunter == Hunter::Noxus && m_altForm) {
                other->m_acceleration = {between[0] * -kbAccel, 0, between[2] * -kbAccel};
                other->m_accelerationTimer = v.AltAttackKnockbackTime * 2;
            }
            if (other->m_hunter == Hunter::Noxus && other->m_altForm) {
                m_acceleration = {between[0] * kbAccel, 0, between[2] * kbAccel};
                m_accelerationTimer = v.AltAttackKnockbackTime * 2;
            }
            DamageSource fromMe, fromOther;
            fromMe.attacker = this;
            fromOther.attacker = other;
            const Vec3 mySpeed = m_speed, otherSpeed = other->m_speed;
            if (m_boosting) {
                other->takeDamage(m_boostDamage, DamageFlags::NoDmgInvuln, &mySpeed, fromMe);
                endAltAttack();
            }
            if (other->m_boosting) {
                takeDamage(other->m_boostDamage, DamageFlags::NoDmgInvuln, &otherSpeed, fromOther);
                other->endAltAttack();
            }
            if (m_deathaltTimer > 0) {
                other->takeDamage(200, DamageFlags::Deathalt | DamageFlags::NoDmgInvuln, &mySpeed, fromMe);
            }
            if (other->m_deathaltTimer > 0) {
                takeDamage(200, DamageFlags::Deathalt | DamageFlags::NoDmgInvuln, &otherSpeed, fromOther);
            }
            checkAltAttackHit2(*this, *other);
            checkAltAttackHit2(*other, *this);
        }
        checkAltAttackHit1(*this, *other);
    }
}

void Player::checkAltAttackHit1(Player& attacker, Player& target, bool halfturret)
{
    // Spire's rocks, and Noxus's spinning blades once they are out; against
    // the target or its Halfturret (a 0.45 sphere).
    if (target.m_health == 0) {
        return;
    }
    DamageSource source;
    source.attacker = &attacker;
    const uint32_t flags = DamageFlags::NoSfx | DamageFlags::NoDmgInvuln | (halfturret ? DamageFlags::Halfturret : 0);
    const Vec3 targetCenter = halfturret ? target.m_halfturret.position : target.sphereCenter();
    if (attacker.m_hunter == Hunter::Spire && attacker.m_altAttack) {
        // CheckSphereOverlapVolume: a rock of radius 0.5 against the target's sphere.
        const float radii = (halfturret ? 0.45f : target.volumeRadius()) + 0.5f;
        const Vec3 toL = targetCenter - attacker.m_spireRockL;
        const Vec3 toR = targetCenter - attacker.m_spireRockR;
        if (dot(toL, toL) <= radii * radii || dot(toR, toR) <= radii * radii) {
            Vec3 dir{};
            if (!halfturret) {
                const float x = target.m_position[0] - attacker.m_position[0];
                const float z = target.m_position[2] - attacker.m_position[2];
                const float factor = std::sqrt(x * x + z * z) * 4;
                dir = factor > 0 ? Vec3{x / factor, 0, z / factor} : Vec3{};
            }
            target.takeDamage(attacker.v.AltAttackDamage, flags, &dir, source);
            attacker.m_soundSource.playSfx(SfxId::SPIRE_ALT_ATTACK_HIT);
        }
    } else if (attacker.m_hunter == Hunter::Noxus && attacker.m_altAttackTime >= attacker.v.AltAttackStartup * 2) {
        const Vec3 between = targetCenter - attacker.sphereCenter();
        const float radius = target.volumeRadius();
        if (between[1] > -radius && between[1] < radius) {
            const float hMagSqr = between[0] * between[0] + between[2] * between[2];
            const float reach = radius + 1.8f;
            if (hMagSqr < reach * reach) {
                const float factor = std::sqrt(hMagSqr) * 8;
                const Vec3 dir = factor > 0 ? Vec3{between[0] / factor, 0, between[2] / factor} : Vec3{};
                target.m_acceleration = dir;
                target.m_accelerationTimer = 8 * 2;
                target.takeDamage(attacker.v.AltAttackDamage, flags, &dir, source);
                attacker.m_soundSource.playSfx(SfxId::NOX_ALT_ATTACK_HIT);
                attacker.spawnEffect(235, {1, 0, 0}, {0, 1, 0}, target.m_position, false); // noxHit
                attacker.endAltAttack();
            }
        }
    }
}

void Player::checkAltAttackHit2(Player& attacker, Player& target, bool halfturret)
{
    // Trace's and Weavel's lunges, on contact.
    if ((attacker.m_hunter != Hunter::Trace && attacker.m_hunter != Hunter::Weavel) || !attacker.m_altAttack || target.m_health == 0) {
        return;
    }
    Vec3 dir = attacker.m_hunter == Hunter::Trace ? Vec3{attacker.m_speed[0], 0, attacker.m_speed[2]}
                                                   : Vec3{attacker.m_field70, 0, attacker.m_field74};
    if (!halfturret) {
        const float kbAccel = attacker.fx(attacker.v.AltAttackKnockbackAccel);
        target.m_acceleration = {dir[0] * kbAccel, -0.1f, dir[2] * kbAccel};
        target.m_accelerationTimer = attacker.v.AltAttackKnockbackTime * 2;
    }
    DamageSource source;
    source.attacker = &attacker;
    target.takeDamage(attacker.v.AltAttackDamage,
        DamageFlags::NoSfx | DamageFlags::NoDmgInvuln | (halfturret ? DamageFlags::Halfturret : 0), &dir, source);
    attacker.m_soundSource.playSfx(attacker.m_hunter == Hunter::Weavel ? SfxId::WEAVEL_ALT_ATTACK_HIT : SfxId::TRACE_ALT_ATTACK_HIT);
    attacker.endAltAttack();
}

void Player::updateSpireRocks()
{
    // UpdateSpireAltCollisionPose: where the L_Rock01 and R_Rock01 nodes of
    // the alt model are in the attack animation (AnimateNodes without node
    // transforms: an unanimated node sits at the origin, its parent dropped).
    const Model* model = m_models.altModel;
    if (model == nullptr || m_models.alt == nullptr) {
        return;
    }
    const auto& nodes = model->nodes();
    if (m_spireRockNodes[0] == -2) {
        m_spireRockNodes[0] = m_spireRockNodes[1] = -1;
        for (size_t i = 0; i < nodes.size(); i++) {
            if (nodes[i].name == "L_Rock01") {
                m_spireRockNodes[0] = static_cast<int>(i);
            } else if (nodes[i].name == "R_Rock01") {
                m_spireRockNodes[1] = static_cast<int>(i);
            }
        }
    }
    const AnimationSet& anims = *m_models.alt;
    const int index = m_altAnim.index;
    const NodeAnimationGroup* group
        = index >= 0 && index < static_cast<int>(anims.node.size()) && !anims.node[index].empty() ? &anims.node[index] : nullptr;
    auto world = [&](auto&& self, int i) -> Mat4 {
        if (group == nullptr || i < 0) {
            return Mat4::identity();
        }
        auto it = group->animations.find(nodes[i].name);
        if (it == group->animations.end()) {
            return Mat4::identity();
        }
        Mat4 m = animateNode(*group, it->second, 1.0f, m_altAnim.frame);
        if (nodes[i].parentIndex >= 0) {
            m *= self(self, nodes[i].parentIndex);
        }
        return m;
    };
    const Mat4 transform = Mat4::fromVectors(m_spireAltFacing, m_spireAltUp, {0, 0, 0});
    for (int k = 0; k < 2; k++) {
        if (m_spireRockNodes[k] >= 0) {
            const Mat4 m = world(world, m_spireRockNodes[k]) * transform;
            (k == 0 ? m_spireRockL : m_spireRockR) = m.translationPart() + m_position;
        }
    }
}

// ---- online (NetPlayerBridge) --------------------------------------------------

void Player::netPlace(const Vec3& position)
{
    m_position = position;
    m_prevPosition = position;
}

void Player::netSetFacing(const Vec3& facing)
{
    const float length = std::sqrt(dot(facing, facing));
    if (!(length > 0.01f)) {
        return;
    }
    const Vec3 f = facing * (1 / length);
    const float hMag = std::sqrt(f[0] * f[0] + f[2] * f[2]);
    setAim(std::atan2(-f[0], -f[2]) / kDegToRad, std::atan2(f[1], hMag) / kDegToRad);
}

void Player::netSetWeapon(int beam)
{
    if (beam == m_currentWeapon || beam < 0 || beam > 8) {
        return;
    }
    m_availableWeapons[beam] = true;
    if (beam != 0 && beam != 2 && beam != 8) {
        updateAffinityWeaponSlot(beam);
    }
    tryEquipWeapon(beam, true);
}

void Player::netSetZoom(bool zoomed)
{
    if (zoomed != m_equip.zoomed) {
        updateZoom(zoomed);
    }
}

void Player::netStartFormSwitch()
{
    // TrySwitchForms(force: true)
    if (m_health == 0 || m_morphing || m_unmorphing) {
        return;
    }
    if (!m_altForm) {
        enterAltForm();
    } else {
        exitAltForm();
    }
}

void Player::netForceForm(bool altForm)
{
    if (altForm == m_altForm) {
        // A stalled transition: the form is right, only its flag is left.
        if (m_morphing && !altForm) {
            exitAltForm();
        }
        if (m_unmorphing && !altForm) {
            m_unmorphing = false;
            setBipedAnimation(Anim::Idle, 0);
            if (m_cameraType != CameraType::First) {
                switchCamera(CameraType::First, facingVector());
            }
        } else if (m_morphing) {
            m_morphing = false;
        }
        return;
    }
    m_morphing = m_unmorphing = false;
    updateForm(altForm);
    if (altForm) {
        switchCamera(v.AltFormStrafe != 0 ? CameraType::Third2 : CameraType::Third1, {m_field70, 0, m_field74});
    } else {
        switchCamera(CameraType::First, facingVector());
    }
}

void Player::netSetShotState(int chargeLevel, int boostDamage, bool doubleDamage)
{
    m_equip.chargeLevel = std::clamp(chargeLevel, 0, 0xFFFF);
    m_boostDamage = std::clamp(boostDamage, 0, 0xFFFF);
    // Held up rather than counted down: the owner says so every frame for as
    // long as it lasts, and it is gone the moment they stop.
    if (doubleDamage) {
        m_doubleDmgTimer = std::max(m_doubleDmgTimer, 8);
    } else {
        m_doubleDmgTimer = 0;
    }
}

void Player::netSetAfflictions(bool frozen, bool disrupted, bool burning)
{
    // The state travels, not the cause: held while the authority says so.
    if (frozen) {
        if (m_frozenTimer < 2) {
            m_frozenTimer = 2;
        }
    } else if (m_frozenTimer > 0) {
        m_frozenTimer = 0;
    }
    if (disrupted) {
        if (m_disruptedTimer < 2) {
            m_disruptedTimer = 2;
        }
    } else if (m_disruptedTimer > 0) {
        m_disruptedTimer = 0;
    }
    if (burning) {
        if (m_burnTimer == 0) {
            m_burnTimer = 150 * 2;
            createBurnEffect();
        }
    } else if (m_burnTimer > 0) {
        m_burnTimer = 1; // out at the next tick
    }
}

} // namespace fp
