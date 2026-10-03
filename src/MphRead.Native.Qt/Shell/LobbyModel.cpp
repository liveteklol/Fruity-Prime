#include "LobbyModel.hpp"

#include "PlayModel.hpp"
#include "ShellBridge.hpp"

#include "../../MphRead.Native/GameState.hpp"
#include "../../MphRead.Native/Menu.hpp"
#include "../../MphRead.Native/Metadata/Metadata.hpp"
#include "../../MphRead.Native/Mods/Chat/NetChat.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/LaunchPlan.hpp"
#include "../../MphRead.Native/Mods/Multiplayer/TeamLayout.hpp"
#include "../../MphRead.Native/Mods/Network/LobbyRules.hpp"
#include "../../MphRead.Native/Mods/Network/NetHostSession.hpp"
#include "../../MphRead.Native/Mods/Network/NetProtocol.hpp"
#include "../../MphRead.Native/Mods/Network/NetSession.hpp"
#include "../../MphRead.Native/Mods/Network/SessionProtocol.hpp"
#include "../../MphRead.Native/Mods/ThumbnailGenerator.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Globalization.hpp"
#include "../../MphRead.Native/NativeRuntime/System/IO.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Managed.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Number.hpp"

#include <QtCore/QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace MphRead::Qt
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;
        namespace Network = ::MphRead::Mods::Network;
        namespace Launcher = ::MphRead::Mods::Launcher;
        using TeamLayout = ::MphRead::Mods::Multiplayer::TeamLayout;
        using Session = Network::NetSession;

        [[nodiscard]] QString Q(const std::string& text)
        {
            return QString::fromStdString(text);
        }

        struct GameType
        {
            const char* Label;
            ::MphRead::GameMode Free;
            ::MphRead::GameMode Team;
            bool TeamOnly;
            bool FfaOnly;
        };

        const std::array<GameType, 7> GameTypes{{
            {"Battle", ::MphRead::GameMode::Battle, ::MphRead::GameMode::BattleTeams, false, false},
            {"Survival", ::MphRead::GameMode::Survival, ::MphRead::GameMode::SurvivalTeams, false, false},
            {"Bounty", ::MphRead::GameMode::Bounty, ::MphRead::GameMode::BountyTeams, false, false},
            {"Defender", ::MphRead::GameMode::Defender, ::MphRead::GameMode::DefenderTeams, false, false},
            {"Nodes", ::MphRead::GameMode::Nodes, ::MphRead::GameMode::NodesTeams, false, false},
            {"Capture", ::MphRead::GameMode::Capture, ::MphRead::GameMode::Capture, true, false},
            {"Prime Hunter", ::MphRead::GameMode::PrimeHunter, ::MphRead::GameMode::PrimeHunter, false, true},
        }};

        struct MatchupOption
        {
            const char* Label;
            Network::MatchFormat Format;
        };

        const std::array<MatchupOption, 8> MatchupTable{{
            {"FFA", Network::MatchFormat::FreeForAll},
            {"Teams", Network::MatchFormat::Auto},
            {"1v1", Network::MatchFormat::OneVsOne},
            {"2v2", Network::MatchFormat::TwoVsTwo},
            {"3v3", Network::MatchFormat::ThreeVsThree},
            {"4v4", Network::MatchFormat::FourVsFour},
            {"2v2v2v2", Network::MatchFormat::TwoVsTwoVsTwoVsTwo},
            {"Custom", Network::MatchFormat::Custom},
        }};

        const std::array<const char*, 7> ToggleLabels{{"Friendly fire", "Affinity weapons", "Shadow freeze",
            "Opponent health", "Require ready", "Join in progress", "Lock teams"}};

        [[nodiscard]] std::string RoomName(const std::string& room)
        {
            const auto [metadata, roomId] = ::MphRead::Metadata::GetRoomByName(room);
            (void)roomId;
            return metadata != nullptr ? metadata->InGameName.value_or(room) : room;
        }

        [[nodiscard]] std::string Minutes(std::uint16_t seconds)
        {
            const double minutes = seconds / 60.0;
            return Runtime::ToStringInvariant(minutes, minutes == std::trunc(minutes) ? "0" : "0.##");
        }

        [[nodiscard]] bool PlayerChoosesTeam(const Network::MatchDefinition& match)
        {
            return ::MphRead::GameState::IsTeamMode(match.Mode) && match.Format != Network::MatchFormat::OneVsOne;
        }

        [[nodiscard]] std::string GoalLabelOf(::MphRead::GameMode mode)
        {
            switch (mode)
            {
            case ::MphRead::GameMode::Survival:
            case ::MphRead::GameMode::SurvivalTeams: return "Lives";
            case ::MphRead::GameMode::Bounty:
            case ::MphRead::GameMode::BountyTeams: return "Bounty goal";
            case ::MphRead::GameMode::Capture: return "Captures";
            case ::MphRead::GameMode::Defender:
            case ::MphRead::GameMode::DefenderTeams: return "Hold time (minutes)";
            case ::MphRead::GameMode::Nodes:
            case ::MphRead::GameMode::NodesTeams: return "Node score";
            case ::MphRead::GameMode::PrimeHunter: return "Prime time (minutes)";
            default: return "Score goal";
            }
        }

        [[nodiscard]] std::string GoalDisplay(::MphRead::GameMode mode, std::uint16_t value)
        {
            if (Network::MatchGoalRules::UsesLives(mode))
            {
                return Runtime::ToStringInvariant(static_cast<std::uint32_t>(value) + 1);
            }
            if (Network::MatchGoalRules::UsesTimeTarget(mode))
            {
                return Minutes(value);
            }
            return Runtime::ToStringInvariant(value);
        }

        [[nodiscard]] int MatchupIndex(Network::MatchFormat format)
        {
            const auto found = std::find_if(MatchupTable.begin(), MatchupTable.end(),
                [format](const MatchupOption& option) { return option.Format == format; });
            return found == MatchupTable.end() ? 0 : static_cast<int>(found - MatchupTable.begin());
        }

        [[nodiscard]] int MatchupIndex(const Network::MatchDefinition& match)
        {
            if (match.Format == Network::MatchFormat::Auto && !::MphRead::GameState::IsTeamMode(match.Mode))
            {
                return MatchupIndex(Network::MatchFormat::FreeForAll);
            }
            return MatchupIndex(match.Format);
        }

        [[nodiscard]] int BaseModeIndex(::MphRead::GameMode mode)
        {
            const auto found = std::find_if(GameTypes.begin(), GameTypes.end(),
                [mode](const GameType& type) { return type.Free == mode || type.Team == mode; });
            return found == GameTypes.end() ? 0 : static_cast<int>(found - GameTypes.begin());
        }
    }

    LobbyModel::LobbyModel(QObject* parent) : QObject(parent)
    {
        _hunter = static_cast<int>(Session::LocalHunter());
        _suit = Session::LocalColor();
        _timer.setInterval(1000 / 30);
        connect(&_timer, &QTimer::timeout, this, [this]() { Tick(); });
        if (PlayModel::UseSample)
        {
            // -uishot: a roster to look at, and no session to lose.
            const auto player = [](const char* state, bool ready, const char* name, const char* detail)
            {
                return QVariantMap{{QStringLiteral("state"), QString::fromLatin1(state)}, {QStringLiteral("ready"), ready},
                    {QStringLiteral("name"), QString::fromLatin1(name)}, {QStringLiteral("detail"), QString::fromUtf8(detail)}};
            };
            _players = {player("READY", true, "Player  [OWNER]", "Samus \xC2\xB7 S1 \xC2\xB7 Team A \xC2\xB7 12 ms"),
                player("WAIT", false, "Livetek", "Sylux \xC2\xB7 S3 \xC2\xB7 Team B \xC2\xB7 38 ms")};
            _targets = {QStringLiteral("Livetek")};
            _owner = true;
            _ownerEnabled = true;
            _playerEnabled = true;
            _chooseTeams = true;
            _teamEnabled = true;
            _mode = 0;
            _matchup = 3;
            _draftRoom = "MP3 PROVING GROUND";
            _status = QStringLiteral("Everyone must be ready.");
            _chat = QStringLiteral("Livetek: gg\nPlayer: one more?");
            RefreshDraft();
            return;
        }
        _timer.start();
        Refresh();
    }

    LobbyModel::~LobbyModel() = default;

    QString LobbyModel::Title() const
    {
        const ShellBridge* const bridge = ShellBridge::Current();
        const std::shared_ptr<Launcher::LobbyContext> context
            = bridge != nullptr && bridge->LobbyPlan().has_value() ? bridge->LobbyPlan()->Lobby() : nullptr;
        return context != nullptr && !context->ServerName.empty()
            ? Q(Runtime::ToUpperInvariant(context->ServerName + " LOBBY")) : QStringLiteral("LOBBY");
    }

    QStringList LobbyModel::Hunters() const
    {
        QStringList hunters;
        for (std::int32_t i = 0; i < Launcher::Hunters::Playable; ++i)
        {
            hunters.push_back(Q(::MphRead::ToString(static_cast<::MphRead::Hunter>(i))));
        }
        return hunters;
    }

    QStringList LobbyModel::Modes() const
    {
        QStringList modes;
        for (const GameType& type : GameTypes)
        {
            modes.push_back(QString::fromLatin1(type.Label));
        }
        return modes;
    }

    QStringList LobbyModel::Matchups() const
    {
        QStringList matchups;
        for (const MatchupOption& option : MatchupTable)
        {
            matchups.push_back(QString::fromLatin1(option.Label));
        }
        return matchups;
    }

    void LobbyModel::SetTarget(int value)
    {
        _target = value;
    }

    void LobbyModel::SetMoveTeam(int value)
    {
        _moveTeam = value;
    }

    QString LobbyModel::Room() const
    {
        return Q(_draftRoom);
    }

    QString LobbyModel::MapName() const
    {
        return Q(RoomName(_draftRoom));
    }

    QString LobbyModel::mapShot() const
    {
        try
        {
            const std::string path = ::MphRead::Mods::ThumbnailGenerator::PathFor(_draftRoom);
            if (!Runtime::StringIsNullOrWhiteSpace(_draftRoom) && Runtime::FileExists(path))
            {
                return QUrl::fromLocalFile(Q(path)).toString();
            }
        }
        catch (const std::exception&)
        {
            // A thumbnail is presentation only; the server validates the map.
        }
        return {};
    }

    QString LobbyModel::GoalLabel() const
    {
        return Q(GoalLabelOf(DraftMatch().Mode));
    }

    QVariantList LobbyModel::Toggles() const
    {
        QVariantList toggles;
        const bool lockShown = PlayerChoosesTeam(DraftMatch());
        for (std::size_t i = 0; i < ToggleLabels.size(); ++i)
        {
            toggles.push_back(QVariantMap{{QStringLiteral("label"), QString::fromLatin1(ToggleLabels[i])},
                {QStringLiteral("on"), _toggles[i]}, {QStringLiteral("shown"), i != 6 || lockShown}});
        }
        return toggles;
    }

    bool LobbyModel::CustomTeamsShown() const
    {
        return DraftMatch().Format == Network::MatchFormat::Custom;
    }

    QString LobbyModel::CustomTeams() const
    {
        return Q(TeamLayout(_custom[0], _custom[1], _custom[2], _custom[3], _custom[4]).ToString());
    }

    QVariantList LobbyModel::CustomLayout() const
    {
        return {static_cast<int>(_custom[0]), static_cast<int>(_custom[1]), static_cast<int>(_custom[2]),
            static_cast<int>(_custom[3]), static_cast<int>(_custom[4])};
    }

    int LobbyModel::MaxPlayers() const
    {
        const std::optional<Network::SessionStatePacket>& session = Session::ServerSession();
        return std::clamp(session.has_value() ? static_cast<int>(session->MaxPlayers) : 8, 2, 8);
    }

    bool LobbyModel::SummaryShown() const
    {
        return DraftMatch().Format != Network::MatchFormat::OneVsOne;
    }

    bool LobbyModel::canEdit() const
    {
        return Session::CanEditLobby() && !Session::LobbyCommandPending();
    }

    void LobbyModel::Tick()
    {
        if (_closed || _launched)
        {
            return;
        }
        Session::Pump();
        if (Session::Refused() || Session::SessionTimedOut() || !Session::Active())
        {
            const std::string reason = Session::Refused()
                ? Session::RefusedReason().Describe(std::optional<std::string>("Server"))
                : "The connection to the server was lost.";
            leave(Q(reason));
            return;
        }
        Refresh();
        TryAutoApply();
        if (Session::ShouldLoadMatch())
        {
            _launched = true;
            _timer.stop();
            const Network::MatchDefinition match = Session::ActiveMatchDefinition().value();
            Launcher::LaunchPlan::Init init;
            init.Kind = Launcher::LaunchKind::Online;
            init.Hunter = Session::LocalHunter();
            init.PlayerName = Session::PlayerName();
            init.RoomKey = match.RoomKey;
            init.Mode = match.Mode;
            if (ShellBridge* const bridge = ShellBridge::Current())
            {
                bridge->StartMatch(Launcher::LaunchPlan(init));
            }
        }
    }

    void LobbyModel::Refresh()
    {
        const std::optional<Network::SessionStatePacket>& sessionValue = Session::ServerSession();
        if (!sessionValue.has_value())
        {
            return;
        }
        const Network::SessionStatePacket session = *sessionValue;
        _hunter = static_cast<int>(Session::LocalHunter());
        _suit = Session::LocalColor();
        const Network::RosterPacket roster = Session::LobbyRoster();
        _rosterCount = roster.Count;

        if (_shownRevision != session.Revision || _shownRosterRevision != roster.Revision
            || Session::Clock() >= _nextPingRefresh)
        {
            _shownRevision = session.Revision;
            _shownRosterRevision = roster.Revision;
            _nextPingRefresh = Session::Clock() + 1;
            std::uint8_t selected = std::numeric_limits<std::uint8_t>::max();
            if (_target >= 0 && static_cast<std::size_t>(_target) < _targetSlots.size())
            {
                selected = _targetSlots[static_cast<std::size_t>(_target)];
            }
            _players.clear();
            _targetSlots.clear();
            _targets.clear();
            const bool showTeam = session.Match.Format != Network::MatchFormat::OneVsOne;
            for (std::int32_t i = 0; i < roster.Count; ++i)
            {
                // LobbyPlayerRow.
                const std::uint8_t slot = Runtime::ManagedAt(roster.Slots, i);
                const std::int8_t teamValue = Runtime::ManagedAt(roster.Teams, i);
                const std::string team = teamValue < 0 ? std::string("FFA")
                                                       : std::string("Team ") + static_cast<char>('A' + teamValue);
                const bool ready = Runtime::ManagedAt(roster.LobbyReady, i);
                const std::string name = Runtime::ManagedAt(roster.Names, i).value_or(std::string{});
                const auto hunter = static_cast<::MphRead::Hunter>(Runtime::ManagedAt(roster.Hunters, i));
                const std::int32_t suit = static_cast<std::int32_t>(Runtime::ManagedAt(roster.Colors, i)) + 1;
                const std::uint16_t ping = Runtime::ManagedAt(roster.Pings, i);
                const std::string detail = ::MphRead::ToString(hunter) + " \xC2\xB7 S" + std::to_string(suit)
                    + (showTeam ? " \xC2\xB7 " + team : std::string{}) + " \xC2\xB7 " + std::to_string(ping) + " ms";
                _players.push_back(QVariantMap{{QStringLiteral("state"), ready ? QStringLiteral("READY") : QStringLiteral("WAIT")},
                    {QStringLiteral("ready"), ready},
                    {QStringLiteral("name"), Q(name + (slot == session.OwnerSlot ? "  [OWNER]" : ""))},
                    {QStringLiteral("detail"), Q(detail)}});
                if (slot != Session::LocalSlot())
                {
                    _targetSlots.push_back(slot);
                    _targets.push_back(Q(name));
                }
            }
            const auto at = std::find(_targetSlots.begin(), _targetSlots.end(), selected);
            _target = at == _targetSlots.end() ? 0 : static_cast<int>(at - _targetSlots.begin());
        }

        const bool matchChanged = _shownMatch == nullptr || *_shownMatch != session.Match
            || _shownRules != static_cast<std::int32_t>(session.RuleFlags);
        if (matchChanged)
        {
            _shownMatch = std::make_unique<Network::MatchDefinition>(session.Match);
            _shownRules = static_cast<std::int32_t>(session.RuleFlags);
            _draftRoom = session.Match.RoomKey.value_or(std::string{});
            _mode = BaseModeIndex(session.Match.Mode);
            _matchup = MatchupIndex(session.Match);
            _time = Q(Minutes(session.Match.TimeLimitSeconds));
            _goal = Q(GoalDisplay(session.Match.Mode, session.Match.PointGoal));
            _toggles = {session.Match.FriendlyFire, session.Match.AffinityWeapons, session.Match.ShadowFreeze,
                !session.Match.HideOpponentHealth, session.RequireReady(), session.AllowJoinInProgress(),
                PlayerChoosesTeam(session.Match) && session.LockTeams()};
            const TeamLayout layout = Network::LobbyRules::ResolveTeamLayout(session.Match);
            const TeamLayout custom = session.Match.CustomTeams.IsValid() ? session.Match.CustomTeams
                : layout.IsValid() ? layout : TeamLayout(2, 2, 2);
            _custom = {custom.TeamCount, custom.TeamA, custom.TeamB, custom.TeamC, custom.TeamD};
            _teams = {QStringLiteral("Auto")};
            for (std::int32_t team = 0; team < layout.TeamCount; ++team)
            {
                _teams.push_back(QStringLiteral("Team ") + QChar(static_cast<char16_t>('A' + team)));
            }
            _moveTeam = 0;
        }

        _ownerEnabled = Session::CanEditLobby() && !Session::LobbyCommandPending();
        _chooseTeams = PlayerChoosesTeam(session.Match);
        const std::int32_t localSlot = Session::LocalSlot();
        if (localSlot >= 0)
        {
            _team = Session::SlotTeamIndex[static_cast<std::size_t>(localSlot)] + 1;
        }
        _playerEnabled = Session::IsInLobby() && !Session::LobbyCommandPending();
        _owner = Session::LocalIsLobbyOwner();
        _teamEnabled = _chooseTeams && _playerEnabled && (!session.LockTeams() || _owner);
        _readyLabel = localSlot >= 0 && Session::SlotLobbyReady[static_cast<std::size_t>(localSlot)]
            ? QStringLiteral("Unready") : QStringLiteral("Ready");
        std::string reason;
        const Network::LobbyResultCode valid
            = Network::LobbyRules::Validate(session.Match, roster, session.RequireReady(), reason);
        _startEnabled = Session::CanEditLobby() && valid == Network::LobbyResultCode::Ok && !Session::LobbyCommandPending();
        _status = Q(Session::ConnectionLost() ? std::string("Connection lost, retrying...")
                : !Session::LobbyMessage().empty() ? Session::LobbyMessage()
                : session.Phase == Network::SessionPhase::Lobby ? reason
                : std::string("Waiting for players to finish loading..."));

        const std::int32_t chatRevision = ::MphRead::Mods::Chat::NetChat::Revision();
        if (_chatRevision != chatRevision)
        {
            _chatRevision = chatRevision;
            const std::vector<std::string>& history = ::MphRead::Mods::Chat::NetChat::History();
            QStringList lines;
            for (std::size_t i = history.size() > 12 ? history.size() - 12 : 0; i < history.size(); ++i)
            {
                lines.push_back(Q(history[i]));
            }
            _chat = lines.join(QLatin1Char('\n'));
            emit chatChanged();
        }
        RefreshDraft();
        emit changed();
        if (matchChanged)
        {
            emit draftChanged();
        }
    }

    void LobbyModel::Identify()
    {
        if (!Session::IsInLobby())
        {
            return;
        }
        Session::SetLocalHunter(static_cast<::MphRead::Hunter>(_hunter));
        Session::SetLocalColor(_suit);
        Launcher::LauncherPrefs::LastHunter(Session::LocalHunter());
        Launcher::LauncherPrefs::LastColor(Session::LocalColor());
        Launcher::LauncherPrefs::Save();
        Session::SendIdentify();
    }

    void LobbyModel::setHunter(int index)
    {
        _hunter = index;
        Identify();
    }

    void LobbyModel::setSuit(int index)
    {
        _suit = index;
        Identify();
    }

    void LobbyModel::setTeam(int index)
    {
        const std::int32_t slot = Session::LocalSlot();
        if (slot >= 0)
        {
            Session::SendLobbyCommand(Network::LobbyCommandType::SetTeam, static_cast<std::uint8_t>(slot),
                static_cast<std::int8_t>(index - 1));
        }
    }

    Network::MatchDefinition LobbyModel::DraftMatch() const
    {
        const Network::MatchFormat format
            = MatchupTable[static_cast<std::size_t>(std::clamp(_matchup, 0, static_cast<int>(MatchupTable.size()) - 1))].Format;
        const GameType& type = GameTypes[static_cast<std::size_t>(std::clamp(_mode, 0, static_cast<int>(GameTypes.size()) - 1))];
        const bool teams = format != Network::MatchFormat::FreeForAll;
        Network::MatchDefinition match;
        match.RoomKey = _draftRoom;
        match.Mode = type.FfaOnly ? type.Free : type.TeamOnly ? type.Team : teams ? type.Team : type.Free;
        match.Format = format;
        match.CustomTeams = TeamLayout(_custom[0], _custom[1], _custom[2], _custom[3], _custom[4]);
        return match;
    }

    void LobbyModel::MatchChoiceChanged(bool resetGoal)
    {
        const GameType& type = GameTypes[static_cast<std::size_t>(std::clamp(_mode, 0, static_cast<int>(GameTypes.size()) - 1))];
        const Network::MatchFormat format = MatchupTable[static_cast<std::size_t>(_matchup)].Format;
        if (type.FfaOnly && format != Network::MatchFormat::FreeForAll)
        {
            _matchup = MatchupIndex(Network::MatchFormat::FreeForAll);
        }
        else if (type.TeamOnly
            && (format == Network::MatchFormat::FreeForAll || format == Network::MatchFormat::TwoVsTwoVsTwoVsTwo))
        {
            _matchup = MatchupIndex(Network::MatchFormat::Auto);
        }
        if (resetGoal)
        {
            const ::MphRead::GameMode mode = DraftMatch().Mode;
            _goal = Q(GoalDisplay(mode, Network::MatchGoalRules::DefaultValue(mode)));
        }
        DraftChanged();
    }

    void LobbyModel::setMode(int index)
    {
        _mode = index;
        MatchChoiceChanged(true);
    }

    void LobbyModel::setMatchup(int index)
    {
        _matchup = index;
        MatchChoiceChanged(false);
    }

    void LobbyModel::setTime(const QString& text)
    {
        _time = text;
        DraftChanged();
    }

    void LobbyModel::setGoal(const QString& text)
    {
        _goal = text;
        DraftChanged();
    }

    void LobbyModel::setToggle(int index, bool on)
    {
        if (index >= 0 && index < static_cast<int>(_toggles.size()))
        {
            _toggles[static_cast<std::size_t>(index)] = on;
            DraftChanged();
        }
    }

    void LobbyModel::setRoom(const QString& room)
    {
        _draftRoom = room.toStdString();
        DraftChanged();
    }

    void LobbyModel::setCustomTeams(int count, const QVariantList& sizes)
    {
        count = std::clamp(count, 2, 4);
        const auto size = [&sizes](int team) { return static_cast<std::uint8_t>(sizes.value(team).toInt()); };
        _custom = {static_cast<std::uint8_t>(count), size(0), size(1),
            count > 2 ? size(2) : std::uint8_t{0}, count > 3 ? size(3) : std::uint8_t{0}};
        DraftChanged();
    }

    void LobbyModel::DraftChanged()
    {
        _draftDirty = true;
        _draftChangedAt = Session::Clock();
        RefreshDraft();
        emit draftChanged();
    }

    void LobbyModel::RefreshDraft()
    {
        Network::MatchDefinition configured;
        std::string reason;
        const bool valid = TryBuildMatch(configured, reason);
        const TeamLayout layout = Network::LobbyRules::ResolveTeamLayout(configured);
        QString summary;
        if (!valid)
        {
            summary = Q(reason);
        }
        else if (_draftDirty)
        {
            summary = QStringLiteral("Changes save automatically.");
        }
        else if (layout.TeamCount == 0)
        {
            summary = QStringLiteral("Free for all");
        }
        else
        {
            const std::string roster = Network::LobbyRules::ExactTeams(configured)
                ? std::to_string(layout.TotalPlayers()) + " players" : std::string("flexible roster");
            summary = Q("Teams: " + layout.ToString() + " \xC2\xB7 " + roster);
        }
        if (summary != _summary)
        {
            _summary = summary;
            emit draftChanged();
        }
        if (!valid)
        {
            _startEnabled = false;
        }
    }

    bool LobbyModel::TryBuildMatch(Network::MatchDefinition& match, std::string& reason) const
    {
        match = DraftMatch();
        if (Network::LobbyRules::ValidateDefinition(match, reason) != Network::LobbyResultCode::Ok)
        {
            return false;
        }
        const TeamLayout layout = Network::LobbyRules::ResolveTeamLayout(match);
        const std::optional<Network::SessionStatePacket>& session = Session::ServerSession();
        if (layout.TeamCount > 0 && (layout.TotalPlayers() < _rosterCount
            || (Network::LobbyRules::ExactTeams(match)
                && layout.TotalPlayers() > (session.has_value() ? session->MaxPlayers : 8))))
        {
            reason = "The matchup must fit the connected players and server limit.";
            return false;
        }
        double minutes = 0;
        if (!Runtime::DoubleTryParseInvariant(_time.toStdString(), minutes) || !std::isfinite(minutes) || minutes < 0
            || minutes * 60 > std::numeric_limits<std::uint16_t>::max())
        {
            reason = "Match time must be minutes from 0 to 1092.25.";
            return false;
        }
        const auto seconds = static_cast<std::uint16_t>(std::round(minutes * 60));
        std::uint16_t goal = 0;
        const std::string text = _goal.toStdString();
        const Runtime::NumberFormatInfo& invariant = Runtime::NumberFormatInfo::InvariantInfo();
        if (Network::MatchGoalRules::UsesLives(match.Mode))
        {
            std::int32_t lives = 0;
            if (!Runtime::TryParseInteger(text, Runtime::NumberStyles::None, invariant, lives) || lives < 1
                || lives > static_cast<std::int32_t>(std::numeric_limits<std::uint16_t>::max()) + 1)
            {
                reason = "Lives must be a whole number from 1 to 65536.";
                return false;
            }
            goal = static_cast<std::uint16_t>(lives - 1);
        }
        else if (Network::MatchGoalRules::UsesTimeTarget(match.Mode))
        {
            double goalMinutes = 0;
            if (!Runtime::DoubleTryParseInvariant(text, goalMinutes) || !std::isfinite(goalMinutes) || goalMinutes <= 0
                || goalMinutes * 60 > std::numeric_limits<std::uint16_t>::max())
            {
                reason = GoalLabelOf(match.Mode) + " must be greater than 0 and at most 1092.25.";
                return false;
            }
            goal = static_cast<std::uint16_t>(std::max(1.0, std::round(goalMinutes * 60)));
        }
        else if (!Runtime::TryParseInteger(text, Runtime::NumberStyles::None, invariant, goal))
        {
            reason = GoalLabelOf(match.Mode) + " must be a whole number from 0 to 65535.";
            return false;
        }
        match.TimeLimitSeconds = seconds;
        match.PointGoal = goal;
        match.FriendlyFire = _toggles[0];
        match.AffinityWeapons = _toggles[1];
        match.ShadowFreeze = _toggles[2];
        match.HideOpponentHealth = !_toggles[3];
        return true;
    }

    void LobbyModel::TryAutoApply()
    {
        if (!_draftDirty || Session::LobbyCommandPending() || !Session::CanEditLobby()
            || Session::Clock() - _draftChangedAt < 0.25)
        {
            return;
        }
        const std::optional<Network::SessionStatePacket>& current = Session::ServerSession();
        if (!current.has_value())
        {
            return;
        }
        Network::MatchDefinition match;
        std::string reason;
        if (!TryBuildMatch(match, reason))
        {
            return;
        }
        Network::SessionStatePacket config = *current;
        config.Match = match;
        config.RuleFlags = match.Rules();
        if (_toggles[4])
        {
            config.RuleFlags |= Network::SessionRules::RequireReady;
        }
        if (_toggles[5])
        {
            config.RuleFlags |= Network::SessionRules::AllowJoinInProgress;
        }
        if (PlayerChoosesTeam(match) && _toggles[6])
        {
            config.RuleFlags |= Network::SessionRules::LockTeams;
        }
        if (Session::SendLobbyCommand(Network::LobbyCommandType::UpdateMatch, 255, -1, false, config))
        {
            _draftDirty = false;
            _summary = QStringLiteral("Saving changes...");
            emit draftChanged();
        }
    }

    void LobbyModel::toggleReady()
    {
        const std::int32_t slot = Session::LocalSlot();
        if (slot >= 0)
        {
            Session::SendLobbyCommand(Network::LobbyCommandType::SetReady, 255, -1,
                !Session::SlotLobbyReady[static_cast<std::size_t>(slot)]);
        }
    }

    void LobbyModel::startMatch()
    {
        Session::SendLobbyCommand(Network::LobbyCommandType::StartMatch);
    }

    void LobbyModel::sendChat(const QString& text)
    {
        ::MphRead::Mods::Chat::NetChat::Send(text.toStdString());
    }

    void LobbyModel::admin(int action)
    {
        static const std::array<Network::LobbyCommandType, 3> commands{Network::LobbyCommandType::SetTeam,
            Network::LobbyCommandType::TransferOwner, Network::LobbyCommandType::KickPlayer};
        if (action < 0 || action > 2 || _target < 0 || static_cast<std::size_t>(_target) >= _targetSlots.size())
        {
            return;
        }
        Session::SendLobbyCommand(commands[static_cast<std::size_t>(action)],
            _targetSlots[static_cast<std::size_t>(_target)], static_cast<std::int8_t>(_moveTeam - 1));
    }

    void LobbyModel::leave(const QString& reason)
    {
        if (_closed)
        {
            return;
        }
        _closed = true;
        _timer.stop();
        Session::Stop();
        Network::NetHostSession::Stop();
        emit closed(reason);
    }
}
