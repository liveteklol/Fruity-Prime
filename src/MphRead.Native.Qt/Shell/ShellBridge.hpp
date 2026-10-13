#pragma once

#include "../../MphRead.Native/Mods/Launcher/Portable/LaunchPlan.hpp"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtGui/QColor>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QQuickItem;

namespace MphRead
{
    class MenuSettings;
}

namespace MphRead::Qt
{
    // What the QML pages see of the shell (the context property "shell").
    // The pages only present and ask; every decision stays in Shell.cpp, the
    // screen models and the portable launcher code they call.
    class ShellBridge final : public QObject
    {
        Q_OBJECT
        // "front", "pause", "end" or empty.
        Q_PROPERTY(QString page READ Page NOTIFY pageChanged)
        // Startup is a state of its own, never inferred from an empty page:
        // BootingQt, QtReady, BootingNative, FrontReady or Failed. Hosts with
        // no separate bootstrap (the desktop shell) start at FrontReady.
        Q_PROPERTY(QString startupState READ StartupState NOTIFY startupChanged)
        Q_PROPERTY(QString startupError READ StartupError NOTIFY startupChanged)
        Q_PROPERTY(QVariantList rooms READ Rooms NOTIFY roomsChanged)
        Q_PROPERTY(bool gameFilesReady READ GameFilesReady NOTIFY roomsChanged)
        Q_PROPERTY(QString playerName READ PlayerName NOTIFY profileChanged)
        Q_PROPERTY(QString version READ Version NOTIFY versionChanged)
        Q_PROPERTY(QColor versionColour READ VersionColour NOTIFY versionChanged)
        // The version line can be pressed to update.
        Q_PROPERTY(bool updatable READ Updatable NOTIFY versionChanged)
        Q_PROPERTY(QString windowLabel READ WindowLabel NOTIFY windowChanged)
        Q_PROPERTY(QString brand READ Brand CONSTANT)
        Q_PROPERTY(QString fontOverride READ FontOverride CONSTANT)
        // Android's display density (px per dp); 1 everywhere else.
        Q_PROPERTY(double deviceDensity READ DeviceDensity CONSTANT)
        Q_PROPERTY(bool backdropSuspended READ BackdropSuspended WRITE SetBackdropSuspended NOTIFY backdropSuspendedChanged)
        // Quake 3 maps being translated in the background
        // (CustomRooms::StartGenerating): whether, how far (0-1), and which.
        Q_PROPERTY(bool mapsBuilding READ MapsBuilding NOTIFY mapsChanged)
        Q_PROPERTY(double mapsProgress READ MapsProgress NOTIFY mapsChanged)
        Q_PROPERTY(QString mapsStage READ MapsStage NOTIFY mapsChanged)

    public:
        using LaunchPlan = ::MphRead::Mods::Launcher::LaunchPlan;

        struct Actions
        {
            std::function<void(LaunchPlan)> Launch;
            std::function<void()> Quit;
            std::function<void()> Resume;
            std::function<void()> Leave;
            std::function<void()> QuitMatch;
            std::function<void()> ToggleFullscreen;
            // Setup finished: the rooms are there to be listed now.
            std::function<void()> GameFilesChanged;
        };

        explicit ShellBridge(Actions actions);
        ~ShellBridge() override;

        // The one bridge there is, for the screen models.
        [[nodiscard]] static ShellBridge* Current() noexcept;

        [[nodiscard]] QString Page() const { return _page; }
        [[nodiscard]] bool Showing() const noexcept { return !_page.isEmpty(); }
        void SetPage(QString page);
        [[nodiscard]] QString StartupState() const { return _startupState; }
        [[nodiscard]] QString StartupError() const { return _startupError; }
        void SetStartup(QString state, QString error = {});
        [[nodiscard]] QVariantList Rooms() const { return _rooms; }
        void SetRooms(const std::vector<std::string>& rooms, bool gameFilesReady);
        [[nodiscard]] bool GameFilesReady() const noexcept { return _gameFilesReady; }
        [[nodiscard]] QString PlayerName() const;
        [[nodiscard]] QString Version() const;
        [[nodiscard]] QColor VersionColour() const;
        [[nodiscard]] bool Updatable() const noexcept { return _updatable; }
        [[nodiscard]] QString WindowLabel() const;
        [[nodiscard]] QString Brand() const;
        [[nodiscard]] QString FontOverride() const;
        [[nodiscard]] double DeviceDensity() const;
        [[nodiscard]] bool BackdropSuspended() const { return _backdropSuspended; }
        [[nodiscard]] bool MapsBuilding() const noexcept { return _mapsBuilding; }
        [[nodiscard]] double MapsProgress() const noexcept { return _mapsProgress; }
        [[nodiscard]] QString MapsStage() const { return _mapsStage; }
        void SetBackdropSuspended(bool value) { _backdropSuspended = value; emit backdropSuspendedChanged(); }

        void SetSettings(std::shared_ptr<::MphRead::MenuSettings> settings);
        [[nodiscard]] const std::shared_ptr<::MphRead::MenuSettings>& Settings() const noexcept { return _settings; }
        [[nodiscard]] QString RoomKey() const;
        void SetRoomKey(const QString& key);

        // StartScreen.ConnectedOrFinished: a persistent lobby opens its
        // screen; anything else starts.
        void Launch(LaunchPlan plan);
        [[nodiscard]] const std::optional<LaunchPlan>& LobbyPlan() const noexcept { return _lobbyPlan; }
        // Ask the pages for one screen alone (captures).
        void RequestScreen(const QString& url, const QVariantMap& props);

        Q_INVOKABLE void quit();
        Q_INVOKABLE void resume();
        Q_INVOKABLE void leaveMatch();
        Q_INVOKABLE void quitFromMatch();
        Q_INVOKABLE void openSupport();
        Q_INVOKABLE void toggleFullscreen();
        // StartScreen.StartUpdateCheck / RefreshVersionLine / UpdateNow.
        Q_INVOKABLE void startUpdateCheck();
        Q_INVOKABLE void refreshVersionLine();
        Q_INVOKABLE void updateNow();
        // Scroll the lists around an item until it is in view.
        Q_INVOKABLE void reveal(QQuickItem* item);
        void GameFilesChanged();
        // The lobby's match is loading: start it, whatever the lobby rule.
        void StartMatch(LaunchPlan plan);
        // Back from a lobby's match: the lobby screen again.
        void OpenLobby();
        // The pad's shoulder buttons: the page's tabs step.
        void StepTabs(int direction) { emit tabStep(direction); }
        // A key or the pad moved the focus: show the focus ring.
        void KeyboardDriving() { emit keyboardDriving(); }
        // A map's picture as a URL, or empty when there is none yet.
        Q_INVOKABLE QString mapShot(const QString& room) const;
        Q_INVOKABLE QString roomName(const QString& room) const;
        // DeckTile.Drift: where a card's glow sits, from its room key.
        Q_INVOKABLE double roomPhase(const QString& room) const;
        // The pause menu's live state: vote, net, spectating, canSpectate,
        // recording, demo.
        Q_INVOKABLE QVariantMap pauseState() const;
        Q_INVOKABLE void answerVote(bool yes);
        Q_INVOKABLE void spectate();
        Q_INVOKABLE void rejoin();
        Q_INVOKABLE void toggleRecording();
        Q_INVOKABLE QString whyNotVoting() const;
        Q_INVOKABLE void systemMessage(const QString& text);
        Q_INVOKABLE void refreshProfile();
        // EndPanelView: the ballot (key, code, name, votes, chosen, leader),
        // the hunter and suit, ready, and the count line.
        Q_INVOKABLE QVariantMap endState() const;
        Q_INVOKABLE void endChoose(const QString& room);
        Q_INVOKABLE void endPick(int hunter, int suit);
        Q_INVOKABLE void endToggleReady();
        Q_INVOKABLE QColor suitColour(int hunter, int suit) const;

    signals:
        void mapsChanged();
        void pageChanged();
        void startupChanged();
        void roomsChanged();
        void profileChanged();
        void windowChanged();
        void versionChanged();
        void lobbyOpened();
        void screenRequested(QString url, QVariantMap props);
        void tabStep(int direction);
        void keyboardDriving();
        void backdropSuspendedChanged();
        void backRequested();

    private:
        void pollMaps();
        bool _mapsBuilding = false;
        double _mapsProgress = 0.0;
        QString _mapsStage;
        bool _mapsSeenBuilding = false;
        std::int32_t _mapsListVersion = 0;
        Actions _actions;
        QString _page;
        QString _startupState = QStringLiteral("FrontReady");
        QString _startupError;
        QVariantList _rooms;
        bool _gameFilesReady = false;
        bool _backdropSuspended = false;
        std::shared_ptr<::MphRead::MenuSettings> _settings;
        std::optional<LaunchPlan> _lobbyPlan;
        void Say(QString text, QColor colour, bool pressable = false);
        QString _version;
        QColor _versionColour;
        bool _updatable = false;
        bool _updating = false;
        bool _updateCheckStarted = false;
        std::shared_ptr<int> _lifetime = std::make_shared<int>(0);
    };
}
