#include "Mods/Network/Session/NetRole.hpp"

#include "NativeRuntime/System/Enum.hpp"

#include <iterator>

namespace MphRead::Mods::Network
{
    namespace
    {
        // NetSession.cs NetRole : int
        constexpr ::MphRead::NativeRuntime::EnumNameEntry NetRoleNames[] = {
            {0x0ULL, "Offline"},
            {0x1ULL, "Host"},
            {0x2ULL, "Client"},
            {0x3ULL, "Server"},
        };
    }

    std::string ToString(NetRole value)
    {
        return ::MphRead::NativeRuntime::ManagedEnumToString(
            value, NetRoleNames, std::size(NetRoleNames), false);
    }
}
