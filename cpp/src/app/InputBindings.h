#pragma once

#include <QString>

#include <array>

namespace fp {

class Settings;

// PlayerControls and InputSettings: what each control of the game is bound
// to -- a key, a mouse button or a wheel direction -- remembered as
// "key.<action>" in the settings, and rebindable from the Controls page.
enum class Action {
    MoveUp, MoveDown, MoveLeft, MoveRight, AimLeft, AimRight, AimUp, AimDown, Shoot, Zoom, Jump, Morph, Boost, AltAttack,
    NextWeapon, PrevWeapon, WeaponMenu, PowerBeam, Missile, VoltDriver, Battlehammer, Imperialist, Judicator, Magmaul, ShockCoil,
    OmegaCannon, AffinitySlot, Scoreboard, Chat, SaveClip, RecordDemo, Spectate, VoteYes, VoteNo, Count
};
constexpr int ActionCount = static_cast<int>(Action::Count);

struct Binding {
    enum Kind { None, Key, Mouse, Wheel } kind = None;
    int code = 0; // Qt::Key, Qt::MouseButton, or +1 (wheel down) / -1 (wheel up)
    bool operator==(const Binding&) const = default;
    QString toString() const;             // "key:87", "mouse:1", "wheel:-1", "none"
    static Binding parse(const QString& text);
    QString label() const;                // what the Controls page shows ("W", "Left click", "Wheel down")
};

class InputBindings {
public:
    InputBindings(); // the defaults
    static const char* id(Action action);    // "moveUp": the settings key after "key."
    static QString title(Action action);     // "Move forward"
    static Binding defaultBinding(Action action);
    void load(const Settings& settings);
    const Binding& operator[](Action action) const { return m_bindings[static_cast<int>(action)]; }
    // The beam a weapon action selects (BeamType), -1 for another action.
    static int weaponOf(Action action);

private:
    std::array<Binding, ActionCount> m_bindings{};
};

} // namespace fp
