// Instagib: the rule survives the wire, an older build's flags do not turn it
// on, every player has a colour of their own, and a rail fades the way it is
// meant to. No window and no game files: the rendering is checked by eye.

#include "../Mods/Network/Domain/MatchDefinition.hpp"
#include "../Mods/Network/Protocol/Packets/SessionPackets.hpp"
#include "../Mods/Render/InstagibRails.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        return condition;
    }

    float Distance(const OpenTK::Mathematics::Vector3& a, const OpenTK::Mathematics::Vector3& b)
    {
        const float x = a.X - b.X;
        const float y = a.Y - b.Y;
        const float z = a.Z - b.Z;
        return std::sqrt(x * x + y * y + z * z);
    }
}

int main()
{
    using namespace MphRead::Mods::Network;
    using MphRead::Mods::Render::InstagibRails;
    bool ok = true;

    MatchDefinition match{};
    ok &= Check(!match.Instagib && (match.Rules() & SessionRules::Instagib) == SessionRules::None,
        "off by default, and off in the rule flags");
    match.Instagib = true;
    ok &= Check((match.Rules() & SessionRules::Instagib) == SessionRules::Instagib,
        "on in the rule flags when it is on");
    match.FriendlyFire = true;
    ok &= Check((match.Rules() & SessionRules::FriendlyFire) == SessionRules::FriendlyFire
        && (match.Rules() & SessionRules::Instagib) == SessionRules::Instagib,
        "it sits beside the other rules, not on top of one");

    MatchStatePacket state{};
    ok &= Check(!state.Instagib(), "a match state that does not say is not instagib");
    state.Flags = MatchStatePacket::FlagInstagib;
    ok &= Check(state.Instagib() && !state.FriendlyFire() && !state.AffinityWeapons(),
        "its bit is its own: the other rules do not move");
    state.Flags = static_cast<std::uint8_t>(MatchStatePacket::RuleFlags(1, true) | MatchStatePacket::FlagFriendlyFire);
    ok &= Check(!state.Instagib() && state.AffinityWeapons() && state.DamageLevel() == 1,
        "the damage level and the affinity rule never read as instagib");

    // Players are told apart by colour: no two of the first eight are close.
    float nearest = 1000.0F;
    for (int a = 0; a < InstagibRails::PaletteSize; ++a)
    {
        for (int b = a + 1; b < InstagibRails::PaletteSize; ++b)
        {
            nearest = std::fmin(nearest,
                Distance(InstagibRails::ColorForSlot(a), InstagibRails::ColorForSlot(b)));
        }
    }
    ok &= Check(nearest > 0.45F, "eight players, eight colours that are not alike");
    ok &= Check(Distance(InstagibRails::ColorForSlot(0), InstagibRails::ColorForSlot(InstagibRails::PaletteSize)) < 1.0e-6F,
        "a ninth slot wraps round rather than reading off the table");
    ok &= Check(Distance(InstagibRails::ColorForSlot(-1), InstagibRails::ColorForSlot(0)) < 1.0e-6F,
        "no slot at all is the first colour");

    const InstagibRails::Look fired = InstagibRails::LookAt(0.0F);
    const InstagibRails::Look middle = InstagibRails::LookAt(0.5F);
    const InstagibRails::Look gone = InstagibRails::LookAt(1.0F);
    ok &= Check(fired.Core > middle.Core && middle.Core > gone.Core && gone.Core < 0.001F,
        "the core fades from full to nothing");
    ok &= Check(fired.Glow > middle.Glow && gone.Glow < 0.001F && fired.Glow < fired.Core,
        "the glow fades too, and is never brighter than the core");
    ok &= Check(fired.Spiral > middle.Spiral && gone.Spiral < 0.1F, "the spiral fades");
    ok &= Check(gone.SpiralRadius > fired.SpiralRadius, "the spiral opens up as it goes");
    ok &= Check(InstagibRails::LookAt(-3.0F).Core <= 1.0001F && InstagibRails::LookAt(7.0F).Core < 0.001F,
        "an age outside the life is clamped");

    std::cout << (ok ? "ALL PASS" : "FAILED") << '\n';
    return ok ? 0 : 1;
}
