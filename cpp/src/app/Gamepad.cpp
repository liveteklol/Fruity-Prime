#include "Gamepad.h"

#include "game/World.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace fp {

namespace {

// GamepadInput: 3.5 degrees a frame at full deflection, a walk at half.
constexpr float TurnPerSecond = 3.5f * 60, WalkThreshold = 0.5f, TriggerThreshold = 0.35f;

double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// GamepadAnalog.ApplyRadialDeadZone: round, and rescaled past its edge so the first movement is the smallest.
std::pair<float, float> radial(float x, float y, float inner)
{
    const float length = std::sqrt(x * x + y * y);
    if (length <= inner || length <= 0) {
        return {0, 0};
    }
    const float scaled = std::min(1.0f, (length - inner) / (1 - inner));
    return {x / length * scaled, y / length * scaled};
}

// The look curve: squared, keeping the sign.
float curve(float v) { return v * std::fabs(v); }

// ---- AimAssist (Mods/Input/AimAssist) ----------------------------------------------------

struct V2 {
    float x = 0, y = 0;
    float length() const { return std::sqrt(x * x + y * y); }
};
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(V2 a, float s) { return {a.x * s, a.y * s}; }
V2 lerp(V2 a, V2 b, float t) { return a + (b - a) * t; }
bool finite(V2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
float smooth(float a, float b, float value)
{
    const float t = std::clamp((value - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
float opposition(float input, float error) { return input * error < 0 ? 1 - smooth(0.02f, 0.8f, std::fabs(input)) : 1; }
float score(float angle, float cone, float distance, bool retained, float motion)
{
    return 0.60f * (1 - std::clamp(angle / cone, 0.0f, 1.0f)) + (retained ? 0.15f : 0) + 0.10f * (1 - std::clamp(distance / 60, 0.0f, 1.0f))
        + 0.10f + 0.05f * std::clamp(motion, 0.0f, 1.0f);
}

struct AssistProfile {
    float cone, releaseCone, inner, rotation, maxSpeed;
    bool head;
};
AssistProfile profileFor(int beam, bool scoped)
{
    // AimAssistWeaponProfile.For
    enum { Standard, Tracking, Precision, Projectile, Splash } weapon = Standard;
    switch (beam) {
    case Beam::ShockCoil: weapon = Tracking; break;
    case Beam::Imperialist: weapon = Precision; break;
    case Beam::Missile:
    case Beam::Magmaul:
    case Beam::OmegaCannon: weapon = Splash; break;
    case Beam::Judicator:
    case Beam::Battlehammer: weapon = Projectile; break;
    default: break;
    }
    if (scoped) {
        return {3.5f, 4.75f, 1.25f, 0.5f, 8, weapon == Standard || weapon == Precision};
    }
    switch (weapon) {
    case Tracking: return {7, 9, 2.4f, 1, 24, false};
    case Precision: return {5, 7, 1.8f, 0.6f, 12, true};
    case Splash: return {7, 9, 2.4f, 0.45f, 12, false};
    case Projectile: return {7, 9, 2.4f, 0.6f, 16, false};
    default: return {7, 9, 2.4f, 0.8f, 20, true};
    }
}

struct AssistTarget {
    int slot;
    V2 body, head;
    float distance;
    bool headVisible;
};

struct AssistState {
    int targetSlot = -1;
    float retained = 0, headBlend = 0;
    V2 previousError, previousOutput, velocity;
    void reset() { *this = {}; }
};

// AimAssist.Apply: friction against a stick that would pull the aim off a
// target, and a gentle rotation onto it while the player is aiming or moving.
V2 applyAssist(AssistState& state, const std::vector<AssistTarget>& targets, V2 raw, float stickIntent, float moveIntent, float dt,
    const AssistProfile& profile)
{
    const float intent = stickIntent > 0.08f ? 1 : moveIntent > 0.20f ? 0.5f : 0;
    if (intent == 0 || dt <= 0 || dt > 0.1f) {
        state.reset();
        return raw;
    }
    int best = -1, retained = -1;
    float bestScore = -1, retainedScore = -1;
    for (size_t i = 0; i < targets.size(); i++) {
        const AssistTarget& t = targets[i];
        const bool keep = t.slot == state.targetSlot;
        const float rangeScale = 1 - 0.4f * smooth(25, 60, t.distance);
        const float cone = (keep ? profile.releaseCone : profile.cone) * rangeScale;
        const float angle = t.body.length();
        if (!finite(t.body) || t.distance < 0.2f || t.distance > 60 || angle > cone) {
            continue;
        }
        const float s = score(angle, cone, t.distance, keep, keep ? std::min(state.velocity.length() / 45, 1.0f) : 0);
        if (keep) {
            retained = static_cast<int>(i);
            retainedScore = s;
        }
        if (s > bestScore) {
            best = static_cast<int>(i);
            bestScore = s;
        }
    }
    if (best < 0) {
        state.reset();
        return raw;
    }
    const bool deliberate = raw.length() / dt > 90;
    if (retained >= 0 && best != retained && bestScore < retainedScore * (deliberate ? 1 : 1.30f)) {
        best = retained;
    }
    const AssistTarget& target = targets[best];
    const bool same = state.targetSlot == target.slot;
    if (!same) {
        state.reset();
    }
    state.targetSlot = target.slot;
    state.retained += dt;
    V2 velocity = same ? (target.body - state.previousError + state.previousOutput) * (1 / dt) : V2{};
    velocity = {std::clamp(velocity.x, -120.0f, 120.0f), std::clamp(velocity.y, -120.0f, 120.0f)};
    state.velocity = lerp(state.velocity, velocity, 1 - std::exp(-12 * dt));
    const float headAngle = target.head.length();
    const bool head = profile.head && same && state.retained >= 0.120f && target.headVisible && finite(target.head) && target.distance > 5
        && (headAngle < target.body.length() * 0.8f || raw.y > 0.02f) && headAngle < 1.5f && raw.y >= -0.02f
        && opposition(raw.x, target.head.x) > 0.5f;
    const float desiredHead = head ? std::min(0.8f, 0.1f + 0.7f * smooth(1.5f, 0, headAngle)) : 0;
    state.headBlend = head ? state.headBlend + (desiredHead - state.headBlend) * (1 - std::exp(-8 * dt)) : 0;
    V2 error = lerp(target.body, target.head, state.headBlend);
    if (!finite(error)) {
        error = target.body;
    }
    const float distanceStrength = (0.55f + 0.45f * smooth(0, 5, target.distance)) * (1 - 0.5f * smooth(25, 60, target.distance));
    const float bubble = 1 - smooth(profile.inner, profile.releaseCone, target.body.length());
    const float opposeX = opposition(raw.x / (dt * 60), error.x);
    const float opposeY = opposition(raw.y / (dt * 60), error.y);
    const float friction = 1 - 0.38f * bubble * distanceStrength;
    const V2 adjusted{raw.x * (1 - (1 - friction) * opposeX), raw.y * (1 - (1 - friction) * opposeY)};
    const float strength = intent * distanceStrength * bubble * profile.rotation;
    V2 rotation{(error.x * 4 + state.velocity.x) * 0.24f * opposeX, (error.y * 4 + state.velocity.y) * 0.15f * opposeY};
    rotation = V2{std::clamp(rotation.x, -profile.maxSpeed, profile.maxSpeed), std::clamp(rotation.y, -profile.maxSpeed, profile.maxSpeed)}
        * (strength * dt);
    rotation.x = std::clamp(rotation.x, -std::fabs(error.x), std::fabs(error.x));
    rotation.y = std::clamp(rotation.y, -std::fabs(error.y), std::fabs(error.y));
    const V2 output = adjusted + rotation;
    state.previousError = target.body;
    state.previousOutput = output;
    return output;
}

// Yaw (0 looking down -Z, positive turning left) and pitch (positive up) of a direction, in degrees.
float yawOf(const Vec3& v) { return std::atan2(-v[0], -v[2]) * 57.29578f; }
float pitchOf(const Vec3& v) { return std::atan2(v[1], std::sqrt(v[0] * v[0] + v[2] * v[2])) * 57.29578f; }

} // namespace

// ---- the device --------------------------------------------------------------------------

struct GamepadInput::State {
    // Normalised: sticks -1..1 (y up positive for the left stick's "forward"), triggers 0..1.
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    std::array<bool, ButtonCount> down{}, previous{};
    bool connected = false;
    std::string name;
    AssistState assist;
    double nextScan = 0;
#ifdef _WIN32
    HMODULE library = nullptr;
    using GetState = DWORD(WINAPI*)(DWORD, void*);
    GetState getState = nullptr;
    int index = -1;
#else
    int fd = -1;
    struct Axis {
        int min = -32768, max = 32767;
    };
    std::array<Axis, ABS_CNT> axes{};
    std::array<float, ABS_CNT> abs{};
#endif
};

GamepadInput::GamepadInput()
    : m_state(new State)
{
#ifdef _WIN32
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        m_state->library = LoadLibraryW(dll);
        if (m_state->library) {
            m_state->getState = reinterpret_cast<State::GetState>(GetProcAddress(m_state->library, "XInputGetState"));
            break;
        }
    }
#endif
}

GamepadInput::~GamepadInput()
{
#ifdef _WIN32
    if (m_state->library) {
        FreeLibrary(m_state->library);
    }
#else
    if (m_state->fd >= 0) {
        ::close(m_state->fd);
    }
#endif
    delete m_state;
}

bool GamepadInput::connected() const { return m_state->connected; }
std::string GamepadInput::name() const { return m_state->name; }
bool GamepadInput::down(Button button) const { return m_state->down[button]; }
bool GamepadInput::pressed(Button button) const { return m_state->down[button] && !m_state->previous[button]; }

void GamepadInput::poll()
{
    State& s = *m_state;
    s.previous = s.down;
    if (!m_options.enabled) {
        s.down = {};
        s.lx = s.ly = s.rx = s.ry = s.lt = s.rt = 0;
        return;
    }
    const double now = nowSeconds();
#ifdef _WIN32
    struct XInputGamepad {
        WORD buttons;
        BYTE leftTrigger, rightTrigger;
        SHORT thumbLX, thumbLY, thumbRX, thumbRY;
    };
    struct XInputState {
        DWORD packet;
        XInputGamepad pad;
    };
    if (s.getState == nullptr) {
        return;
    }
    // One pad: the first that answers, looked for again every two seconds while there is none.
    if (s.index < 0 && now >= s.nextScan) {
        s.nextScan = now + 2;
        for (DWORD i = 0; i < 4; i++) {
            XInputState st{};
            if (s.getState(i, &st) == ERROR_SUCCESS) {
                s.index = static_cast<int>(i);
                s.name = "XInput pad " + std::to_string(i + 1);
                break;
            }
        }
    }
    XInputState st{};
    if (s.index < 0 || s.getState(static_cast<DWORD>(s.index), &st) != ERROR_SUCCESS) {
        s.index = -1;
        s.connected = false;
        s.down = {};
        return;
    }
    s.connected = true;
    auto axis = [](SHORT v) { return std::clamp(v / 32767.0f, -1.0f, 1.0f); };
    s.lx = axis(st.pad.thumbLX);
    s.ly = axis(st.pad.thumbLY);
    s.rx = axis(st.pad.thumbRX);
    s.ry = axis(st.pad.thumbRY);
    s.lt = st.pad.leftTrigger / 255.0f;
    s.rt = st.pad.rightTrigger / 255.0f;
    const WORD b = st.pad.buttons;
    s.down[Up] = b & 0x0001;
    s.down[Down] = b & 0x0002;
    s.down[Left] = b & 0x0004;
    s.down[Right] = b & 0x0008;
    s.down[Start] = b & 0x0010;
    s.down[Back] = b & 0x0020;
    s.down[LStick] = b & 0x0040;
    s.down[RStick] = b & 0x0080;
    s.down[LB] = b & 0x0100;
    s.down[RB] = b & 0x0200;
    s.down[A] = b & 0x1000;
    s.down[B] = b & 0x2000;
    s.down[X] = b & 0x4000;
    s.down[Y] = b & 0x8000;
#else
    // The first event device that looks like a gamepad: two sticks' worth of axes and a south button.
    if (s.fd < 0 && now >= s.nextScan) {
        s.nextScan = now + 2;
        if (DIR* dir = opendir("/dev/input")) {
            while (dirent* entry = readdir(dir)) {
                if (std::string(entry->d_name).rfind("event", 0) != 0) {
                    continue;
                }
                const std::string path = std::string("/dev/input/") + entry->d_name;
                const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
                if (fd < 0) {
                    continue;
                }
                unsigned long keys[(KEY_CNT + 63) / 64] = {};
                unsigned long absBits[(ABS_CNT + 63) / 64] = {};
                ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
                ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absBits), absBits);
                auto has = [](const unsigned long* bits, int bit) { return (bits[bit / 64] >> (bit % 64)) & 1; };
                if (has(keys, BTN_SOUTH) && has(absBits, ABS_X) && has(absBits, ABS_Y)) {
                    char name[128] = {};
                    ioctl(fd, EVIOCGNAME(sizeof name), name);
                    s.name = name;
                    for (int a = 0; a < ABS_CNT; a++) {
                        input_absinfo info{};
                        if (has(absBits, a) && ioctl(fd, EVIOCGABS(a), &info) == 0 && info.maximum > info.minimum) {
                            s.axes[a] = {info.minimum, info.maximum};
                        }
                    }
                    s.fd = fd;
                    break;
                }
                ::close(fd);
            }
            closedir(dir);
        }
    }
    if (s.fd < 0) {
        s.connected = false;
        return;
    }
    input_event ev{};
    while (true) {
        const ssize_t got = ::read(s.fd, &ev, sizeof ev);
        if (got != sizeof ev) {
            if (got < 0 && errno != EAGAIN) {
                ::close(s.fd); // unplugged
                s.fd = -1;
                s.connected = false;
                s.down = {};
                return;
            }
            break;
        }
        if (ev.type == EV_ABS && ev.code < ABS_CNT) {
            const State::Axis& axis = s.axes[ev.code];
            const bool trigger = ev.code == ABS_Z || ev.code == ABS_RZ || ev.code == ABS_BRAKE || ev.code == ABS_GAS;
            const float t = static_cast<float>(ev.value - axis.min) / static_cast<float>(axis.max - axis.min);
            s.abs[ev.code] = trigger ? std::clamp(t, 0.0f, 1.0f) : std::clamp(t * 2 - 1, -1.0f, 1.0f);
            if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) {
                s.abs[ev.code] = static_cast<float>(ev.value);
            }
        } else if (ev.type == EV_KEY) {
            const bool on = ev.value != 0;
            switch (ev.code) {
            case BTN_SOUTH: s.down[A] = on; break;
            case BTN_EAST: s.down[B] = on; break;
            case BTN_NORTH: s.down[Y] = on; break; // the top face button, by the kernel's gamepad spec
            case BTN_WEST: s.down[X] = on; break;
            case BTN_TL: s.down[LB] = on; break;
            case BTN_TR: s.down[RB] = on; break;
            case BTN_TL2: s.abs[ABS_Z] = on ? 1.0f : 0.0f; break;
            case BTN_TR2: s.abs[ABS_RZ] = on ? 1.0f : 0.0f; break;
            case BTN_SELECT: s.down[Back] = on; break;
            case BTN_START: s.down[Start] = on; break;
            case BTN_THUMBL: s.down[LStick] = on; break;
            case BTN_THUMBR: s.down[RStick] = on; break;
            case BTN_DPAD_UP: s.down[Up] = on; break;
            case BTN_DPAD_DOWN: s.down[Down] = on; break;
            case BTN_DPAD_LEFT: s.down[Left] = on; break;
            case BTN_DPAD_RIGHT: s.down[Right] = on; break;
            default: break;
            }
        }
    }
    s.connected = true;
    s.lx = s.abs[ABS_X];
    s.ly = -s.abs[ABS_Y]; // evdev's y grows downward
    s.rx = s.abs[ABS_RX];
    s.ry = -s.abs[ABS_RY];
    s.lt = s.abs[ABS_Z];
    s.rt = s.abs[ABS_RZ];
    if (s.abs[ABS_HAT0X] != 0 || s.abs[ABS_HAT0Y] != 0 || s.previous[Left] || s.previous[Right] || s.previous[Up] || s.previous[Down]) {
        s.down[Left] = s.abs[ABS_HAT0X] < 0;
        s.down[Right] = s.abs[ABS_HAT0X] > 0;
        s.down[Up] = s.abs[ABS_HAT0Y] < 0;
        s.down[Down] = s.abs[ABS_HAT0Y] > 0;
    }
#endif
    s.down[LT] = s.lt > TriggerThreshold;
    s.down[RT] = s.rt > TriggerThreshold;
}

void GamepadInput::apply(PlayerInput& input, const Player& player)
{
    // GamepadInput.Apply: the pad's contribution ors onto what the keyboard set.
    const State& s = *m_state;
    if (!s.connected || !m_options.enabled) {
        return;
    }
    const auto [mx, my] = radial(s.lx, s.ly, m_options.deadzone);
    input.forward |= my > WalkThreshold;
    input.back |= my < -WalkThreshold;
    input.left |= mx < -WalkThreshold;
    input.right |= mx > WalkThreshold;
    input.jumpPressed |= pressed(A);
    input.boostHeld |= down(A);
    input.morphPressed |= pressed(B);
    input.shootHeld |= down(RT);
    input.shootPressed |= pressed(RT);
    input.altAttackHeld |= down(RT);
    input.altAttackPressed |= pressed(RT);
    input.zoomPressed |= pressed(LT);
    bool any = std::fabs(mx) > 0 || std::fabs(my) > 0;
    for (int b = 0; b < ButtonCount; b++) {
        any = any || s.down[b];
    }
    const auto [ax, ay] = radial(s.rx, s.ry, m_options.deadzone);
    input.hasInput |= any || std::fabs(ax) > 0 || std::fabs(ay) > 0;
    // The bumpers and the d-pad reach every weapon (there is no wheel on a pad).
    Player& self = const_cast<Player&>(player);
    if (pressed(RB) || pressed(Right)) {
        self.cycleWeapon(1);
    }
    if (pressed(LB) || pressed(Left)) {
        self.cycleWeapon(-1);
    }
    if (pressed(Up)) {
        self.selectWeapon(Beam::Missile);
    }
    if (pressed(Down)) {
        self.selectWeapon(Beam::PowerBeam);
    }
}

std::pair<float, float> GamepadInput::look(float dt, const Player& player, const World& world)
{
    // The right stick: degrees to turn this frame, x to the right and y down
    // (as the mouse's), bent by the aim assist toward a target in reach.
    State& s = *m_state;
    if (!s.connected || !m_options.enabled || dt <= 0) {
        s.assist.reset();
        return {0, 0};
    }
    const auto [ax, ay] = radial(s.rx, s.ry, m_options.deadzone);
    const float speed = TurnPerSecond * m_options.sensitivity * dt;
    // In yaw/pitch terms: positive x turns left, positive y looks up.
    V2 raw{-curve(ax) * speed, curve(ay) * speed * (m_options.invertY ? -1 : 1)};
    const bool eligible = m_options.aimAssist && player.inPlay() && !player.isAltForm();
    if (!eligible) {
        s.assist.reset();
        return {-raw.x, -raw.y};
    }
    // The targets: every opponent in play, visible, and inside the release cone.
    const AssistProfile profile = profileFor(player.currentWeapon(), player.zoomed());
    const CameraPose cam = player.camera();
    const Vec3 aim = player.aimVector();
    std::vector<AssistTarget> targets;
    for (size_t i = 0; i < world.playerCount(); i++) {
        const Player& target = *const_cast<World&>(world).player(i);
        if (&target == &player || !target.inPlay() || !world.slotActive(i) || target.alpha() < 0.95f
            || areAllies(player.teamIndex(), target.teamIndex())) {
            continue;
        }
        const float height = target.values().MaxPickupHeight / 4096.0f;
        const Vec3 center = target.volumeCenter();
        const Vec3 top = target.position() + Vec3{0, height - 0.3f, 0};
        const Vec3 chest = target.isAltForm() ? center : center + (top - center) * 0.65f;
        const Vec3 headPoint = target.position() + Vec3{0, height - 0.15f, 0};
        auto error = [&](const Vec3& point) {
            const Vec3 d = point - cam.position;
            float yaw = yawOf(d) - yawOf(aim);
            yaw = std::remainder(yaw, 360.0f);
            return V2{yaw, pitchOf(d) - pitchOf(aim)};
        };
        const Vec3 toChest = chest - cam.position;
        const float distance = std::sqrt(dot(toChest, toChest));
        const V2 body = error(chest);
        if (!finite(body) || distance > 60 || body.length() > profile.releaseCone) {
            continue;
        }
        CollisionResult hit;
        if (world.collision() != nullptr && world.collision()->checkBetweenPoints(cam.position, chest, TestFlags::Beams, hit)) {
            continue;
        }
        const V2 head = error(headPoint);
        bool headVisible = !target.isAltForm() && profile.head && head.length() < 1.5f;
        if (headVisible && world.collision() != nullptr && world.collision()->checkBetweenPoints(cam.position, headPoint, TestFlags::Beams, hit)) {
            headVisible = false;
        }
        targets.push_back({static_cast<int>(i), body, head, distance, headVisible});
    }
    const auto [mx, my] = radial(s.lx, s.ly, m_options.deadzone);
    const V2 out = applyAssist(s.assist, targets, raw, std::sqrt(ax * ax + ay * ay), std::sqrt(mx * mx + my * my), dt, profile);
    return {-out.x, -out.y};
}

} // namespace fp
