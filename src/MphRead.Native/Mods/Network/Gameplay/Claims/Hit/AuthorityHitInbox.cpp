#include "Mods/Network/Gameplay/Claims/Hit/AuthorityHitInbox.hpp"

namespace MphRead::Mods::Network
{
    AuthorityHitInbox::Entry* AuthorityHitInbox::TryAllocate() noexcept
    {
        for (Entry& entry : _entries)
        {
            if (!entry.Live)
            {
                entry = Entry{};
                return &entry;
            }
        }
        return nullptr;
    }

    void AuthorityHitInbox::ForgetSlot(std::int32_t slot) noexcept
    {
        for (Entry& entry : _entries)
        {
            if (entry.Live && (entry.ShooterSlot == slot || entry.VictimSlot == slot))
            {
                entry.Live = false;
            }
        }
    }

    void AuthorityHitInbox::Clear() noexcept
    {
        _entries.fill(Entry{});
    }
}
