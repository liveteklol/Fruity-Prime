#include "CreateServerModel.hpp"

#include "Await.hpp"
#include "PlayModel.hpp"
#include "ShellBridge.hpp"

#include "../../MphRead.Native/Entities/Players/PlayerEntity.hpp"
#include "../../MphRead.Native/Metadata/Metadata.hpp"
#include "../../MphRead.Native/Mods/DebugLog.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/LaunchPlan.hpp"
#include "../../MphRead.Native/Mods/Network/LocalServer.hpp"
#include "../../MphRead.Native/Mods/Network/MatchDefinition.hpp"
#include "../../MphRead.Native/Mods/Network/NetHostSession.hpp"
#include "../../MphRead.Native/Mods/Network/NetLaunch.hpp"
#include "../../MphRead.Native/Mods/Network/NetMaster.hpp"
#include "../../MphRead.Native/Mods/Network/NetProtocol.hpp"
#include "../../MphRead.Native/Mods/Network/NetSession.hpp"
#include "../../MphRead.Native/Mods/Update/UpdateCheck.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Globalization.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Runtime.hpp"

#include <algorithm>
#include <array>
#include <future>

namespace MphRead::Qt
{
    namespace
    {
        namespace Network = ::MphRead::Mods::Network;
        namespace Launcher = ::MphRead::Mods::Launcher;
        using Launcher::LauncherPrefs;

        const QColor TextDim(138, 147, 166);
        const QColor Warm(255, 179, 71);
        const QColor Bad(0xa8, 0x54, 0x54);

        [[nodiscard]] QString Q(const std::string& text)
        {
            return QString::fromStdString(text);
        }

        struct ModeOption
        {
            const char* Label;
            ::MphRead::GameMode Mode;
        };

        const std::array<ModeOption, 12> ModeTable{{
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

        // UiCapture.Fleet: what -uishot shows on the host page.
        [[nodiscard]] std::vector<Network::HostCandidate> Fleet()
        {
            return {
                Network::HostCandidate{"net.livetek.fr", "net.livetek.fr", 27889, true, true, 3},
                Network::HostCandidate{"Fruity Prime - West Europe", "20.16.135.109", 27889, true, std::nullopt, 39},
                Network::HostCandidate{"Fruity Prime - Japan", "13.78.14.98", 27889, false, std::nullopt, -1},
            };
        }

        [[nodiscard]] QString NobodyHosts()
        {
            return CreateServerModel::CanRunHere()
                ? QStringLiteral("No server will open one for you. Pick Dedicated server to run it here.")
                : QStringLiteral("No server will open one for you. Try again in a moment, or join somebody else's "
                                 "from the browser.");
        }
    }

    CreateServerModel::CreateServerModel(QObject* parent) : QObject(parent)
    {
        std::vector<std::string> rooms;
        if (ShellBridge* const bridge = ShellBridge::Current())
        {
            for (const QVariant& room : bridge->Rooms())
            {
                rooms.push_back(room.toMap().value(QStringLiteral("key")).toString().toStdString());
            }
        }
        if (_rotation.empty() && !rooms.empty())
        {
            _rotation.push_back(rooms.front());
        }
        Refresh();
        // After the properties the page sets: a capture never asks anybody.
        QTimer::singleShot(0, this, [this]()
        {
            if (!_sample && !PlayModel::UseSample)
            {
                AskDirectories();
            }
        });
    }

    CreateServerModel::~CreateServerModel()
    {
        if (_work != nullptr)
        {
            _work->request_stop();
        }
    }

    void CreateServerModel::SetSample(bool value)
    {
        if (_sample == value)
        {
            return;
        }
        _sample = value;
        if (_sample)
        {
            _candidates = Fleet();
            _asking = false;
            ShowHosts();
        }
        emit changed();
    }

    QString CreateServerModel::LobbyName() const
    {
        const std::string player = ::MphRead::NativeRuntime::StringTrim(LauncherPrefs::PlayerName());
        return Q(player.empty() ? std::string("Player") : player) + QStringLiteral("'s lobby");
    }

    QStringList CreateServerModel::Modes() const
    {
        QStringList modes;
        for (const ModeOption& mode : ModeTable)
        {
            modes.push_back(QString::fromLatin1(mode.Label));
        }
        return modes;
    }

    QStringList CreateServerModel::Hunters() const
    {
        QStringList hunters;
        for (std::int32_t i = 0; i < Launcher::Hunters::Playable; ++i)
        {
            hunters.push_back(Q(::MphRead::ToString(static_cast<::MphRead::Hunter>(i))));
        }
        hunters.push_back(Q(::MphRead::ToString(::MphRead::Hunter::Random)));
        return hunters;
    }

    int CreateServerModel::LastHunter() const
    {
        return std::max(0, static_cast<int>(Hunters().indexOf(Q(::MphRead::ToString(LauncherPrefs::LastHunter())))));
    }

    bool CreateServerModel::CanRunHere()
    {
        return !::MphRead::NativeRuntime::IsAndroid();
    }

    int CreateServerModel::MaxRotation()
    {
        return Network::HostRequestPacket::MaxRotation;
    }

    void CreateServerModel::SetKind(int value)
    {
        if (_kind == value)
        {
            return;
        }
        _kind = value;
        Refresh();
    }

    bool CreateServerModel::Dedicated() const
    {
        return CanRunHere() && _kind == 1;
    }

    QStringList CreateServerModel::Rotation() const
    {
        QStringList rotation;
        for (const std::string& room : _rotation)
        {
            rotation.push_back(Q(room));
        }
        return rotation;
    }

    void CreateServerModel::SetRotation(const QStringList& value)
    {
        _rotation.clear();
        for (const QString& room : value)
        {
            _rotation.push_back(room.toStdString());
        }
        emit changed();
    }

    QString CreateServerModel::roomName(const QString& room) const
    {
        const auto [meta, roomId] = ::MphRead::Metadata::GetRoomByName(room.toStdString());
        (void)roomId;
        return meta != nullptr ? Q(meta->InGameName.value_or(room.toStdString())) : room;
    }

    QString CreateServerModel::MapsLabel() const
    {
        if (_rotation.empty())
        {
            return QStringLiteral("none picked");
        }
        const QString first = roomName(Q(_rotation[0]));
        return _rotation.size() == 1 ? first
                                     : first + QStringLiteral(" +") + QString::number(_rotation.size() - 1)
                + QStringLiteral(" more");
    }

    void CreateServerModel::Say(QString text, QColor colour)
    {
        _note = std::move(text);
        _noteColour = colour;
        emit changed();
    }

    void CreateServerModel::Refresh()
    {
        if (!Dedicated())
        {
            _fetchVisible = false;
            _goEnabled = !_busy;
            if (_asking)
            {
                Say(QStringLiteral("Asking who can run one..."), TextDim);
            }
            else if (_chosen != nullptr)
            {
                Say(Q(_chosen->Label) + QStringLiteral(" opens the match on its own machine. Nothing to forward here."),
                    TextDim);
            }
            else
            {
                Say(NobodyHosts(), Warm);
            }
            return;
        }
        const bool ready = Network::LocalServer::Ready();
        _fetchVisible = !ready && !_busy;
        _goEnabled = ready && !_busy;
        if (!ready)
        {
            Say(Network::LocalServer::CanInstall()
                    ? Q(::MphRead::Mods::Update::UpdateCheck::ServerBinaryName())
                        + QStringLiteral(" is not here yet -- install it below.")
                    : QStringLiteral("No server package is published for this platform. Use Hosted."),
                Warm);
            return;
        }
        Say(QStringLiteral("Runs here, in its own window. Forward UDP ") + QString::number(Network::NetConfig::DefaultPort)
                + QStringLiteral(" to this PC for anyone outside to join; you join over 127.0.0.1 either way."),
            Warm);
    }

    void CreateServerModel::AskDirectories()
    {
        _asking = true;
        _candidates.clear();
        _chosen.reset();
        ShowHosts();
        const std::weak_ptr<int> alive = _lifetime;
        Network::NetMasterClient::FindHosts(LauncherPrefs::MasterHost(), LauncherPrefs::MasterPort(),
            [this, alive](Network::HostCandidate candidate)
            {
                Post(this, [this, alive, candidate = std::move(candidate)]()
                {
                    if (!alive.expired())
                    {
                        Arrived(candidate);
                    }
                });
            },
            [this, alive]()
            {
                Post(this, [this, alive]()
                {
                    if (alive.expired() || _finished)
                    {
                        return;
                    }
                    _asking = false;
                    ::MphRead::Mods::DebugLog::Line("net", std::to_string(_candidates.size()) + " host(s) asked, "
                        + std::to_string(std::count_if(_candidates.begin(), _candidates.end(),
                            [](const Network::HostCandidate& c) { return c.WillHost(); }))
                        + " can run a match");
                    if (_chosen == nullptr)
                    {
                        _hostLabel = QStringLiteral("nobody");
                    }
                    ShowHosts();
                    Refresh();
                });
            });
    }

    void CreateServerModel::Arrived(const Network::HostCandidate& candidate)
    {
        if (_finished)
        {
            return;
        }
        Network::NetMasterClient::Merge(_candidates, candidate);
        const auto best = std::find_if(_candidates.begin(), _candidates.end(),
            [](const Network::HostCandidate& entry) { return entry.WillHost(); });
        if (best != _candidates.end() && (_chosen == nullptr || !_chosen->WillHost()))
        {
            _chosen = std::make_unique<Network::HostCandidate>(*best);
            _hostLabel = Q(best->Label);
            _asking = false;
            Refresh();
        }
        ShowHosts();
    }

    void CreateServerModel::ShowHosts()
    {
        _ordered = _candidates;
        std::stable_sort(_ordered.begin(), _ordered.end(),
            [](const Network::HostCandidate& a, const Network::HostCandidate& b) { return a.WillHost() && !b.WillHost(); });
        _hosts.clear();
        int usable = 0;
        for (std::size_t i = 0; i < _ordered.size(); ++i)
        {
            const Network::HostCandidate& candidate = _ordered[i];
            usable += candidate.WillHost() ? 1 : 0;
            _hosts.push_back(QVariantMap{{QStringLiteral("title"), Q(candidate.Label)},
                {QStringLiteral("detail"), Q(candidate.Describe())},
                {QStringLiteral("usable"), candidate.WillHost()}, {QStringLiteral("index"), static_cast<int>(i)}});
        }
        if (_asking)
        {
            _hosts.push_back(QVariantMap{{QStringLiteral("note"), QStringLiteral("asking the rest...")},
                {QStringLiteral("colour"), TextDim}});
        }
        else if (_ordered.empty())
        {
            _hosts.push_back(QVariantMap{
                {QStringLiteral("note"), QStringLiteral("The directory did not answer, so there is no list of servers to ask.")},
                {QStringLiteral("colour"), Warm}});
        }
        if (usable > 0)
        {
            _hostsNote = QString::number(usable)
                + QStringLiteral(" can run a match for you. A server can open one when its admin allows it a port range.");
        }
        else if (_asking)
        {
            _hostsNote.clear();
        }
        else
        {
            _hostsNote = CanRunHere()
                ? QStringLiteral("None of these hosts are configured to create lobbies. The server admin can enable hosted "
                                 "lobbies with -hostports FIRST-LAST, or Dedicated server can run one on this machine.")
                : QStringLiteral("None of these hosts are configured to create lobbies. The server admin must enable a "
                                 "hosted-game port range.");
        }
        _hostsNoteColour = usable > 0 ? TextDim : Warm;
        emit hostsChanged();
    }

    void CreateServerModel::askAgain()
    {
        _hostLabel = QStringLiteral("asking...");
        if (_sample)
        {
            return;
        }
        AskDirectories();
        Refresh();
    }

    void CreateServerModel::chooseHost(int index)
    {
        if (index < 0 || index >= static_cast<int>(_ordered.size()) || !_ordered[static_cast<std::size_t>(index)].WillHost())
        {
            return;
        }
        _chosen = std::make_unique<Network::HostCandidate>(_ordered[static_cast<std::size_t>(index)]);
        _hostLabel = Q(_chosen->Label);
        Refresh();
    }

    void CreateServerModel::SetBusy(bool busy, QString label)
    {
        _busy = busy;
        _goLabel = std::move(label);
        _goEnabled = !busy;
        if (busy)
        {
            _fetchVisible = false;
        }
        emit changed();
    }

    void CreateServerModel::Fail(std::optional<std::string> why)
    {
        SetBusy(false, QStringLiteral("continue"));
        Refresh();
        Say(Q(why.value_or("that did not work")), Bad);
    }

    void CreateServerModel::leave()
    {
        _finished = true;
        if (_work != nullptr)
        {
            _work->request_stop();
        }
    }

    void CreateServerModel::fetch()
    {
        if (_busy || !Network::LocalServer::CanInstall())
        {
            return;
        }
        SetBusy(true, QStringLiteral("installing"));
        _progressVisible = true;
        _fraction = 0;
        _stage = QStringLiteral("Fetching the dedicated-server package");
        emit changed();
        const auto cancel = std::make_shared<std::stop_source>();
        _work = cancel;
        const std::weak_ptr<int> alive = _lifetime;
        auto task = std::async(std::launch::async, [this, alive, cancel]()
        {
            const std::stop_token token = cancel->get_token();
            return Network::LocalServer::Install([this, alive, cancel](float fraction)
            {
                Post(this, [this, alive, cancel, fraction]()
                {
                    if (!alive.expired() && !cancel->stop_requested())
                    {
                        _fraction = fraction < 0 ? 0 : fraction;
                        emit changed();
                    }
                });
            }, &token);
        }).share();
        Await(this, std::move(task), [this, cancel](bool ok)
        {
            if (cancel->stop_requested())
            {
                return;
            }
            _progressVisible = false;
            if (!ok)
            {
                Fail(Network::LocalServer::LastError().value_or("the package could not be installed"));
                return;
            }
            SetBusy(false, QStringLiteral("continue"));
            Refresh();
        });
    }

    void CreateServerModel::go(const QString& lobbyName, int modeIndex, int hunterIndex)
    {
        if (_finished || _busy)
        {
            return;
        }
        if (_rotation.empty())
        {
            Say(QStringLiteral("Pick at least one map first."), Warm);
            return;
        }
        std::string name = ::MphRead::NativeRuntime::StringTrim(lobbyName.toStdString());
        if (name.empty())
        {
            name = "Fruity lobby";
        }
        const ::MphRead::GameMode mode
            = ModeTable[static_cast<std::size_t>(std::clamp(modeIndex, 0, static_cast<int>(ModeTable.size()) - 1))].Mode;
        const auto hunter = static_cast<::MphRead::Hunter>(
            hunterIndex < Launcher::Hunters::Playable ? hunterIndex : static_cast<int>(::MphRead::Hunter::Random));
        Maps maps;
        for (const std::string& room : _rotation)
        {
            maps.emplace_back(room, mode);
        }
        std::string player = ::MphRead::NativeRuntime::StringTrim(LauncherPrefs::PlayerName());
        if (player.empty())
        {
            player = "Player";
        }
        LauncherPrefs::LastHunter(hunter);
        LauncherPrefs::Save();
        if (Dedicated())
        {
            StartHere(std::move(name), std::move(player), hunter, std::move(maps));
            return;
        }
        StartOnServer(std::move(name), std::move(player), hunter, mode, std::move(maps));
    }

    void CreateServerModel::StartHere(std::string name, std::string player, ::MphRead::Hunter hunter, Maps maps)
    {
        SetBusy(true, QStringLiteral("starting"));
        Say(QStringLiteral("Starting ") + Q(name) + QStringLiteral(" on this machine..."), TextDim);
        const auto cancel = std::make_shared<std::stop_source>();
        _work = cancel;
        auto task = std::async(std::launch::async, [name, maps, cancel]()
        {
            return Network::LocalServer::Start(name, maps, ::MphRead::Entities::PlayerEntity::SlotCapacity, 7 * 60,
                Network::MatchGoalRules::DefaultValue(maps[0].second), LauncherPrefs::MasterHost(),
                LauncherPrefs::MasterPort(), LauncherPrefs::ListHostedGame(), cancel->get_token(), true);
        }).share();
        Await(this, std::move(task), [this, name, player, hunter, maps](std::int32_t port)
        {
            if (port < 0)
            {
                Fail(Network::LocalServer::LastError().value_or("the server would not start"));
                return;
            }
            Say(QStringLiteral("Joining your server on 127.0.0.1:") + QString::number(port) + QStringLiteral("..."),
                TextDim);
            auto join = std::async(std::launch::async, [port, player, hunter]()
            {
                return Network::NetLaunch::Connect("127.0.0.1", port, player, hunter, 8000, -1,
                    Network::LocalServer::OwnerToken());
            }).share();
            Await(this, std::move(join), [this, name, player, hunter, maps, port](bool joined)
            {
                if (!joined)
                {
                    // The process stays up for anyone already joining; only
                    // this client's failed connection stops.
                    Network::NetSession::Stop();
                    Fail(Network::NetLaunch::LastJoinError() + " -- the server is running; try joining 127.0.0.1:"
                        + std::to_string(port) + " from the browser.");
                    return;
                }
                LauncherPrefs::ServerAddress("127.0.0.1");
                LauncherPrefs::ServerPort(port);
                LauncherPrefs::LastKind(static_cast<std::int32_t>(Launcher::LaunchKind::Online));
                LauncherPrefs::Save();
                Launcher::LaunchPlan::Init init;
                init.Kind = Launcher::LaunchKind::Online;
                init.Lobby = std::make_shared<Launcher::LobbyContext>(name,
                    "Local server \xC2\xB7 port " + std::to_string(port), true);
                init.Hunter = hunter;
                init.RoomKey = std::string();
                init.Mode = maps[0].second;
                init.Port = port;
                init.PlayerName = player;
                _finished = true;
                _busy = false;
                if (ShellBridge* const bridge = ShellBridge::Current())
                {
                    bridge->Launch(Launcher::LaunchPlan(init));
                }
            });
        });
    }

    void CreateServerModel::StartOnServer(std::string name, std::string player, ::MphRead::Hunter hunter,
        ::MphRead::GameMode mode, Maps maps)
    {
        if (_chosen == nullptr)
        {
            Say(NobodyHosts(), Warm);
            return;
        }
        const std::string host = _chosen->Host;
        const std::int32_t port = _chosen->Port;
        SetBusy(true, QStringLiteral("starting"));
        Say(QStringLiteral("Asking ") + Q(host) + QStringLiteral(" to open your lobby..."), TextDim);
        auto task = std::async(std::launch::async, [host, port, name, mode, maps]()
        {
            return Network::NetMasterClient::RequestGame(host, port, maps[0].first, mode, 7 * 60,
                Network::MatchGoalRules::DefaultValue(mode), ::MphRead::Entities::PlayerEntity::SlotCapacity, name, 6000,
                std::optional<Maps>(maps), Network::ServerSessionPolicy::Lobby);
        }).share();
        Await(this, std::move(task), [this, name, player, hunter, mode, host](Network::HostedGame game)
        {
            if (!game.Started)
            {
                Fail(game.Reason.empty() ? std::optional<std::string>(host + " would not open a game")
                                         : std::optional<std::string>(game.Reason));
                return;
            }
            auto join = std::async(std::launch::async, [game, player, hunter]()
            {
                return Network::NetLaunch::Connect(game.Host, game.Port, player, hunter, 8000, -1, game.OwnerToken);
            }).share();
            Await(this, std::move(join), [this, name, player, hunter, mode, game](bool joined)
            {
                if (!joined)
                {
                    Network::NetSession::Stop();
                    Network::NetHostSession::Stop();
                    Fail(Network::NetLaunch::LastJoinError());
                    return;
                }
                LauncherPrefs::ServerAddress(game.Host);
                LauncherPrefs::ServerPort(game.Port);
                LauncherPrefs::LastKind(static_cast<std::int32_t>(Launcher::LaunchKind::Host));
                LauncherPrefs::Save();
                Launcher::LaunchPlan::Init init;
                init.Kind = Launcher::LaunchKind::Host;
                init.Lobby = std::make_shared<Launcher::LobbyContext>(name, game.Host + ":" + std::to_string(game.Port));
                init.Hunter = hunter;
                init.RoomKey = std::string();
                init.Mode = mode;
                init.Port = game.Port;
                init.PlayerName = player;
                _finished = true;
                _busy = false;
                if (ShellBridge* const bridge = ShellBridge::Current())
                {
                    bridge->Launch(Launcher::LaunchPlan(init));
                }
            });
        });
    }
}
