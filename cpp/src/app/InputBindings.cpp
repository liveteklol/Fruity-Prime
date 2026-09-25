#include "InputBindings.h"

#include "Settings.h"

#include <QKeySequence>

namespace fp {

namespace {

struct ActionInfo {
    const char* id;
    const char* title;
    Binding binding;
};

// PlayerControls.GetDefault, plus the keys the C# keeps apart (chat, the clip).
const ActionInfo actions[ActionCount] = {
    {"moveUp", "Move forward", {Binding::Key, Qt::Key_W}},
    {"moveDown", "Move back", {Binding::Key, Qt::Key_S}},
    {"moveLeft", "Strafe left", {Binding::Key, Qt::Key_A}},
    {"moveRight", "Strafe right", {Binding::Key, Qt::Key_D}},
    {"aimLeft", "Turn left", {Binding::Key, Qt::Key_Left}},
    {"aimRight", "Turn right", {Binding::Key, Qt::Key_Right}},
    {"aimUp", "Look up", {Binding::Key, Qt::Key_Up}},
    {"aimDown", "Look down", {Binding::Key, Qt::Key_Down}},
    {"shoot", "Fire", {Binding::Mouse, Qt::LeftButton}},
    {"zoom", "Zoom", {Binding::Mouse, Qt::RightButton}},
    {"jump", "Jump", {Binding::Key, Qt::Key_Space}},
    {"morph", "Morph", {Binding::Key, Qt::Key_C}},
    {"boost", "Boost (alt form)", {Binding::Key, Qt::Key_Space}},
    {"altAttack", "Alt attack", {Binding::Mouse, Qt::LeftButton}},
    {"nextWeapon", "Next weapon", {Binding::Wheel, 1}},
    {"prevWeapon", "Previous weapon", {Binding::Wheel, -1}},
    {"weaponMenu", "Weapon menu", {Binding::Mouse, Qt::MiddleButton}},
    {"powerBeam", "Power Beam", {Binding::Key, Qt::Key_1}},
    {"missile", "Missile", {Binding::Key, Qt::Key_2}},
    {"voltDriver", "Volt Driver", {Binding::Key, Qt::Key_3}},
    {"battlehammer", "Battlehammer", {Binding::Key, Qt::Key_4}},
    {"imperialist", "Imperialist", {Binding::Key, Qt::Key_5}},
    {"judicator", "Judicator", {Binding::Key, Qt::Key_6}},
    {"magmaul", "Magmaul", {Binding::Key, Qt::Key_7}},
    {"shockCoil", "Shock Coil", {Binding::Key, Qt::Key_8}},
    {"omegaCannon", "Omega Cannon", {Binding::Key, Qt::Key_9}},
    {"affinitySlot", "Affinity weapon", {}},
    {"scoreboard", "Scoreboard", {Binding::Key, Qt::Key_Tab}},
    {"chat", "Chat", {Binding::Key, Qt::Key_T}},
    {"saveClip", "Save clip", {Binding::Key, Qt::Key_F10}},
    {"recordDemo", "Record demo", {Binding::Key, Qt::Key_F9}},
    {"spectate", "Spectate / rejoin", {Binding::Key, Qt::Key_O}},
    {"voteYes", "Vote yes", {Binding::Key, Qt::Key_F1}},
    {"voteNo", "Vote no", {Binding::Key, Qt::Key_F2}},
};

} // namespace

QString Binding::toString() const
{
    switch (kind) {
    case Key: return QStringLiteral("key:%1").arg(code);
    case Mouse: return QStringLiteral("mouse:%1").arg(code);
    case Wheel: return QStringLiteral("wheel:%1").arg(code);
    default: return QStringLiteral("none");
    }
}

Binding Binding::parse(const QString& text)
{
    const QString kind = text.section(':', 0, 0);
    bool ok = false;
    const int code = text.section(':', 1, 1).toInt(&ok);
    if (!ok) {
        return {};
    }
    if (kind == QLatin1String("key")) {
        return {Key, code};
    }
    if (kind == QLatin1String("mouse")) {
        return {Mouse, code};
    }
    if (kind == QLatin1String("wheel")) {
        return {Wheel, code < 0 ? -1 : 1};
    }
    return {};
}

QString Binding::label() const
{
    switch (kind) {
    case Key: {
        const QString name = QKeySequence(code).toString(QKeySequence::NativeText);
        return name.isEmpty() ? QStringLiteral("Key %1").arg(code) : name;
    }
    case Mouse:
        switch (code) {
        case Qt::LeftButton: return QStringLiteral("Left click");
        case Qt::RightButton: return QStringLiteral("Right click");
        case Qt::MiddleButton: return QStringLiteral("Middle click");
        case Qt::BackButton: return QStringLiteral("Mouse 4");
        case Qt::ForwardButton: return QStringLiteral("Mouse 5");
        default: return QStringLiteral("Mouse %1").arg(code);
        }
    case Wheel: return code < 0 ? QStringLiteral("Wheel up") : QStringLiteral("Wheel down");
    default: return QStringLiteral("-");
    }
}

InputBindings::InputBindings()
{
    for (int i = 0; i < ActionCount; i++) {
        m_bindings[i] = actions[i].binding;
    }
}

const char* InputBindings::id(Action action) { return actions[static_cast<int>(action)].id; }
QString InputBindings::title(Action action) { return QString::fromLatin1(actions[static_cast<int>(action)].title); }
Binding InputBindings::defaultBinding(Action action) { return actions[static_cast<int>(action)].binding; }

void InputBindings::load(const Settings& settings)
{
    for (int i = 0; i < ActionCount; i++) {
        const QVariant value = settings.get(QStringLiteral("key.") + QLatin1String(actions[i].id));
        m_bindings[i] = value.isValid() && !value.toString().isEmpty() ? Binding::parse(value.toString()) : actions[i].binding;
    }
}

int InputBindings::weaponOf(Action action)
{
    // BeamType: Power Beam, Volt Driver, Missile, Battlehammer, Imperialist, Judicator, Magmaul, Shock Coil, Omega Cannon.
    switch (action) {
    case Action::PowerBeam: return 0;
    case Action::VoltDriver: return 1;
    case Action::Missile: return 2;
    case Action::Battlehammer: return 3;
    case Action::Imperialist: return 4;
    case Action::Judicator: return 5;
    case Action::Magmaul: return 6;
    case Action::ShockCoil: return 7;
    case Action::OmegaCannon: return 8;
    default: return -1;
    }
}

} // namespace fp
