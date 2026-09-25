// AreaVolumeEntity: the volumes that act on the players standing in them.

#include "World.h"

#include <QtGlobal>

namespace fp {

namespace {

// Message
constexpr uint32_t MessageDamage = 7, MessageGravity = 15, MessageDeath = 21, MessageUnused22 = 22;
// TriggerFlags
constexpr uint32_t TriggerBiped = 0x200, TriggerAlt = 0x400;

} // namespace

void World::sendAreaMessage(uint32_t message, int param1, Player& player)
{
    // PlayerEntity.HandleMessage. Messages for other entities (a volume's
    // parent or child) are not sent: no arena's volumes have any.
    if (message == MessageDamage) {
        player.takeDamage(param1, DamageFlags::IgnoreInvuln, nullptr, {});
    } else if (message == MessageDeath) {
        player.takeDamage(param1, DamageFlags::Death, nullptr, {});
    } else if (message == MessageGravity) {
        player.applyGravity(param1);
    }
}

bool World::prioritizeGravity(AreaVolume& volume, const Vec3& position, int slot)
{
    // PrioritizeGravity: where gravity volumes overlap, the highest priority one acts.
    bool result = true;
    for (const AreaVolume& other : m_areaVolumes) {
        if (other.insideMessage == volume.insideMessage && other.volume.testPoint(position)) {
            if (other.priority > volume.prioritySlots[slot]) {
                volume.prioritySlots[slot] = other.priority;
            }
            if (volume.priority != volume.prioritySlots[slot]) {
                result = false;
            }
        }
    }
    return result;
}

void World::areaVolumeInside(AreaVolume& volume, Player& player)
{
    // SendInsideEvent: once, unless an identical volume already sent it.
    const int slot = player.slot() & 15;
    volume.triggeredSlots[slot] = true;
    if (!volume.allowMultiple) {
        for (const AreaVolume& other : m_areaVolumes) {
            if (&other != &volume && other.parentId == volume.parentId && other.triggeredSlots[slot]
                && other.insideMessage == volume.insideMessage && other.insideParam1 == volume.insideParam1
                && other.insideParam2 == volume.insideParam2) {
                return;
            }
        }
    }
    if (volume.insideMessage != MessageUnused22) {
        sendAreaMessage(volume.insideMessage, volume.insideParam1, player);
    }
}

void World::areaVolumeTrigger(AreaVolume& volume, Player& player)
{
    // Trigger: on entering, then every cooldown while inside when it allows several.
    const int slot = player.slot() & 15;
    if (!volume.triggeredSlots[slot]) {
        areaVolumeInside(volume, player);
    } else if (volume.allowMultiple) {
        if (volume.cooldownSlots[slot] > 0) {
            volume.cooldownSlots[slot]--;
        } else {
            areaVolumeInside(volume, player);
            volume.cooldownSlots[slot] = volume.cooldownTime;
        }
    }
}

void World::areaVolumeExit(AreaVolume& volume, Player& player)
{
    // SendExitEvent
    const int slot = player.slot() & 15;
    if (!volume.triggeredSlots[slot]) {
        return;
    }
    volume.triggeredSlots[slot] = false;
    volume.prioritySlots[slot] = volume.priority;
    volume.cooldownSlots[slot] = volume.cooldownTime;
    if (volume.insideMessage == MessageGravity) {
        for (AreaVolume& other : m_areaVolumes) {
            other.prioritySlots[slot] = other.priority;
        }
    }
    for (const AreaVolume& other : m_areaVolumes) {
        if (&other != &volume && other.childId == volume.childId && other.triggeredSlots[slot]
            && other.exitMessage == volume.exitMessage && other.exitParam1 == volume.exitParam1
            && other.exitParam2 == volume.exitParam2) {
            return;
        }
    }
    if (volume.exitMessage == MessageDamage || volume.exitMessage == MessageDeath) {
        sendAreaMessage(volume.exitMessage, volume.exitParam1, player);
    }
}

void World::processAreaVolumes()
{
    static bool dumped = false;
    if (!dumped && qEnvironmentVariableIsSet("FP_DEBUG_AREAS")) {
        dumped = true;
        for (const AreaVolume& v : m_areaVolumes) {
            qInfo("area %d msg %u(%d) prio %u type %d pos (%.2f %.2f %.2f) r %.2f d1 %.2f d2 %.2f d3 %.2f v1 (%.2f %.2f %.2f)", v.id,
                v.insideMessage, v.insideParam1, v.priority, static_cast<int>(v.volume.type), v.volume.position[0], v.volume.position[1],
                v.volume.position[2], v.volume.radius, v.volume.d1, v.volume.d2, v.volume.d3, v.volume.v1[0], v.volume.v1[1], v.volume.v1[2]);
        }
    }
    // Process, for the players. (Volumes can also answer to beams; none in
    // the arenas do.)
    for (AreaVolume& volume : m_areaVolumes) {
        if (!volume.active) {
            continue;
        }
        for (PlayerSlot& slot : m_slots) {
            Player& player = *slot.player;
            if (!(player.isAltForm() ? (volume.triggerFlags & TriggerAlt) : (volume.triggerFlags & TriggerBiped))) {
                continue;
            }
            if (player.health() > 0 && volume.volume.testPoint(player.position())) {
                bool trigger = true;
                if (volume.insideMessage == MessageGravity) {
                    trigger = prioritizeGravity(volume, player.position(), player.slot() & 15);
                }
                if (trigger) {
                    areaVolumeTrigger(volume, player);
                }
            } else {
                areaVolumeExit(volume, player);
            }
        }
    }
}

} // namespace fp
