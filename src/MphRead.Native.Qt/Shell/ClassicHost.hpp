#pragma once

#include "../../MphRead.Native/Mods/ClassicMenu/Host.hpp"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

namespace MphRead::Qt
{
    class PlayModel;
    class SettingsModel;
    class CreateServerModel;
    class LobbyModel;
    class RowModel;

    // The DS menus' Host on the launcher's own models: the same PlayModel,
    // LobbyModel, CreateServerModel and SettingsModel the QML pages use, so
    // a server joined, a match started or a setting changed from the menus
    // is exactly the launcher's. Models are made when first needed and live
    // with the item that shows the menus.
    class ClassicHost final : public QObject, public Mods::ClassicMenu::Host
    {
        Q_OBJECT

    public:
        explicit ClassicHost(QObject* parent = nullptr);
        ~ClassicHost() override;

        // A lobby the launcher opened (a server joined or created).
        void OpenRoom();
        [[nodiscard]] SettingsModel* Settings() const;

        // ---- Host ----
        [[nodiscard]] std::vector<std::string> Arenas() const override;
        void StartOffline(const Mods::ClassicMenu::OfflineMatch& match) override;
        void StartAdventure(int slot, bool newGame) override;
        [[nodiscard]] bool SlotUsed(int slot) const override;
        [[nodiscard]] int MaxBots() const override;
        void WatchClip(int index) override;

        void RefreshServers() override;
        void StopServers() override;
        [[nodiscard]] std::vector<Mods::ClassicMenu::ServerRow> Servers() const override;
        [[nodiscard]] std::string ServerNote() const override;
        void Join(int row, int hunter, int suit) override;
        [[nodiscard]] std::string Address() const override;
        void SetAddress(const std::string& text) override;
        [[nodiscard]] std::string PlayerName() const override;
        void SetPlayerName(const std::string& text) override;

        [[nodiscard]] std::vector<std::string> Rotations() const override;
        [[nodiscard]] std::vector<std::string> HostChoices() const override;
        [[nodiscard]] std::vector<std::string> Kinds() const override;
        void CreateServer(const Mods::ClassicMenu::CreateChoice& choice) override;
        [[nodiscard]] std::string CreateNote() const override;

        [[nodiscard]] bool InLobby() const override;
        [[nodiscard]] bool LobbyOwner() const override;
        [[nodiscard]] std::vector<Mods::ClassicMenu::PlayerRow> LobbyPlayers() const override;
        [[nodiscard]] std::string LobbyTitle() const override;
        [[nodiscard]] std::string LobbyStatus() const override;
        void LobbyReady() override;
        void LobbyStart() override;
        void LobbyLeave() override;
        void LobbyChat(const std::string& text) override;
        void LobbySetHunter(int hunter) override;
        void LobbySetSuit(int suit) override;
        void LobbyAdmin(int player, int action) override;

        [[nodiscard]] std::vector<Mods::ClassicMenu::SettingRow> Settings(const std::string& section) const override;
        void StepSetting(const std::string& section, const std::string& id, int dir) override;
        void ClickSetting(const std::string& section, const std::string& id) override;
        void SetSettingText(const std::string& section, const std::string& id, const std::string& text) override;
        void SaveSettings() override;
        [[nodiscard]] bool Listening() const override;

        [[nodiscard]] Mods::ClassicMenu::PauseInfo Pause() const override;
        void Resume() override;
        void Spectate() override;
        void Rejoin() override;
        void AnswerVote(bool yes) override;
        void LeaveMatch() override;
        void QuitFromMatch() override;
        [[nodiscard]] Mods::ClassicMenu::EndInfo End() const override;
        void EndVote(int arena) override;
        void EndPick(int hunter, int suit) override;
        void EndReady() override;

        void Say(const std::string& text) override;
        void Quit() override;

    signals:
        // The lobby closed, and why (said on the shell's message line).
        void roomClosed(QString reason);

    private:
        [[nodiscard]] PlayModel* Play() const;
        [[nodiscard]] CreateServerModel* Create() const;
        [[nodiscard]] RowModel* Section(const std::string& section) const;
        [[nodiscard]] std::vector<int> SectionRows(const std::string& section) const;

        mutable QPointer<PlayModel> _play;
        mutable QPointer<SettingsModel> _settings;
        mutable QPointer<CreateServerModel> _create;
        QPointer<LobbyModel> _lobby;
        std::string _address;
    };
}
