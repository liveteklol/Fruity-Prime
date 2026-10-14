#include "Mods/Network/Session/SessionAdmissionChannel.hpp"

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Session/ClientConnectionHealth.hpp"
#include "Mods/Network/Session/NetLifecycleTracker.hpp"
#include "Mods/Network/Session/SessionAuthorityState.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Session/NetSlotManager.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Console.hpp"

using ::MphRead::NativeRuntime::ConsoleWriteLine;

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    void SessionAdmissionChannel::HandleWelcome(std::span<const std::uint8_t> payload,
        std::uint32_t clientId, SessionRuntimeState& runtime, SessionMatchIdentity& matchIdentity,
        ClientConnectionHealth& connectionHealth, SessionAuthorityState& authority,
        StreamMatcher matchesStream)
    {
        if (payload.size() != 17 || payload[0] >= Entities::PlayerEntity::SlotCapacity
            || Runtime::ReadUInt32LittleEndian(payload.subspan(1)) != clientId)
        {
            return;
        }
        if (matchIdentity.HasMatch()
            && (matchesStream == nullptr || !matchesStream(
                Runtime::ReadUInt16LittleEndian(payload.subspan(5)),
                Runtime::ReadUInt64LittleEndian(payload.subspan(7)))))
        {
            return;
        }
        const std::uint16_t generation = Runtime::ReadUInt16LittleEndian(payload.subspan(15));
        const std::uint16_t currentGeneration = NetPlayerLifecycle::Generation(payload[0]);
        if (generation == 0 || (currentGeneration != 0 && generation != currentGeneration
            && !NetLifecycleTracker::Newer(generation, currentGeneration)))
        {
            return;
        }
        NetPlayerLifecycle::SetOccupant(payload[0], generation);
        if (connectionHealth.Admit())
        {
            if (authority.StandDown())
            {
                ConsoleWriteLine("[net] re-admitted; standing down as the "
                    "simulation authority until the server says otherwise");
                NetLog::Event("re-admitted, authority relinquished");
            }
        }
        if (payload.size() >= 1)
        {
            const std::int32_t assigned = payload[0];
            if (assigned >= Entities::PlayerEntity::SlotCapacity)
            {
                return;
            }
            if (runtime.LocalSlot() >= 0 && assigned != runtime.LocalSlot())
            {
                ConsoleWriteLine("[net] came back as slot " + Runtime::ToString(assigned)
                    + ", was slot " + Runtime::ToString(runtime.LocalSlot())
                    + "; releasing the old one");
                NetLog::Event("reconnected into slot " + Runtime::ToString(assigned)
                    + ", was " + Runtime::ToString(runtime.LocalSlot()));
                NetSlotManager::ReleaseSlot(runtime.LocalSlot());
            }
            runtime.SetLocalSlot(assigned);
            ConsoleWriteLine("[net] joined as slot " + Runtime::ToString(runtime.LocalSlot()));
            NetLog::Event("server assigned slot " + Runtime::ToString(runtime.LocalSlot()));
        }
    }

    void SessionAdmissionChannel::HandleAuthority(std::span<const std::uint8_t> payload,
        SessionRuntimeState& runtime, SessionAuthorityState& authority,
        StreamMatcher matchesStream)
    {
        // Older servers could grant a client authority. This protocol keeps
        // all lobby owners and participants as clients.
    }

    void SessionAdmissionChannel::HandleRefused(std::span<const std::uint8_t> payload,
        std::int32_t localSlot, ClientConnectionHealth& connectionHealth)
    {
        if (payload.size() >= 1 && (localSlot < 0 || payload[0] == RefusedPacket::ReasonKicked))
        {
            connectionHealth.Refuse(RefusedPacket::Read(payload));
        }
    }
}
