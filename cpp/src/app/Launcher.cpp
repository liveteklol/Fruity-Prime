#include "Launcher.h"

#include "Gamepad.h"
#include "audio/Music.h"
#include "audio/Sfx.h"
#include "game/Hud.h"
#include "game/MatchSetup.h"
#include "net/NetMaster.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QProcess>

#include <algorithm>

namespace fp {

namespace {

const char* const hunterNames[] = {"Samus", "Kanden", "Trace", "Sylux", "Noxus", "Spire", "Weavel"};

bool parseAddress(const QString& text, std::string& host, uint16_t& port)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    const int colon = trimmed.lastIndexOf(':');
    host = (colon > 0 ? trimmed.left(colon) : trimmed).toStdString();
    port = colon > 0 ? static_cast<uint16_t>(trimmed.mid(colon + 1).toUInt()) : net::DefaultPort;
    return !host.empty() && port != 0;
}

QVariantMap serverRow(const QString& name, const QString& address, const QString& room, const QString& mode, int players, int maxPlayers,
    int ping, bool ok, const QString& note)
{
    QVariantMap row;
    row["name"] = name;
    row["address"] = address;
    row["room"] = room;
    row["mode"] = mode;
    row["players"] = players;
    row["maxPlayers"] = maxPlayers;
    row["ping"] = ping;
    row["ok"] = ok;
    row["note"] = note;
    return row;
}

} // namespace

Launcher::Launcher(GameWindow& window, Settings& settings, GamepadInput& pad, std::filesystem::path root, QObject* parent)
    : QObject(parent)
    , m_window(window)
    , m_settings(settings)
    , m_pad(pad)
    , m_root(std::move(root))
{
    for (const RoomMetadata* room : matchRooms()) {
        QVariantMap entry;
        entry["key"] = QString::fromUtf8(room->name);
        entry["name"] = QString::fromUtf8(room->inGameName != nullptr && room->inGameName[0] != '\0' ? room->inGameName : room->name);
        m_rooms.append(entry);
    }
    for (int i = 0; i < 12; i++) {
        const GameMode mode = static_cast<GameMode>(i);
        QVariantMap entry;
        entry["key"] = QString::fromLatin1(modeKey(mode));
        entry["name"] = QString::fromLatin1(modeTitle(mode));
        m_modes.append(entry);
    }
    connect(&m_window, &GameWindow::menuRequested, this, [this] {
        if (m_screen.isEmpty()) {
            m_window.setPaused(true);
            setScreen(QStringLiteral("pause"));
        } else if (m_screen == QLatin1String("pause")) {
            resume();
        } else if (inMatch()) {
            setScreen(QStringLiteral("pause"));
        } else {
            setScreen(QStringLiteral("start"));
        }
    });
    connect(&m_window, &GameWindow::matchChanged, this, &Launcher::matchChanged);
    // The results are over: back to the front screen, as the C# does.
    m_window.setAutoRestart(false);
    connect(&m_window, &GameWindow::matchOver, this, &Launcher::showStart);
    connect(&m_window, &GameWindow::sessionEnded, this, [this](const QString& reason) {
        showStart();
        setStatus(reason);
    });
    m_joinTimer.setInterval(16);
    connect(&m_joinTimer, &QTimer::timeout, this, &Launcher::pumpJoin);
}

Launcher::~Launcher()
{
    if (m_scanCancelled) {
        *m_scanCancelled = true;
    }
    if (m_scan.joinable()) {
        m_scan.detach(); // it holds nothing of this object but the flag
    }
}

QStringList Launcher::hunters() const
{
    QStringList names;
    for (const char* name : hunterNames) {
        names << QString::fromLatin1(name);
    }
    return names;
}

QString Launcher::matchTitle() const
{
    const World* world = m_window.world();
    if (world == nullptr) {
        return {};
    }
    const RoomMetadata* room = findRoom(m_settings.text(QStringLiteral("match.room")).toStdString());
    const QString name = room != nullptr && room->inGameName != nullptr ? QString::fromUtf8(room->inGameName) : QString();
    return QStringLiteral("%1 — %2").arg(name, QString::fromLatin1(modeTitle(world->mode())));
}

void Launcher::setScreen(const QString& screen)
{
    if (m_screen == screen) {
        return;
    }
    m_screen = screen;
    // The pointer and the keys are the screens' while one is up.
    m_window.setUiCapturesInput(!screen.isEmpty());
    emit screenChanged();
}

void Launcher::setStatus(const QString& status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged();
}

void Launcher::showBackdrop()
{
    Backdrop backdrop = buildBackdrop(m_root, m_settings.text(QStringLiteral("match.room")).toStdString());
    m_window.showBackdrop(std::move(backdrop.scene), std::move(backdrop.flight));
}

void Launcher::showStart()
{
    m_joinTimer.stop();
    m_joining.reset();
    Music::instance().stop(0.5f);
    showBackdrop();
    setScreen(QStringLiteral("start"));
    setStatus({});
    emit matchChanged();
}

std::unique_ptr<Hud> Launcher::makeHud(int hunter) const
{
    auto hud = std::make_unique<Hud>(m_root, hunter);
    hud->setProMode(m_settings.flag(QStringLiteral("display.proHud")));
    return hud;
}

void Launcher::startOffline()
{
    MatchOptions options;
    options.roomKey = m_settings.text(QStringLiteral("match.room")).toStdString();
    options.mode = modeFromKey(m_settings.text(QStringLiteral("match.mode")).toStdString()).value_or(GameMode::Battle);
    options.hunter = std::clamp(m_settings.integer(QStringLiteral("player.hunter")), 0, 6);
    options.suit = m_settings.integer(QStringLiteral("player.suit"));
    options.bots = m_settings.integer(QStringLiteral("match.bots"));
    options.botLevel = m_settings.integer(QStringLiteral("match.botLevel"));
    options.teams = m_settings.integer(QStringLiteral("match.teams"));
    options.friendlyFire = m_settings.flag(QStringLiteral("match.friendlyFire"));
    options.intro = m_settings.flag(QStringLiteral("display.intro"));
    if (const int goal = m_settings.integer(QStringLiteral("match.pointGoal")); goal >= 0) {
        options.pointGoal = goal;
    }
    if (const double minutes = m_settings.number(QStringLiteral("match.timeLimit")); minutes >= 0) {
        options.timeLimitSeconds = static_cast<float>(minutes * 60);
    }
    if (const double seconds = m_settings.number(QStringLiteral("match.timeGoal")); seconds >= 0) {
        options.timeGoalSeconds = static_cast<float>(seconds);
    }
    std::optional<MatchRoom> room = buildOfflineMatch(m_root, options);
    if (!room) {
        setStatus(QStringLiteral("%1 could not be loaded.").arg(QString::fromStdString(options.roomKey)));
        return;
    }
    enterMatch(std::move(room), nullptr);
}

void Launcher::joinServer(const QString& address)
{
    std::string host;
    uint16_t port = 0;
    if (!parseAddress(address, host, port)) {
        setStatus(QStringLiteral("\"%1\" is not a server address.").arg(address));
        return;
    }
    m_joinTimer.stop();
    auto client = std::make_unique<net::Client>();
    const int hunter = std::clamp(m_settings.integer(QStringLiteral("player.hunter")), 0, 6);
    if (!client->connect(host, port, m_settings.text(QStringLiteral("player.name")).toStdString(), hunter,
            m_settings.integer(QStringLiteral("player.suit")))) {
        setStatus(QStringLiteral("%1 could not be resolved.").arg(address));
        return;
    }
    m_joining = std::move(client);
    m_joinDeadline = QDateTime::currentMSecsSinceEpoch() + 10000;
    setStatus(QStringLiteral("Joining %1…").arg(address));
    emit statusChanged();
    m_joinTimer.start();
}

void Launcher::pumpJoin()
{
    if (!m_joining) {
        m_joinTimer.stop();
        return;
    }
    m_joining->update();
    if (m_joining->refused()) {
        setStatus(QString::fromStdString(m_joining->refusedReason()));
        m_joining.reset();
        m_joinTimer.stop();
        emit statusChanged();
        return;
    }
    if (!m_joining->connected() || QDateTime::currentMSecsSinceEpoch() > m_joinDeadline) {
        setStatus(QStringLiteral("%1 did not answer: it may be off, full, or UDP may be blocked.")
                      .arg(QString::fromStdString(m_joining->serverName())));
        m_joining.reset();
        m_joinTimer.stop();
        emit statusChanged();
        return;
    }
    if (m_joining->localSlot() < 0 || !m_joining->match()) {
        return; // still waiting to be admitted and told what is running
    }
    m_joinTimer.stop();
    const std::string key = m_joining->match()->roomKey;
    const RoomMetadata* room = findRoom(key);
    if (room == nullptr) {
        setStatus(QStringLiteral("The server is running %1, which this build does not have.").arg(QString::fromStdString(key)));
        m_joining.reset();
        return;
    }
    const int hunter = std::clamp(m_settings.integer(QStringLiteral("player.hunter")), 0, 6);
    const int mode = net::portMode(m_joining->match()->mode);
    std::optional<MatchRoom> loaded
        = loadMatchRoom(m_root, *room, mode >= 0 ? static_cast<GameMode>(mode) : GameMode::Battle, 8, hunter, true);
    if (!loaded) {
        setStatus(QStringLiteral("%1 could not be loaded.").arg(QString::fromStdString(key)));
        m_joining.reset();
        return;
    }
    enterMatch(std::move(loaded), std::move(m_joining));
}

void Launcher::enterMatch(std::optional<MatchRoom> room, std::unique_ptr<net::Client> client)
{
    const int hunter = std::clamp(m_settings.integer(QStringLiteral("player.hunter")), 0, 6);
    std::unique_ptr<NetGame> net;
    if (client) {
        NetGame::preloadModels(*room->scene, m_root);
        net = std::make_unique<NetGame>(std::move(client));
    }
    const int roomId = room->scene->room != nullptr ? room->scene->room->id : -1;
    m_window.startMatch(std::move(*room), std::move(net), makeHud(hunter));
    if (roomId >= 0) {
        Music::instance().playRoomMusic(roomId, 0); // SceneSetup: TryPlayRoomMusic
    }
    setStatus({});
    setScreen(QString());
    emit matchChanged();
}

void Launcher::hostLocal()
{
    // LocalServer: the server is its own process, so it outlives this one.
    const QString rotation = QDir::temp().filePath(QStringLiteral("fruityprime-rotation-%1.txt").arg(QCoreApplication::applicationPid()));
    QFile file(rotation);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("The rotation could not be written."));
        return;
    }
    const QString mode = m_settings.text(QStringLiteral("match.mode"));
    for (const QString& map : m_settings.text(QStringLiteral("host.maps")).split(',', Qt::SkipEmptyParts)) {
        file.write(QStringLiteral("%1 | %2 | 7 | 7\n").arg(map.trimmed(), mode).toUtf8());
    }
    file.close();
    uint16_t port = 0;
    for (uint16_t candidate = net::DefaultPort; candidate < net::DefaultPort + 100 && port == 0; candidate++) {
        net::Transport probe;
        if (probe.open(candidate)) {
            port = candidate;
        }
    }
    QStringList args{QStringLiteral("--server"), QStringLiteral("--files"), QString::fromStdString(m_root.string()),
        QStringLiteral("--port"), QString::number(port), QStringLiteral("--max-players"),
        QString::number(std::clamp(m_settings.integer(QStringLiteral("host.maxPlayers")), 2, 8)), QStringLiteral("--rotation"), rotation,
        QStringLiteral("--server-name"),
        m_settings.text(QStringLiteral("host.name")).isEmpty() ? m_settings.text(QStringLiteral("player.name")) + QStringLiteral("'s server")
                                                               : m_settings.text(QStringLiteral("host.name")),
        QStringLiteral("--no-master")};
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) {
        setStatus(QStringLiteral("The server could not be started."));
        return;
    }
    // The loopback is the one address certain to reach a server on this box.
    joinServer(QStringLiteral("127.0.0.1:%1").arg(port));
}

void Launcher::noteServers(const QVariantList& rows, bool done)
{
    m_servers = rows;
    m_scanning = !done;
    emit serversChanged();
    emit statusChanged();
}

void Launcher::refreshServers()
{
    if (m_scanning) {
        return;
    }
    if (m_scanCancelled) {
        *m_scanCancelled = true;
    }
    if (m_scan.joinable()) {
        m_scan.detach();
    }
    m_scanCancelled = std::make_shared<std::atomic<bool>>(false);
    m_servers.clear();
    m_scanning = true;
    emit serversChanged();

    std::string host;
    uint16_t port = net::MasterDefaultPort;
    if (!parseAddress(m_settings.text(QStringLiteral("online.directory")), host, port)) {
        host = net::MasterDefaultHost;
    }
    auto cancelled = m_scanCancelled;
    QPointer<Launcher> self(this);
    m_scan = std::thread([self, cancelled, host, port] {
        const net::MasterListResult list = net::queryMaster(host, port);
        QVariantList rows;
        if (!list.answered) {
            rows.append(serverRow(QStringLiteral("The directory did not answer"), QString::fromStdString(host), {}, {}, 0, 0, 0, false,
                QStringLiteral("check the address in Settings")));
        }
        // Each row is confirmed by this machine: a server the directory lists
        // but nothing here can reach is a server this player cannot join.
        for (const net::MasterListing& server : list.servers) {
            if (*cancelled) {
                return;
            }
            int latency = 0;
            const auto status = net::queryStatus(server.address, server.port, 1200, latency);
            const QString address = QStringLiteral("%1:%2").arg(QString::fromStdString(server.address)).arg(server.port);
            const int mode = status ? net::portMode(status->match.mode) : net::portMode(server.mode);
            rows.append(serverRow(QString::fromStdString(status ? status->name : server.name), address,
                QString::fromStdString(status ? status->match.roomKey : server.roomKey),
                QString::fromLatin1(mode >= 0 ? modeTitle(static_cast<GameMode>(mode)) : "Battle"),
                status ? status->match.playerCount : server.players, status ? status->maxPlayers : server.maxPlayers, latency,
                status.has_value(), status ? (status->protocol != net::ProtocolVersion ? QStringLiteral("another version") : QString())
                                           : QStringLiteral("did not answer")));
            const QVariantList snapshot = rows;
            QMetaObject::invokeMethod(
                self, [self, snapshot] {
                    if (self) {
                        self->noteServers(snapshot, false);
                    }
                }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(
            self, [self, rows] {
                if (self) {
                    self->noteServers(rows, true);
                }
            }, Qt::QueuedConnection);
    });
}

void Launcher::resume()
{
    if (!inMatch()) {
        setScreen(QStringLiteral("start"));
        return;
    }
    m_window.setPaused(false);
    setScreen(QString());
}

void Launcher::leaveMatch()
{
    showStart();
}

void Launcher::toggleSpectating()
{
    if (m_window.spectating()) {
        m_window.stopSpectating();
    } else {
        m_window.startSpectating();
    }
    emit matchChanged();
    resume();
}

void Launcher::quit()
{
    m_settings.save();
    QCoreApplication::quit();
}

void Launcher::applySettings()
{
    m_settings.save();
    InputBindings bindings;
    bindings.load(m_settings);
    m_window.setBindings(bindings);
    m_window.setMouse(static_cast<float>(m_settings.number(QStringLiteral("mouse.sensitivity"))),
        m_settings.flag(QStringLiteral("mouse.invertX")), m_settings.flag(QStringLiteral("mouse.invertY")));
    m_window.setFov(static_cast<float>(m_settings.number(QStringLiteral("display.fov"))));
    m_pad.setOptions({m_settings.flag(QStringLiteral("pad.enabled")), static_cast<float>(m_settings.number(QStringLiteral("pad.sensitivity"))),
        static_cast<float>(m_settings.number(QStringLiteral("pad.deadzone"))), m_settings.flag(QStringLiteral("pad.invertY")),
        m_settings.flag(QStringLiteral("pad.aimAssist"))});
    Sfx::instance().volume = 0.35f * static_cast<float>(m_settings.number(QStringLiteral("audio.sfx"))) / 100.0f;
    Music::instance().userVolume = static_cast<float>(m_settings.number(QStringLiteral("audio.music"))) / 100.0f;
    if (Hud* hud = m_window.hud(); hud != nullptr) {
        hud->setProMode(m_settings.flag(QStringLiteral("display.proHud")));
    }
    const bool fullscreen = m_settings.flag(QStringLiteral("display.fullscreen"));
    if (fullscreen != (m_window.visibility() == QWindow::FullScreen)) {
        m_window.toggleFullscreen();
    }
    emit bindingsChanged();
}

QVariantList Launcher::bindings() const
{
    InputBindings current;
    current.load(m_settings);
    QVariantList rows;
    for (int i = 0; i < ActionCount; i++) {
        const Action action = static_cast<Action>(i);
        QVariantMap row;
        row["action"] = QString::fromLatin1(InputBindings::id(action));
        row["title"] = InputBindings::title(action);
        row["label"] = current[action].label();
        rows.append(row);
    }
    return rows;
}

void Launcher::setBinding(const QString& action, int key, int mouseButton, int wheel)
{
    Binding binding;
    if (mouseButton > 0) {
        binding = {Binding::Mouse, mouseButton};
    } else if (wheel != 0) {
        binding = {Binding::Wheel, wheel < 0 ? -1 : 1};
    } else if (key != 0) {
        binding = {Binding::Key, key};
    }
    m_settings.set(QStringLiteral("key.") + action, binding.toString());
    applySettings();
}

void Launcher::clearBinding(const QString& action)
{
    m_settings.set(QStringLiteral("key.") + action, Binding().toString());
    applySettings();
}

void Launcher::resetBindings()
{
    m_settings.reset(QStringLiteral("key."));
    applySettings();
}

} // namespace fp
