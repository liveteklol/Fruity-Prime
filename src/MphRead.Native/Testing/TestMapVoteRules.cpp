#include "../Mods/Network/Domain/MapVoteRules.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
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
    using namespace MphRead::Mods::Network::MapVoteRules;
    constexpr std::array<std::int32_t, 9> Expected{1, 1, 2, 3, 3, 4, 5, 5, 6};
    bool passed = true;
    for (std::int32_t eligible = 0; eligible < static_cast<std::int32_t>(Expected.size()); ++eligible)
    {
        passed &= Check(RequiredYesVotes(eligible) == Expected[static_cast<std::size_t>(eligible)],
            "70-percent threshold rounds upward");
    }
    passed &= Check(RequiredYesVotes(-1) == 1, "invalid electorate cannot pass by default");
    passed &= Check(Passed(6, RequiredYesVotes(8)), "threshold yes vote passes");
    passed &= Check(!Passed(5, RequiredYesVotes(8)), "one below threshold does not pass");
    passed &= Check(StillPossible(8, 2, RequiredYesVotes(8)), "remaining ballots can still reach threshold");
    passed &= Check(!StillPossible(8, 3, RequiredYesVotes(8)), "too many no votes make passage impossible");
    passed &= Check(!StillPossible(2, 3, 1), "invalid no count is not treated as possible");
    return passed ? 0 : 1;
}
