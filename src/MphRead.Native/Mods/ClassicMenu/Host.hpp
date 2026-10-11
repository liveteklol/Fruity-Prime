#pragma once

// What the DS menus need from the launcher once they stand in for it: the
// server list, the room, the settings, a match to start, a pause menu and a
// results panel to answer. The menus own the look and the order of pages;
// everything that is decided or remembered is the launcher's (PlayModel,
// LobbyModel, CreateServerModel, SettingsModel, ShellBridge), reached
// through this. Nothing here knows about Qt.

#include <string>
#include <vector>

namespace MphRead::Mods::ClassicMenu
{
    struct ServerRow final
    {
        std::string Name;
        std::string Arena;
        std::string Mode;
        std::string Players;  // "4/8"
        int Ping = -1;        // -1: has not answered
    };

    struct PlayerRow final
    {
        std::string Name;
        std::string Hunter;   // lower case, as the hunter select writes it
        std::string Team;     // "red", "blue", ... or "-"
        std::string State;    // "host", "ready", "wait", a bot's skill
        bool You = false;
        bool Bot = false;
    };

    // One row of a settings section, already formatted.
    struct SettingRow final
    {
        std::string Id;
        std::string Label;
        std::string Value;
        bool Steps = true;    // a choice, a toggle or a slider: the arrows apply
        bool Text = false;    // words to type: the DS keyboard opens
    };

    struct PauseInfo final
    {
        bool Vote = false;        // a map vote is waiting for an answer
        bool CanSpectate = false;
        bool Spectating = false;
    };

    struct EndInfo final
    {
        std::vector<std::string> Arenas;   // what can be voted for, display names
        std::vector<int> Votes;
        int Mine = -1;
        bool Ready = false;
        std::string ReadyLine;             // "2 of 4 ready"
    };

    // The offline match the menus have put together.
    struct OfflineMatch final
    {
        int Mode = 0;         // the DS's order: battle, survival, bounty, defender, prime hunter, capture, nodes
        int Arena = 0;        // index into Arenas()
        int Hunter = 0;       // samus, kanden, spire, trace, noxus, sylux, weavel
        int Suit = 0;
        int Bots = 0;
        int Skill = 1;
        int Teams = 0;        // 0 off, 1 two teams, 2 four teams
    };

    struct CreateChoice final
    {
        std::string Name;
        int Mode = 0;
        int Hunter = 0;
        int Rotation = 0;
        int Host = 0;
        int Kind = 0;         // 0 hosted, 1 dedicated
    };

    class Host
    {
    public:
        virtual ~Host() = default;

        // ---- play ----
        [[nodiscard]] virtual std::vector<std::string> Arenas() const = 0;   // display names, in the offline list's order
        virtual void StartOffline(const OfflineMatch& match) = 0;
        virtual void StartAdventure(int slot, bool newGame) = 0;
        [[nodiscard]] virtual bool SlotUsed(int slot) const = 0;
        [[nodiscard]] virtual int MaxBots() const = 0;
        virtual void WatchClip(int index) = 0;

        // ---- online ----
        virtual void RefreshServers() = 0;
        virtual void StopServers() = 0;
        [[nodiscard]] virtual std::vector<ServerRow> Servers() const = 0;
        [[nodiscard]] virtual std::string ServerNote() const = 0;
        virtual void Join(int row, int hunter, int suit) = 0;
        [[nodiscard]] virtual std::string Address() const = 0;
        virtual void SetAddress(const std::string& text) = 0;
        [[nodiscard]] virtual std::string PlayerName() const = 0;
        virtual void SetPlayerName(const std::string& text) = 0;

        // ---- create a server ----
        [[nodiscard]] virtual std::vector<std::string> Rotations() const = 0;
        [[nodiscard]] virtual std::vector<std::string> HostChoices() const = 0;
        [[nodiscard]] virtual std::vector<std::string> Kinds() const = 0;
        virtual void CreateServer(const CreateChoice& choice) = 0;
        [[nodiscard]] virtual std::string CreateNote() const = 0;

        // ---- the room (a network lobby) ----
        [[nodiscard]] virtual bool InLobby() const = 0;
        [[nodiscard]] virtual bool LobbyOwner() const = 0;
        [[nodiscard]] virtual std::vector<PlayerRow> LobbyPlayers() const = 0;
        [[nodiscard]] virtual std::string LobbyTitle() const = 0;
        [[nodiscard]] virtual std::string LobbyStatus() const = 0;
        virtual void LobbyReady() = 0;
        virtual void LobbyStart() = 0;
        virtual void LobbyLeave() = 0;
        virtual void LobbyChat(const std::string& text) = 0;
        virtual void LobbySetHunter(int hunter) = 0;
        virtual void LobbySetSuit(int suit) = 0;
        // 0 move to red, 1 move to blue, 2 transfer the room, 3 kick
        virtual void LobbyAdmin(int player, int action) = 0;

        // ---- settings, one section at a time ----
        // section: "display", "audio", "keyboard", "profile", "online"
        [[nodiscard]] virtual std::vector<SettingRow> Settings(const std::string& section) const = 0;
        virtual void StepSetting(const std::string& section, const std::string& id, int dir) = 0;
        virtual void ClickSetting(const std::string& section, const std::string& id) = 0;
        virtual void SetSettingText(const std::string& section, const std::string& id, const std::string& text) = 0;
        virtual void SaveSettings() = 0;
        // A key row is waiting for its key: keys go to the settings, not the menus.
        [[nodiscard]] virtual bool Listening() const = 0;

        // ---- over a match ----
        [[nodiscard]] virtual PauseInfo Pause() const = 0;
        virtual void Resume() = 0;
        virtual void Spectate() = 0;
        virtual void Rejoin() = 0;
        virtual void AnswerVote(bool yes) = 0;
        virtual void LeaveMatch() = 0;
        virtual void QuitFromMatch() = 0;
        [[nodiscard]] virtual EndInfo End() const = 0;
        virtual void EndVote(int arena) = 0;
        virtual void EndPick(int hunter, int suit) = 0;
        virtual void EndReady() = 0;

        // ---- the rest ----
        virtual void Say(const std::string& text) = 0;
        virtual void Quit() = 0;
    };
}
