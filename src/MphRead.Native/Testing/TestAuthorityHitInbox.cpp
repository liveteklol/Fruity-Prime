#include "Mods/Network/Gameplay/Claims/Hit/AuthorityHitInbox.hpp"

#include <iostream>

namespace
{
    using MphRead::Mods::Network::AuthorityHitInbox;

    bool Check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    AuthorityHitInbox inbox;
    bool passed = true;

    AuthorityHitInbox::Entry* first = inbox.TryAllocate();
    passed &= Check(first != nullptr && !first->Live,
        "allocation returns a clean inactive queue entry");
    first->ShooterSlot = 2;
    first->VictimSlot = 5;
    first->Id = 37;
    first->Live = true;

    std::size_t allocated = 1;
    for (; allocated < AuthorityHitInbox::Capacity; ++allocated)
    {
        AuthorityHitInbox::Entry* entry = inbox.TryAllocate();
        if (entry == nullptr)
        {
            break;
        }
        entry->ShooterSlot = static_cast<std::uint8_t>(allocated % 8);
        entry->VictimSlot = static_cast<std::uint8_t>((allocated + 1) % 8);
        entry->Live = true;
    }
    passed &= Check(allocated == AuthorityHitInbox::Capacity && inbox.TryAllocate() == nullptr,
        "the fixed-capacity authority inbox refuses overflow without replacing live claims");
    passed &= Check(inbox.At(0).Live && inbox.At(0).Id == 37,
        "overflow leaves the oldest live claim intact");

    inbox.ForgetSlot(5);
    passed &= Check(!inbox.At(0).Live,
        "slot removal releases claims where the slot is the victim");
    passed &= Check(inbox.TryAllocate() == &inbox.At(0) && !inbox.At(0).Live && inbox.At(0).Id == 0,
        "reused storage is cleared before another claim is assembled");
    inbox.At(0).ShooterSlot = 2;
    inbox.At(0).Live = true;
    inbox.ForgetSlot(2);
    passed &= Check(!inbox.At(0).Live,
        "slot removal releases claims where the slot is the shooter");

    inbox.Clear();
    bool empty = true;
    for (const AuthorityHitInbox::Entry& entry : inbox.Entries())
    {
        empty &= !entry.Live;
    }
    passed &= Check(empty && inbox.TryAllocate() != nullptr,
        "clear releases all entries and makes the inbox reusable");

    if (!passed)
    {
        return 1;
    }
    std::cout << "AuthorityHitInbox PASS\n";
    return 0;
}
