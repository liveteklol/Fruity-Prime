#pragma once

// -classicframes DIR [SCRIPT]: the DS menus run one tick at a time with no
// window and no clock, both screens written as the DS shows them, so a run
// can be laid frame by frame against the game itself (the MPH recomp's
// debug server hands out the same 256 x 384 pictures).

#include <string>

namespace MphRead::Mods::ClassicMenu
{
    // SCRIPT is steps separated by ";": "tick:N" (N ticks, nothing written),
    // "cap:N" (N ticks, a picture after each, DIR/tNNNNN.png by tick),
    // "shot:NAME", "a", "b", "start", "up", "down", "left", "right",
    // "touch:X:Y" (DS pixels on the touch screen), "page:P" and "describe"
    // (DIR/tNNNNN.txt: every item's state and what it drew). The default
    // runs the title for ten seconds. 0 when every step ran.
    [[nodiscard]] int RunFrames(const std::string& directory, const std::string& script);
}
