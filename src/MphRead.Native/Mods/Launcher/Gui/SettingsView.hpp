#pragma once

#include "../../../NativeRuntime/Avalonia/Avalonia.hpp"
#include "GamepadSettingsPanel.hpp"
#include "KeyRow.hpp"
#include "PadRow.hpp"
#include "Rows.hpp"
#include "SliderRow.hpp"
#include "UiMark.hpp"
#include "UiTabs.hpp"
#include "../../Input/TouchSettings.hpp"
#include "../../../NativeRuntime/Rhi/LowLatency.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MphRead
{
    class MenuSettings;
}

namespace MphRead::Mods::Launcher::Gui
{
    namespace Av = ::MphRead::NativeRuntime::Avalonia;

    class UiWord;

    // The same five-page settings surface used by the launcher and pause menu.
    class SettingsView final : public Av::Controls::UserControl
    {
    public:
        explicit SettingsView(std::shared_ptr<::MphRead::MenuSettings> settings,
            bool inGame = false);

        Av::Event<SettingsView&> Closed;
        Av::Event<SettingsView&> GameFilesRequested;
        Av::Event<SettingsView&> StylusPlacementRequested;

        [[nodiscard]] bool Saved() const noexcept { return _saved; }
        [[nodiscard]] std::string WindowTitle() const;
        [[nodiscard]] bool InGame() const noexcept { return _inGame; }

        // Open a named page; Controls also accepts its Keyboard/Gamepad/Stylus sub-page.
        void ShowSection(std::string_view name, std::int32_t sub = 0);

    protected:
        void OnKeyDown(Av::Input::KeyEventArgs& e) override;

    private:
        struct Section final
        {
            std::string Name;
            Av::Controls::ControlPtr Page;
        };

        struct TouchRow final
        {
            ::MphRead::Mods::Input::TouchControl Control;
            std::shared_ptr<ToggleRow> Row;
        };

        [[nodiscard]] std::shared_ptr<Av::Controls::StackPanel> AddSection(std::string name);
        void ShowPage(std::int32_t index);
        void Close();
        void BuildPages();
        void BuildCredits(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildDisplay(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildAudio(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildControls(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildKeyboard(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildGamepad(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildStylus(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildStylusZone(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildTouchControls(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildProfile(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void BuildDebugLogs(const std::shared_ptr<Av::Controls::StackPanel>& page);
        void ShowCrosshairRows();
        void ShowRadarRows();
        void ShowStylusRows();
        void ShowTouchRows();
        void TryCommit();
        void Commit();
        void ShareLogs();

        static void Heading(const std::shared_ptr<Av::Controls::StackPanel>& page,
            const std::string& text);
        static void Explain(const std::shared_ptr<Av::Controls::StackPanel>& page,
            const std::string& text,
            std::optional<Av::Media::Color> color = std::nullopt);
        static bool ParseEndpoint(std::string_view text, std::string& host,
            std::int32_t& port);
        static std::int32_t FpsLimitStopIndex(std::int32_t cap);
        static std::int32_t SensitivityToSlider(float sensitivity);
        static float SliderToSensitivity(std::int32_t value);
        static std::int32_t Percent(std::string_view stored, std::int32_t fallback);
        static std::string LogLocation();

        std::shared_ptr<::MphRead::MenuSettings> _settings;
        const bool _inGame;
        bool _saved = false;
        const NativeRuntime::Rhi::LowLatencyMode _originalLowLatency;
        std::shared_ptr<Av::Controls::Panel> _pages;
        std::vector<Section> _sections;
        std::shared_ptr<UiTabs> _tabs;
        std::shared_ptr<UiTabs> _controlTabs;

        std::shared_ptr<ChoiceRow> _windowRow;
        std::shared_ptr<ChoiceRow> _rendererRow;
        std::shared_ptr<ChoiceRow> _lowLatencyRow;
        std::shared_ptr<Note> _lowLatencyNote;
        std::shared_ptr<ChoiceRow> _clipSecondsRow;
        std::shared_ptr<SliderRow> _resolutionScale;
        std::shared_ptr<ToggleRow> _lightingRow;
        std::shared_ptr<ToggleRow> _fogRow;
        std::shared_ptr<ToggleRow> _filteringRow;
        std::shared_ptr<ToggleRow> _celRow;
        std::shared_ptr<ToggleRow> _fpsRow;
        std::shared_ptr<SliderRow> _fpsLimitRow;
        std::shared_ptr<SliderRow> _fovRow;
        std::shared_ptr<ToggleRow> _proHud;
        std::shared_ptr<ChoiceRow> _crosshairSizeRow;
        std::shared_ptr<ChoiceRow> _crosshairStyleRow;
        std::shared_ptr<ChoiceRow> _weaponStyleRow;
        std::shared_ptr<ToggleRow> _radarRow;
        std::shared_ptr<ToggleRow> _radarBackgroundRow;
        std::shared_ptr<ToggleRow> _radarOutlinesRow;
        std::shared_ptr<SliderRow> _sfxVolume;
        std::shared_ptr<SliderRow> _musicVolume;
        std::shared_ptr<ChoiceRow> _languageRow;
        std::shared_ptr<SliderRow> _sensitivity;
        std::shared_ptr<ToggleRow> _invertY;
        std::shared_ptr<ToggleRow> _invertX;
        std::shared_ptr<ToggleRow> _penTablet;
        std::shared_ptr<ToggleRow> _scrollAllWeapons;
        std::shared_ptr<GamepadSettingsPanel> _gamepadSettings;
        std::shared_ptr<ToggleRow> _repositionFilter;
        std::shared_ptr<Av::Controls::StackPanel> _stylusAdvanced;
        std::shared_ptr<DeckButton> _stylusAdvancedButton;
        bool _stylusAdvancedOpen = false;
        std::shared_ptr<ToggleRow> _stylusZone;
        std::shared_ptr<SliderRow> _stylusOpacity;
        std::vector<Av::Controls::ControlPtr> _stylusRows;
        std::shared_ptr<ToggleRow> _touchButtonsRow;
        std::vector<TouchRow> _touchRows;
        std::vector<std::shared_ptr<PadRow>> _padRows;
        std::vector<std::shared_ptr<KeyRow>> _keyRows;
        std::shared_ptr<FieldRow> _playerName;
        std::shared_ptr<ChoiceRow> _hunterRow;
        std::shared_ptr<ChoiceRow> _colorRow;
        std::shared_ptr<FieldRow> _serverRow;
        std::shared_ptr<FieldRow> _masterRow;
        std::shared_ptr<ToggleRow> _autoUpdate;
        std::shared_ptr<Note> _saveError;
        std::shared_ptr<UiWord> _shareLogs;
        std::shared_ptr<Note> _shareError;
        bool _sharing = false;
        std::shared_ptr<std::uint8_t> _lifetime = std::make_shared<std::uint8_t>(0);
    };
}
