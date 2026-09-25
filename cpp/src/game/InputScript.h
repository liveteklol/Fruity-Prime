#pragma once

#include "World.h"

#include <string>
#include <vector>

namespace fp {

// Scripted play, one step per half second (30 ticks), separated by ';':
// w/s forward/back, a/d strafe, j jump, m morph, b boost, x alt attack (X: keep holding it),
// f fire (pressed on the step's first tick, held through it; a charge weapon
// fires when the next step lets go), z zoom, lN/rN turn N degrees, uN/nN look
// up/down, gN equip beam N, t track the nearest living opponent (aim at it every tick), T chase it (track and walk at it), '.' idle.
class InputScript {
public:
    InputScript() = default;
    explicit InputScript(const std::string& script);

    bool empty() const { return m_steps.empty(); }
    int ticks() const { return static_cast<int>(m_steps.size()) * 30; }
    // The input for `tick`, applying the step's one-off actions to the world.
    PlayerInput input(int tick, World& world) const;

private:
    std::vector<std::string> m_steps;
};

} // namespace fp
