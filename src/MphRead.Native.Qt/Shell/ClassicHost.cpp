#include "ClassicHost.hpp"

#include "CreateServerModel.hpp"
#include "LobbyModel.hpp"
#include "PlayModel.hpp"
#include "RowModel.hpp"
#include "SettingsModel.hpp"
#include "ShellBridge.hpp"

#include "../../MphRead.Native/Mods/Launcher/Portable/LauncherPrefs.hpp"

#include <QtCore/QVariantMap>

#include <algorithm>
#include <array>
#include <cctype>

namespace MphRead::Qt
{
    namespace
    {
        using Prefs = ::MphRead::Mods::Launcher::LauncherPrefs;
        namespace Menu = ::MphRead::Mods::ClassicMenu;

        [[nodiscard]] std::string S(const QString& text) { return text.toStdString(); }
        [[nodiscard]] QString Q(const std::string& text) { return QString::fromStdString(text); }

        [[nodiscard]] std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // The hunter select's order (samus, kanden, spire, trace, noxus,
        // sylux, weavel) in the game's own (samus, kanden, trace, sylux,
        // noxus, spire, weavel).
        constexpr std::array<int, 7> HunterIndex{0, 1, 5, 2, 4, 3, 6};
        [[nodiscard]] int FromDs(int hunter) { return HunterIndex[static_cast<std::size_t>((hunter % 7 + 7) % 7)]; }

        // The DS's modes (battle, survival, bounty, defender, prime hunter,
        // capture, nodes) in the launcher's table, with and without teams.
        [[nodiscard]] int ModeIndex(int mode, bool teams)
        {
            switch ((mode % 7 + 7) % 7)
            {
            case 0: return teams ? 1 : 0;
            case 1: return teams ? 3 : 2;
            case 2: return teams ? 6 : 5;
            case 3: return teams ? 8 : 7;
            case 4: return 11;
            case 5: return 4;
            default: return teams ? 10 : 9;
            }
        }

        // The rows a settings section shows in the menus.
        [[nodiscard]] bool Shown(const Row& row)
        {
            static const std::array<const char*, 8> kinds{"choice", "toggle", "slider", "field", "key", "word", "button", "pad"};
            if (std::find(kinds.begin(), kinds.end(), S(row.Type)) == kinds.end()) return false;
            return !row.Shown || row.Shown();
        }
    }

    ClassicHost::ClassicHost(QObject* parent) : QObject(parent)
    {
    }

    ClassicHost::~ClassicHost() = default;

    PlayModel* ClassicHost::Play() const
    {
        if (_play == nullptr) _play = new PlayModel(const_cast<ClassicHost*>(this));
        return _play;
    }

    SettingsModel* ClassicHost::Settings() const
    {
        if (_settings == nullptr) _settings = new SettingsModel(const_cast<ClassicHost*>(this));
        return _settings;
    }

    CreateServerModel* ClassicHost::Create() const
    {
        if (_create == nullptr) _create = new CreateServerModel(const_cast<ClassicHost*>(this));
        return _create;
    }

    // ---------------------------------------------------------------- play

    std::vector<std::string> ClassicHost::Arenas() const
    {
        std::vector<std::string> out;
        const ShellBridge* const bridge = ShellBridge::Current();
        if (bridge == nullptr) return out;
        for (const QVariant& room : bridge->Rooms())
        {
            const QVariantMap m = room.toMap();
            const QString name = m.value(QStringLiteral("name")).toString();
            out.push_back(S(name.isEmpty() ? m.value(QStringLiteral("key")).toString() : name));
        }
        return out;
    }

    void ClassicHost::StartOffline(const Menu::OfflineMatch& match)
    {
        const ShellBridge* const bridge = ShellBridge::Current();
        if (bridge == nullptr) return;
        const QVariantList rooms = bridge->Rooms();
        if (rooms.isEmpty()) { Say("There is no arena to play: the game files are not ready."); return; }
        const QString room = rooms[std::clamp(match.Arena, 0, static_cast<int>(rooms.size()) - 1)].toMap().value(QStringLiteral("key")).toString();
        Play()->start(room, ModeIndex(match.Mode, match.Teams > 0), FromDs(match.Hunter), match.Suit,
            std::clamp(match.Bots, 0, MaxBots()), std::clamp(match.Skill, 0, 3));
    }

    void ClassicHost::StartAdventure(int slot, bool newGame)
    {
        // the adventure is Samus's: the hunter row the launcher shows has her first
        Play()->startAdventure(slot, 0, newGame);
    }

    bool ClassicHost::SlotUsed(int slot) const
    {
        return Play()->slotUsed(slot);
    }

    int ClassicHost::MaxBots() const
    {
        // the engine's player slots, less yours
        return std::max(0, static_cast<int>(Play()->Bots().size()) - 1);
    }

    void ClassicHost::WatchClip(int index)
    {
        PlayModel* const play = Play();
        if (play->Face() != 3) play->SetFace(3);
        const QVariantList clips = play->Clips();
        // the last row is "open a file", which the DS has no picker for
        if (index < 0 || index >= static_cast<int>(clips.size()) - 1)
        {
            Say("There is no clip there yet. Save one in a match (F9 by default).");
            return;
        }
        play->watch(clips[index].toMap().value(QStringLiteral("path")).toString());
    }

    // ---------------------------------------------------------------- online

    void ClassicHost::RefreshServers()
    {
        PlayModel* const play = Play();
        if (play->Face() != 0) play->SetFace(0);
        else play->reloadServers();
    }

    void ClassicHost::StopServers()
    {
        if (_play != nullptr) _play->stopPolling();
    }

    std::vector<Menu::ServerRow> ClassicHost::Servers() const
    {
        std::vector<Menu::ServerRow> out;
        if (_play == nullptr) return out;
        for (const QVariant& row : _play->Servers())
        {
            const QVariantMap m = row.toMap();
            Menu::ServerRow r;
            r.Name = S(m.value(QStringLiteral("name")).toString());
            const bool asking = m.value(QStringLiteral("asking")).toBool();
            r.Arena = asking ? "asking..." : S(m.value(QStringLiteral("map")).toString());
            r.Mode = S(m.value(QStringLiteral("mode")).toString());
            r.Players = S(m.value(QStringLiteral("players")).toString());
            bool ok = false;
            const int ping = m.value(QStringLiteral("ping")).toString().toInt(&ok);
            r.Ping = ok ? ping : -1;
            if (r.Players == "\xE2\x80\x94") r.Players = "-";
            out.push_back(std::move(r));
        }
        return out;
    }

    std::string ClassicHost::ServerNote() const
    {
        return _play != nullptr && !_play->Note().isEmpty() ? Lower(S(_play->Note())) : "every server, with its arena, mode, players and ping.";
    }

    void ClassicHost::Join(int row, int hunter, int suit)
    {
        PlayModel* const play = Play();
        const QVariantList servers = play->Servers();
        if (row < 0 || row >= static_cast<int>(servers.size()))
        {
            // no row picked: the address typed in
            if (!_address.empty()) play->join(Q(Prefs::PlayerName()), Q(_address), FromDs(hunter), suit);
            return;
        }
        play->join(Q(Prefs::PlayerName()), servers[row].toMap().value(QStringLiteral("endpoint")).toString(), FromDs(hunter), suit);
    }

    std::string ClassicHost::Address() const
    {
        return _address;
    }

    void ClassicHost::SetAddress(const std::string& text)
    {
        _address = text;
        if (!text.empty()) Play()->queryStatus(Q(text));
    }

    std::string ClassicHost::PlayerName() const
    {
        return Prefs::PlayerName();
    }

    void ClassicHost::SetPlayerName(const std::string& text)
    {
        if (text.empty()) return;
        Prefs::PlayerName(text);
        Prefs::Save();
        if (ShellBridge* const bridge = ShellBridge::Current()) bridge->refreshProfile();
    }

    // ---------------------------------------------------------------- create

    std::vector<std::string> ClassicHost::Rotations() const
    {
        return {"every arena", "the first arena"};
    }

    std::vector<std::string> ClassicHost::HostChoices() const
    {
        std::vector<std::string> out;
        for (const QVariant& host : Create()->Hosts())
        {
            const QString title = host.toMap().value(QStringLiteral("title")).toString();
            if (!title.isEmpty()) out.push_back(S(title));
        }
        if (out.empty()) out.push_back(S(Create()->HostLabel()));
        return out;
    }

    std::vector<std::string> ClassicHost::Kinds() const
    {
        return Create()->CanRunHere() ? std::vector<std::string>{"hosted", "dedicated"} : std::vector<std::string>{"hosted"};
    }

    void ClassicHost::CreateServer(const Menu::CreateChoice& choice)
    {
        CreateServerModel* const create = Create();
        QStringList rotation;
        if (const ShellBridge* const bridge = ShellBridge::Current())
        {
            for (const QVariant& room : bridge->Rooms())
            {
                rotation.push_back(room.toMap().value(QStringLiteral("key")).toString());
                if (choice.Rotation == 1 || rotation.size() >= CreateServerModel::MaxRotation()) break;
            }
        }
        create->SetRotation(rotation);
        create->SetKind(choice.Kind);
        create->chooseHost(choice.Host);
        create->go(Q(choice.Name), ModeIndex(choice.Mode, false), FromDs(choice.Hunter));
    }

    std::string ClassicHost::CreateNote() const
    {
        const CreateServerModel* const create = Create();
        if (!create->Stage().isEmpty() && create->ProgressVisible()) return Lower(S(create->Stage()));
        return create->Note().isEmpty() ? std::string("a server on this machine, or on the directory's.") : Lower(S(create->Note()));
    }

    // ---------------------------------------------------------------- the room

    void ClassicHost::OpenRoom()
    {
        if (_lobby != nullptr) _lobby->deleteLater();
        _lobby = new LobbyModel(this);
        connect(_lobby, &LobbyModel::closed, this, [this](const QString& reason)
        {
            emit roomClosed(reason);
            if (_lobby != nullptr) _lobby->deleteLater();
        });
    }

    bool ClassicHost::InLobby() const
    {
        return _lobby != nullptr;
    }

    bool ClassicHost::LobbyOwner() const
    {
        return _lobby != nullptr && _lobby->Owner();
    }

    // LobbyModel's rows: "name  [OWNER]", "Hunter · S1 · Team A · 12 ms".
    std::vector<Menu::PlayerRow> ClassicHost::LobbyPlayers() const
    {
        std::vector<Menu::PlayerRow> out;
        if (_lobby == nullptr) return out;
        const QStringList targets = _lobby->Targets();
        for (const QVariant& player : _lobby->Players())
        {
            const QVariantMap m = player.toMap();
            Menu::PlayerRow r;
            QString name = m.value(QStringLiteral("name")).toString();
            const bool owner = name.contains(QStringLiteral("[OWNER]"));
            name = name.remove(QStringLiteral("[OWNER]")).trimmed();
            r.Name = S(name.toUpper());
            const QStringList parts = m.value(QStringLiteral("detail")).toString().split(QString::fromUtf8(" \xC2\xB7 "));
            r.Hunter = parts.isEmpty() ? "-" : Lower(S(parts[0]));
            r.Team = "-";
            for (const QString& part : parts)
            {
                if (!part.startsWith(QStringLiteral("Team "))) continue;
                const QChar t = part.at(part.size() - 1);
                r.Team = t == QLatin1Char('A') ? "red" : t == QLatin1Char('B') ? "blue" : t == QLatin1Char('C') ? "green" : "gold";
            }
            r.State = owner ? "host" : m.value(QStringLiteral("ready")).toBool() ? "ready" : "wait";
            r.You = !targets.contains(name);
            out.push_back(std::move(r));
        }
        return out;
    }

    std::string ClassicHost::LobbyTitle() const
    {
        return _lobby != nullptr ? S(_lobby->Title()) : "LOBBY";
    }

    std::string ClassicHost::LobbyStatus() const
    {
        return _lobby != nullptr ? S(_lobby->Status()) : "";
    }

    void ClassicHost::LobbyReady()
    {
        if (_lobby != nullptr) _lobby->toggleReady();
    }

    void ClassicHost::LobbyStart()
    {
        if (_lobby == nullptr) return;
        if (!_lobby->StartEnabled()) { Say(S(_lobby->Status())); return; }
        _lobby->startMatch();
    }

    void ClassicHost::LobbyLeave()
    {
        if (_lobby != nullptr) _lobby->leave(QStringLiteral("You left the lobby."));
    }

    void ClassicHost::LobbyChat(const std::string& text)
    {
        if (_lobby != nullptr) _lobby->sendChat(Q(text));
    }

    void ClassicHost::LobbySetHunter(int hunter)
    {
        if (_lobby != nullptr) _lobby->setHunter(FromDs(hunter));
    }

    void ClassicHost::LobbySetSuit(int suit)
    {
        if (_lobby != nullptr) _lobby->setSuit(suit);
    }

    // player: a row of LobbyPlayers; action: 0 red, 1 blue, 2 transfer, 3 kick
    void ClassicHost::LobbyAdmin(int player, int action)
    {
        if (_lobby == nullptr) return;
        const auto rows = LobbyPlayers();
        if (player < 0 || player >= static_cast<int>(rows.size())) return;
        const QStringList targets = _lobby->Targets();
        QString name = Q(rows[static_cast<std::size_t>(player)].Name);
        int target = -1;
        for (int i = 0; i < targets.size(); ++i) if (targets[i].compare(name, ::Qt::CaseInsensitive) == 0) target = i;
        if (target < 0) return;
        _lobby->SetTarget(target);
        if (action <= 1)
        {
            _lobby->SetMoveTeam(action + 1);
            _lobby->admin(0);
        }
        else _lobby->admin(action - 1);
    }

    // ---------------------------------------------------------------- settings

    RowModel* ClassicHost::Section(const std::string& section) const
    {
        SettingsModel* const s = Settings();
        QObject* const model = section == "display" ? s->Display()
            : section == "audio" ? s->Audio()
            : section == "keyboard" ? s->Keyboard()
            : section == "gamepad" ? s->Gamepad()
            : section == "stylus" ? s->Stylus()
            : s->Profile();
        return qobject_cast<RowModel*>(model);
    }

    // The rows of a section the menus show; profile and online split
    // the launcher's one profile page.
    std::vector<int> ClassicHost::SectionRows(const std::string& section) const
    {
        std::vector<int> out;
        RowModel* const model = Section(section);
        if (model == nullptr) return out;
        const auto& rows = model->Rows();
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const Row& row = rows[i];
            if (!Shown(row)) continue;
            const std::string id = S(row.Id);
            const bool online = id == "server" || id == "master" || id == "autoUpdate";
            if (section == "online" && !online) continue;
            if (section == "profile" && online) continue;
            out.push_back(static_cast<int>(i));
        }
        return out;
    }

    std::vector<Menu::SettingRow> ClassicHost::Settings(const std::string& section) const
    {
        std::vector<Menu::SettingRow> out;
        RowModel* const model = Section(section);
        if (model == nullptr) return out;
        const auto& rows = model->Rows();
        for (const int i : SectionRows(section))
        {
            const Row& row = rows[static_cast<std::size_t>(i)];
            Menu::SettingRow r;
            r.Id = std::to_string(i);
            r.Label = S(row.Label);
            const std::string type = S(row.Type);
            if (type == "choice")
            {
                r.Value = row.Index >= 0 && row.Index < row.Options.size() ? Lower(S(row.Options[row.Index])) : "-";
            }
            else if (type == "toggle") r.Value = row.On ? "ON" : "OFF";
            else if (type == "slider") r.Value = row.Format ? S(row.Format(row.Value)) : std::to_string(row.Value);
            else if (type == "field") { r.Value = S(row.Text); r.Steps = false; r.Text = true; }
            else if (type == "key" || type == "pad") { r.Value = row.Live ? S(row.Live()) : S(row.Text); r.Steps = false; }
            else { r.Value = "open"; r.Steps = false; }
            out.push_back(std::move(r));
        }
        return out;
    }

    void ClassicHost::StepSetting(const std::string& section, const std::string& id, int dir)
    {
        RowModel* const model = Section(section);
        if (model == nullptr) return;
        const int i = std::stoi(id);
        auto& rows = model->Rows();
        if (i < 0 || static_cast<std::size_t>(i) >= rows.size()) return;
        const Row& row = rows[static_cast<std::size_t>(i)];
        const std::string type = S(row.Type);
        if (type == "choice" && !row.Options.isEmpty())
        {
            const int n = static_cast<int>(row.Options.size());
            model->setIndex(i, (row.Index + dir + n) % n);
        }
        else if (type == "toggle") model->setOn(i, !row.On);
        else if (type == "slider") model->setValue(i, std::clamp(row.Value + dir * std::max(1, row.Step), row.Min, row.Max));
    }

    void ClassicHost::ClickSetting(const std::string& section, const std::string& id)
    {
        RowModel* const model = Section(section);
        if (model == nullptr) return;
        const int i = std::stoi(id);
        auto& rows = model->Rows();
        if (i < 0 || static_cast<std::size_t>(i) >= rows.size()) return;
        const std::string type = S(rows[static_cast<std::size_t>(i)].Type);
        if (type == "key" && section == "keyboard") Settings()->listenKey(i);
        else if (type == "pad") Settings()->padListen(i);
        else model->click(i);
    }

    void ClassicHost::SetSettingText(const std::string& section, const std::string& id, const std::string& text)
    {
        RowModel* const model = Section(section);
        if (model == nullptr) return;
        model->setText(std::stoi(id), Q(text));
    }

    void ClassicHost::SaveSettings()
    {
        if (_settings != nullptr && !_settings->save()) Say(S(_settings->Error()));
    }

    bool ClassicHost::Listening() const
    {
        return _settings != nullptr && _settings->Listening();
    }

    // ---------------------------------------------------------------- over a match

    Menu::PauseInfo ClassicHost::Pause() const
    {
        Menu::PauseInfo info;
        const ShellBridge* const bridge = ShellBridge::Current();
        if (bridge == nullptr) return info;
        const QVariantMap state = bridge->pauseState();
        info.Vote = state.value(QStringLiteral("vote")).toBool();
        info.CanSpectate = state.value(QStringLiteral("canSpectate")).toBool();
        info.Spectating = state.value(QStringLiteral("spectating")).toBool();
        return info;
    }

    void ClassicHost::Resume() { if (ShellBridge* b = ShellBridge::Current()) b->resume(); }
    void ClassicHost::Spectate() { if (ShellBridge* b = ShellBridge::Current()) b->spectate(); }
    void ClassicHost::Rejoin() { if (ShellBridge* b = ShellBridge::Current()) b->rejoin(); }
    void ClassicHost::AnswerVote(bool yes) { if (ShellBridge* b = ShellBridge::Current()) b->answerVote(yes); }
    void ClassicHost::LeaveMatch() { if (ShellBridge* b = ShellBridge::Current()) b->leaveMatch(); }
    void ClassicHost::QuitFromMatch() { if (ShellBridge* b = ShellBridge::Current()) b->quitFromMatch(); }

    Menu::EndInfo ClassicHost::End() const
    {
        Menu::EndInfo info;
        const ShellBridge* const bridge = ShellBridge::Current();
        if (bridge == nullptr) return info;
        const QVariantMap state = bridge->endState();
        int i = 0;
        for (const QVariant& tile : state.value(QStringLiteral("ballot")).toList())
        {
            const QVariantMap m = tile.toMap();
            info.Arenas.push_back(S(m.value(QStringLiteral("name")).toString()));
            info.Votes.push_back(m.value(QStringLiteral("votes")).toInt());
            if (m.value(QStringLiteral("chosen")).toBool()) info.Mine = i;
            ++i;
        }
        info.Ready = state.value(QStringLiteral("ready")).toBool();
        info.ReadyLine = Lower(S(state.value(QStringLiteral("count")).toString()));
        return info;
    }

    void ClassicHost::EndVote(int arena)
    {
        ShellBridge* const bridge = ShellBridge::Current();
        if (bridge == nullptr) return;
        const QVariantList ballot = bridge->endState().value(QStringLiteral("ballot")).toList();
        if (arena >= 0 && arena < static_cast<int>(ballot.size()))
        {
            bridge->endChoose(ballot[arena].toMap().value(QStringLiteral("key")).toString());
        }
    }

    void ClassicHost::EndPick(int hunter, int suit)
    {
        if (ShellBridge* const bridge = ShellBridge::Current()) bridge->endPick(FromDs(hunter), suit);
    }

    void ClassicHost::EndReady()
    {
        if (ShellBridge* const bridge = ShellBridge::Current()) bridge->endToggleReady();
    }

    // ---------------------------------------------------------------- the rest

    void ClassicHost::Say(const std::string& text)
    {
        if (text.empty()) return;
        if (ShellBridge* const bridge = ShellBridge::Current()) bridge->systemMessage(Q(text));
    }

    void ClassicHost::Quit()
    {
        if (ShellBridge* const bridge = ShellBridge::Current()) bridge->quit();
    }
}
