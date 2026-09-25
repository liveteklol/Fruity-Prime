#pragma once

#include "GameWindow.h"
#include "InputBindings.h"
#include "Settings.h"
#include "net/NetClient.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>

namespace fp {

class GamepadInput;

// The launcher, as the C#'s Shell is: one screen at a time drawn over the
// game window -- the front screen, Play (offline or a server), Settings, the
// pause menu -- with a room flying its intro behind them. Everything QML
// shows and every button it presses is here; the QML holds no state of its
// own beyond which screen is up.
class Launcher : public QObject {
    Q_OBJECT
    // Which screen is up: "start", "play", "settings", "pause", "" (the match).
    Q_PROPERTY(QString screen READ screen WRITE setScreen NOTIFY screenChanged)
    // What the launcher is doing or has to say (joining a server, a refusal).
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(bool inMatch READ inMatch NOTIFY matchChanged)
    Q_PROPERTY(bool online READ online NOTIFY matchChanged)
    Q_PROPERTY(bool spectating READ spectating NOTIFY matchChanged)
    Q_PROPERTY(QString matchTitle READ matchTitle NOTIFY matchChanged)
    Q_PROPERTY(QVariantList rooms READ rooms CONSTANT)
    Q_PROPERTY(QVariantList modes READ modes CONSTANT)
    Q_PROPERTY(QStringList hunters READ hunters CONSTANT)
    // The server browser's rows, filled in as each server answers.
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY serversChanged)
    // Every control of the game and what it is bound to (the Controls page).
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY bindingsChanged)

public:
    Launcher(GameWindow& window, Settings& settings, GamepadInput& pad, std::filesystem::path root, QObject* parent = nullptr);
    ~Launcher() override;

    QString screen() const { return m_screen; }
    void setScreen(const QString& screen);
    QString status() const { return m_status; }
    bool busy() const { return m_joining || m_scanning; }
    bool inMatch() const { return m_window.inMatch(); }
    bool online() const { return m_window.net() != nullptr; }
    bool spectating() const { return m_window.spectating(); }
    QString matchTitle() const;
    QVariantList rooms() const { return m_rooms; }
    QVariantList modes() const { return m_modes; }
    QStringList hunters() const;
    QVariantList servers() const { return m_servers; }
    bool scanning() const { return m_scanning; }
    QVariantList bindings() const;

    // The front screen, with a room flying behind it. Any match is left.
    Q_INVOKABLE void showStart();
    // Play offline: the settings' room, mode, bots and hunter.
    Q_INVOKABLE void startOffline();
    // Join "host" or "host:port"; the answer comes back in `status`.
    Q_INVOKABLE void joinServer(const QString& address);
    // A dedicated server on this machine, joined over the loopback.
    Q_INVOKABLE void hostLocal();
    // Ask the directory who is up, then every server what it is running.
    Q_INVOKABLE void refreshServers();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void leaveMatch();
    Q_INVOKABLE void quit();
    // The screens' own settings writes take effect at once (Settings.Apply).
    Q_INVOKABLE void applySettings();
    // Controls: Qt::Key, or a negative code for a mouse button (-1 left, -2 right, -3 middle).
    Q_INVOKABLE void setBinding(const QString& action, int key, int mouseButton, int wheel);
    Q_INVOKABLE void clearBinding(const QString& action);
    Q_INVOKABLE void resetBindings();
    Q_INVOKABLE void toggleFullscreen() { m_window.toggleFullscreen(); }
    // Spectate the match, or play again (the pause menu's row).
    Q_INVOKABLE void toggleSpectating();

signals:
    void screenChanged();
    void statusChanged();
    void matchChanged();
    void serversChanged();
    void bindingsChanged();

private:
    void setStatus(const QString& status);
    void pumpJoin();
    void enterMatch(std::optional<MatchRoom> room, std::unique_ptr<net::Client> client);
    std::unique_ptr<Hud> makeHud(int hunter) const;
    // The room the launcher flies behind its screens.
    void showBackdrop();
    void noteServers(const QVariantList& rows, bool done);

    GameWindow& m_window;
    Settings& m_settings;
    GamepadInput& m_pad;
    std::filesystem::path m_root;
    QString m_screen, m_status;
    QVariantList m_rooms, m_modes, m_servers;
    bool m_scanning = false;

    // Joining: the client is ticked until the server admits it and says what it is running.
    std::unique_ptr<net::Client> m_joining;
    QTimer m_joinTimer;
    qint64 m_joinDeadline = 0;
    // The browser's queries run off the frame, so a server that does not answer never stalls it.
    std::thread m_scan;
    std::shared_ptr<std::atomic<bool>> m_scanCancelled;
};

} // namespace fp
