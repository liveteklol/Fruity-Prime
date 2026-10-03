#pragma once

#include "../../../NativeRuntime/Avalonia/Avalonia.hpp"
#include "../Portable/LaunchPlan.hpp"

#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace MphRead
{
    class MenuSettings;
}

namespace MphRead::Mods::Update
{
    struct UpdateInfo;
    class IUpdateInstaller;
}

namespace MphRead::Mods::Launcher::Gui
{
    namespace Av = ::MphRead::NativeRuntime::Avalonia;

    class DeckButton;
    class DeckChip;
    class DeckWordmark;
    class GamepadNavigation;
    class LobbyScreen;
    class UiWord;

    // The front screen and the stack it owns over its backdrop.
    class StartScreen final : public Av::Controls::UserControl
    {
    public:
        // The settings page, pushed over the front screen.
        void OpenSettings();
        [[nodiscard]] static std::shared_ptr<StartScreen> Create(
            const std::shared_ptr<::MphRead::MenuSettings>& settings,
            const std::vector<std::string>& rooms);
        ~StartScreen() override;

        Av::Event<StartScreen&, ::MphRead::Mods::Launcher::LaunchPlan> Done;
        Av::Event<StartScreen&, ::MphRead::Mods::Launcher::LaunchPlan> MatchRequested;

        [[nodiscard]] const ::MphRead::Mods::Launcher::LaunchPlan& Plan() const noexcept
        {
            return _plan;
        }
        void ResumeLobby();
        void SuspendLobby();
        void Reset();
        [[nodiscard]] bool GoBack();
        void ShowPauseMenu(std::function<void()> onResume,
            std::function<void()> onLeave, std::function<void()> onQuit);

    protected:
        void OnAttachedToVisualTree() override;
        void OnDetachedFromVisualTree() override;
        void OnKeyDown(Av::Input::KeyEventArgs& e) override;

    private:
        StartScreen(const std::shared_ptr<::MphRead::MenuSettings>& settings,
            const std::vector<std::string>& rooms);
        [[nodiscard]] std::shared_ptr<StartScreen> Self();
        void StartUpdateCheck();
        [[nodiscard]] static std::shared_ptr<DeckButton> SupportMark();
        [[nodiscard]] static std::string PlayerNameOrDefault();
        void LayOutBar(double width);
        void LayOutWordmark(Av::Size frame);
        [[nodiscard]] static std::shared_ptr<UiWord> Word(const std::string& text,
            const Av::Media::FontFamilyPtr& font, double size, Av::Media::Color colour,
            std::function<void()> go);
        void ShowGround(bool show);
        void Push(const Av::Controls::ControlPtr& view);
        void Pop();
        void Finish(::MphRead::Mods::Launcher::LaunchPlan plan);
        void OpenPlay();
        void OpenCreateServer();
        void ConnectedOrFinished(::MphRead::Mods::Launcher::LaunchPlan plan);
        void OpenSetup();
        void AskToQuit();
        void OpenVote();
        void CatchUpPreviews();
        void RefreshRooms();
        [[nodiscard]] static std::string VersionNumber();
        void Say(std::string text, Av::Media::Color colour, bool pressable = false);
        void RefreshVersionLine();
        void UpdateNow();
        void FetchAndInstall(::MphRead::Mods::Update::UpdateInfo update,
            const std::shared_ptr<::MphRead::Mods::Update::IUpdateInstaller>& installer);

        static constexpr double BarTurnsWidth = 470;
        static constexpr double WindowWidthGuess = 940;

        std::shared_ptr<::MphRead::MenuSettings> _settings;
        std::vector<std::string> _rooms;
        std::vector<Av::Controls::ControlPtr> _stack;
        std::vector<Av::Controls::ControlPtr> _ground;
        std::shared_ptr<Av::Controls::Panel> _root;
        std::shared_ptr<Av::Controls::Panel> _overlay;
        std::shared_ptr<Av::Controls::StackPanel> _menu;
        std::shared_ptr<Av::Controls::StackPanel> _bar;
        std::shared_ptr<Av::Controls::Grid> _foot;
        std::shared_ptr<DeckWordmark> _wordmark;
        std::shared_ptr<Av::Controls::TextBlock> _subtitle;
        std::shared_ptr<Av::Controls::TextBlock> _version;
        std::shared_ptr<Av::Controls::TextBlock> _help;
        std::shared_ptr<Av::Controls::Border> _versionBox;
        std::shared_ptr<Av::Controls::Border> _dark;
        std::shared_ptr<DeckChip> _chip;
        std::shared_ptr<DeckButton> _heart;
        std::shared_ptr<DeckButton> _heartCorner;
        std::shared_ptr<LobbyScreen> _lobby;
        std::shared_ptr<Av::Threading::DispatcherTimer> _hintTimer;
        std::shared_ptr<Av::Threading::DispatcherTimer> _gamepadTimer;
        std::shared_ptr<GamepadNavigation> _gamepadNavigation;
        std::shared_future<int> _previewTask;
        ::MphRead::Mods::Launcher::LaunchPlan _plan;
        std::string _controllerPrompt;
        bool _finished = false;
        bool _updateCheckStarted = false;
        bool _updatable = false;
        bool _updating = false;
        bool _groundShown = true;
    };
}
