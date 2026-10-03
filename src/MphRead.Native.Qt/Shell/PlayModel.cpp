#include "PlayModel.hpp"

#include "ShellBridge.hpp"

#include "../../MphRead.Native/Entities/Players/PlayerEntity.hpp"
#include "../../MphRead.Native/Metadata/Metadata.hpp"
#include "../../MphRead.Native/Mods/Branding.hpp"
#include "../../MphRead.Native/Mods/HunterSuits.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/AdventureSave.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/LaunchPlan.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/NativeFilePicker.hpp"
#include "../../MphRead.Native/Mods/Network/DemoFile.hpp"
#include "../../MphRead.Native/Mods/Network/DemoLibrary.hpp"
#include "../../MphRead.Native/Mods/Network/DemoPlayback.hpp"
#include "../../MphRead.Native/Mods/Network/MapVote.hpp"
#include "../../MphRead.Native/Mods/Network/NetLaunch.hpp"
#include "../../MphRead.Native/Mods/Network/NetMaster.hpp"
#include "../../MphRead.Native/Mods/Network/NetSession.hpp"
#include "../../MphRead.Native/Mods/Network/NetStatus.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Globalization.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Number.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Tasks.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <utility>

namespace MphRead::Qt
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;
        namespace Network = ::MphRead::Mods::Network;
        namespace Launcher = ::MphRead::Mods::Launcher;

        // GuiTheme's voices for the note.
        const QColor TextDim(138, 147, 166);
        const QColor Warm(255, 179, 71);
        const QColor Good(0x5f, 0x9e, 0x72);
        const QColor Warn(0xc0, 0x8a, 0x3e);
        const QColor Bad(0xa8, 0x54, 0x54);

        struct Mode
        {
            const char* Label;
            ::MphRead::GameMode Value;
        };

        // PlayScreen._modes, in its order.
        const std::array<Mode, 12> ModeTable{{
            {"Battle", ::MphRead::GameMode::Battle},
            {"Battle teams", ::MphRead::GameMode::BattleTeams},
            {"Survival", ::MphRead::GameMode::Survival},
            {"Survival teams", ::MphRead::GameMode::SurvivalTeams},
            {"Capture", ::MphRead::GameMode::Capture},
            {"Bounty", ::MphRead::GameMode::Bounty},
            {"Bounty teams", ::MphRead::GameMode::BountyTeams},
            {"Defender", ::MphRead::GameMode::Defender},
            {"Defender teams", ::MphRead::GameMode::DefenderTeams},
            {"Nodes", ::MphRead::GameMode::Nodes},
            {"Nodes teams", ::MphRead::GameMode::NodesTeams},
            {"Prime hunter", ::MphRead::GameMode::PrimeHunter},
        }};

        // PlayScreen._hunters: the seven, then Random.
        const std::array<::MphRead::Hunter, 8> HunterTable{{
            ::MphRead::Hunter::Samus, ::MphRead::Hunter::Kanden, ::MphRead::Hunter::Trace,
            ::MphRead::Hunter::Sylux, ::MphRead::Hunter::Noxus, ::MphRead::Hunter::Spire,
            ::MphRead::Hunter::Weavel, ::MphRead::Hunter::Random,
        }};

        [[nodiscard]] ::MphRead::Hunter HunterAt(int index)
        {
            return HunterTable[static_cast<std::size_t>(std::clamp(index, 0, 7))];
        }

        [[nodiscard]] QString Q(const std::string& text)
        {
            return QString::fromStdString(text);
        }

        [[nodiscard]] QString RoomName(const std::string& key)
        {
            const auto [meta, ignored] = ::MphRead::Metadata::GetRoomByName(key);
            (void)ignored;
            return Q(meta != nullptr && meta->InGameName.has_value() && !meta->InGameName->empty()
                ? *meta->InGameName : key);
        }

        [[nodiscard]] QColor PingColour(int ms)
        {
            return ms < 60 ? Good : ms < 120 ? Warn : Bad;
        }

        [[nodiscard]] bool ParseEndpoint(std::string text, std::string& host, std::int32_t& port)
        {
            text = Runtime::StringTrim(text);
            if (text.empty())
            {
                return false;
            }
            const std::size_t colon = text.find_last_of(':');
            if (colon == std::string::npos || colon == 0)
            {
                host = text;
                return true;
            }
            std::int32_t parsed = 0;
            if (!Runtime::Int32TryParseInvariant(std::string_view(text).substr(colon + 1), parsed)
                || parsed < 1 || parsed > 65535)
            {
                return false;
            }
            host = text.substr(0, colon);
            port = parsed;
            return true;
        }

        [[nodiscard]] std::pair<std::string, std::int32_t> Endpoint(const QString& typed)
        {
            std::string host = Launcher::LauncherPrefs::ServerAddress();
            std::int32_t port = Launcher::LauncherPrefs::ServerPort();
            (void)ParseEndpoint(typed.toStdString(), host, port);
            return {host, port};
        }

        [[nodiscard]] std::string Describe(const Network::ServerStatus& status)
        {
            const std::string players = status.MaxPlayers > 0
                ? std::to_string(status.Players) + "/" + std::to_string(status.MaxPlayers)
                : std::to_string(status.Players);
            const std::string ping = status.Latency >= 0 ? std::to_string(status.Latency) + " ms" : "-- ms";
            return status.RoomKey + " (" + Network::NetStatus::ModeName(status.Mode) + ") "
                + players + " players, " + ping;
        }

        // Run on the UI thread, if the model is still there.
        template <typename Work>
        void Post(QPointer<PlayModel> model, std::weak_ptr<int> lifetime, Work work)
        {
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                [model, lifetime, work = std::move(work)]() mutable
                {
                    if (model != nullptr && lifetime.lock() != nullptr)
                    {
                        work(*model);
                    }
                }, ::Qt::QueuedConnection);
        }

        struct Sample
        {
            const char* Name;
            const char* Endpoint;
            const char* Room;
            ::MphRead::GameMode Mode;
            int Players;
            int MaxPlayers;
            int Ping;
        };

        // UiCapture.BrowserRows.
        const std::array<Sample, 13> Samples{{
            {"Combat Hall 24/7", "82.66.14.9:27888", "MP3 PROVING GROUND", ::MphRead::GameMode::Battle, 4, 8, 24},
            {"Prime EU #1", "85.214.228.188:27888", "AD2 MAGMA VENTS", ::MphRead::GameMode::BountyTeams, 7, 8, 48},
            {"Arcterra Pickup", "83.19.146.2:27890", "MP9 CRYOCHASM", ::MphRead::GameMode::Survival, 2, 6, 91},
            {"MPH Speedrun", "51.140.44.19:27888", "AD1 TRANSFER LOCK DM", ::MphRead::GameMode::PrimeHunter, 1, 4, 168},
            {"hunters.us.west", "34.94.2.11:27888", "MP8 FIRE CONTROL", ::MphRead::GameMode::BattleTeams, 6, 8, 212},
            {"Sic Transit 24/7", "91.198.174.192:27888", "MP12 SIC TRANSIT", ::MphRead::GameMode::Capture, 3, 8, 57},
            {"LAN - Pi 4", "192.168.1.42:27888", "MP2 HARVESTER", ::MphRead::GameMode::Capture, 0, 8, 3},
            {"Ice Hive Rotation", "80.50.24.3:27888", "MP9 CRYOCHASM", ::MphRead::GameMode::Nodes, 4, 8, 74},
            {"Head Shot only", "92.222.10.7:27888", "MP6 HEADSHOT", ::MphRead::GameMode::Battle, 5, 6, 31},
            {"Elder Passage.CTF", "54.230.11.9:27888", "MP4 HIGHGROUND - EXPANDED", ::MphRead::GameMode::Capture, 5, 8, 143},
            {"Data Shrine.1v1", "217.160.0.153:27888", "MP1 SANCTORUS", ::MphRead::GameMode::Battle, 2, 2, 39},
            {"Fuel Stack Rotation", "51.148.31.4:27888", "MP13 ACCELERATOR", ::MphRead::GameMode::Bounty, 2, 8, 66},
            {"old.vesper", "45.33.32.156:27888", "", ::MphRead::GameMode::Battle, 0, 0, -1},
        }};
    }

    bool PlayModel::UseSample = false;

    PlayModel::PlayModel(QObject* parent)
        : QObject(parent), _noteColour(TextDim)
    {
        _poll.setInterval(4000);
        QObject::connect(&_poll, &QTimer::timeout, this, [this]() { queryStatus(_endpoint); });
    }

    PlayModel::~PlayModel()
    {
        _lifetime.reset();
        if (_statusCancel != nullptr)
        {
            _statusCancel->request_stop();
        }
    }

    void PlayModel::SetFace(int face)
    {
        if (face == _face && !_servers.isEmpty())
        {
            return;
        }
        _face = face;
        emit faceChanged();
        Rebuild();
    }

    void PlayModel::Rebuild()
    {
        stopPolling();
        _servers.clear();
        emit serversChanged();
        _asked = _replied = _live = 0;
        SetNote(QString(), TextDim);
        switch (_face)
        {
        case 0:
            SetBusy(false, QStringLiteral("join"));
            reloadServers();
            StartPolling();
            break;
        case 1:
            SetBusy(false, QStringLiteral("start"));
            if (ShellBridge::Current() != nullptr && ShellBridge::Current()->Rooms().isEmpty())
            {
                SetNote(QStringLiteral("No multiplayer rooms were found. Set the game files up from Settings."), Warm);
            }
            break;
        case 2:
            SetBusy(false, QStringLiteral("start"));
            BuildStory();
            break;
        case 3:
            SetBusy(false, QStringLiteral("watch"));
            BuildClips();
            break;
        default:
            SetBusy(false, QStringLiteral("call the vote"));
            SetNote(QStringLiteral("Nobody has picked. The rotation decides."), TextDim);
            break;
        }
    }

    void PlayModel::say(const QString& text, const QColor& colour)
    {
        SetNote(text, colour.isValid() ? colour : TextDim);
    }

    void PlayModel::SetNote(QString text, QColor colour)
    {
        _note = std::move(text);
        _noteColour = colour;
        emit noteChanged();
    }

    void PlayModel::SetBusy(bool busy, QString label)
    {
        _busy = busy;
        _goLabel = std::move(label);
        emit busyChanged();
    }

    QStringList PlayModel::Modes() const
    {
        QStringList modes;
        for (const Mode& mode : ModeTable)
        {
            modes.push_back(QString::fromUtf8(mode.Label));
        }
        return modes;
    }

    QStringList PlayModel::Hunters() const
    {
        QStringList hunters;
        for (const ::MphRead::Hunter hunter : HunterTable)
        {
            hunters.push_back(Q(::MphRead::ToString(hunter)));
        }
        return hunters;
    }

    QStringList PlayModel::Bots() const
    {
        QStringList bots;
        for (int i = 0; i < ::MphRead::Entities::PlayerEntity::SlotCapacity; ++i)
        {
            bots.push_back(QString::number(i));
        }
        return bots;
    }

    int PlayModel::LastHunter() const
    {
        const ::MphRead::Hunter last = Launcher::LauncherPrefs::LastHunter();
        const auto found = std::find(HunterTable.begin(), HunterTable.end(), last);
        return found == HunterTable.end() ? 0 : static_cast<int>(found - HunterTable.begin());
    }

    int PlayModel::LastColour() const
    {
        return std::clamp(Launcher::LauncherPrefs::LastColor(), 0, 3);
    }

    int PlayModel::LastBots() const
    {
        return Launcher::LauncherPrefs::Bots();
    }

    int PlayModel::LastSkill() const
    {
        return Launcher::LauncherPrefs::BotLevel();
    }

    QString PlayModel::PlayerName() const
    {
        const std::string name = Runtime::StringTrim(Launcher::LauncherPrefs::PlayerName());
        return name.empty() ? QStringLiteral("Player") : Q(name);
    }

    QString PlayModel::ServerEndpoint() const
    {
        return Q(Launcher::LauncherPrefs::ServerAddress() + ":"
            + std::to_string(Launcher::LauncherPrefs::ServerPort()));
    }

    QString PlayModel::ChosenRoom() const
    {
        const ShellBridge* const bridge = ShellBridge::Current();
        return bridge != nullptr ? bridge->RoomKey() : QString();
    }

    QString PlayModel::VotingRoom() const
    {
        const std::optional<Network::MatchStatePacket> match = Network::NetSession::ServerMatch();
        if (!match.has_value())
        {
            return {};
        }
        return Q(match->RoomKey.value_or(""));
    }

    QColor PlayModel::suitColour(int hunter, int suit) const
    {
        const ::MphRead::Hunter which = HunterAt(hunter);
        if (which == ::MphRead::Hunter::Random)
        {
            return Warm;
        }
        try
        {
            const ::MphRead::ColorRgba sampled = ::MphRead::Mods::HunterSuits::Color(which, suit);
            return QColor(sampled.Red, sampled.Green, sampled.Blue);
        }
        catch (const std::exception&)
        {
            return Warm;
        }
    }

    // ------------------------------------------------------------ Online

    void PlayModel::StartPolling()
    {
        queryStatus(_endpoint.isEmpty() ? ServerEndpoint() : _endpoint);
        _poll.start();
    }

    void PlayModel::stopPolling()
    {
        _poll.stop();
        if (_statusCancel != nullptr)
        {
            _statusCancel->request_stop();
            _statusCancel.reset();
        }
    }

    void PlayModel::reloadServers()
    {
        _servers.clear();
        _replied = 0;
        _live = 0;
        emit serversChanged();
        if (UseSample)
        {
            for (const Sample& sample : Samples)
            {
                QVariantMap row;
                row.insert(QStringLiteral("name"), QString::fromUtf8(sample.Name));
                row.insert(QStringLiteral("endpoint"), QString::fromUtf8(sample.Endpoint));
                row.insert(QStringLiteral("asking"), true);
                _servers.push_back(row);
                Network::ServerStatus status;
                status.Online = sample.Ping >= 0;
                status.RoomKey = sample.Room;
                status.Mode = sample.Mode;
                status.Players = sample.Players;
                status.MaxPlayers = sample.MaxPlayers;
                status.Latency = sample.Ping;
                SetStatus(static_cast<int>(_servers.size()) - 1, status);
            }
            _asked = static_cast<int>(Samples.size());
            _replied = _asked;
            emit serversChanged();
            Answered();
            return;
        }

        SetNote(Q("Asking " + Launcher::LauncherPrefs::MasterHost() + "..."), TextDim);
        const QPointer<PlayModel> self(this);
        const std::weak_ptr<int> lifetime = _lifetime;
        Runtime::TaskRun([self, lifetime]()
        {
            const Network::MasterListResult result = Network::NetMasterClient::Query(
                Launcher::LauncherPrefs::MasterHost(), Launcher::LauncherPrefs::MasterPort());
            Post(self, lifetime, [result](PlayModel& model)
            {
                if (model._finished || model._face != 0)
                {
                    return;
                }
                if (!result.Answered)
                {
                    model.SetNote(QStringLiteral("The directory did not answer. It may be down, or UDP "
                        "may not reach it. The address beside the list still works."), Warm);
                    return;
                }
                if (result.Servers == nullptr || result.Servers->empty())
                {
                    model.SetNote(QStringLiteral("The directory is up and has nobody listed."), Warm);
                    return;
                }
                model._asked = static_cast<int>(result.Servers->size());
                model.SetNote(QStringLiteral("asking %1 servers…").arg(model._asked), TextDim);
                for (const Network::MasterListing& listing : *result.Servers)
                {
                    model.AddServer(listing);
                }
            });
        });
    }

    void PlayModel::AddServer(const Network::MasterListing& listing)
    {
        QVariantMap row;
        row.insert(QStringLiteral("name"), Q(listing.ServerName.empty() ? listing.Endpoint() : listing.ServerName));
        row.insert(QStringLiteral("endpoint"), Q(listing.Endpoint()));
        row.insert(QStringLiteral("asking"), true);
        const int index = static_cast<int>(_servers.size());
        _servers.push_back(row);
        emit serversChanged();
        const QPointer<PlayModel> self(this);
        const std::weak_ptr<int> lifetime = _lifetime;
        Runtime::TaskRun([self, lifetime, index, address = listing.Address, port = listing.Port]()
        {
            const Network::ServerStatus status = Network::NetStatus::Query(address, port, false);
            Post(self, lifetime, [index, status](PlayModel& model)
            {
                if (index >= model._servers.size())
                {
                    return;
                }
                model.SetStatus(index, status);
                ++model._replied;
                if (status.Online)
                {
                    ++model._live;
                }
                emit model.serversChanged();
                model.Answered();
            });
        });
    }

    void PlayModel::SetStatus(int index, const Network::ServerStatus& status)
    {
        QVariantMap row = _servers[index].toMap();
        row.insert(QStringLiteral("asking"), false);
        row.insert(QStringLiteral("answered"), status.Online);
        if (!status.Online)
        {
            row.insert(QStringLiteral("map"), QStringLiteral("did not answer"));
            row.insert(QStringLiteral("mode"), QString());
            row.insert(QStringLiteral("players"), QStringLiteral("—"));
            row.insert(QStringLiteral("ping"), QStringLiteral("—"));
            row.insert(QStringLiteral("pingColour"), Bad);
            row.insert(QStringLiteral("roomKey"), QString());
        }
        else
        {
            row.insert(QStringLiteral("roomKey"), Q(status.RoomKey));
            row.insert(QStringLiteral("map"), RoomName(status.RoomKey));
            row.insert(QStringLiteral("mode"), Q(Network::NetStatus::ModeName(status.Mode)));
            row.insert(QStringLiteral("players"), status.MaxPlayers > 0
                ? QStringLiteral("%1/%2").arg(status.Players).arg(status.MaxPlayers)
                : QString::number(status.Players));
            row.insert(QStringLiteral("ping"), status.Latency >= 0 ? QString::number(status.Latency)
                                                                   : QStringLiteral("—"));
            row.insert(QStringLiteral("pingColour"), status.Latency >= 0 ? PingColour(status.Latency) : TextDim);
        }
        _servers[index] = row;
    }

    void PlayModel::Answered()
    {
        if (UseSample)
        {
            _live = 0;
            for (const QVariant& row : std::as_const(_servers))
            {
                _live += row.toMap().value(QStringLiteral("answered")).toBool() ? 1 : 0;
            }
        }
        SetNote(_replied < _asked
            ? QStringLiteral("asking %1 servers… %2 answered").arg(_asked).arg(_replied)
            : QStringLiteral("%1 of %2 answered. Click one to join.").arg(_live).arg(_asked), TextDim);
    }

    void PlayModel::queryStatus(const QString& endpoint)
    {
        _endpoint = endpoint;
        if (_face != 0 || UseSample)
        {
            return;
        }
        if (_statusCancel != nullptr)
        {
            _statusCancel->request_stop();
        }
        _statusCancel = std::make_shared<std::stop_source>();
        const std::stop_token token = _statusCancel->get_token();
        const auto [host, port] = Endpoint(endpoint);
        const QPointer<PlayModel> self(this);
        const std::weak_ptr<int> lifetime = _lifetime;
        Runtime::TaskRun([self, lifetime, token, host, port]()
        {
            const Network::ServerStatus status = Network::NetStatus::Query(host, port, true);
            if (token.stop_requested())
            {
                return;
            }
            Post(self, lifetime, [token, host, port, status](PlayModel& model)
            {
                if (token.stop_requested() || model._finished || model._face != 0 || model._asked > 0)
                {
                    return;
                }
                const std::string where = host + ":" + std::to_string(port);
                model.SetNote(Q(status.Online ? where + " -- " + Describe(status)
                                              : where + " -- no answer. It may be off, or UDP may be blocked."),
                    status.Online ? Good : Warm);
            });
        });
    }

    void PlayModel::join(const QString& typedName, const QString& endpoint, int hunterIndex, int suit)
    {
        if (_busy)
        {
            return;
        }
        const auto [host, port] = Endpoint(endpoint);
        const std::string typed = Runtime::StringTrim(typedName.toStdString());
        const std::string name = typed.empty() ? PlayerName().toStdString() : typed;
        const ::MphRead::Hunter hunter = HunterAt(hunterIndex);
        stopPolling();
        SetBusy(true, QStringLiteral("joining"));
        SetNote(Q("Connecting to " + host + ":" + std::to_string(port) + "..."), TextDim);

        Launcher::LauncherPrefs::PlayerName(name);
        Launcher::LauncherPrefs::LastHunter(hunter);
        Launcher::LauncherPrefs::LastColor(suit);
        Launcher::LauncherPrefs::ServerAddress(host);
        Launcher::LauncherPrefs::ServerPort(port);
        Launcher::LauncherPrefs::LastKind(static_cast<std::int32_t>(Launcher::LaunchKind::Online));
        Launcher::LauncherPrefs::Save();

        const QPointer<PlayModel> self(this);
        const std::weak_ptr<int> lifetime = _lifetime;
        Runtime::TaskRun([self, lifetime, host, port, name, hunter]()
        {
            const bool joined = Network::NetLaunch::Connect(host, port, name, hunter);
            Post(self, lifetime, [port, name, hunter, joined](PlayModel& model)
            {
                model.SetBusy(false, QStringLiteral("join"));
                if (!joined)
                {
                    Network::NetSession::Stop();
                    model.SetNote(Q(Network::NetLaunch::LastJoinError()), Bad);
                    model.StartPolling();
                    return;
                }
                Launcher::LaunchPlan::Init init;
                init.Kind = Launcher::LaunchKind::Online;
                init.Hunter = hunter;
                init.PlayerName = name;
                init.RoomKey = "";
                init.Mode = ::MphRead::GameMode::Battle;
                init.Port = port;
                model._finished = true;
                if (ShellBridge* const bridge = ShellBridge::Current())
                {
                    bridge->Launch(Launcher::LaunchPlan(init));
                }
            });
        });
    }

    void PlayModel::sessionEnded(const QString& reason)
    {
        _finished = false;
        SetNote(reason, Bad);
        StartPolling();
    }

    // ----------------------------------------------------------- Offline

    void PlayModel::start(const QString& room, int modeIndex, int hunterIndex, int suit, int bots, int skill)
    {
        if (room.isEmpty())
        {
            SetNote(QStringLiteral("Pick a map first."), Warm);
            return;
        }
        const ::MphRead::Hunter hunter = HunterAt(hunterIndex);
        const ::MphRead::GameMode mode = ModeTable[static_cast<std::size_t>(std::clamp(modeIndex, 0, 11))].Value;
        ShellBridge* const bridge = ShellBridge::Current();
        if (bridge != nullptr)
        {
            bridge->SetRoomKey(room);
        }
        Launcher::LauncherPrefs::LastHunter(hunter);
        Launcher::LauncherPrefs::LastColor(suit);
        Launcher::LauncherPrefs::Bots(bots);
        Launcher::LauncherPrefs::BotLevel(skill);
        Launcher::LauncherPrefs::LastKind(static_cast<std::int32_t>(Launcher::LaunchKind::Offline));
        Launcher::LauncherPrefs::Save();

        Launcher::LaunchPlan::Init init;
        init.Kind = Launcher::LaunchKind::Offline;
        init.Hunter = hunter;
        init.PlayerName = Launcher::LauncherPrefs::PlayerName();
        init.RoomKey = room.toStdString();
        init.Mode = mode;
        init.Bots = bots;
        init.BotLevel = skill;
        _finished = true;
        stopPolling();
        if (bridge != nullptr)
        {
            bridge->Launch(Launcher::LaunchPlan(init));
        }
    }

    // ------------------------------------------------------------- Story

    void PlayModel::BuildStory()
    {
        _slots.clear();
        for (std::uint8_t slot = 1; slot <= Launcher::AdventureSave::SlotCount; ++slot)
        {
            const Launcher::AdventureSave::SlotInfo info = Launcher::AdventureSave::Read(slot);
            QVariantMap row;
            row.insert(QStringLiteral("title"), QStringLiteral("Slot %1").arg(slot));
            row.insert(QStringLiteral("detail"), Q(info.Describe()));
            row.insert(QStringLiteral("slot"), static_cast<int>(slot));
            row.insert(QStringLiteral("used"), info.Used);
            _slots.push_back(row);
        }
        emit slotsChanged();
    }

    bool PlayModel::slotUsed(int slot) const
    {
        return Launcher::AdventureSave::Read(static_cast<std::uint8_t>(slot)).Used;
    }

    void PlayModel::startAdventure(int slot, int hunterIndex, bool newGame)
    {
        const bool used = slotUsed(slot);
        const ::MphRead::Hunter hunter = HunterAt(hunterIndex);
        Launcher::LauncherPrefs::LastHunter(hunter);
        Launcher::LauncherPrefs::LastKind(static_cast<std::int32_t>(Launcher::LaunchKind::Adventure));
        Launcher::LauncherPrefs::Save();
        Launcher::LaunchPlan::Init init;
        init.Kind = Launcher::LaunchKind::Adventure;
        init.Hunter = hunter;
        init.PlayerName = Launcher::LauncherPrefs::PlayerName();
        init.RoomKey = "";
        init.SaveSlot = static_cast<std::uint8_t>(slot);
        init.NewGame = !used || newGame;
        _finished = true;
        if (ShellBridge* const bridge = ShellBridge::Current())
        {
            bridge->Launch(Launcher::LaunchPlan(init));
        }
    }

    // ------------------------------------------------------------- Clips

    void PlayModel::BuildClips()
    {
        _clips.clear();
        const std::shared_ptr<const std::vector<Network::DemoRecording>> demos = Network::DemoLibrary::List();
        if (demos != nullptr)
        {
            for (const Network::DemoRecording& demo : *demos)
            {
                QVariantMap row;
                row.insert(QStringLiteral("title"), Q(demo.Room().empty() ? demo.FileName() : demo.Room()));
                row.insert(QStringLiteral("detail"), Q(Network::DemoLibrary::Describe(demo)));
                row.insert(QStringLiteral("path"), Q(demo.Path()));
                _clips.push_back(row);
            }
        }
        if (demos == nullptr || demos->empty())
        {
            SetNote(Q("Nothing recorded yet. Clips are made from the pause menu during an online match, "
                "and are written to:\n" + Network::DemoLibrary::Directory()), TextDim);
        }
        QVariantMap open;
        open.insert(QStringLiteral("title"), QStringLiteral("Open a file..."));
        open.insert(QStringLiteral("detail"), QStringLiteral("a demo from somewhere else on this device"));
        open.insert(QStringLiteral("path"), QString());
        _clips.push_back(open);
        emit clipsChanged();
    }

    void PlayModel::watch(const QString& path)
    {
        if (path.isEmpty())
        {
            importClip();
            return;
        }
        SetBusy(true, QStringLiteral("loading"));
        const QPointer<PlayModel> self(this);
        const std::weak_ptr<int> lifetime = _lifetime;
        Runtime::TaskRun([self, lifetime, file = path.toStdString()]()
        {
            const bool joined = Network::DemoPlayback::Join(file);
            Post(self, lifetime, [file, joined](PlayModel& model)
            {
                model.SetBusy(false, QStringLiteral("watch"));
                if (!joined)
                {
                    const std::optional<std::string> error = Network::DemoPlayback::LastError();
                    model.SetNote(Q(error.value_or("That file could not be read as a demo.")), Bad);
                    return;
                }
                Launcher::LaunchPlan::Init init;
                init.Kind = Launcher::LaunchKind::Demo;
                init.DemoPath = file;
                init.Hunter = ::MphRead::Hunter::Samus;
                init.PlayerName = "";
                init.RoomKey = "";
                model._finished = true;
                if (ShellBridge* const bridge = ShellBridge::Current())
                {
                    bridge->Launch(Launcher::LaunchPlan(init));
                }
            });
        });
    }

    void PlayModel::importClip()
    {
        if (!Launcher::NativeFilePicker::Available())
        {
            SetNote(Q("This desktop has no file dialog to open (install zenity or kdialog). Clips in "
                + Network::DemoLibrary::Directory() + " are listed here without one."), Bad);
            return;
        }
        std::string extension(Network::DemoFile::Extension);
        if (!extension.empty() && extension.front() == '.')
        {
            extension.erase(extension.begin());
        }
        std::shared_future<std::optional<std::string>> chosen = Launcher::NativeFilePicker::OpenFile(
            "Clips", std::string(::MphRead::Mods::Branding::Name) + " demo", extension);
        const QPointer<PlayModel> self(this);
        const std::weak_ptr<int> lifetime = _lifetime;
        Runtime::TaskRun([self, lifetime, chosen]()
        {
            const std::optional<std::string> file = chosen.get();
            Post(self, lifetime, [file](PlayModel& model)
            {
                if (file.has_value())
                {
                    model.watch(Q(*file));
                }
            });
        });
    }

    // -------------------------------------------------------------- Vote

    void PlayModel::propose(const QString& room)
    {
        if (room.isEmpty())
        {
            return;
        }
        _finished = true;
        Network::MapVote::Propose(std::optional<std::string>(room.toStdString()));
    }
}
