#include "InputScript.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fp {

InputScript::InputScript(const std::string& script)
{
    size_t start = 0;
    while (start <= script.size()) {
        size_t end = script.find(';', start);
        if (end == std::string::npos) {
            end = script.size();
        }
        if (end > start) {
            m_steps.push_back(script.substr(start, end - start));
        }
        start = end + 1;
    }
}

PlayerInput InputScript::input(int tick, World& world) const
{
    PlayerInput input;
    if (m_steps.empty()) {
        return input;
    }
    const std::string& step = m_steps[std::min<size_t>(tick / 30, m_steps.size() - 1)];
    const bool first = tick % 30 == 0 && tick / 30 < static_cast<int>(m_steps.size());
    auto has = [&](char c) { return step.find(c) != std::string::npos; };
    const bool turn = !step.empty() && (step[0] == 'l' || step[0] == 'r' || step[0] == 'u' || step[0] == 'n');
    if (!step.empty() && step[0] == 'g') {
        if (first) {
            world.player()->selectWeapon(std::atoi(step.c_str() + 1));
        }
        return input;
    }
    if (!turn) {
        input.forward = has('w');
        input.back = has('s');
        input.left = has('a');
        input.right = has('d');
        input.jumpPressed = has('j') && first;
        input.morphPressed = has('m') && first;
        input.boostHeld = has('b');
        input.altAttackPressed = has('x') && first;
        input.altAttackHeld = has('x') || has('X'); // X: still held from the step before
        input.shootHeld = has('f');
        input.shootPressed = has('f') && first;
        input.zoomPressed = has('z') && first;
        input.hasInput = step != ".";
        if (has('T')) {
            input.forward = true; // chase: walk at the opponent tracked below
        }
        if (has('t') || has('T')) {
            // Track the nearest living opponent: aim at its middle every tick.
            Player& player = *world.player();
            const CameraPose eye = player.camera();
            float best = 1e30f;
            for (size_t i = 1; i < world.playerCount(); i++) {
                const Player& other = *world.player(i);
                const Vec3 to = other.volumeCenter() - eye.position;
                if (other.health() > 0 && dot(to, to) < best) {
                    best = dot(to, to);
                    const float h = std::sqrt(to[0] * to[0] + to[2] * to[2]);
                    player.setAim(std::atan2(-to[0], -to[2]) * 57.29578f, std::atan2(to[1], h) * 57.29578f);
                }
            }
        }
    } else if (first) {
        Player& player = *world.player();
        const float degrees = std::strtof(step.c_str() + 1, nullptr);
        switch (step[0]) {
        case 'l': player.setAim(player.yaw() + degrees, player.pitch()); break;
        case 'r': player.setAim(player.yaw() - degrees, player.pitch()); break;
        case 'u': player.setAim(player.yaw(), player.pitch() + degrees); break;
        case 'n': player.setAim(player.yaw(), player.pitch() - degrees); break;
        }
    }
    return input;
}

} // namespace fp
