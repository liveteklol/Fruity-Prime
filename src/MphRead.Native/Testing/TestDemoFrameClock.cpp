#include "Mods/Network/Replay/DemoFrameClock.hpp"

#include <iostream>
#include <limits>

namespace
{
    using MphRead::Mods::Network::DemoFrameClock;

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
    DemoFrameClock clock;
    bool passed = true;

    passed &= Check(clock.FrameAt(1'000'000) == 0,
        "an unstarted clock has no recording frame");
    clock.Start(1'000'000, 1'000'000);
    passed &= Check(clock.FrameAt(999'999) == 0 && clock.FrameAt(1'000'000) == 0,
        "timestamps before or at the origin map to frame zero");
    passed &= Check(clock.FrameAt(1'016'666) == 0 && clock.FrameAt(1'016'667) == 1,
        "frame boundaries use integer 60 Hz conversion");
    passed &= Check(clock.FrameAt(2'000'000) == 60 && clock.FrameAt(51'000'000) == 3000,
        "elapsed monotonic time maps to the corresponding demo frame");

    clock.Start(0, 0);
    passed &= Check(clock.FrameAt(500'000) == 0,
        "a zero timestamp frequency disables conversion safely");
    clock.Start(0, 1);
    passed &= Check(clock.FrameAt(std::numeric_limits<std::int64_t>::max())
        == std::numeric_limits<std::uint32_t>::max(),
        "very long recordings saturate instead of overflowing the frame counter");
    clock.Reset();
    passed &= Check(clock.FrameAt(2'000'000) == 0,
        "reset disables the old recording timeline");
    passed &= Check(!clock.HasAuthorityFrame(), "reset clears the authority anchor");

    clock.Start(100, 60);
    passed &= Check(!clock.HasAuthorityFrame(), "starting a recording clears the authority anchor");
    clock.AnchorAuthorityFrame(1000, 7);
    passed &= Check(clock.HasAuthorityFrame() && clock.FrameAtAuthority(1000, 0) == 7
        && clock.FrameAtAuthority(1001, 0) == 8 && clock.FrameAtAuthority(1050, 0) == 57,
        "authority ticks retain their exact intervals on the demo timeline");
    passed &= Check(clock.FrameAtAuthority(999, 20) == 20,
        "an older reordered snapshot uses its local arrival time safely");
    clock.AnchorAuthorityFrame(2000, 20);
    passed &= Check(clock.FrameAtAuthority(2000, 0) == 20,
        "a new stream can explicitly replace the timeline anchor");
    clock.AnchorAuthorityFrame(std::numeric_limits<std::uint32_t>::max(), 100);
    passed &= Check(clock.FrameAtAuthority(0, 0) == 101,
        "authority frame wrap preserves the next tick");
    clock.Reset();
    passed &= Check(!clock.HasAuthorityFrame() && clock.FrameAtAuthority(2001, 12) == 12,
        "reset clears both local and authority clocks");

    if (!passed)
    {
        return 1;
    }
    std::cout << "DemoFrameClock PASS\n";
    return 0;
}
