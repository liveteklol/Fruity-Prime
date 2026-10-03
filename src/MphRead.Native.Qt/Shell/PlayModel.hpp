#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtGui/QColor>

#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    struct MasterListing;
    struct ServerStatus;
}

namespace MphRead::Qt
{
    // PlayScreen's decisions, for the QML page that draws it: the server list
    // and its polling, the offline start, the adventure slots, the clips and
    // the map ballot. The page owns selection and layout; this owns what
    // choosing means.
    class PlayModel : public QObject
    {
        Q_OBJECT
        // Face: 0 Online, 1 Offline, 2 Story, 3 Clips, 4 Vote.
        Q_PROPERTY(int face READ Face WRITE SetFace NOTIFY faceChanged)
        Q_PROPERTY(QVariantList servers READ Servers NOTIFY serversChanged)
        Q_PROPERTY(QString note READ Note NOTIFY noteChanged)
        Q_PROPERTY(QColor noteColour READ NoteColour NOTIFY noteChanged)
        Q_PROPERTY(bool busy READ Busy NOTIFY busyChanged)
        Q_PROPERTY(QString goLabel READ GoLabel NOTIFY busyChanged)
        Q_PROPERTY(QVariantList saveSlots READ Slots NOTIFY slotsChanged)
        Q_PROPERTY(QVariantList clips READ Clips NOTIFY clipsChanged)
        Q_PROPERTY(QStringList modes READ Modes CONSTANT)
        Q_PROPERTY(QStringList hunters READ Hunters CONSTANT)
        Q_PROPERTY(QStringList bots READ Bots CONSTANT)
        Q_PROPERTY(int lastHunter READ LastHunter CONSTANT)
        Q_PROPERTY(int lastColour READ LastColour CONSTANT)
        Q_PROPERTY(int lastBots READ LastBots CONSTANT)
        Q_PROPERTY(int lastSkill READ LastSkill CONSTANT)
        Q_PROPERTY(QString playerName READ PlayerName CONSTANT)
        Q_PROPERTY(QString serverEndpoint READ ServerEndpoint CONSTANT)
        Q_PROPERTY(QString chosenRoom READ ChosenRoom CONSTANT)
        Q_PROPERTY(QString votingRoom READ VotingRoom CONSTANT)

    public:
        explicit PlayModel(QObject* parent = nullptr);
        ~PlayModel() override;

        // The server rows -uishot shows, instead of asking the directory.
        static bool UseSample;

        [[nodiscard]] int Face() const noexcept { return _face; }
        void SetFace(int face);
        [[nodiscard]] QVariantList Servers() const { return _servers; }
        [[nodiscard]] QString Note() const { return _note; }
        [[nodiscard]] QColor NoteColour() const { return _noteColour; }
        [[nodiscard]] bool Busy() const noexcept { return _busy; }
        [[nodiscard]] QString GoLabel() const { return _goLabel; }
        [[nodiscard]] QVariantList Slots() const { return _slots; }
        [[nodiscard]] QVariantList Clips() const { return _clips; }
        [[nodiscard]] QStringList Modes() const;
        [[nodiscard]] QStringList Hunters() const;
        [[nodiscard]] QStringList Bots() const;
        [[nodiscard]] int LastHunter() const;
        [[nodiscard]] int LastColour() const;
        [[nodiscard]] int LastBots() const;
        [[nodiscard]] int LastSkill() const;
        [[nodiscard]] QString PlayerName() const;
        [[nodiscard]] QString ServerEndpoint() const;
        [[nodiscard]] QString ChosenRoom() const;
        [[nodiscard]] QString VotingRoom() const;

        // The note line, when the page has something to say itself.
        Q_INVOKABLE void say(const QString& text, const QColor& colour);
        Q_INVOKABLE void reloadServers();
        Q_INVOKABLE void queryStatus(const QString& endpoint);
        Q_INVOKABLE void stopPolling();
        Q_INVOKABLE void join(const QString& name, const QString& endpoint, int hunter, int suit);
        Q_INVOKABLE void start(const QString& room, int mode, int hunter, int suit, int bots, int skill);
        Q_INVOKABLE bool slotUsed(int slot) const;
        Q_INVOKABLE void startAdventure(int slot, int hunter, bool newGame);
        Q_INVOKABLE void watch(const QString& path);
        Q_INVOKABLE void importClip();
        Q_INVOKABLE void propose(const QString& room);
        Q_INVOKABLE QColor suitColour(int hunter, int suit) const;
        Q_INVOKABLE void sessionEnded(const QString& reason);

    signals:
        void faceChanged();
        void serversChanged();
        void noteChanged();
        void busyChanged();
        void slotsChanged();
        void clipsChanged();

    private:
        void Rebuild();
        void StartPolling();
        void Answered();
        void AddServer(const ::MphRead::Mods::Network::MasterListing& listing);
        void SetStatus(int row, const ::MphRead::Mods::Network::ServerStatus& status);
        void SetNote(QString text, QColor colour);
        void SetBusy(bool busy, QString label);
        void BuildStory();
        void BuildClips();

        int _face = 0;
        QVariantList _servers;
        QString _note;
        QColor _noteColour;
        bool _busy = false;
        QString _goLabel;
        QVariantList _slots;
        QVariantList _clips;
        QTimer _poll;
        QString _endpoint;
        int _asked = 0;
        int _replied = 0;
        int _live = 0;
        // Replies from threads check this before touching the model.
        std::shared_ptr<int> _lifetime = std::make_shared<int>(0);
        std::shared_ptr<std::stop_source> _statusCancel;
        bool _finished = false;
    };
}
