#pragma once

#include "InputBindings.h"
#include "UiOverlay.h"
#include "game/CameraSequence.h"
#include "game/InputScript.h"
#include "game/MatchRoom.h"
#include "game/NetGame.h"
#include "game/World.h"
#include "audio/Mixer.h"
#include "render/SceneRenderer.h"

#include <QSet>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace fp {

class Hud;
class GamepadInput;

// The one window of the program (the C#'s RenderWindow): a room drawn in
// Vulkan with the screens (QML) over it. Behind the launcher it flies a
// room's intro on a loop; in a match the world ticks at 60 Hz and the player
// is driven from the keyboard, the mouse and the pad through the bindings.
class GameWindow : public VulkanWindow, public OverlaySource {
    Q_OBJECT
public:
    // `scene` may be null: an empty room until one is shown.
    explicit GameWindow(std::unique_ptr<Scene> scene);
    ~GameWindow() override;
    // The sound output; Sfx is loaded on it. The window keeps it until it closes.
    void setAudio(std::unique_ptr<Mixer> mixer) { m_mixer = std::move(mixer); }
    Mixer* mixer() const { return m_mixer.get(); }
    VulkanRenderer* createRenderer() override;
    void deviceAboutToBeDestroyed() override;
    SceneRenderer* renderer() const { return m_renderer; }

    // ---- the screens
    // The screens (QML) over the game; nullptr until the device exists.
    UiOverlay* ui() const { return m_ui.get(); }
    void setUiEnabled(bool enabled) { m_uiEnabled = enabled; }
    // Called once the screens are loaded, to hand them their objects.
    void setUiSetup(std::function<void(UiOverlay&)> setup) { m_uiSetup = std::move(setup); }

    // ---- what is shown
    // The launcher's ground: `scene` behind the screens, the camera flying
    // `flight` on a loop (or still, without one). Any match is over.
    void showBackdrop(std::unique_ptr<Scene> scene, std::optional<CameraSequence> flight);
    // A match: its room replaces whatever was shown. `net` for one on a server.
    void startMatch(MatchRoom room, std::unique_ptr<NetGame> net, std::unique_ptr<Hud> hud);
    bool inMatch() const { return m_world != nullptr; }
    World* world() const { return m_world.get(); }
    NetGame* net() const { return m_net.get(); }
    // Off: an offline match that ends raises matchOver instead of starting
    // another one in the same room (the launcher takes it from there).
    void setAutoRestart(bool restart) { m_autoRestart = restart; }
    // The pause menu is up: an offline match stops, an online one goes on without this player's input.
    void setPaused(bool paused);
    bool paused() const { return m_paused; }
    // The screens hold the pointer and the keys (a menu is open).
    void setUiCapturesInput(bool captures);

    // First-person walking against the room's collision; F toggles back to the free camera.
    // The world ticks at 60 Hz whenever it is set; walking moves its player.
    void setWorld(std::unique_ptr<World> world, bool walking);
    // The HUD over the walking player's view; H cycles stock, Pro and none.
    void setHud(std::unique_ptr<Hud> hud);
    Hud* hud() const { return m_hud.get(); }
    // A match on a server: the world ticks through it (NetGame).
    void setNet(std::unique_ptr<NetGame> net) { m_net = std::move(net); }
    // How a room the server moves to is loaded (MatchRoom), for the key and the mode.
    using RoomLoader = std::function<std::optional<MatchRoom>(const std::string& roomKey, GameMode mode)>;
    void setRoomLoader(RoomLoader loader) { m_roomLoader = std::move(loader); }
    // The game files, for the HUDs of the hunters a spectator watches.
    void setGameFiles(std::filesystem::path root) { m_root = std::move(root); }

    // ---- input
    void setBindings(const InputBindings& bindings) { m_bindings = bindings; }
    // Degrees per mouse count, and which axes are turned round.
    void setMouse(float sensitivity, bool invertX, bool invertY);
    void setGamepad(GamepadInput* pad) { m_pad = pad; }
    // The view's width in degrees (the DS's 78 by default); a zoom still narrows it.
    void setFov(float degrees) { m_fov = degrees; }
    // SpectatorMode: watch the others (hidden, not solid), or play again.
    void startSpectating();
    void stopSpectating();
    bool spectating() const { return m_spectating; }
    // Toggles borderless fullscreen (F11, Alt+Enter).
    void toggleFullscreen();

    // ---- tests and tools
    // Renders `frames` frames, saves a grab to `path`, then quits.
    void screenshotAfter(int frames, const QString& path);
    void setInitialCamera(const Camera& camera) { m_initialCamera = camera; }
    void setNoCull(bool noCull) { m_noCull = noCull; }
    // Scripted input instead of the keyboard, one simulation tick per frame
    // so what a screenshot shows does not depend on how fast frames come.
    void setScript(InputScript script) { m_script = std::move(script); }
    void setFixedStep(bool fixed) { m_fixedStep = fixed; }
    // Renders for warmup + seconds, prints frame-time and process stats, then quits.
    void benchmark(double warmupSeconds, double seconds);

    const HudDrawList& overlay(int width, int height) override;
    const std::vector<Image>& overlayTextures() const override;

signals:
    // Escape during a match: the pause menu.
    void menuRequested();
    // The match ended (results up), a new one started, the room changed.
    void matchChanged();
    // The server connection ended on its own (refused, dropped, closed).
    void sessionEnded(const QString& reason);
    // An offline match is over and nothing restarted it.
    void matchOver();
    // A second of play has gone by (for the screens' clocks).
    void secondElapsed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    void onFrame(int frame);
    void finish(int exitCode);
    void benchFrame(qint64 nowNs);
    void simulationTick();
    PlayerInput readInput();
    // The camera the scene is drawn from: the winner once the match is over,
    // else the room's intro while it plays, else the player's eyes.
    CameraPose viewPose(float& fovY) const;
    // An input event for the screens: true when they took it.
    bool uiTakes(QEvent* event);
    // The server moved to another room: the scene, the world and the renderer's resources are replaced.
    void changeRoom();
    // Bindings: a key, button or wheel step went down or up.
    void press(const Binding& input, bool autoRepeat);
    void release(const Binding& input);
    bool held(Action action) const;
    bool takePressed(Action action);
    void cycleWatched(int direction);
    Player* watchedPlayer() const;
    void captureMouse(bool capture);
    void dropHeldInput();

    std::unique_ptr<Scene> m_pendingScene;
    SceneRenderer* m_renderer = nullptr;
    std::optional<Camera> m_initialCamera;
    bool m_noCull = false;
    std::unique_ptr<World> m_world;
    std::unique_ptr<NetGame> m_net;
    RoomLoader m_roomLoader;
    std::unique_ptr<Mixer> m_mixer;
    std::unique_ptr<UiOverlay> m_ui;
    bool m_uiEnabled = false, m_uiCaptures = false;
    std::function<void(UiOverlay&)> m_uiSetup;
    std::filesystem::path m_root;

    // The launcher's ground
    std::optional<CameraSequence> m_flight;

    bool m_walking = false, m_paused = false, m_autoRestart = true;
    std::unique_ptr<Hud> m_hud;
    bool m_hudVisible = true;
    HudDrawList m_hudDraws;
    MatchState m_lastMatchState = MatchState::InProgress;

    // Input
    InputBindings m_bindings;
    GamepadInput* m_pad = nullptr;
    QSet<int> m_keys;                 // held keys (the free camera's and the bindings')
    Qt::MouseButtons m_buttons;       // held mouse buttons
    std::array<bool, ActionCount> m_pressed{}; // went down since the last tick
    bool m_inputSinceTick = false;
    float m_mouseSensitivity = 0.25f, m_fov = 78;
    bool m_invertX = false, m_invertY = false;
    bool m_mouseCaptured = false;
    QPointF m_lastMouse;
    bool m_dragging = false;
    bool m_chatting = false; // a line of chat is being typed
    QString m_chatText;
    InputScript m_script;
    int m_scriptTick = 0;
    bool m_fixedStep = false;
    double m_simAccumulator = 0;
    qint64 m_lastFrameNs = 0;
    int m_secondTicks = 0;

    // SpectatorMode: watching somebody (a world slot), or a free camera (-1).
    bool m_spectating = false;
    int m_watched = -1;
    std::map<int, std::unique_ptr<Hud>> m_spectatorHuds; // the watched hunters' HUDs

    // Screenshots and the benchmark
    int m_shotFrame = -1;
    QString m_shotPath;
    double m_benchWarmup = 0;
    double m_benchSeconds = 0;
    std::vector<double> m_frameTimesMs;
    qint64 m_benchStartNs = 0;
    qint64 m_lastBenchFrameNs = 0;
    double m_cpuAtBenchStart = 0;
};

} // namespace fp
