// PlayerSound.cs: the sounds a player makes, and the main player's own
// (the low-energy alarm, the power-ups' countdowns, the Octolith's hum).
#include "Player.h"
#include "Rng.h"
#include "formats/Metadata.h"

#include <cmath>

namespace fp {

namespace {

constexpr int TerrainLava = 8;

int tableValue(const auto& table, int row, int column)
{
    const Sfx& sfx = Sfx::instance();
    if (!sfx.loaded() || row < 0 || row >= static_cast<int>(table.size()) || column < 0 || column >= static_cast<int>(table[row].size())) {
        return -1;
    }
    return table[row][column];
}

// EntityBase.ExponentialDecay for one 60 Hz tick.
float exponentialDecay(float step, float value) { return value * std::pow(std::pow(step, 30.0f), 1 / 60.0f); }

constexpr SfxId dblDamageIds[3] = {SfxId::DBL_DAMAGE_A, SfxId::DBL_DAMAGE_B, SfxId::DBL_DAMAGE_C};
constexpr SfxId cloakIds[3] = {SfxId::CLOAK_A, SfxId::CLOAK_B, SfxId::CLOAK_C};

} // namespace

int hunterSfx(Hunter hunter, HunterSfx sfx)
{
    return tableValue(Sfx::instance().data().hunterSfx, static_cast<int>(hunter), static_cast<int>(sfx));
}

int beamSfx(int beam, BeamSfx sfx) { return tableValue(Sfx::instance().data().beamSfx, beam, static_cast<int>(sfx)); }

int terrainSfx(int terrain, TerrainSfx sfx) { return tableValue(Sfx::instance().data().terrainSfx, terrain, static_cast<int>(sfx)); }

void Player::playHunterSfx(HunterSfx sfx)
{
    int id = hunterSfx(m_hunter, sfx);
    if (id == -1) {
        if (m_hunter != Hunter::Guardian || sfx != HunterSfx::Spawn) {
            return;
        }
        id = hunterSfx(Hunter::Samus, sfx);
    }
    if (sfx == HunterSfx::Death && m_isMain) {
        Sfx::instance().playFreeSfx(id);
        return;
    }
    const float recency = sfx == HunterSfx::Damage ? 5 / 30.0f : -1;
    if (!m_isMain) {
        // Other players' hits and deaths sound as an enemy's.
        if (sfx == HunterSfx::Damage) {
            id = hunterSfx(m_hunter, HunterSfx::DamageEnemy);
        } else if (sfx == HunterSfx::Death) {
            id = hunterSfx(m_hunter, HunterSfx::DeathEnemy);
        }
    }
    m_soundSource.playSfx(id, false, false, recency, true);
}

int Player::playMissileSfx(HunterSfx sfx)
{
    const int id = hunterSfx(m_hunter, sfx);
    if (id == -1 || Sfx::instance().timedSfxMute > 0) {
        return -1;
    }
    return Sfx::instance().playFreeSfx(id);
}

void Player::playRandomDamageSfx()
{
    if (m_hunter == Hunter::Samus && m_damageSfxTimer == 0) {
        // 369-371: DAMAGE2 to DAMAGE4
        m_soundSource.playSfx(static_cast<int>(randomInt1(3)) + 369);
        m_damageSfxTimer = 90 / 30.0f;
    }
}

void Player::playBeamEmptySfx(int beam)
{
    const int id = beamSfx(beam, BeamSfx::Empty);
    if (id != -1) {
        m_soundSource.playSfx(id, false, true);
    }
}

void Player::playBeamShotSfx(int beam, bool charged, bool continuous, bool homing, float amountA)
{
    stopBeamChargeSfx(beam);
    if (continuous) {
        amountA = homing ? amountA + 0x3FFF : 0;
        m_soundSource.playSfx(beamSfx(beam, BeamSfx::Shot), true, false, -1, false, false, amountA);
        return;
    }
    BeamSfx sfx;
    if (charged) {
        sfx = beam == affinityWeapons().at(static_cast<int>(m_hunter)) ? BeamSfx::AffinityChargeShot : BeamSfx::ChargeShot;
    } else {
        sfx = m_hunter == Hunter::Weavel && beam == 3 ? BeamSfx::AffinityChargeShot : BeamSfx::Shot; // Battlehammer
    }
    const int id = beamSfx(beam, sfx);
    if (id != -1) {
        m_soundSource.playSfx(id);
    }
}

int Player::beamChargeSfx(int beam) const
{
    if (beam == 2) { // Missile
        return hunterSfx(m_hunter, HunterSfx::MissileCharge);
    }
    if (beam == 5 && m_hunter == Hunter::Noxus) { // Judicator
        return Sfx::instance().loaded() ? static_cast<int>(SfxId::SHOTGUN_CHARGE1_NOX) : -1;
    }
    return beamSfx(beam, BeamSfx::Charge);
}

void Player::playBeamChargeSfx(int beam)
{
    const int id = beamChargeSfx(beam);
    if (id != -1) {
        m_soundSource.playSfx(id, true);
    }
}

void Player::stopBeamChargeSfx(int beam)
{
    const int id = beamChargeSfx(beam);
    if (id != -1) {
        m_soundSource.stopSfx(id);
    }
}

void Player::stopContinuousBeamSfx(int beam)
{
    m_soundSource.stopSfx(beamSfx(beam, BeamSfx::Shot));
    m_soundSource.stopSfx(beamSfx(beam, BeamSfx::AffinityChargeShot));
}

void Player::updateHealthSfx(int health)
{
    Sfx& sfx = Sfx::instance();
    if (sfx.timedSfxMute > 0) {
        return;
    }
    if (health > 0 && health < 25) {
        if (!sfx.isHandlePlaying(m_healthSfxHandle)) {
            m_healthSfxHandle = sfx.playFreeSfx(SfxId::ENERGY_ALARM);
        }
    } else if (m_healthSfxHandle != -1) {
        sfx.stopSoundByHandle(m_healthSfxHandle);
        m_healthSfxHandle = -1;
    }
}

void Player::updateWalkingSfx()
{
    if (!m_movingBiped || m_hSpeedMag <= 0) {
        m_walkSfxTimer = 0; // the C#'s 10 / 30 is an integer division
        m_walkSfxIndex = 0;
        return;
    }
    m_walkSfxTimer += 1 / 60.0f;
    int id = -1;
    if (m_walkSfxTimer >= 15 / 30.0f && m_walkSfxIndex == 0) {
        id = terrainSfx(m_standTerrain, TerrainSfx::Walk1);
        m_walkSfxIndex = 1;
    }
    if (m_walkSfxTimer >= 25 / 30.0f) {
        id = terrainSfx(m_standTerrain, TerrainSfx::Walk2);
        m_walkSfxTimer = 5 / 30.0f;
        m_walkSfxIndex = 0;
    }
    if (m_standTerrain == TerrainLava && m_hunter != Hunter::Spire) {
        id = -1;
    }
    const float amountB = static_cast<float>(randomInt1(0x7FFF) * 2);
    if (id != -1) {
        m_soundSource.playSfx(id, false, false, -1, false, false, 0xFFFF, amountB);
    }
}

int Player::altMovementSfx() const
{
    if (m_hunter == Hunter::Samus) {
        return terrainSfx(m_standTerrain, TerrainSfx::Roll);
    }
    if (m_hunter == Hunter::Trace) {
        return terrainSfx(m_standTerrain, TerrainSfx::TraceAlt);
    }
    return hunterSfx(m_hunter, HunterSfx::Roll);
}

void Player::updateAltMovementSfx()
{
    updateMovementSfxAmount(0xFFFF * m_hSpeedMag / fx(v.AltMinHSpeed));
    const int id = altMovementSfx();
    if (id != -1) {
        m_soundSource.playSfx(id, true, false, -1, false, false, m_moveSfxAmount);
    }
}

void Player::updateSlidingSfx(float amount)
{
    updateMovementSfxAmount(amount);
    const int id = terrainSfx(m_standTerrain, TerrainSfx::Slide);
    if (id != -1) {
        m_soundSource.playSfx(id, true, false, -1, false, false, m_moveSfxAmount);
    }
}

void Player::updateMovementSfxAmount(float amount)
{
    const float previous = m_moveSfxAmount;
    if (!m_grounded) {
        amount = exponentialDecay(0.5f, previous);
    } else if (m_frameCount % 2 == 0) {
        amount = amount < previous ? previous + (amount - previous) / 4 : previous + (amount - previous) / 2;
    } else {
        amount = m_moveSfxAmount;
    }
    m_moveSfxAmount = amount < 1000 ? 0 : amount;
}

void Player::stopTerrainSfx(int previousTerrain)
{
    int current, previous;
    if (m_hunter == Hunter::Samus) {
        current = terrainSfx(m_standTerrain, TerrainSfx::Roll);
        previous = terrainSfx(previousTerrain, TerrainSfx::Roll);
    } else if (m_hunter == Hunter::Trace) {
        current = terrainSfx(m_standTerrain, TerrainSfx::TraceAlt);
        previous = terrainSfx(previousTerrain, TerrainSfx::TraceAlt);
    } else {
        current = previous = hunterSfx(m_hunter, HunterSfx::Roll);
    }
    if (current != previous && previous != -1) {
        m_soundSource.stopSfx(previous);
    }
    current = terrainSfx(m_standTerrain, TerrainSfx::Slide);
    previous = terrainSfx(previousTerrain, TerrainSfx::Slide);
    if (current != previous && previous != -1) {
        m_soundSource.stopSfx(previous);
    }
}

void Player::stopAltFormSfx()
{
    if (m_altForm) {
        m_soundSource.stopSfx(terrainSfx(m_standTerrain, TerrainSfx::Slide));
    } else {
        m_soundSource.stopSfx(SfxId::NOX_TOP_ATTACK2);
        m_soundSource.stopSfx(SfxId::NOX_TOP_ENERGY_DRAIN2);
        m_soundSource.stopSfx(altMovementSfx());
    }
}

void Player::playLandingSfx()
{
    const float amountA = 0xFFFF * m_timeBeforeLanding / (90.0f * 2);
    m_soundSource.playSfx(terrainSfx(m_standTerrain, TerrainSfx::Land), false, false, -1, false, false, amountA);
}

void Player::updateBurningSfx(bool burning)
{
    const float previous = m_burnSfxAmount;
    float amount = 0xFFFF;
    if (!burning) {
        amount = exponentialDecay(0.875f, previous);
        if (amount < 50) {
            amount = 0;
        }
    }
    if (amount > 0) {
        m_burnSfxAmount = amount;
        m_soundSource.playSfx(SfxId::DGN_LAVA_DAMAGE, true, false, -1, false, false, amount);
    } else if (previous > 0) {
        m_burnSfxAmount = 0;
        m_soundSource.stopSfx(SfxId::DGN_LAVA_DAMAGE);
    }
}

void Player::updateDoubleDamageSfx(int index, bool play)
{
    // The countdown's three stages play on their own in updateTimedSounds.
    Sfx& sfx = Sfx::instance();
    if (index != -1) {
        if (m_dblDamageSfxHandle != -1) {
            sfx.stopSoundByHandle(m_dblDamageSfxHandle);
            m_dblDamageSfxHandle = -1;
        }
        m_dblDamageSfxId = play ? static_cast<int>(dblDamageIds[index]) : -1;
    } else if (m_dblDamageSfxHandle != -1) {
        sfx.stopSoundByHandle(m_dblDamageSfxHandle);
    }
}

void Player::updateCloakSfx(int index, bool play)
{
    Sfx& sfx = Sfx::instance();
    if (index != -1) {
        if (m_cloakSfxHandle != -1) {
            sfx.stopSoundByHandle(m_cloakSfxHandle);
            m_cloakSfxHandle = -1;
        }
        m_cloakSfxId = play ? static_cast<int>(cloakIds[index]) : -1;
    } else if (m_cloakSfxHandle != -1) {
        sfx.stopSoundByHandle(m_cloakSfxHandle);
    }
}

void Player::stopFlagCarrySfx()
{
    Sfx::instance().stopSoundByHandle(m_flagCarrySfxHandle);
    m_flagCarrySfxHandle = -1;
    m_flagCarrySfxOn = false;
}

void Player::stopAllSfx()
{
    stopFlagCarrySfx();
    updateHealthSfx(0);
    updateDoubleDamageSfx(0, false);
    updateCloakSfx(0, false);
    Sfx& sfx = Sfx::instance();
    sfx.stopFreeSfxScripts();
    sfx.stopEnvironmentSfx();
    sfx.stopAllSound(false);
}

void Player::updateTimedSounds()
{
    Sfx& sfx = Sfx::instance();
    if (m_damageSfxTimer > 0) {
        m_damageSfxTimer = std::max(m_damageSfxTimer - 1 / 60.0f, 0.0f);
    }
    if (m_dblDamageSfxId != -1 && !sfx.isHandlePlaying(m_dblDamageSfxHandle) && sfx.timedSfxMute == 0) {
        m_dblDamageSfxHandle = sfx.playFreeSfx(m_dblDamageSfxId);
    }
    if (m_cloakSfxId != -1 && !sfx.isHandlePlaying(m_cloakSfxHandle) && sfx.timedSfxMute == 0) {
        m_cloakSfxHandle = sfx.playFreeSfx(m_cloakSfxId);
    }
    if (m_flagCarrySfxOn && !sfx.isHandlePlaying(m_flagCarrySfxHandle)) {
        m_flagCarrySfxHandle = sfx.playFreeSfx(SfxId::FLAG_CARRIED);
    }
}

} // namespace fp
