#include "SettingsView.hpp"
#include "Shell.hpp"
#include "../../../NativeRuntime/Rhi/SceneBackend.hpp"

#include "ControllerNav.hpp"
#include "CrosshairPreview.hpp"
#include "DeckButton.hpp"
#include "HunterStand.hpp"
#include "UiLayout.hpp"
#include "UiWord.hpp"
#include "../../../Features.hpp"
#include "../../../GameState.hpp"
#include "../../../Menu.hpp"
#include "../../../NativeRuntime/Avalonia/Threading.hpp"
#include "../../../NativeRuntime/System/Globalization.hpp"
#include "../../../NativeRuntime/System/Managed.hpp"
#include "../../../NativeRuntime/System/Number.hpp"
#include "../../Branding.hpp"
#include "../../Credits.hpp"
#include "../../DebugLog.hpp"
#include "../../GameSettings.hpp"
#include "../../Input/PadBindings.hpp"
#include "../../Input/PointerInput.hpp"
#include "../../Input/StylusZone.hpp"
#include "../../Input/TouchSettings.hpp"
#include "../../InputSettings.hpp"
#include "../../LogShare.hpp"
#include "../../Network/DemoClip.hpp"
#include "../../Network/PlayerColors.hpp"
#include "../../PauseMenu.hpp"
#include "../../Render/Crosshair.hpp"
#include "../../Render/FrameTiming.hpp"
#include "../../Render/Radar.hpp"
#include "../../RenderOptions.hpp"
#include "../../RespawnChoice.hpp"
#include "../../Update/Updater.hpp"
#include "../../WindowMode.hpp"
#include "../Portable/GameFiles.hpp"
#include "../Portable/LauncherPrefs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::Int32TryParseCurrentCulture;
using ::MphRead::NativeRuntime::Int32TryParseInvariant;
using ::MphRead::NativeRuntime::MathRoundToInt32;
using ::MphRead::NativeRuntime::SingleTryParseInvariant;
using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::MphRead::NativeRuntime::StringTrim;
using ::MphRead::NativeRuntime::ToStringInvariant;
using ::MphRead::NativeRuntime::Utf16ToUtf8;
using ::MphRead::NativeRuntime::Utf8ToUtf16;

namespace MphRead::Mods::Launcher::Gui
{
    using namespace ::MphRead::NativeRuntime::Avalonia;
    using namespace ::MphRead::Mods::Render;
    using ::MphRead::NativeRuntime::RequireReference;
    namespace AvInput = ::MphRead::NativeRuntime::Avalonia::Input;
    namespace ModInput = ::MphRead::Mods::Input;

    namespace
    {
        struct FpsLimitStop final
        {
            std::string_view Label;
            std::int32_t Cap;
        };

        constexpr std::array<FpsLimitStop, 13> FpsLimitStops{{
            {"Display (VSync)", Render::FrameTiming::DisplayRate},
            {"30 fps", 30}, {"60 fps", 60}, {"75 fps", 75}, {"90 fps", 90},
            {"100 fps", 100}, {"120 fps", 120}, {"144 fps", 144},
            {"165 fps", 165}, {"180 fps", 180}, {"200 fps", 200},
            {"240 fps", 240}, {"Unlimited", Render::FrameTiming::MaxCap}
        }};

        constexpr std::array<std::string_view, 6> LanguageNames{{
            "English", "Japanese", "French", "Spanish", "German", "Italian"
        }};

        template <typename T>
        std::shared_ptr<T> Add(const std::shared_ptr<Controls::StackPanel>& page,
            std::shared_ptr<T> control)
        {
            page->Children.Add(control);
            return control;
        }

        [[nodiscard]] std::vector<std::string> CopyNames(
            const std::array<std::string, 3>& names)
        {
            return std::vector<std::string>(names.begin(), names.end());
        }

        [[nodiscard]] std::vector<std::string> CopyNames(
            const std::array<std::string, 5>& names)
        {
            return std::vector<std::string>(names.begin(), names.end());
        }
    }

    SettingsView::SettingsView(std::shared_ptr<::MphRead::MenuSettings> settings,
        bool inGame)
        : _settings(std::move(settings)), _inGame(inGame),
          _originalLowLatency(LauncherPrefs::LowLatency()),
          _pages(std::make_shared<Controls::Panel>())
    {
        (void)RequireReference(_settings);
        Background(Media::Brushes::Transparent());
        Focusable(true);

        BuildPages();
        std::vector<std::string> names;
        names.reserve(_sections.size());
        for (const Section& section : _sections)
        {
            names.push_back(section.Name);
        }
        _tabs = std::make_shared<UiTabs>(names);
        _tabs->Changed += [this](UiTabs& tabs) { ShowPage(tabs.Index()); };

        auto cancel = std::make_shared<UiMark>(UiMark::Shape::Cancel, "cancel");
        cancel->Click += [this](UiMark&) { Close(); };
        auto save = std::make_shared<UiMark>(UiMark::Shape::Accept,
            inGame ? "apply" : "save");
        save->Click += [this](UiMark&) { TryCommit(); };

        std::shared_ptr<Controls::Panel> root = UiLayout::Page(inGame,
            UiLayout::WellSettings, "settings", _tabs, _pages, cancel, save);
        _saveError = std::make_shared<Note>("", GuiTheme::Warm);
        _saveError->IsVisible(false);
        _saveError->HorizontalAlignment(Layout::HorizontalAlignment::Center);
        _saveError->VerticalAlignment(Layout::VerticalAlignment::Bottom);
        _saveError->Margin(Thickness(0, 0, 0, UiLayout::MarksBottom + 38));
        root->Children.Add(_saveError);
        Content(root);
        ShowPage(0);

        const std::shared_ptr<UiTabs> tabs = _tabs;
        AttachedToVisualTree += [tabs](Controls::Control&)
        {
            // Attach children before asking the top-level focus manager to focus a tab.
            Threading::Dispatcher::UIThread().Post([tabs]
            {
                tabs->FocusSelected();
            }, Threading::DispatcherPriority::Background);
        };
    }

    std::string SettingsView::WindowTitle() const
    {
        std::string title(Branding::Name);
        title += " settings";
        return title;
    }

    std::shared_ptr<Controls::StackPanel> SettingsView::AddSection(std::string name)
    {
        auto page = std::make_shared<Controls::StackPanel>();
        page->Spacing(2);
        page->Margin(Thickness(0));

        auto scroll = std::make_shared<Controls::ScrollViewer>();
        scroll->Content(page);
        scroll->IsVisible(false);
        scroll->HorizontalScrollBarVisibility(Controls::ScrollBarVisibility::Disabled);
        _pages->Children.Add(scroll);
        _sections.push_back(Section{std::move(name), scroll});
        return page;
    }

    void SettingsView::ShowSection(std::string_view name, std::int32_t sub)
    {
        for (std::size_t i = 0; i < _sections.size(); ++i)
        {
            if (!StringEqualsOrdinalIgnoreCase(_sections[i].Name, name))
            {
                continue;
            }
            _tabs->Index(static_cast<std::int32_t>(i));
            ShowPage(static_cast<std::int32_t>(i));
            if (_controlTabs != nullptr
                && StringEqualsOrdinalIgnoreCase(name, "Controls"))
            {
                _controlTabs->Index(std::clamp(sub, 0, 2));
            }
            return;
        }
    }

    void SettingsView::ShowPage(std::int32_t index)
    {
        for (std::size_t i = 0; i < _sections.size(); ++i)
        {
            _sections[i].Page->IsVisible(static_cast<std::int32_t>(i) == index);
        }
    }

    void SettingsView::Close()
    {
        if (!_saved)
        {
            LauncherPrefs::LowLatency(_originalLowLatency);
            RenderOptions::FieldOfView(RenderOptions::ParseFov(
                std::string_view(_settings->FieldOfView), RenderOptions::DefaultFov));
        }
        Closed(*this);
    }

    void SettingsView::OnKeyDown(AvInput::KeyEventArgs& e)
    {
        if (e.Key == AvInput::Key::Escape)
        {
            Close();
            e.Handled = true;
            return;
        }
        UserControl::OnKeyDown(e);
    }

    void SettingsView::BuildPages()
    {
        BuildDisplay(AddSection("Display"));
        BuildAudio(AddSection("Audio"));
        BuildControls(AddSection("Controls"));
        BuildProfile(AddSection("Profile"));
        BuildCredits(AddSection("Credits"));
    }

    void SettingsView::Heading(
        const std::shared_ptr<Controls::StackPanel>& page, const std::string& text)
    {
        auto caption = std::make_shared<Caption>(text);
        caption->Height(30);
        caption->Margin(Thickness(0, 8, 0, 4));
        page->Children.Add(caption);
    }

    void SettingsView::Explain(
        const std::shared_ptr<Controls::StackPanel>& page, const std::string& text,
        std::optional<Media::Color> color)
    {
        page->Children.Add(std::make_shared<Note>(text, color));
    }

    void SettingsView::BuildCredits(const std::shared_ptr<Controls::StackPanel>& page)
    {
        Heading(page, "Credits");
        Explain(page, Credits::Summary());
        page->Children.Add(std::make_shared<Caption>(std::string(Credits::Author)));
        page->Children.Add(std::make_shared<Note>(std::string(Credits::ForkWork)));

        auto support = std::make_shared<UiWord>("\xE2\x98\x95 Support this project", 15);
        auto supportUrl = std::make_shared<Note>("");
        supportUrl->IsVisible(false);
        support->Click += [supportUrl](UiWord&)
        {
            if (!Update::Updater::OpenLink(std::string(Credits::SupportUrl)))
            {
                supportUrl->Text(std::string(Credits::SupportUrl));
                supportUrl->IsVisible(true);
            }
        };
        page->Children.Add(support);
        page->Children.Add(supportUrl);

        Heading(page, "Built on");
        for (const Credits::Entry& entry : Credits::Entries())
        {
            page->Children.Add(std::make_shared<Caption>(entry.Who().value_or("")));
            std::string what = entry.What().value_or("");
            const std::string where = entry.Where().value_or("");
            if (!where.empty())
            {
                what += "\n" + where;
            }
            page->Children.Add(std::make_shared<Note>(what));
        }
    }

    std::int32_t SettingsView::FpsLimitStopIndex(std::int32_t cap)
    {
        for (std::size_t i = 0; i < FpsLimitStops.size(); ++i)
        {
            if (FpsLimitStops[i].Cap == cap)
            {
                return static_cast<std::int32_t>(i);
            }
        }
        std::int32_t best = 0;
        for (std::size_t i = 1; i < FpsLimitStops.size(); ++i)
        {
            if (FpsLimitStops[i].Cap <= cap)
            {
                best = static_cast<std::int32_t>(i);
            }
        }
        return best;
    }

    void SettingsView::BuildDisplay(const std::shared_ptr<Controls::StackPanel>& page)
    {
#if !defined(__ANDROID__)
        Heading(page, "Window");
        _windowRow = Add(page, std::make_shared<ChoiceRow>("Mode",
            std::vector<std::string>{"Windowed", "Fullscreen (borderless)"},
            LauncherPrefs::WindowMode() == WindowStartMode::BorderlessFullscreen ? 1 : 0));
#endif
        // The window (or the Android surface) is made for one backend, so
        // this is read at the next start.
#if defined(__ANDROID__)
        constexpr const char* ownGl = "OpenGL ES";
#else
        constexpr const char* ownGl = "OpenGL";
#endif
#if defined(__ANDROID__)
        constexpr const char* rendererLabel = "Renderer (next start)";
#else
        constexpr const char* rendererLabel = "Renderer";
#endif
        _rendererRow = Add(page, std::make_shared<ChoiceRow>(rendererLabel,
            std::vector<std::string>{ownGl, "Vulkan", "Auto"},
            LauncherPrefs::Renderer() == "vulkan" ? 1 : LauncherPrefs::Renderer() == "auto" ? 2 : 0));

        Heading(page, "View");
        _fovRow = Add(page, std::make_shared<SliderRow>("Field of view",
            RenderOptions::FieldOfView(), [](std::int32_t value)
            {
                return std::to_string(value) + "\xC2\xB0"
                    + (value == RenderOptions::DefaultFov ? " (DS)" : "");
            }, 120, RenderOptions::MinFov, RenderOptions::MaxFov, 1));
        _fovRow->ValueChanged += [this](SliderRow& row)
        {
            RenderOptions::FieldOfView(row.Value());
        };

        Heading(page, "Performance");
        _lowLatencyRow = Add(page, std::make_shared<ChoiceRow>("Low Latency",
            std::vector<std::string>{"Off", "On", "On + Boost"}, static_cast<std::int32_t>(LauncherPrefs::LowLatency())));
        _lowLatencyNote = std::make_shared<Note>("");
        const auto updateLatency = [this]
        {
            namespace Rhi = ::MphRead::NativeRuntime::Rhi;
            const auto mode = static_cast<Rhi::LowLatencyMode>(std::clamp(_lowLatencyRow->Index(), 0, 2));
            const auto state = Rhi::ResolveLowLatency(mode, Rhi::SceneLowLatencyCaps());
            _lowLatencyNote->Text(state.fallbackReason.empty()
                ? (state.effective == Rhi::LowLatencyMode::Off ? "Normal frame scheduling." : "On limits queued GPU work to one frame.")
                : std::string(state.fallbackReason));
        };
        _lowLatencyRow->Changed += [this, updateLatency](ChoiceRow&)
        {
            LauncherPrefs::LowLatency(static_cast<::MphRead::NativeRuntime::Rhi::LowLatencyMode>(std::clamp(_lowLatencyRow->Index(), 0, 2)));
            updateLatency();
        };
        updateLatency();
        page->Children.Add(_lowLatencyNote);
        _resolutionScale = Add(page, std::make_shared<SliderRow>("Render scale",
            RenderOptions::ResolutionScale(), [](std::int32_t value)
            {
                return std::to_string(std::max(RenderOptions::MinScale, value)) + "%";
            }));
        _fpsLimitRow = Add(page, std::make_shared<SliderRow>("FPS limit",
            FpsLimitStopIndex(FrameTiming::FrameRateCap()), [](std::int32_t value)
            {
                const std::int32_t index = std::clamp(value, 0,
                    static_cast<std::int32_t>(FpsLimitStops.size()) - 1);
                return std::string(FpsLimitStops[static_cast<std::size_t>(index)].Label);
            }, 120, 0, static_cast<std::int32_t>(FpsLimitStops.size()) - 1, 1));
        _lightingRow = Add(page, std::make_shared<ToggleRow>("Lighting", RenderOptions::Lighting()));
        _fogRow = Add(page, std::make_shared<ToggleRow>("Fog", RenderOptions::Fog()));
        _filteringRow = Add(page,
            std::make_shared<ToggleRow>("Texture filtering", RenderOptions::TextureFiltering()));
        _fpsRow = Add(page, std::make_shared<ToggleRow>("FPS counter", RenderOptions::ShowFps()));

        Heading(page, "Cel shading");
        _celRow = Add(page, std::make_shared<ToggleRow>("Cel shading", RenderOptions::CelShading()));

        Heading(page, "HUD");
        _proHud = Add(page, std::make_shared<ToggleRow>("Pro mode HUD", Features::ProHud()));
        _crosshairSizeRow = Add(page, std::make_shared<ChoiceRow>("Crosshair size",
            CopyNames(Crosshair::SizeNames), static_cast<std::int32_t>(Crosshair::Size)));
        _crosshairStyleRow = Add(page, std::make_shared<ChoiceRow>("Crosshair type",
            CopyNames(Crosshair::StyleNames), static_cast<std::int32_t>(Crosshair::Style)));
        _crosshairStyleRow->Preview([this](Media::DrawingContext& context, Rect area)
        {
            CrosshairPreview::Draw(context, area,
                static_cast<CrosshairStyle>(_crosshairStyleRow->Index()),
                static_cast<CrosshairSize>(_crosshairSizeRow->Index()));
        });
        _crosshairSizeRow->Changed += [this](ChoiceRow&)
        {
            _crosshairStyleRow->InvalidateVisual();
        };
        _weaponStyleRow = Add(page, std::make_shared<ChoiceRow>("Weapon",
            std::vector<std::string>{"Static (Quake)", "Dynamic (Metroid)"},
            Features::ProHudFixedWeapon() ? 0 : 1));
        _proHud->Changed += [this](ToggleRow&) { ShowCrosshairRows(); };
        ShowCrosshairRows();

        _radarRow = Add(page, std::make_shared<ToggleRow>("Radar", Radar::Enabled));
        _radarBackgroundRow = Add(page,
            std::make_shared<ToggleRow>("Radar background", Radar::ShowBackground));
        _radarOutlinesRow = Add(page,
            std::make_shared<ToggleRow>("Radar outlines", Radar::ShowOutlines));
        _radarRow->Changed += [this](ToggleRow&) { ShowRadarRows(); };
        ShowRadarRows();
    }

    void SettingsView::ShowCrosshairRows()
    {
        const bool visible = _proHud->On();
        _crosshairSizeRow->IsVisible(visible);
        _crosshairStyleRow->IsVisible(visible);
        _weaponStyleRow->IsVisible(visible);
    }

    void SettingsView::ShowRadarRows()
    {
        const bool visible = _radarRow->On();
        _radarBackgroundRow->IsVisible(visible);
        _radarOutlinesRow->IsVisible(visible);
    }

    std::int32_t SettingsView::Percent(std::string_view stored, std::int32_t fallback)
    {
        float parsed = 0.0F;
        if (!SingleTryParseInvariant(stored, parsed))
        {
            return fallback;
        }
        return std::clamp(MathRoundToInt32(static_cast<double>(parsed * 100.0F)), 0, 100);
    }

    void SettingsView::BuildAudio(const std::shared_ptr<Controls::StackPanel>& page)
    {
        Heading(page, "Volume");
        _sfxVolume = Add(page, std::make_shared<SliderRow>("Sound effects",
            Percent(_settings->SfxVolume, 35)));
        _musicVolume = Add(page, std::make_shared<SliderRow>("Music",
            Percent(_settings->MusicVolume, 50)));
        Heading(page, "Language");
        std::vector<std::string> languages;
        languages.reserve(LanguageNames.size());
        for (std::string_view language : LanguageNames)
        {
            languages.emplace_back(language);
        }
        const auto found = std::find(languages.begin(), languages.end(), _settings->Language);
        const std::int32_t index = found == languages.end() ? 0
            : static_cast<std::int32_t>(std::distance(languages.begin(), found));
        _languageRow = Add(page, std::make_shared<ChoiceRow>("Text",
            std::move(languages), index));
    }

    std::int32_t SettingsView::SensitivityToSlider(float sensitivity)
    {
        return std::clamp(MathRoundToInt32(static_cast<double>(sensitivity * 100.0F)), 1, 300);
    }

    float SettingsView::SliderToSensitivity(std::int32_t value)
    {
        return static_cast<float>(value) / 100.0F;
    }

    void SettingsView::BuildControls(const std::shared_ptr<Controls::StackPanel>& page)
    {
        auto keyboard = std::make_shared<Controls::StackPanel>();
        keyboard->Spacing(2);
        auto gamepad = std::make_shared<Controls::StackPanel>();
        gamepad->Spacing(2);
        gamepad->IsVisible(false);
        auto stylus = std::make_shared<Controls::StackPanel>();
        stylus->Spacing(2);
        stylus->IsVisible(false);

        _controlTabs = std::make_shared<UiTabs>(
            std::vector<std::string>{"Keyboard", "Gamepad", "Stylus"});
        _controlTabs->Margin(Thickness(0, 0, 0, 8));
        _controlTabs->Changed += [keyboard, gamepad, stylus](UiTabs& tabs)
        {
            keyboard->IsVisible(tabs.Index() == 0);
            gamepad->IsVisible(tabs.Index() == 1);
            stylus->IsVisible(tabs.Index() == 2);
        };
        page->Children.Add(_controlTabs);
        page->Children.Add(keyboard);
        page->Children.Add(gamepad);
        page->Children.Add(stylus);
        BuildKeyboard(keyboard);
        BuildGamepad(gamepad);
        BuildStylus(stylus);
    }

    void SettingsView::BuildKeyboard(const std::shared_ptr<Controls::StackPanel>& page)
    {
        Heading(page, "Mouse");
        _sensitivity = Add(page, std::make_shared<SliderRow>("Sensitivity",
            SensitivityToSlider(InputSettings::MouseSensitivity()),
            [](std::int32_t value)
            {
                return ::MphRead::NativeRuntime::ToStringInvariant(
                    SliderToSensitivity(value), "0.00") + "x";
            }, 120, 1, 300, 1));
        _invertY = Add(page, std::make_shared<ToggleRow>(
            "Invert vertical aim", InputSettings::InvertMouseY()));
        _invertX = Add(page, std::make_shared<ToggleRow>(
            "Invert horizontal aim", InputSettings::InvertMouseX()));

        auto advanced = std::make_shared<Controls::StackPanel>();
        advanced->Spacing(2);
        advanced->IsVisible(false);
        _scrollAllWeapons = Add(advanced, std::make_shared<ToggleRow>(
            "Wheel cycles every weapon", InputSettings::ScrollAllWeapons()));
        BuildTouchControls(advanced);

        auto advancedButton = std::make_shared<DeckButton>("Advanced",
            Deck::Face::Slate(), 0.9, 0.8, 0.38, 3);
        advancedButton->HorizontalAlignment(Layout::HorizontalAlignment::Left);
        advancedButton->Margin(Thickness(0, 8, 0, 4));
        ControllerNav::Identify(*advancedButton, "keyboard.advanced");
        advancedButton->Click += [advanced](DeckButton&)
        {
            advanced->IsVisible(!advanced->IsVisible());
        };
        page->Children.Add(advancedButton);
        page->Children.Add(advanced);

        Heading(page, "Keys");
        _keyRows.clear();
        _keyRows.push_back(Add(page, std::make_shared<KeyRow>("Chat",
            [] { return InputSettings::ChatKey(); },
            [](KeyRowGlfwKey key) { InputSettings::ChatKey(key); })));
        _keyRows.push_back(Add(page, std::make_shared<KeyRow>("Save clip",
            [] { return InputSettings::ClipKey(); },
            [](KeyRowGlfwKey key) { InputSettings::ClipKey(key); })));

        std::vector<std::string> lengths;
        lengths.reserve(std::size(Network::DemoClip::Lengths));
        for (std::int32_t seconds : Network::DemoClip::Lengths)
        {
            lengths.push_back(std::to_string(seconds) + " seconds");
        }
        const auto selected = std::find(std::begin(Network::DemoClip::Lengths),
            std::end(Network::DemoClip::Lengths), Network::DemoClip::Seconds());
        const std::int32_t selectedIndex = selected == std::end(Network::DemoClip::Lengths)
            ? 0 : static_cast<std::int32_t>(std::distance(
                std::begin(Network::DemoClip::Lengths), selected));
        _clipSecondsRow = Add(page, std::make_shared<ChoiceRow>("Clip length",
            std::move(lengths), selectedIndex));

        for (const InputBindingProperty& property : InputSettings::Bindings())
        {
            _keyRows.push_back(Add(page, std::make_shared<KeyRow>(property)));
        }
    }

    void SettingsView::BuildGamepad(const std::shared_ptr<Controls::StackPanel>& page)
    {
        Heading(page, "Controller");
        _gamepadSettings = Add(page, std::make_shared<GamepadSettingsPanel>());
        Heading(page, "Controller buttons");
        _padRows.clear();
        for (ModInput::PadAction action : ModInput::PadBindings::Actions())
        {
            _padRows.push_back(Add(page, std::make_shared<PadRow>(action)));
        }

        auto reset = std::make_shared<DeckButton>("Reset to defaults",
            Deck::Face::Brass(), 0.9, 0.8, 0.38, 3);
        reset->HorizontalAlignment(Layout::HorizontalAlignment::Left);
        reset->Margin(Thickness(0, 10, 0, 0));
        ControllerNav::Identify(*reset, "controller.reset");
        reset->Click += [this](DeckButton&)
        {
            InputSettings::Reset();
            _sensitivity->Value(SensitivityToSlider(InputSettings::MouseSensitivity()));
            _invertY->On(InputSettings::InvertMouseY());
            _invertX->On(InputSettings::InvertMouseX());
            _penTablet->On(ModInput::PointerInput::StylusMode());
            if (_repositionFilter != nullptr)
            {
                _repositionFilter->On(ModInput::PointerInput::GuardJumps());
            }
            if (_stylusZone != nullptr && _stylusOpacity != nullptr)
            {
                _stylusZone->On(ModInput::StylusZone::Wanted());
                _stylusOpacity->Value(MathRoundToInt32(
                    static_cast<double>(ModInput::StylusZone::Opacity() * 100.0F)));
            }
            ShowStylusRows();
            _scrollAllWeapons->On(InputSettings::ScrollAllWeapons());
            _gamepadSettings->Reload();
            for (const std::shared_ptr<PadRow>& row : _padRows)
            {
                row->InvalidateVisual();
            }
            for (const std::shared_ptr<KeyRow>& row : _keyRows)
            {
                row->InvalidateVisual();
            }
            if (_touchButtonsRow != nullptr)
            {
                _touchButtonsRow->On(ModInput::TouchSettings::ButtonsVisible);
            }
            for (const TouchRow& item : _touchRows)
            {
                item.Row->On(ModInput::TouchSettings::IsEnabled(item.Control));
            }
        };
        page->Children.Add(reset);
    }

    void SettingsView::BuildStylus(const std::shared_ptr<Controls::StackPanel>& page)
    {
        Heading(page, "Pen tablet");
        _penTablet = Add(page, std::make_shared<ToggleRow>(
            "Stylus mode", ModInput::PointerInput::StylusMode()));
        BuildStylusZone(page);
        _penTablet->Changed += [this](ToggleRow&) { ShowStylusRows(); };
        ShowStylusRows();
    }

    void SettingsView::BuildStylusZone(const std::shared_ptr<Controls::StackPanel>& page)
    {
#if defined(__ANDROID__)
        (void)page;
        return;
#else
        _stylusZone = Add(page, std::make_shared<ToggleRow>(
            "DS touch-screen zone", ModInput::StylusZone::Wanted()));
        _stylusRows.push_back(_stylusZone);

        auto place = std::make_shared<DeckButton>("Configure stylus zone",
            Deck::Face::Slate(), 0.9, 0.8, 0.38, 3);
        place->HorizontalAlignment(Layout::HorizontalAlignment::Left);
        place->Margin(Thickness(0, 8, 0, 0));
        ControllerNav::Identify(*place, "stylus.configure_zone");
        place->Click += [this](DeckButton&)
        {
            ModInput::StylusZone::BeginPlacement();
            StylusPlacementRequested(*this);
        };
        page->Children.Add(place);
        _stylusRows.push_back(place);

        _stylusAdvanced = std::make_shared<Controls::StackPanel>();
        _stylusAdvanced->Spacing(2);
        _stylusAdvanced->IsVisible(false);
        _repositionFilter = Add(_stylusAdvanced, std::make_shared<ToggleRow>(
            "Reposition filtering", ModInput::PointerInput::GuardJumps()));
        _stylusOpacity = Add(_stylusAdvanced, std::make_shared<SliderRow>(
            "Overlay opacity",
            MathRoundToInt32(static_cast<double>(ModInput::StylusZone::Opacity() * 100.0F)),
            [](std::int32_t value) { return std::to_string(value) + "%"; },
            120, 4, 60, 2));
        _stylusAdvanced->Children.Add(std::make_shared<Note>(
            "Reposition filtering ignores tablet jumps after lift/re-contact. The overlay opacity only affects the DS touch-screen guide."));
        _stylusAdvancedButton = std::make_shared<DeckButton>("Advanced",
            Deck::Face::Slate(), 0.9, 0.8, 0.38, 3);
        _stylusAdvancedButton->HorizontalAlignment(Layout::HorizontalAlignment::Left);
        _stylusAdvancedButton->Margin(Thickness(0, 8, 0, 4));
        ControllerNav::Identify(*_stylusAdvancedButton, "stylus.advanced");
        _stylusAdvancedButton->Click += [this](DeckButton&)
        {
            _stylusAdvancedOpen = !_stylusAdvancedOpen;
            ShowStylusRows();
        };
        page->Children.Add(_stylusAdvancedButton);
        page->Children.Add(_stylusAdvanced);
        _stylusRows.push_back(_stylusAdvancedButton);
#endif
    }

    void SettingsView::ShowStylusRows()
    {
        const bool enabled = _penTablet != nullptr && _penTablet->On();
        for (const Controls::ControlPtr& row : _stylusRows)
        {
            row->IsVisible(enabled);
        }
        if (_stylusAdvanced != nullptr)
        {
            _stylusAdvanced->IsVisible(enabled && _stylusAdvancedOpen);
        }
    }

    void SettingsView::BuildTouchControls(const std::shared_ptr<Controls::StackPanel>& page)
    {
#if !defined(__ANDROID__)
        (void)page;
        return;
#else
        Heading(page, "On-screen buttons");
        _touchButtonsRow = Add(page, std::make_shared<ToggleRow>(
            "Show on-screen buttons", ModInput::TouchSettings::ButtonsVisible));
        page->Children.Add(std::make_shared<Note>(
            "The stick, aiming, the double tap that jumps and the flick that boosts are not buttons, so they keep working with every one of these off."));
        for (const ModInput::TouchControlOrderEntry& entry : ModInput::TouchSettings::Order)
        {
            auto row = Add(page, std::make_shared<ToggleRow>(entry.Label,
                ModInput::TouchSettings::IsEnabled(entry.Control)));
            _touchRows.push_back(TouchRow{entry.Control, row});
        }
        _touchButtonsRow->Changed += [this](ToggleRow&) { ShowTouchRows(); };
        ShowTouchRows();
#endif
    }

    void SettingsView::ShowTouchRows()
    {
        const bool visible = _touchButtonsRow != nullptr && _touchButtonsRow->On();
        for (const TouchRow& item : _touchRows)
        {
            item.Row->IsVisible(visible);
        }
    }

    void SettingsView::BuildProfile(const std::shared_ptr<Controls::StackPanel>& page)
    {
        Heading(page, "You");
        _playerName = Add(page, std::make_shared<FieldRow>("Your name",
            LauncherPrefs::PlayerName(), 200));

        std::vector<std::string> hunters;
        hunters.reserve(8);
        for (std::int32_t i = 0; i < 7; ++i)
        {
            hunters.push_back(::MphRead::ToString(static_cast<Hunter>(i)));
        }
        hunters.push_back(::MphRead::ToString(Hunter::Random));
        const std::string savedHunter = ::MphRead::ToString(LauncherPrefs::LastHunter());
        const auto hunter = std::find(hunters.begin(), hunters.end(), savedHunter);
        _hunterRow = Add(page, std::make_shared<ChoiceRow>("Hunter", hunters,
            hunter == hunters.end() ? 0 : static_cast<std::int32_t>(
                std::distance(hunters.begin(), hunter))));

        std::vector<std::string> colors;
        colors.reserve(Network::PlayerColors::Count);
        for (std::int32_t i = 1; i <= Network::PlayerColors::Count; ++i)
        {
            colors.push_back(std::to_string(i));
        }
        _colorRow = Add(page, std::make_shared<ChoiceRow>("Suit colour", colors,
            Network::PlayerColors::Clamp(LauncherPrefs::LastColor())));

        Heading(page, "Servers");
        _serverRow = Add(page, std::make_shared<FieldRow>("Default server",
            LauncherPrefs::ServerAddress() + ":" + std::to_string(LauncherPrefs::ServerPort()), 220));
        _masterRow = Add(page, std::make_shared<FieldRow>("Server directory",
            LauncherPrefs::MasterHost() + ":" + std::to_string(LauncherPrefs::MasterPort()), 220));
        _autoUpdate = Add(page, std::make_shared<ToggleRow>("Check for updates on startup",
            LauncherPrefs::AutoUpdate()));

        Heading(page, "Game files");
        auto files = std::make_shared<UiWord>("Game files", 15);
        files->Click += [this](UiWord&)
        {
            GameFilesRequested(*this);
            Close();
        };
        page->Children.Add(files);
        page->Children.Add(std::make_shared<Note>(GameFiles::Describe(),
            GameFiles::Ready() ? std::optional<Media::Color>(GuiTheme::Good)
                : std::optional<Media::Color>(GuiTheme::Warm)));

        BuildDebugLogs(page);

        Heading(page, "Hunter");
        std::vector<std::string> standNames;
        standNames.reserve(HunterStand::Names.size());
        for (std::string_view name : HunterStand::Names)
        {
            standNames.emplace_back(name);
        }
        const std::string savedStand = ::MphRead::ToString(LauncherPrefs::LastHunter());
        const auto standIt = std::find(standNames.begin(), standNames.end(), savedStand);
        const std::int32_t standIndex = standIt == standNames.end() ? 0
            : static_cast<std::int32_t>(std::distance(standNames.begin(), standIt));
        auto standRow = Add(page, std::make_shared<ChoiceRow>("Default hunter",
            standNames, standIndex));
        auto stand = std::make_shared<HunterStand>();
        stand->Height(150);
        stand->Margin(Thickness(0, 4, 0, 4));
        stand->Name2(standNames[static_cast<std::size_t>(standRow->Index())]);
        standRow->Changed += [stand, standNames](ChoiceRow& row)
        {
            stand->Name2(standNames[static_cast<std::size_t>(row.Index())]);
        };
        page->Children.Add(stand);

        Heading(page, "Account");
        auto signIn = std::make_shared<UiWord>("Sign in", 15, nullptr, GuiTheme::Accent);
        auto signInNote = std::make_shared<Note>("");
        signInNote->IsVisible(false);
        signIn->Click += [signInNote](UiWord&)
        {
            signInNote->Text("Ranking feature will be coming soon!");
            signInNote->IsVisible(true);
        };
        page->Children.Add(signIn);
        page->Children.Add(signInNote);
    }

    void SettingsView::BuildDebugLogs(const std::shared_ptr<Controls::StackPanel>& page)
    {
        Heading(page, "Debugging");
        auto row = std::make_shared<ToggleRow>("Write a debugging log",
            LauncherPrefs::DebugLogs());
        auto where = std::make_shared<Note>("");
        _shareLogs = std::make_shared<UiWord>("\xE2\x86\x97 Share logs", 15,
            nullptr, GuiTheme::TextDim);
        _shareLogs->IsVisible(::MphRead::Mods::LogShare::Available());
        _shareError = std::make_shared<Note>("", GuiTheme::Warm);
        _shareError->IsVisible(false);

        row->Changed += [this, row, where](ToggleRow&)
        {
            LauncherPrefs::DebugLogs(row->On());
            LauncherPrefs::Save();
            if (row->On())
            {
                DebugLog::Attach();
                DebugLog::Line("launcher", "debug logging turned on from the settings");
            }
            else
            {
                DebugLog::Line("launcher", "debug logging turned off from the settings");
                DebugLog::Detach();
            }
            where->Text(LogLocation());
            _shareLogs->IsVisible(::MphRead::Mods::LogShare::Available());
        };
        where->Text(LogLocation());
        page->Children.Add(row);
        page->Children.Add(where);
        _shareLogs->Click += [this](UiWord&) { ShareLogs(); };
        page->Children.Add(_shareLogs);
        page->Children.Add(_shareError);
    }

    std::string SettingsView::LogLocation()
    {
        if (!LauncherPrefs::DebugLogs())
        {
            return "Everything this build can say about itself, written to a file for a bug report.";
        }
        const std::optional<std::string> path = DebugLog::Path();
        return path.has_value() ? "Writing to " + *path : "Logging starts with the next run.";
    }

    void SettingsView::ShareLogs()
    {
        if (_sharing || _shareLogs == nullptr)
        {
            return;
        }
        const std::shared_ptr<::MphRead::Mods::ILogShare> sharer
            = ::MphRead::Mods::LogShare::Current();
        if (sharer == nullptr)
        {
            return;
        }

        _sharing = true;
        _shareLogs->Text("\xE2\x86\x97 Zipping\xE2\x80\xA6");
        const std::u16string name = ::MphRead::Mods::LogArchive::FileName();
        const std::weak_ptr<std::uint8_t> lifetime = _lifetime;
        std::thread([this, lifetime, sharer, name]
        {
            std::u16string path;
            std::u16string error;
            bool built = false;
            try
            {
                path = sharer->StagingPath(name);
            }
            catch (const std::exception& exception)
            {
                error = Utf8ToUtf16(exception.what());
            }
            if (error.empty())
            {
                built = ::MphRead::Mods::LogArchive::Create(path, error);
            }

            Threading::Dispatcher::UIThread().Post(
                [this, lifetime, sharer, name, path = std::move(path),
                    error = std::move(error), built]() mutable
                {
                    if (lifetime.expired())
                    {
                        return;
                    }
                    if (built)
                    {
                        built = sharer->Share(path, name, error);
                    }
                    _sharing = false;
                    _shareLogs->Text("\xE2\x86\x97 Share logs");
                    _shareError->Text(Utf16ToUtf8(error));
                    _shareError->IsVisible(!built);
                });
        }).detach();
    }

    bool SettingsView::ParseEndpoint(std::string_view input, std::string& host,
        std::int32_t& port)
    {
        const std::string text = StringTrim(input);
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
        if (!Int32TryParseInvariant(std::string_view(text).substr(colon + 1), parsed)
            || parsed < 1 || parsed > 65535)
        {
            return false;
        }
        host = text.substr(0, colon);
        port = parsed;
        return true;
    }

    void SettingsView::TryCommit()
    {
        try
        {
            Commit();
        }
        catch (const std::exception& exception)
        {
            _saveError->Text(std::string("Could not save: ") + exception.what());
            _saveError->IsVisible(true);
        }
    }

    void SettingsView::Commit()
    {
        ::MphRead::MenuSettings& settings = RequireReference(_settings);
        if (_lowLatencyRow) LauncherPrefs::LowLatency(static_cast<::MphRead::NativeRuntime::Rhi::LowLatencyMode>(std::clamp(_lowLatencyRow->Index(), 0, 2)));
        if (_rendererRow != nullptr)
        {
            static constexpr std::array<const char*, 3> Renderers{"opengl", "vulkan", "auto"};
            const char* chosen = Renderers[static_cast<std::size_t>(std::clamp(_rendererRow->Index(), 0, 2))];
            LauncherPrefs::Renderer(chosen);
#if !defined(__ANDROID__)
            // Applied now: the window is remade on the chosen renderer.
            ::MphRead::NativeRuntime::Rhi::SceneBackendRequest request{};
            if (::MphRead::NativeRuntime::Rhi::ParseSceneBackendRequest(chosen, request))
                Shell::RequestRenderer(request, true);
#endif
        }
        if (_windowRow != nullptr)
        {
            const WindowStartMode mode = _windowRow->Index() == 1
                ? WindowStartMode::BorderlessFullscreen : WindowStartMode::Windowed;
            LauncherPrefs::WindowMode(mode);
            WindowMode::Startup(mode);
            const bool wantFullscreen = mode == WindowStartMode::BorderlessFullscreen;
            if (wantFullscreen != WindowMode::IsFullscreen())
            {
                PauseMenu::RequestFullscreenToggle();
            }
        }
        if (_clipSecondsRow != nullptr)
        {
            const std::int32_t index = std::clamp(_clipSecondsRow->Index(), 0,
                static_cast<std::int32_t>(std::size(Network::DemoClip::Lengths)) - 1);
            Network::DemoClip::Seconds(Network::DemoClip::Lengths[index]);
        }

        settings.ResolutionScale = ToStringInvariant(
            std::max(RenderOptions::MinScale, _resolutionScale->Value()));
        RenderOptions::FieldOfView(_fovRow->Value());
        settings.FieldOfView = ToStringInvariant(_fovRow->Value());
        settings.Lighting = std::string(RenderOptions::OnOff(_lightingRow->On()));
        settings.Fog = std::string(RenderOptions::OnOff(_fogRow->On()));
        settings.TextureFiltering = std::string(RenderOptions::OnOff(_filteringRow->On()));
        settings.ShowFps = std::string(RenderOptions::OnOff(_fpsRow->On()));
        const std::int32_t fpsIndex = std::clamp(_fpsLimitRow->Value(), 0,
            static_cast<std::int32_t>(FpsLimitStops.size()) - 1);
        const std::int32_t cap = FpsLimitStops[static_cast<std::size_t>(fpsIndex)].Cap;
        FrameTiming::SetFrameRateCap(cap);
        settings.FrameRateCap = FrameTiming::CapString(cap);
        settings.CelShading = std::string(RenderOptions::OnOff(_celRow->On()));
        settings.CelBands = "8";
        settings.CelEdge = "50";
        Features::ProHud(_proHud->On());
        Crosshair::Size = static_cast<CrosshairSize>(_crosshairSizeRow->Index());
        Crosshair::Style = static_cast<CrosshairStyle>(_crosshairStyleRow->Index());
        Features::ProHudFixedWeapon(_weaponStyleRow->Index() == 0);
        Radar::Enabled = _radarRow->On();
        Radar::ShowBackground = _radarBackgroundRow->On();
        Radar::ShowOutlines = _radarOutlinesRow->On();

        settings.SfxVolume = ToStringInvariant(
            static_cast<float>(_sfxVolume->Value()) / 100.0F);
        settings.MusicVolume = ToStringInvariant(
            static_cast<float>(_musicVolume->Value()) / 100.0F);
        settings.Language = _languageRow->Value();

        InputSettings::MouseSensitivity(SliderToSensitivity(_sensitivity->Value()));
        InputSettings::InvertMouseY(_invertY->On());
        InputSettings::InvertMouseX(_invertX->On());
        ModInput::PointerInput::StylusMode(_penTablet->On());
        if (_repositionFilter != nullptr)
        {
            ModInput::PointerInput::GuardJumps(_repositionFilter->On());
        }
        if (_stylusZone != nullptr && _stylusOpacity != nullptr)
        {
            ModInput::StylusZone::Enabled(_stylusZone->On());
            ModInput::StylusZone::Opacity(std::clamp(
                static_cast<float>(_stylusOpacity->Value()) / 100.0F, 0.02F, 1.0F));
        }
        InputSettings::ScrollAllWeapons(_scrollAllWeapons->On());
        if (_touchButtonsRow != nullptr)
        {
            ModInput::TouchSettings::ButtonsVisible = _touchButtonsRow->On();
            for (const TouchRow& item : _touchRows)
            {
                ModInput::TouchSettings::SetEnabled(item.Control, item.Row->On());
            }
        }
        InputSettings::Save();
        InputSettings::ApplyToPlayers();

        const std::string playerName = StringTrim(_playerName->Value());
        if (!playerName.empty())
        {
            LauncherPrefs::PlayerName(playerName);
        }
        Hunter hunter{};
        if (!::MphRead::TryParse(_hunterRow->Value(), false, hunter))
        {
            throw std::invalid_argument("Requested value was not found.");
        }
        LauncherPrefs::LastHunter(hunter);
        std::int32_t suit = 0;
        if (Int32TryParseCurrentCulture(_colorRow->Value(), suit))
        {
            LauncherPrefs::LastColor(Network::PlayerColors::Clamp(suit - 1));
        }
        RespawnChoice::Request(LauncherPrefs::LastHunter(), LauncherPrefs::LastColor());

        std::string host = LauncherPrefs::ServerAddress();
        std::int32_t port = LauncherPrefs::ServerPort();
        if (ParseEndpoint(_serverRow->Value(), host, port))
        {
            LauncherPrefs::ServerAddress(host);
            LauncherPrefs::ServerPort(port);
        }
        std::string masterHost = LauncherPrefs::MasterHost();
        std::int32_t masterPort = LauncherPrefs::MasterPort();
        if (ParseEndpoint(_masterRow->Value(), masterHost, masterPort))
        {
            LauncherPrefs::MasterHost(masterHost);
            LauncherPrefs::MasterPort(masterPort);
        }
        LauncherPrefs::AutoUpdate(_autoUpdate->On());

        GameState::CommitSettings(_settings);
        LauncherPrefs::Save();
        GameSettings::Apply(_settings);
        _saved = true;
        Close();
    }
}
