#include "GameWindow.h"

#include "Gamepad.h"
#include "audio/Music.h"
#include "audio/Sfx.h"
#include "game/Hud.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QStandardPaths>
#include <QWheelEvent>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace fp {

namespace {

QString roomTitle(const Scene* scene)
{
    if (scene == nullptr || scene->room == nullptr) {
        return QStringLiteral("Fruity Prime");
    }
    const RoomMetadata& room = *scene->room;
    return QStringLiteral("Fruity Prime — %1").arg(QString::fromUtf8(room.inGameName ? room.inGameName : room.name));
}

} // namespace

GameWindow::GameWindow(std::unique_ptr<Scene> scene)
    : m_pendingScene(scene ? std::move(scene) : std::make_unique<Scene>())
{
    setTitle(roomTitle(m_pendingScene.get()));
}

GameWindow::~GameWindow()
{
    captureMouse(false);
    Music::instance().unload(); // before the mixer they play through
    Sfx::instance().unload();
}

// ---- the screens -------------------------------------------------------------------

void GameWindow::deviceAboutToBeDestroyed()
{
    if (m_ui) {
        m_ui->shutdown();
        m_ui.reset();
    }
}

VulkanRenderer* GameWindow::createRenderer()
{
    if (m_uiEnabled && !m_ui) {
        m_ui = std::make_unique<UiOverlay>(this);
        if (m_ui->initialize()) {
            if (m_uiSetup) {
                m_uiSetup(*m_ui);
            }
            if (!m_ui->loadFromModule(QStringLiteral("FruityPrimeUi"), QStringLiteral("Main"))) {
                m_ui.reset();
            }
        } else {
            m_ui.reset();
        }
    }
    m_renderer = new SceneRenderer(this, std::move(m_pendingScene));
    m_renderer->setUiLayer(m_ui.get());
    m_renderer->setFrameCallback([this](int frame) { onFrame(frame); });
    m_renderer->setInitialCamera(m_initialCamera);
    m_renderer->setNoCull(m_noCull);
    m_renderer->setOverlay(this);
    return m_renderer;
}

bool GameWindow::uiTakes(QEvent* event)
{
    if (!m_ui || !m_ui->root() || !m_ui->root()->isVisible() || !m_uiCaptures) {
        return false;
    }
    event->setAccepted(false);
    m_ui->sendEvent(event);
    return true;
}

void GameWindow::setUiCapturesInput(bool captures)
{
    if (m_uiCaptures == captures) {
        return;
    }
    m_uiCaptures = captures;
    dropHeldInput();
    // The pointer is the screens' while they hold it, the game's otherwise.
    captureMouse(!captures && inMatch() && m_walking && !m_chatting);
}

// ---- what is shown --------------------------------------------------------------------

void GameWindow::showBackdrop(std::unique_ptr<Scene> scene, std::optional<CameraSequence> flight)
{
    captureMouse(false);
    Sfx::instance().stopAllSound(true);
    m_spectating = false;
    m_watched = -1;
    m_spectatorHuds.clear();
    m_world.reset();
    m_net.reset();
    m_walking = false;
    m_paused = false;
    if (!scene) {
        scene = std::make_unique<Scene>();
    }
    setTitle(roomTitle(scene.get()));
    if (m_renderer != nullptr) {
        m_renderer->replaceScene(std::move(scene));
    } else {
        m_pendingScene = std::move(scene);
    }
    m_flight = std::move(flight);
    if (m_flight) {
        m_flight->loop = true;
        m_flight->start();
    }
    emit matchChanged();
}

void GameWindow::startMatch(MatchRoom room, std::unique_ptr<NetGame> net, std::unique_ptr<Hud> hud)
{
    Sfx::instance().stopAllSound(true);
    m_flight.reset();
    m_spectating = false;
    m_watched = -1;
    m_spectatorHuds.clear();
    m_world.reset();
    m_net = std::move(net);
    setTitle(roomTitle(room.scene.get()));
    const std::string roomKey = room.scene->room != nullptr ? room.scene->room->name : std::string();
    if (m_renderer != nullptr) {
        m_renderer->replaceScene(std::move(room.scene));
    } else {
        m_pendingScene = std::move(room.scene);
    }
    setWorld(std::move(room.world), true);
    if (m_net) {
        m_net->attach(*m_world, roomKey);
    }
    if (hud) {
        setHud(std::move(hud));
    }
    m_paused = false;
    m_lastMatchState = MatchState::InProgress;
    dropHeldInput();
    captureMouse(!m_uiCaptures);
    emit matchChanged();
}

void GameWindow::setPaused(bool paused)
{
    m_paused = paused;
    dropHeldInput();
}

void GameWindow::setWorld(std::unique_ptr<World> world, bool walking)
{
    m_world = std::move(world);
    m_walking = walking && m_world->collision() != nullptr;
    m_world->setShowPlayer(m_walking);
}

void GameWindow::setHud(std::unique_ptr<Hud> hud)
{
    m_hud = std::move(hud);
}

void GameWindow::setMouse(float sensitivity, bool invertX, bool invertY)
{
    m_mouseSensitivity = 0.25f * sensitivity;
    m_invertX = invertX;
    m_invertY = invertY;
}

void GameWindow::toggleFullscreen()
{
    if (visibility() == QWindow::FullScreen) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void GameWindow::captureMouse(bool capture)
{
    // Look with the mouse: the pointer is hidden and brought back to the
    // middle after every move. (Where the platform refuses to move it --
    // Wayland -- dragging with a button held still turns the view.)
    if (capture == m_mouseCaptured || isOffscreen()) {
        return;
    }
    m_mouseCaptured = capture;
    if (capture) {
        setCursor(Qt::BlankCursor);
        setMouseGrabEnabled(true);
        QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
        m_lastMouse = QPointF(width() / 2.0, height() / 2.0);
    } else {
        unsetCursor();
        setMouseGrabEnabled(false);
    }
}

void GameWindow::dropHeldInput()
{
    m_keys.clear();
    m_buttons = {};
    m_pressed = {};
    m_dragging = false;
}

// ---- spectating ---------------------------------------------------------------------------

Player* GameWindow::watchedPlayer() const
{
    if (!m_spectating || m_watched < 0 || !m_world || static_cast<size_t>(m_watched) >= m_world->playerCount()) {
        return nullptr;
    }
    return m_world->player(static_cast<size_t>(m_watched));
}

void GameWindow::startSpectating()
{
    // SpectatorMode.Start: the main player sits the match out, hidden and not
    // solid, and the view goes to somebody who is playing.
    if (m_spectating || !m_world || !m_walking) {
        return;
    }
    m_spectating = true;
    m_world->player()->netSetSpectating(true);
    m_watched = 0;
    cycleWatched(1);
    if (m_watched < 0 && m_renderer) {
        // Nobody to watch: a free camera where the eyes were.
        const CameraPose pose = m_world->player()->camera();
        Camera& cam = m_renderer->camera();
        cam.position = QVector3D(pose.position[0], pose.position[1], pose.position[2]);
        cam.yaw = m_world->player()->yaw();
        cam.pitch = m_world->player()->pitch();
    }
    dropHeldInput();
}

void GameWindow::stopSpectating()
{
    // SpectatorMode.Rejoin
    if (!m_spectating) {
        return;
    }
    m_spectating = false;
    m_watched = -1;
    m_world->player()->netSetSpectating(false);
}

void GameWindow::cycleWatched(int direction)
{
    // FindNextActiveSlot / FindPreviousActiveSlot: somebody spawned and alive, never this machine's player.
    const int count = static_cast<int>(m_world->playerCount());
    const int from = m_watched < 0 ? 0 : m_watched;
    for (int offset = 1; offset <= count; offset++) {
        const int index = ((from + direction * offset) % count + count) % count;
        const Player& candidate = *m_world->player(static_cast<size_t>(index));
        if (index != 0 && m_world->slotActive(static_cast<size_t>(index)) && candidate.inPlay()) {
            m_watched = index;
            return;
        }
    }
    m_watched = -1;
}

CameraPose GameWindow::viewPose(float& fovY) const
{
    if (Player* watched = watchedPlayer()) {
        fovY = m_fov * watched->fovScale();
        return watched->camera();
    }
    Player* player = m_world->player();
    const std::optional<CameraPose> winner = m_world->matchEndCamera();
    const std::optional<CameraView> intro = winner ? std::nullopt : m_world->introCamera();
    fovY = intro ? intro->fov : m_fov * (winner ? 1.0f : player->fovScale()); // zoom
    return winner ? *winner : intro ? CameraPose{intro->position, intro->target, intro->up} : player->camera();
}

// ---- the HUD -------------------------------------------------------------------------------

const HudDrawList& GameWindow::overlay(int width, int height)
{
    Player* player = m_world ? m_world->player() : nullptr;
    Hud* hud = m_hud.get();
    if (Player* watched = watchedPlayer(); watched != nullptr && m_hud) {
        // The watched hunter's own HUD.
        auto& spectatorHud = m_spectatorHuds[static_cast<int>(watched->hunter())];
        if (!spectatorHud) {
            spectatorHud = std::make_unique<Hud>(m_root, static_cast<int>(watched->hunter()));
        }
        spectatorHud->setProMode(m_hud->proMode());
        hud = spectatorHud.get();
        player = watched;
    }
    if (hud && m_hudVisible && m_walking && player) {
        HudContext context = m_world->hudContext();
        // PlayerInput: the scoreboard while its button (Tab) is held.
        context.showScoreboard = held(Action::Scoreboard) || (m_pad != nullptr && m_pad->down(GamepadInput::Back));
        if (m_net) {
            context.chat = m_net->recentChat();
            for (const std::string& line : m_net->voteLines()) {
                context.chat.push_back(line);
            }
            context.netStatus = m_net->trouble();
            if (m_chatting) {
                context.chat.push_back("SAY: " + m_chatText.toStdString() + "_");
            }
        }
        if (m_spectating) {
            context.chat.push_back(m_watched >= 0 ? "SPECTATING " + m_world->slotName(static_cast<size_t>(m_watched))
                    + " - CLICK: NEXT, RIGHT CLICK: PREVIOUS, SPACE: FREE CAMERA"
                                                  : "SPECTATING - FREE CAMERA - CLICK: WATCH A PLAYER");
        }
        if (m_renderer != nullptr) {
            // The locators go where the scene's camera puts them.
            const Camera& cam = m_renderer->camera();
            context.camera = {{cam.position.x(), cam.position.y(), cam.position.z()}, {cam.target.x(), cam.target.y(), cam.target.z()},
                {cam.up.x(), cam.up.y(), cam.up.z()}};
            context.fovY = cam.fovY;
        }
        if (m_spectating && m_watched < 0) {
            context.hitMarker = 0;
        }
        hud->build(*player, context, width, height, m_hudDraws);
    } else {
        m_hudDraws.clear();
    }
    return m_hudDraws;
}

const std::vector<Image>& GameWindow::overlayTextures() const
{
    static const std::vector<Image> none;
    if (const Player* watched = watchedPlayer()) {
        const auto it = m_spectatorHuds.find(static_cast<int>(watched->hunter()));
        if (it != m_spectatorHuds.end() && it->second) {
            return it->second->textures();
        }
    }
    return m_hud ? m_hud->textures() : none;
}

// ---- the simulation ------------------------------------------------------------------------

bool GameWindow::held(Action action) const
{
    const Binding& b = m_bindings[action];
    switch (b.kind) {
    case Binding::Key: return m_keys.contains(b.code);
    case Binding::Mouse: return (m_buttons & static_cast<Qt::MouseButton>(b.code)) != 0;
    default: return false;
    }
}

bool GameWindow::takePressed(Action action)
{
    return std::exchange(m_pressed[static_cast<int>(action)], false);
}

PlayerInput GameWindow::readInput()
{
    // PlayerEntity.ProcessInput: the bindings, then the pad on top.
    PlayerInput input;
    const bool play = !m_paused && !m_uiCaptures && !m_chatting && !m_spectating;
    if (!play && m_pad != nullptr) {
        m_pad->poll(); // the pad's menu button still has to be heard
    }
    if (m_pad != nullptr && m_pad->pressed(GamepadInput::Start) && inMatch() && !m_uiCaptures) {
        emit menuRequested();
    }
    if (play) {
        input.forward = held(Action::MoveUp);
        input.back = held(Action::MoveDown);
        input.left = held(Action::MoveLeft);
        input.right = held(Action::MoveRight);
        input.jumpPressed = takePressed(Action::Jump);
        input.morphPressed = takePressed(Action::Morph);
        input.boostHeld = held(Action::Boost);
        input.altAttackPressed = takePressed(Action::AltAttack);
        input.altAttackHeld = held(Action::AltAttack);
        input.shootHeld = held(Action::Shoot);
        input.shootPressed = takePressed(Action::Shoot);
        input.zoomPressed = takePressed(Action::Zoom);
        input.hasInput = m_inputSinceTick || !m_keys.isEmpty() || m_buttons != Qt::NoButton;
        if (m_pad != nullptr && m_world && m_world->player()) {
            m_pad->poll();
            m_pad->apply(input, *m_world->player());
        }
        if (m_world && m_world->player()) {
            Player& p = *m_world->player();
            for (int i = 0; i < ActionCount; i++) {
                const int beam = InputBindings::weaponOf(static_cast<Action>(i));
                if (beam >= 0 && m_pressed[i]) {
                    p.selectWeapon(beam);
                }
            }
            if (takePressed(Action::NextWeapon)) {
                p.cycleWeapon(1);
            }
            if (takePressed(Action::PrevWeapon)) {
                p.cycleWeapon(-1);
            }
            if (takePressed(Action::AffinitySlot)) {
                p.selectWeapon(p.weaponSlot(2)); // the hunter's affinity weapon's slot
            }
        }
    }
    m_pressed = {};
    m_inputSinceTick = false;
    return input;
}

void GameWindow::simulationTick()
{
    PlayerInput input = !m_script.empty() ? m_script.input(m_scriptTick++, *m_world) : readInput();
    if (m_net) {
        m_net->tick(input, m_walking);
        if (m_net->roomChanged()) {
            changeRoom();
            return;
        }
        if (!m_net->client().connected() || m_net->client().refused()) {
            const QString reason = m_net->client().refused() ? QString::fromStdString(m_net->client().refusedReason())
                                                              : QStringLiteral("The server closed the session.");
            emit sessionEnded(reason);
            return;
        }
    } else if (!m_paused) {
        m_world->tick(input, m_walking);
    } else {
        return; // offline and paused: nothing moves
    }
    const std::vector<HudEvent> events = m_world->takeHudEvents();
    if (m_hud && m_world->player()) {
        for (const HudEvent& event : events) {
            m_hud->onEvent(event);
        }
        m_hud->tick(*m_world->player(), m_world->hudContext());
    }
    if (m_net && m_net->playback() && !m_spectating) {
        startSpectating(); // a demo is watched: there is nobody to be
    }
    if (m_spectating) {
        m_world->player()->netSetSpectating(true); // a respawn keeps it out of the match
        if (Player* watched = watchedPlayer(); watched == nullptr ? m_watched >= 0 : !watched->inPlay()) {
            cycleWatched(1); // the one being watched died or left
        }
    }
    if (m_world->matchState() != m_lastMatchState) {
        m_lastMatchState = m_world->matchState();
        emit matchChanged();
    }
    if (!m_net && m_world->matchFinished()) {
        if (!m_autoRestart) {
            emit matchOver();
            return;
        }
        m_world->restartMatch();
        m_lastMatchState = MatchState::InProgress;
        emit matchChanged();
    }
    // Sfx.Update: the listener is the camera.
    if (m_walking && m_world->player()) {
        float fov;
        const CameraPose pose = viewPose(fov);
        Vec3 facing = pose.target - pose.position;
        const float length = std::sqrt(dot(facing, facing));
        facing = length > 0 ? facing * (1 / length) : Vec3{0, 0, -1};
        Sfx::instance().update(1 / 60.0f, pose.position, facing, pose.up);
    }
    if (++m_secondTicks >= 60) {
        m_secondTicks = 0;
        emit secondElapsed();
    }
}

void GameWindow::changeRoom()
{
    const std::string key = m_net->serverRoom();
    const RoomMetadata* room = findRoom(key);
    std::optional<MatchRoom> loaded;
    if (room != nullptr && m_roomLoader) {
        loaded = m_roomLoader(key, m_net->serverMode());
    }
    if (!loaded) {
        qCritical("[net] the server moved to %s, which this build cannot load", key.c_str());
        m_net->client().reportMatchLoadFailed("this build cannot load " + key);
        emit sessionEnded(QStringLiteral("The server moved to %1, which this build cannot load.").arg(QString::fromStdString(key)));
        return;
    }
    qInfo("[net] loading %s", key.c_str());
    // The old world's sounds and models go before the scene they belong to.
    Sfx::instance().stopAllSound(true);
    m_spectatorHuds.clear();
    m_world.reset();
    m_renderer->replaceScene(std::move(loaded->scene));
    setWorld(std::move(loaded->world), m_walking);
    m_net->attach(*m_world, key);
    if (m_spectating) {
        m_world->player()->netSetSpectating(true);
        m_watched = 0;
        cycleWatched(1);
    }
    Music::instance().playRoomMusic(room->id, 0);
    setTitle(QStringLiteral("Fruity Prime — %1").arg(QString::fromUtf8(room->inGameName ? room->inGameName : room->name)));
    m_lastMatchState = MatchState::InProgress;
    emit matchChanged();
}

void GameWindow::screenshotAfter(int frames, const QString& path)
{
    m_shotFrame = frames;
    m_shotPath = path;
}

namespace {

qint64 monotonicNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

const qint64 processStartNs = monotonicNs();

#ifdef _WIN32
double processCpuSeconds()
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    auto seconds = [](const FILETIME& ft) {
        return ((static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 1e7;
    };
    return seconds(kernel) + seconds(user);
}

long peakRssKb()
{
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    return static_cast<long>(counters.PeakWorkingSetSize / 1024);
}
#else
double processCpuSeconds()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}

long peakRssKb()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}
#endif

} // namespace

void GameWindow::finish(int exitCode)
{
    // A window that was never shown does not end the event loop by closing.
    if (isOffscreen()) {
        QCoreApplication::exit(exitCode);
    } else {
        close();
        if (exitCode != 0) {
            QCoreApplication::exit(exitCode);
        }
    }
}

void GameWindow::benchmark(double warmupSeconds, double seconds)
{
    m_benchWarmup = warmupSeconds;
    m_benchSeconds = seconds;
}

void GameWindow::benchFrame(qint64 nowNs)
{
    if (m_benchStartNs == 0) {
        m_benchStartNs = nowNs;
        std::printf("bench: first frame %.0f ms after process start\n", (nowNs - processStartNs) / 1e6);
        return;
    }
    const double sinceStart = (nowNs - m_benchStartNs) / 1e9;
    if (sinceStart < m_benchWarmup) {
        m_lastBenchFrameNs = nowNs;
        m_cpuAtBenchStart = processCpuSeconds();
        return;
    }
    m_frameTimesMs.push_back((nowNs - m_lastBenchFrameNs) / 1e6);
    m_lastBenchFrameNs = nowNs;
    if (sinceStart < m_benchWarmup + m_benchSeconds) {
        return;
    }
    std::vector<double> sorted = m_frameTimesMs;
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](double p) { return sorted[std::min(sorted.size() - 1, static_cast<size_t>(p * sorted.size()))]; };
    const double total = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    const double cpu = processCpuSeconds() - m_cpuAtBenchStart;
    std::printf("bench: %zu frames in %.2f s = %.1f fps | frame ms avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f\n",
        sorted.size(), total / 1000.0, sorted.size() / (total / 1000.0), total / sorted.size(), pct(0.5), pct(0.95), pct(0.99),
        sorted.back());
    std::printf("bench: process cpu %.2f s over %.2f s wall = %.0f%% of one core | %.2f ms cpu/frame | peak rss %.1f MB\n", cpu,
        total / 1000.0, 100.0 * cpu / (total / 1000.0), 1000.0 * cpu / sorted.size(), peakRssKb() / 1024.0);
    std::fflush(stdout);
    m_benchSeconds = 0;
    QMetaObject::invokeMethod(this, [this] { finish(0); }, Qt::QueuedConnection);
}

void GameWindow::onFrame(int frame)
{
    const qint64 now = monotonicNs();
    if (m_benchSeconds > 0) {
        benchFrame(now);
    }
    const float dt = m_lastFrameNs == 0 ? 0.0f : std::min(0.1f, (now - m_lastFrameNs) / 1e9f);
    m_lastFrameNs = now;

    Camera& cam = m_renderer->camera();
    const float turn = 120.0f * dt;
    Player* player = m_world ? m_world->player() : nullptr;
    if (m_world) {
        if (m_walking && !m_paused && !m_uiCaptures && !m_chatting && !m_spectating) {
            // The keyboard's aim, then the pad's stick.
            float yaw = player->yaw(), pitch = player->pitch();
            yaw += (held(Action::AimLeft) ? turn : 0) - (held(Action::AimRight) ? turn : 0);
            pitch += (held(Action::AimUp) ? turn : 0) - (held(Action::AimDown) ? turn : 0);
            if (m_pad != nullptr) {
                const auto [dx, dy] = m_pad->look(dt, *player, *m_world);
                yaw -= dx;
                pitch -= dy;
            }
            player->setAim(yaw, pitch);
        }
        // The simulation runs at 60 Hz whatever the frame rate, as FrameTiming does.
        if (m_fixedStep) {
            simulationTick();
        } else {
            m_simAccumulator = std::min(m_simAccumulator + dt, 5.0 / 60.0);
            while (m_simAccumulator >= 1.0 / 60.0 && m_world) {
                simulationTick();
                m_simAccumulator -= 1.0 / 60.0;
            }
        }
    } else if (m_flight) {
        // The launcher's ground: the room's intro, over and over.
        m_flight->process(dt);
        if (m_flight->complete()) {
            m_flight->start();
        }
    }
    Music::instance().update(dt);
    player = m_world ? m_world->player() : nullptr; // a room change replaced the world
    if (m_walking && player && !(m_spectating && m_watched < 0)) {
        float fov;
        const CameraPose pose = viewPose(fov);
        cam.position = QVector3D(pose.position[0], pose.position[1], pose.position[2]);
        cam.target = QVector3D(pose.target[0], pose.target[1], pose.target[2]);
        cam.up = QVector3D(pose.up[0], pose.up[1], pose.up[2]);
        cam.useTarget = true;
        cam.yaw = player->yaw();
        cam.pitch = player->pitch();
        cam.fovY = fov;
    } else if (!m_world && m_flight) {
        const CameraView& view = m_flight->view();
        cam.position = QVector3D(view.position[0], view.position[1], view.position[2]);
        cam.target = QVector3D(view.target[0], view.target[1], view.target[2]);
        cam.up = QVector3D(view.up[0], view.up[1], view.up[2]);
        cam.useTarget = true;
        cam.fovY = view.fov;
    } else if (!m_uiCaptures) {
        cam.useTarget = false;
        cam.fovY = m_fov;
        float speed = m_keys.contains(Qt::Key_Shift) ? 40.0f : 10.0f;
        QVector3D move;
        if (m_keys.contains(Qt::Key_W) || m_keys.contains(Qt::Key_Z)) {
            move += cam.forward();
        }
        if (m_keys.contains(Qt::Key_S)) {
            move -= cam.forward();
        }
        if (m_keys.contains(Qt::Key_D)) {
            move += cam.right();
        }
        if (m_keys.contains(Qt::Key_A) || m_keys.contains(Qt::Key_Q)) {
            move -= cam.right();
        }
        if (m_keys.contains(Qt::Key_Space)) {
            move += QVector3D(0, 1, 0);
        }
        if (m_keys.contains(Qt::Key_C) || m_keys.contains(Qt::Key_Control)) {
            move -= QVector3D(0, 1, 0);
        }
        cam.yaw += (m_keys.contains(Qt::Key_Left) ? turn : 0) - (m_keys.contains(Qt::Key_Right) ? turn : 0);
        cam.pitch = std::clamp(cam.pitch + (m_keys.contains(Qt::Key_Up) ? turn : 0) - (m_keys.contains(Qt::Key_Down) ? turn : 0), -89.0f, 89.0f);
        cam.position += move * speed * dt;
    }

    if (m_shotFrame > 0 && frame == m_shotFrame) {
        QMetaObject::invokeMethod(
            this,
            [this] {
                const QImage image = grab();
                if (image.isNull() || !image.save(m_shotPath)) {
                    qCritical("screenshot to %s failed", qPrintable(m_shotPath));
                    finish(2);
                    return;
                }
                qInfo("screenshot saved to %s (%dx%d)", qPrintable(m_shotPath), image.width(), image.height());
                finish(0);
            },
            Qt::QueuedConnection);
    }
}

// ---- input --------------------------------------------------------------------------------

void GameWindow::press(const Binding& input, bool autoRepeat)
{
    for (int i = 0; i < ActionCount; i++) {
        if (m_bindings[static_cast<Action>(i)] == input && !autoRepeat) {
            m_pressed[i] = true;
        }
    }
    m_inputSinceTick = true;
}

void GameWindow::release(const Binding&) {}

void GameWindow::keyPressEvent(QKeyEvent* event)
{
    // The window's own keys, whatever holds the rest: fullscreen.
    if (event->key() == Qt::Key_F11 || (event->key() == Qt::Key_Return && (event->modifiers() & Qt::AltModifier))) {
        if (!event->isAutoRepeat()) {
            toggleFullscreen();
        }
        return;
    }
    if (uiTakes(event)) {
        return;
    }
    if (m_chatting) {
        // Typing a line of chat: the keys are the line's, not the game's.
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            m_net->say(m_chatText.trimmed().toStdString());
            m_chatting = false;
            captureMouse(true);
        } else if (event->key() == Qt::Key_Escape) {
            m_chatting = false;
            captureMouse(true);
        } else if (event->key() == Qt::Key_Backspace) {
            m_chatText.chop(1);
        } else if (!event->text().isEmpty() && event->text()[0].isPrint() && m_chatText.size() < 90) {
            m_chatText += event->text();
        }
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (inMatch()) {
            emit menuRequested();
        } else if (!m_uiEnabled) {
            close();
        }
        return;
    }
    const Binding key{Binding::Key, event->key()};
    if (m_net && !event->isAutoRepeat() && (m_bindings[Action::Chat] == key || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        m_chatting = true;
        m_chatText.clear();
        dropHeldInput(); // nothing stays held while typing
        captureMouse(false);
        return;
    }
    if (!inMatch()) {
        m_keys.insert(event->key()); // the free camera
        return;
    }
    if (!event->isAutoRepeat()) {
        if (m_bindings[Action::Spectate] == key && m_walking) {
            // Spectate, or rejoin the match (the pause menu's two buttons).
            m_spectating ? stopSpectating() : startSpectating();
            return;
        }
        if (m_spectating && event->key() == Qt::Key_Space) {
            // SpectatorMode.ToggleView: out of somebody's eyes, or into them.
            if (m_watched >= 0 && m_renderer) {
                const CameraPose pose = watchedPlayer()->camera();
                Camera& cam = m_renderer->camera();
                cam.position = QVector3D(pose.position[0], pose.position[1], pose.position[2]);
                cam.yaw = watchedPlayer()->yaw();
                cam.pitch = watchedPlayer()->pitch();
                m_watched = -1;
            } else {
                cycleWatched(1);
            }
            return;
        }
        if (m_net && m_bindings[Action::RecordDemo] == key && !m_net->playback()) {
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/demos");
            m_net->toggleRecording(dir.toStdString());
            return;
        }
        if (m_net && (m_bindings[Action::VoteYes] == key || m_bindings[Action::VoteNo] == key)) {
            m_net->answerVote(m_bindings[Action::VoteYes] == key); // MapVote: F1 yes, F2 no
            return;
        }
        if (event->key() == Qt::Key_H && m_hud) {
            // Stock, Pro, none.
            if (!m_hudVisible) {
                m_hudVisible = true;
                m_hud->setProMode(false);
            } else if (!m_hud->proMode()) {
                m_hud->setProMode(true);
            } else {
                m_hudVisible = false;
            }
            return;
        }
        if (event->key() == Qt::Key_F && m_world->collision() && qEnvironmentVariableIsSet("FP_DEBUG_FLY")) {
            m_walking = !m_walking;
            m_world->setShowPlayer(m_walking);
            return;
        }
    }
    m_keys.insert(event->key());
    press(key, event->isAutoRepeat());
}

void GameWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat()) {
        return;
    }
    m_keys.remove(event->key());
    uiTakes(event);
}

void GameWindow::mousePressEvent(QMouseEvent* event)
{
    if (uiTakes(event)) {
        return;
    }
    m_lastMouse = event->position();
    m_dragging = true;
    if (!inMatch()) {
        return;
    }
    if (m_spectating) {
        cycleWatched(event->button() == Qt::RightButton ? -1 : 1);
        return;
    }
    if (!m_mouseCaptured && !m_paused) {
        captureMouse(true); // a click back into the game takes the pointer again
    }
    m_buttons |= event->button();
    press({Binding::Mouse, static_cast<int>(event->button())}, false);
}

void GameWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (uiTakes(event)) {
        m_buttons = {};
        m_dragging = false;
        return;
    }
    m_buttons &= ~event->button();
    m_dragging = event->buttons() != Qt::NoButton;
}

void GameWindow::wheelEvent(QWheelEvent* event)
{
    if (uiTakes(event)) {
        return;
    }
    if (inMatch() && event->angleDelta().y() != 0) {
        press({Binding::Wheel, event->angleDelta().y() < 0 ? 1 : -1}, false);
    }
}

void GameWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (uiTakes(event)) {
        return;
    }
    const QPointF delta = event->position() - m_lastMouse;
    m_lastMouse = event->position();
    if (m_mouseCaptured) {
        // Back to the middle; the move that brings it there is not a look.
        const QPoint centre(width() / 2, height() / 2);
        if ((event->position() - QPointF(centre)).manhattanLength() > 1) {
            QCursor::setPos(mapToGlobal(centre));
            m_lastMouse = QPointF(centre);
        }
    } else if (!m_dragging) {
        return;
    }
    m_inputSinceTick = true;
    const float dx = static_cast<float>(delta.x()) * m_mouseSensitivity * (m_invertX ? -1 : 1);
    const float dy = static_cast<float>(delta.y()) * m_mouseSensitivity * (m_invertY ? -1 : 1);
    if (m_walking && m_world && !m_spectating && !m_paused) {
        Player* p = m_world->player();
        p->setAim(p->yaw() - dx, p->pitch() - dy);
        return;
    }
    if (m_renderer && (!m_world || (m_spectating && m_watched < 0))) {
        Camera& cam = m_renderer->camera();
        cam.yaw -= dx;
        cam.pitch = std::clamp(cam.pitch - dy, -89.0f, 89.0f);
    }
}

void GameWindow::focusOutEvent(QFocusEvent* event)
{
    dropHeldInput(); // a key released in another window never comes back here
    VulkanWindow::focusOutEvent(event);
}

bool GameWindow::event(QEvent* e)
{
    if (e->type() == QEvent::Close && m_ui && m_ui->root()) {
        captureMouse(false);
    }
    return VulkanWindow::event(e);
}

} // namespace fp
