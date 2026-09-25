#pragma once

#include "game/Player.h"

#include <utility>

namespace fp {

class World;

// A game controller (the C#'s GamepadInput and GamepadDesktop): its sticks
// and buttons folded into the same PlayerInput the keyboard fills, and the
// right stick turning the view. XInput on Windows, the evdev joystick
// interface on Linux; nothing to install either way.
class GamepadInput {
public:
    GamepadInput();
    ~GamepadInput();
    GamepadInput(const GamepadInput&) = delete;
    GamepadInput& operator=(const GamepadInput&) = delete;

    struct Options {
        bool enabled = true;
        float sensitivity = 1.0f; // multiplies the look speed
        float deadzone = 0.15f;
        bool invertY = false;
        bool aimAssist = true;
    };
    void setOptions(const Options& options) { m_options = options; }
    const Options& options() const { return m_options; }

    // Reads the pad; once a frame, before the input is used.
    void poll();
    bool connected() const;
    std::string name() const;
    // The buttons and the left stick onto `input`.
    void apply(PlayerInput& input, const Player& player);
    // The right stick's turn this frame (degrees, x then y), aim assist included.
    std::pair<float, float> look(float dt, const Player& player, const World& world);

    // What a menu reads: the d-pad and face buttons as they went down this frame.
    enum Button { A, B, X, Y, LB, RB, LT, RT, Back, Start, LStick, RStick, Up, Down, Left, Right, ButtonCount };
    bool pressed(Button button) const;
    bool down(Button button) const;

private:
    struct State;
    State* m_state;
    Options m_options;
};

} // namespace fp
