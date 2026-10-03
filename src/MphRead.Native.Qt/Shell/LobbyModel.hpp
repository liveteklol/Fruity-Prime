#pragma once

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::Network
{
    struct MatchDefinition;
}

namespace MphRead::Qt
{
    // LobbyScreen: a persistent lobby's roster, the player's own choices, the
    // owner's match draft (saved as it changes) and administration, the chat,
    // and the hand-off to the match when the server loads one. Everything the
    // page shows is here; the page only draws it and reports edits.
    class LobbyModel : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(QString title READ Title CONSTANT)
        Q_PROPERTY(QStringList hunters READ Hunters CONSTANT)
        Q_PROPERTY(QStringList modes READ Modes CONSTANT)
        Q_PROPERTY(QStringList matchups READ Matchups CONSTANT)
        Q_PROPERTY(QVariantList players READ Players NOTIFY changed)
        Q_PROPERTY(QStringList targets READ Targets NOTIFY changed)
        Q_PROPERTY(int target READ Target WRITE SetTarget NOTIFY changed)
        Q_PROPERTY(QStringList teams READ Teams NOTIFY changed)
        Q_PROPERTY(int hunter READ Hunter NOTIFY changed)
        Q_PROPERTY(int suit READ Suit NOTIFY changed)
        Q_PROPERTY(int team READ Team NOTIFY changed)
        Q_PROPERTY(int moveTeam READ MoveTeam WRITE SetMoveTeam NOTIFY changed)
        Q_PROPERTY(bool chooseTeams READ ChooseTeams NOTIFY changed)
        Q_PROPERTY(bool playerEnabled READ PlayerEnabled NOTIFY changed)
        Q_PROPERTY(bool teamEnabled READ TeamEnabled NOTIFY changed)
        Q_PROPERTY(bool owner READ Owner NOTIFY changed)
        Q_PROPERTY(bool ownerEnabled READ OwnerEnabled NOTIFY changed)
        Q_PROPERTY(QString readyLabel READ ReadyLabel NOTIFY changed)
        Q_PROPERTY(bool startEnabled READ StartEnabled NOTIFY changed)
        Q_PROPERTY(QString status READ Status NOTIFY changed)
        Q_PROPERTY(QString chat READ Chat NOTIFY chatChanged)
        // The draft.
        Q_PROPERTY(QString room READ Room NOTIFY draftChanged)
        Q_PROPERTY(QString mapName READ MapName NOTIFY draftChanged)
        Q_PROPERTY(int mode READ Mode NOTIFY draftChanged)
        Q_PROPERTY(int matchup READ Matchup NOTIFY draftChanged)
        Q_PROPERTY(QString time READ Time NOTIFY draftChanged)
        Q_PROPERTY(QString goal READ Goal NOTIFY draftChanged)
        Q_PROPERTY(QString goalLabel READ GoalLabel NOTIFY draftChanged)
        Q_PROPERTY(QVariantList toggles READ Toggles NOTIFY draftChanged)
        Q_PROPERTY(bool customTeamsShown READ CustomTeamsShown NOTIFY draftChanged)
        Q_PROPERTY(QString customTeams READ CustomTeams NOTIFY draftChanged)
        Q_PROPERTY(QVariantList customLayout READ CustomLayout NOTIFY draftChanged)
        Q_PROPERTY(int maxPlayers READ MaxPlayers NOTIFY changed)
        Q_PROPERTY(QString summary READ Summary NOTIFY draftChanged)
        Q_PROPERTY(bool summaryShown READ SummaryShown NOTIFY draftChanged)

    public:
        explicit LobbyModel(QObject* parent = nullptr);
        ~LobbyModel() override;

        [[nodiscard]] QString Title() const;
        [[nodiscard]] QStringList Hunters() const;
        [[nodiscard]] QStringList Modes() const;
        [[nodiscard]] QStringList Matchups() const;
        [[nodiscard]] QVariantList Players() const { return _players; }
        [[nodiscard]] QStringList Targets() const { return _targets; }
        [[nodiscard]] int Target() const noexcept { return _target; }
        void SetTarget(int value);
        [[nodiscard]] QStringList Teams() const { return _teams; }
        [[nodiscard]] int Hunter() const noexcept { return _hunter; }
        [[nodiscard]] int Suit() const noexcept { return _suit; }
        [[nodiscard]] int Team() const noexcept { return _team; }
        [[nodiscard]] int MoveTeam() const noexcept { return _moveTeam; }
        void SetMoveTeam(int value);
        [[nodiscard]] bool ChooseTeams() const noexcept { return _chooseTeams; }
        [[nodiscard]] bool PlayerEnabled() const noexcept { return _playerEnabled; }
        [[nodiscard]] bool TeamEnabled() const noexcept { return _teamEnabled; }
        [[nodiscard]] bool Owner() const noexcept { return _owner; }
        [[nodiscard]] bool OwnerEnabled() const noexcept { return _ownerEnabled; }
        [[nodiscard]] QString ReadyLabel() const { return _readyLabel; }
        [[nodiscard]] bool StartEnabled() const noexcept { return _startEnabled; }
        [[nodiscard]] QString Status() const { return _status; }
        [[nodiscard]] QString Chat() const { return _chat; }
        [[nodiscard]] QString Room() const;
        [[nodiscard]] QString MapName() const;
        [[nodiscard]] int Mode() const noexcept { return _mode; }
        [[nodiscard]] int Matchup() const noexcept { return _matchup; }
        [[nodiscard]] QString Time() const { return _time; }
        [[nodiscard]] QString Goal() const { return _goal; }
        [[nodiscard]] QString GoalLabel() const;
        [[nodiscard]] QVariantList Toggles() const;
        [[nodiscard]] bool CustomTeamsShown() const;
        [[nodiscard]] QString CustomTeams() const;
        [[nodiscard]] QVariantList CustomLayout() const;
        [[nodiscard]] int MaxPlayers() const;
        [[nodiscard]] QString Summary() const { return _summary; }
        [[nodiscard]] bool SummaryShown() const;

        Q_INVOKABLE void setHunter(int index);
        Q_INVOKABLE void setSuit(int index);
        Q_INVOKABLE void setTeam(int index);
        Q_INVOKABLE void setMode(int index);
        Q_INVOKABLE void setMatchup(int index);
        Q_INVOKABLE void setTime(const QString& text);
        Q_INVOKABLE void setGoal(const QString& text);
        Q_INVOKABLE void setToggle(int index, bool on);
        Q_INVOKABLE void setRoom(const QString& room);
        // Teams 2-4 and each team's size.
        Q_INVOKABLE void setCustomTeams(int count, const QVariantList& sizes);
        Q_INVOKABLE bool canEdit() const;
        Q_INVOKABLE void toggleReady();
        Q_INVOKABLE void startMatch();
        Q_INVOKABLE void sendChat(const QString& text);
        // 0 move to team, 1 transfer ownership, 2 kick.
        Q_INVOKABLE void admin(int action);
        Q_INVOKABLE void leave(const QString& reason = QString());
        Q_INVOKABLE QString mapShot() const;

    signals:
        void changed();
        void draftChanged();
        void chatChanged();
        void closed(QString reason);

    private:
        void Tick();
        void Refresh();
        void Identify();
        void MatchChoiceChanged(bool resetGoal);
        void DraftChanged();
        void RefreshDraft();
        [[nodiscard]] ::MphRead::Mods::Network::MatchDefinition DraftMatch() const;
        [[nodiscard]] bool TryBuildMatch(::MphRead::Mods::Network::MatchDefinition& match, std::string& reason) const;
        void TryAutoApply();

        QTimer _timer;
        QVariantList _players;
        QStringList _targets;
        std::vector<std::uint8_t> _targetSlots;
        int _target = 0;
        QStringList _teams{QStringLiteral("Auto"), QStringLiteral("Team A"), QStringLiteral("Team B")};
        int _hunter = 0;
        int _suit = 0;
        int _team = 0;
        int _moveTeam = 0;
        bool _chooseTeams = false;
        bool _playerEnabled = false;
        bool _teamEnabled = false;
        bool _owner = false;
        bool _ownerEnabled = false;
        QString _readyLabel = QStringLiteral("Ready");
        bool _startEnabled = false;
        QString _status;
        QString _chat;
        std::string _draftRoom;
        int _mode = 0;
        int _matchup = 0;
        QString _time = QStringLiteral("7");
        QString _goal = QStringLiteral("7");
        // Friendly fire, affinity weapons, shadow freeze, opponent health,
        // require ready, join in progress, lock teams.
        std::array<bool, 7> _toggles{false, false, false, true, false, false, false};
        std::array<std::uint8_t, 5> _custom{2, 2, 2, 0, 0};
        QString _summary;
        std::unique_ptr<::MphRead::Mods::Network::MatchDefinition> _shownMatch;
        std::int32_t _shownRules = -1;
        std::optional<std::uint16_t> _shownRevision;
        std::optional<std::uint32_t> _shownRosterRevision;
        std::int32_t _chatRevision = -1;
        std::int32_t _rosterCount = 0;
        double _nextPingRefresh = 0;
        bool _draftDirty = false;
        double _draftChangedAt = 0;
        bool _closed = false;
        bool _launched = false;
    };
}
