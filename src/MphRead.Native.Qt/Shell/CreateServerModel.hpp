#pragma once

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtGui/QColor>

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::Network
{
    struct HostCandidate;
}

namespace MphRead
{
    enum class GameMode : std::uint8_t;
    enum class Hunter : std::uint8_t;
}

namespace MphRead::Qt
{
    // CreateServerScreen's decisions: who can host, the rotation, and the
    // two ways to start -- a lobby on a server that offers one, or the
    // dedicated server on this machine -- then joining it.
    class CreateServerModel : public QObject
    {
        Q_OBJECT
        // The capture's fleet instead of asking the directory.
        Q_PROPERTY(bool sample READ Sample WRITE SetSample NOTIFY changed)
        Q_PROPERTY(QString lobbyName READ LobbyName CONSTANT)
        Q_PROPERTY(QStringList modes READ Modes CONSTANT)
        Q_PROPERTY(QStringList hunters READ Hunters CONSTANT)
        Q_PROPERTY(int lastHunter READ LastHunter CONSTANT)
        Q_PROPERTY(bool canRunHere READ CanRunHere CONSTANT)
        Q_PROPERTY(int kind READ Kind WRITE SetKind NOTIFY changed)
        Q_PROPERTY(QString hostLabel READ HostLabel NOTIFY changed)
        Q_PROPERTY(QStringList rotation READ Rotation WRITE SetRotation NOTIFY changed)
        Q_PROPERTY(QString mapsLabel READ MapsLabel NOTIFY changed)
        Q_PROPERTY(QString note READ Note NOTIFY changed)
        Q_PROPERTY(QColor noteColour READ NoteColour NOTIFY changed)
        Q_PROPERTY(bool busy READ Busy NOTIFY changed)
        Q_PROPERTY(QString goLabel READ GoLabel NOTIFY changed)
        Q_PROPERTY(bool goEnabled READ GoEnabled NOTIFY changed)
        Q_PROPERTY(bool fetchVisible READ FetchVisible NOTIFY changed)
        Q_PROPERTY(bool progressVisible READ ProgressVisible NOTIFY changed)
        Q_PROPERTY(double fraction READ Fraction NOTIFY changed)
        Q_PROPERTY(QString stage READ Stage NOTIFY changed)
        // HostPicker: [{title, detail, usable, index}], its note, and asking.
        Q_PROPERTY(QVariantList hosts READ Hosts NOTIFY hostsChanged)
        Q_PROPERTY(QString hostsNote READ HostsNote NOTIFY hostsChanged)
        Q_PROPERTY(QColor hostsNoteColour READ HostsNoteColour NOTIFY hostsChanged)
        Q_PROPERTY(bool asking READ Asking NOTIFY hostsChanged)
        Q_PROPERTY(int maxRotation READ MaxRotation CONSTANT)

    public:
        explicit CreateServerModel(QObject* parent = nullptr);
        ~CreateServerModel() override;

        [[nodiscard]] bool Sample() const noexcept { return _sample; }
        void SetSample(bool value);
        [[nodiscard]] QString LobbyName() const;
        [[nodiscard]] QStringList Modes() const;
        [[nodiscard]] QStringList Hunters() const;
        [[nodiscard]] int LastHunter() const;
        [[nodiscard]] static bool CanRunHere();
        [[nodiscard]] int Kind() const noexcept { return _kind; }
        void SetKind(int value);
        [[nodiscard]] QString HostLabel() const { return _hostLabel; }
        [[nodiscard]] QStringList Rotation() const;
        void SetRotation(const QStringList& value);
        [[nodiscard]] QString MapsLabel() const;
        [[nodiscard]] QString Note() const { return _note; }
        [[nodiscard]] QColor NoteColour() const { return _noteColour; }
        [[nodiscard]] bool Busy() const noexcept { return _busy; }
        [[nodiscard]] QString GoLabel() const { return _goLabel; }
        [[nodiscard]] bool GoEnabled() const noexcept { return _goEnabled; }
        [[nodiscard]] bool FetchVisible() const noexcept { return _fetchVisible; }
        [[nodiscard]] bool ProgressVisible() const noexcept { return _progressVisible; }
        [[nodiscard]] double Fraction() const noexcept { return _fraction; }
        [[nodiscard]] QString Stage() const { return _stage; }
        [[nodiscard]] QVariantList Hosts() const { return _hosts; }
        [[nodiscard]] QString HostsNote() const { return _hostsNote; }
        [[nodiscard]] QColor HostsNoteColour() const { return _hostsNoteColour; }
        [[nodiscard]] bool Asking() const noexcept { return _asking; }
        [[nodiscard]] static int MaxRotation();

        // The room's in-game name.
        Q_INVOKABLE QString roomName(const QString& room) const;
        Q_INVOKABLE void askAgain();
        Q_INVOKABLE void chooseHost(int index);
        Q_INVOKABLE void fetch();
        Q_INVOKABLE void go(const QString& name, int mode, int hunter);
        Q_INVOKABLE void leave();

    signals:
        void changed();
        void hostsChanged();

    private:
        using Maps = std::vector<std::pair<std::string, ::MphRead::GameMode>>;
        [[nodiscard]] bool Dedicated() const;
        void Refresh();
        void Say(QString text, QColor colour);
        void AskDirectories();
        void Arrived(const ::MphRead::Mods::Network::HostCandidate& candidate);
        void ShowHosts();
        void Fail(std::optional<std::string> why);
        void SetBusy(bool busy, QString label);
        void StartHere(std::string name, std::string player, ::MphRead::Hunter hunter, Maps maps);
        void StartOnServer(std::string name, std::string player, ::MphRead::Hunter hunter, ::MphRead::GameMode mode,
            Maps maps);

        bool _sample = false;
        int _kind = 0;
        QString _hostLabel = QStringLiteral("asking...");
        std::vector<std::string> _rotation;
        QString _note;
        QColor _noteColour;
        bool _busy = false;
        QString _goLabel = QStringLiteral("continue");
        bool _goEnabled = true;
        bool _fetchVisible = false;
        bool _progressVisible = false;
        double _fraction = 0;
        QString _stage;
        bool _asking = true;
        bool _finished = false;
        QVariantList _hosts;
        QString _hostsNote;
        QColor _hostsNoteColour;
        std::vector<::MphRead::Mods::Network::HostCandidate> _candidates;
        std::vector<::MphRead::Mods::Network::HostCandidate> _ordered;
        std::unique_ptr<::MphRead::Mods::Network::HostCandidate> _chosen;
        std::shared_ptr<std::stop_source> _work;
        std::shared_ptr<int> _lifetime = std::make_shared<int>(0);
    };
}
