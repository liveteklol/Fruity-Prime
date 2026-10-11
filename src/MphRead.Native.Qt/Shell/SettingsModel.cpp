#include "SettingsModel.hpp"
#include "../../MphRead.Native/Mods/Input/KeyCapture.hpp"

#include "ShellBridge.hpp"
#include "../Platform/QtKeys.hpp"

#include "../../MphRead.Native/Features.hpp"
#include "../../MphRead.Native/GameState.hpp"
#include "../../MphRead.Native/Menu.hpp"
#include "../../MphRead.Native/Mods/Credits.hpp"
#include "../../MphRead.Native/Mods/DebugLog.hpp"
#include "../../MphRead.Native/Mods/GameSettings.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadCalibration.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadHaptics.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadMappingWizard.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadMappings.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadManager.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadOptions.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadProfiles.hpp"
#include "../../MphRead.Native/Mods/Input/GamepadUiRouter.hpp"
#include "../../MphRead.Native/Mods/Input/PadBindings.hpp"
#include "../../MphRead.Native/Mods/Input/PointerInput.hpp"
#include "../../MphRead.Native/Mods/Input/StylusZone.hpp"
#include "../../MphRead.Native/Mods/InputSettings.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/GameFiles.hpp"
#include "../../MphRead.Native/Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "../../MphRead.Native/Mods/LogShare.hpp"
#include "../../MphRead.Native/Mods/Network/DemoClip.hpp"
#include "../../MphRead.Native/Mods/Network/PlayerColors.hpp"
#include "../../MphRead.Native/Mods/PauseMenu.hpp"
#include "../../MphRead.Native/Mods/Render/Crosshair.hpp"
#include "../../MphRead.Native/Mods/Render/FrameTiming.hpp"
#include "../../MphRead.Native/Mods/Render/Radar.hpp"
#include "../../MphRead.Native/Mods/RenderOptions.hpp"
#include "../../MphRead.Native/Mods/RespawnChoice.hpp"
#include "../../MphRead.Native/Mods/Update/Updater.hpp"
#include "../../MphRead.Native/Mods/WindowMode.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/LowLatency.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/SceneBackend.hpp"
#if defined(__ANDROID__)
#include "../../MphRead.Native.Android/AndroidGlContextGate.hpp"
#endif
#include "../../MphRead.Native/Mods/Launcher/Shell.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Globalization.hpp"
#include "../../MphRead.Native/NativeRuntime/System/IO.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Runtime.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Number.hpp"

#include <QtGui/QKeyEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace MphRead::Qt
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;
        namespace Mods = ::MphRead::Mods;
        namespace Render = ::MphRead::Mods::Render;
        namespace Input = ::MphRead::Mods::Input;
        using ::MphRead::Mods::Launcher::LauncherPrefs;
        using ::MphRead::Mods::Launcher::GameFiles;

        const QColor Good(0x5f, 0x9e, 0x72);
        const QColor Warm(255, 179, 71);
        const QColor Accent(255, 179, 71);
        const QColor TextDim(138, 147, 166);

        [[nodiscard]] QString Q(std::string_view text)
        {
            return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
        }

        const std::array<std::int32_t, 16> FpsLimitStops{
            30, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240,
            360, 480, 540, 1000, Render::FrameTiming::Unlimited
        };

        const std::array<const char*, 6> LanguageNames{{
            "English", "Japanese", "French", "Spanish", "German", "Italian"
        }};

        const std::array<const char*, 5> Presets{{
            "Default", "Bumper Jumper", "Southpaw", "Classic", "Custom"
        }};

        const std::array<const char*, 7> StandNames{{
            "Samus", "Kanden", "Trace", "Sylux", "Noxus", "Spire", "Weavel"
        }};

        const std::array<const char*, 4> Resolutions{{"Swap", "Replace", "Keep Both", "Cancel"}};

        [[nodiscard]] std::int32_t Percent(std::string_view stored, std::int32_t fallback)
        {
            float parsed = 0.0F;
            if (!Runtime::SingleTryParseInvariant(stored, parsed))
            {
                return fallback;
            }
            return std::clamp(Runtime::MathRoundToInt32(static_cast<double>(parsed * 100.0F)), 0, 100);
        }

        [[nodiscard]] std::int32_t SensitivityToSlider(float sensitivity)
        {
            return std::clamp(Runtime::MathRoundToInt32(static_cast<double>(sensitivity * 100.0F)), 1, 300);
        }

        [[nodiscard]] float SliderToSensitivity(std::int32_t value)
        {
            return static_cast<float>(value) / 100.0F;
        }

        [[nodiscard]] bool ParseEndpoint(std::string_view input, std::string& host, std::int32_t& port)
        {
            const std::string text = Runtime::StringTrim(input);
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

        [[nodiscard]] QString LogLocation()
        {
            if (!LauncherPrefs::DebugLogs())
            {
                return QStringLiteral("Everything this build can say about itself, written to a file for a bug report.");
            }
            const std::optional<std::string> path = Mods::DebugLog::Path();
            return path.has_value() ? QStringLiteral("Writing to ") + Q(*path)
                                    : QStringLiteral("Logging starts with the next run.");
        }

        // Row builders, in SettingsView's vocabulary.
        [[nodiscard]] Row Heading(const QString& text)
        {
            Row row;
            row.Type = QStringLiteral("caption");
            row.Label = text;
            return row;
        }

        [[nodiscard]] Row Caption(const QString& text)
        {
            Row row;
            row.Type = QStringLiteral("caption");
            row.Label = text;
            // A bare Caption, without Heading's height and margins.
            row.Face = QStringLiteral("bare");
            return row;
        }

        [[nodiscard]] Row NoteRow(const QString& text, QColor colour = QColor(), int lines = 2)
        {
            Row row;
            row.Type = QStringLiteral("note");
            row.Text = text;
            row.Colour = colour;
            row.Lines = lines;
            return row;
        }

        [[nodiscard]] Row Choice(const QString& id, const QString& label, QStringList options, int index)
        {
            Row row;
            row.Type = QStringLiteral("choice");
            row.Id = id;
            row.Label = label;
            row.Index = options.isEmpty() ? 0 : std::clamp(index, 0, static_cast<int>(options.size()) - 1);
            row.Options = std::move(options);
            return row;
        }

        [[nodiscard]] Row Slider(const QString& id, const QString& label, int value,
            std::function<QString(int)> format = nullptr, double labelWidth = 120, int min = 0, int max = 100,
            int step = 5)
        {
            Row row;
            row.Type = QStringLiteral("slider");
            row.Id = id;
            row.Label = label;
            row.Min = min;
            row.Max = std::max(min + 1, max);
            row.Value = std::clamp(value, row.Min, row.Max);
            row.Step = std::max(1, step);
            row.LabelWidth = labelWidth;
            row.Format = format ? std::move(format) : [](int v) { return QString::number(v) + QStringLiteral("%"); };
            return row;
        }

        [[nodiscard]] Row Toggle(const QString& id, const QString& label, bool on)
        {
            Row row;
            row.Type = QStringLiteral("toggle");
            row.Id = id;
            row.Label = label;
            row.On = on;
            return row;
        }

        [[nodiscard]] Row Field(const QString& id, const QString& label, const QString& value, double boxWidth)
        {
            Row row;
            row.Type = QStringLiteral("field");
            row.Id = id;
            row.Label = label;
            row.Text = value;
            row.BoxWidth = boxWidth;
            return row;
        }

        [[nodiscard]] Row Word(const QString& id, const QString& text, QColor colour = QColor())
        {
            Row row;
            row.Type = QStringLiteral("word");
            row.Id = id;
            row.Text = text;
            row.Colour = colour;
            return row;
        }

        [[nodiscard]] Row Button(const QString& id, const QString& text, const QString& face, double top, double bottom)
        {
            Row row;
            row.Type = QStringLiteral("button");
            row.Id = id;
            row.Text = text;
            row.Face = face;
            row.Top = top;
            row.Bottom = bottom;
            return row;
        }

        [[nodiscard]] QString Hunter(std::size_t index)
        {
            return QString::fromLatin1(StandNames[std::min<std::size_t>(index, StandNames.size() - 1)]);
        }
    }

    // PadRow's capture, for the one row listening at a time.
    struct SettingsModel::PadCapture
    {
        int Row = -1;
        Input::PadAction Action{};
        bool Listening = false;
        bool Picking = false;
        int PickIndex = 0;
        int Choice = 0;
        Input::GamepadButtons Pending = Input::GamepadButtons::None;
        Input::GamepadButtons Modifier = Input::GamepadButtons::None;
        Input::GamepadButtons Baseline = Input::GamepadButtons::None;
        std::int64_t DeviceRevision = 0;
        std::optional<std::string> Conflict;
        std::optional<std::string> Message;
    };

    // GamepadSetupPanel's run: calibration or manual mapping.
    struct SettingsModel::PadSetup
    {
        Input::GamepadButtons Buttons = Input::GamepadButtons::None;
        std::unique_ptr<Input::GamepadCalibration> Calibration;
        std::unique_ptr<Input::GamepadMappingWizard> Mapping;
        std::optional<std::string> Device;
        std::int64_t Started = 0;
        std::int64_t Revision = 0;
        bool MappingMode = false;
        bool Complete = false;
        bool CanApply = false;
    };

    SettingsModel::SettingsModel(QObject* parent)
        : QObject(parent), _pad(std::make_unique<PadCapture>()), _setup(std::make_unique<PadSetup>())
    {
        _setupStatus = QStringLiteral(
            "Calibration measures drift and trigger travel. Manual mapping is available on desktop.");
        _keyEdges = std::make_unique<Input::GamepadEdges>();
        _keyTimer.setInterval(16);
        connect(&_keyTimer, &QTimer::timeout, this, [this]() { KeyTick(); });
        _setupTimer.setInterval(50);
        connect(&_setupTimer, &QTimer::timeout, this, [this]() { SetupTick(); });
        if (ShellBridge* const bridge = ShellBridge::Current())
        {
            _settings = bridge->Settings();
        }
        if (_settings == nullptr)
        {
            _settings = std::make_shared<::MphRead::MenuSettings>();
        }
        _originalLowLatency = static_cast<int>(LauncherPrefs::LowLatency());
        BuildDisplay();
        BuildAudio();
        BuildKeyboard();
        BuildGamepad();
        BuildStylus();
        BuildProfile();
        BuildCredits();

        _padTimer.setInterval(30);
        connect(&_padTimer, &QTimer::timeout, this, [this]() { PadTick(); });
        _deviceTimer.setInterval(100);
        connect(&_deviceTimer, &QTimer::timeout, this, [this]()
        {
            const Input::GamepadSnapshot snapshot = Input::GamepadManager::Snapshot();
            if ((_profileRevision != Input::GamepadProfiles::Revision() || _runtimeRevision != snapshot.Revision)
                && !Input::GamepadContexts::Capturing())
            {
                BuildGamepad();
            }
            RefreshDevices();
            _gamepad.Refresh();
        });
        _deviceTimer.start();
    }

    SettingsModel::~SettingsModel()
    {
        stopKey();
        if (!_saved)
        {
            Mods::RenderOptions::FieldOfView(Mods::RenderOptions::ParseFov(
                std::string_view(_settings->FieldOfView), Mods::RenderOptions::DefaultFov));
            LauncherPrefs::LowLatency(static_cast<NativeRuntime::Rhi::LowLatencyMode>(_originalLowLatency));
        }
        if (_setup->Device.has_value())
        {
            Input::GamepadContexts::Capturing(false);
            Input::GamepadMappingWizard::RequestedDevice.reset();
        }
        if (_pad->Listening)
        {
            Input::GamepadContexts::Capturing(false);
        }
    }

    void SettingsModel::SetInGame(bool value)
    {
        if (_inGame == value)
        {
            return;
        }
        _inGame = value;
        emit inGameChanged();
    }

    Row* SettingsModel::Get(RowModel& model, const QString& id)
    {
        return model.Find(id);
    }

    int SettingsModel::IndexOf(RowModel& model, const QString& id) const
    {
        const std::vector<Row>& rows = model.Rows();
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].Id == id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // ------------------------------------------------------------ Display

    namespace
    {
        // "Vulkan 1.1": the API version the GPU offers, or why it cannot be
        // chosen here.
        QString VulkanChoiceLabel()
        {
            const auto& vulkan = NativeRuntime::Rhi::ProbeVulkanSupport();
            if (!vulkan.Available) return QStringLiteral("Vulkan (not supported)");
            return vulkan.Version.empty() ? QStringLiteral("Vulkan")
                : QStringLiteral("Vulkan ") + QString::fromStdString(vulkan.Version);
        }

        // 0 OpenGL, 1 Vulkan.
        int RendererChoice()
        {
            const std::string renderer = LauncherPrefs::Renderer();
            if (renderer == "auto")
                return !NativeRuntime::Rhi::AutoFellBack() && NativeRuntime::Rhi::ProbeVulkanSupport().Available ? 1 : 0;
            return renderer == "vulkan" ? 1 : 0;
        }
    }

    void SettingsModel::BuildDisplay()
    {
        std::vector<Row> rows;
#if !defined(__ANDROID__)
        // Windowed or fullscreen; how fullscreen is done is a checkbox under
        // it, not a third mode: exclusive unless asked otherwise, since the
        // borderless kind is drawn through the desktop compositor. Always
        // shown -- it is also what the pause menu's Fullscreen and F11 enter
        // from a windowed start.
        rows.push_back(Heading(QStringLiteral("Window")));
        rows.push_back(Choice(QStringLiteral("window"), QStringLiteral("Mode"),
            {QStringLiteral("Windowed"), QStringLiteral("Fullscreen")},
            LauncherPrefs::WindowMode() == Mods::WindowStartMode::Windowed ? 0 : 1));
        rows.push_back(Toggle(QStringLiteral("windowedFullscreen"), QStringLiteral("When fullscreen, use a borderless fullscreen window"),
            LauncherPrefs::WindowedFullscreen()));
        // Shown from the start, not only once the box is ticked: what the box
        // costs is part of what it means, and a warning that appears as it is
        // ticked reads as something having gone wrong.
        rows.push_back(NoteRow(QStringLiteral(
            "On: a borderless fullscreen window, drawn through the desktop compositor (DWM), which adds "
            "display latency. Off: exclusive fullscreen, the lowest latency."),
            QColor(0xc0, 0x8a, 0x3e))); // Theme.warn
#endif
        // Switched in place on save: the window is remade on the chosen
        // renderer and the match and these menus carry on. A preference never
        // chosen ("auto") shows as what it runs: Vulkan where it can start.
        rows.push_back(Choice(QStringLiteral("renderer"), QStringLiteral("Renderer"),
            {QStringLiteral("OpenGL"), VulkanChoiceLabel()}, RendererChoice()));

        rows.push_back(Heading(QStringLiteral("View")));
        Row fov = Slider(QStringLiteral("fov"), QStringLiteral("Field of view"), Mods::RenderOptions::FieldOfView(),
            [](int value)
            {
                return QString::number(value) + QStringLiteral("°")
                    + (value == Mods::RenderOptions::DefaultFov ? QStringLiteral(" (DS)") : QString());
            },
            120, Mods::RenderOptions::MinFov, Mods::RenderOptions::MaxFov, 1);
        fov.Changed = [](Row& row) { Mods::RenderOptions::FieldOfView(row.Value); };
        rows.push_back(std::move(fov));

        rows.push_back(Heading(QStringLiteral("Performance")));
        // Live, as the field of view is, and put back by cancel.
        Row latency = Choice(QStringLiteral("lowLatency"), QStringLiteral("Low Latency"),
            {QStringLiteral("Off"), QStringLiteral("On"), QStringLiteral("On + Boost")},
            static_cast<int>(LauncherPrefs::LowLatency()));
        latency.Changed = [](Row& row)
        {
            LauncherPrefs::LowLatency(static_cast<NativeRuntime::Rhi::LowLatencyMode>(std::clamp(row.Index, 0, 2)));
        };
        rows.push_back(std::move(latency));
        Row latencyNote = NoteRow(QString());
        latencyNote.Live = []()
        {
            namespace Rhi = NativeRuntime::Rhi;
            const auto state = Rhi::ResolveLowLatency(LauncherPrefs::LowLatency(), Rhi::SceneLowLatencyCaps());
            if (!state.fallbackReason.empty())
            {
                return QString::fromStdString(state.fallbackReason);
            }
            if (state.effective == Rhi::LowLatencyMode::Off)
            {
                return QStringLiteral("Normal frame scheduling.");
            }
            return state.provider == Rhi::LowLatencyProvider::Nvidia
                ? QStringLiteral("NVIDIA Reflex paces the frames.")
                : QStringLiteral("On limits queued GPU work to one frame.");
        };
        rows.push_back(std::move(latencyNote));
        rows.push_back(Slider(QStringLiteral("scale"), QStringLiteral("Render scale"),
            Mods::RenderOptions::ResolutionScale(),
            [](int value) { return QString::number(std::max(Mods::RenderOptions::MinScale, value)) + QStringLiteral("%"); }));
        rows.push_back(Toggle(QStringLiteral("vsync"), QStringLiteral("VSync"), Render::FrameTiming::VSync()));
        const auto currentCap = Render::FrameTiming::FrameRateCap();
        _fpsLimitCaps.assign(FpsLimitStops.begin(), FpsLimitStops.end());
        auto current = std::find(_fpsLimitCaps.begin(), _fpsLimitCaps.end(), currentCap);
        if (current == _fpsLimitCaps.end())
        {
            // Retain custom CLI/saved limits when another Settings row is saved.
            current = _fpsLimitCaps.insert(std::lower_bound(_fpsLimitCaps.begin(),
                _fpsLimitCaps.end() - 1, currentCap), currentCap);
        }
        const auto currentIndex = static_cast<int>(current - _fpsLimitCaps.begin());
        rows.push_back(Slider(QStringLiteral("fpsLimit"), QStringLiteral("FPS limit"),
            currentIndex,
            [caps = _fpsLimitCaps](int value)
            {
                const int index = std::clamp(value, 0, static_cast<int>(caps.size()) - 1);
                const auto cap = caps[static_cast<std::size_t>(index)];
                return cap == Render::FrameTiming::Unlimited ? QStringLiteral("Unlimited")
                    : cap == Render::FrameTiming::DisplayRate ? QStringLiteral("Display")
                    : QString::number(cap) + QStringLiteral(" fps");
            },
            120, 0, static_cast<int>(_fpsLimitCaps.size()) - 1, 1));
        rows.push_back(Toggle(QStringLiteral("lighting"), QStringLiteral("Lighting"), Mods::RenderOptions::Lighting()));
        rows.push_back(Toggle(QStringLiteral("fog"), QStringLiteral("Fog"), Mods::RenderOptions::Fog()));
        rows.push_back(Toggle(QStringLiteral("filtering"), QStringLiteral("Texture filtering"),
            Mods::RenderOptions::TextureFiltering()));
        rows.push_back(Toggle(QStringLiteral("fps"), QStringLiteral("FPS counter"), Mods::RenderOptions::ShowFps()));
        rows.push_back(Toggle(QStringLiteral("performance"), QStringLiteral("Performance mode"),
            Mods::RenderOptions::PerformanceMode()));

        rows.push_back(Heading(QStringLiteral("Cel shading")));
        rows.push_back(Toggle(QStringLiteral("cel"), QStringLiteral("Cel shading"), Mods::RenderOptions::CelShading()));

        rows.push_back(Heading(QStringLiteral("HUD")));
        rows.push_back(Toggle(QStringLiteral("proHud"), QStringLiteral("Pro mode HUD"), ::MphRead::Features::ProHud()));
        QStringList sizes;
        for (const std::string& name : Render::Crosshair::SizeNames)
        {
            sizes.push_back(Q(name));
        }
        QStringList styles;
        for (const std::string& name : Render::Crosshair::StyleNames)
        {
            styles.push_back(Q(name));
        }
        const auto proHud = [this]()
        {
            const Row* row = _display.Find(QStringLiteral("proHud"));
            return row != nullptr && row->On;
        };
        Row size = Choice(QStringLiteral("crosshairSize"), QStringLiteral("Crosshair size"), sizes,
            static_cast<int>(Render::Crosshair::Size));
        size.Shown = proHud;
        rows.push_back(std::move(size));
        Row style = Choice(QStringLiteral("crosshairStyle"), QStringLiteral("Crosshair type"), styles,
            static_cast<int>(Render::Crosshair::Style));
        style.Preview = QStringLiteral("crosshair");
        style.Shown = proHud;
        style.Extra = [this]()
        {
            const Row* row = _display.Find(QStringLiteral("crosshairSize"));
            return QVariantMap{{QStringLiteral("size"), row != nullptr ? row->Index : 0}};
        };
        rows.push_back(std::move(style));
        Row weapon = Choice(QStringLiteral("weapon"), QStringLiteral("Weapon"),
            {QStringLiteral("Static (Quake)"), QStringLiteral("Dynamic (Metroid)")},
            ::MphRead::Features::ProHudFixedWeapon() ? 0 : 1);
        weapon.Shown = proHud;
        rows.push_back(std::move(weapon));

        rows.push_back(Toggle(QStringLiteral("radar"), QStringLiteral("Radar"), Render::Radar::Enabled));
        const auto radar = [this]()
        {
            const Row* row = _display.Find(QStringLiteral("radar"));
            return row != nullptr && row->On;
        };
        Row background = Toggle(QStringLiteral("radarBackground"), QStringLiteral("Radar background"),
            Render::Radar::ShowBackground);
        background.Shown = radar;
        rows.push_back(std::move(background));
        Row outlines = Toggle(QStringLiteral("radarOutlines"), QStringLiteral("Radar outlines"),
            Render::Radar::ShowOutlines);
        outlines.Shown = radar;
        rows.push_back(std::move(outlines));
        _display.Reset(std::move(rows));
    }

    QVariantMap SettingsModel::crosshair(int style, int size) const
    {
        const auto chosenStyle = static_cast<Render::CrosshairStyle>(std::clamp(style, 0, 4));
        const float scale = Render::Crosshair::ScaleOf(static_cast<Render::CrosshairSize>(std::clamp(size, 0, 2)));
        QVariantList bars;
        for (const Render::CrosshairBar& bar : Render::Crosshair::BarsOf(chosenStyle, scale))
        {
            // Whole pixels, and with Y up.
            const auto [left, right, bottom, top] = Render::Crosshair::EdgesOf(bar);
            bars.push_back(QVariantList{left, -top, right - left, top - bottom});
        }
        const auto [radius, thickness] = Render::Crosshair::RingOf(chosenStyle, scale);
        return {{QStringLiteral("bars"), bars}, {QStringLiteral("radius"), radius},
            {QStringLiteral("thickness"), thickness}};
    }

    // -------------------------------------------------------------- Audio

    void SettingsModel::BuildAudio()
    {
        std::vector<Row> rows;
        rows.push_back(Heading(QStringLiteral("Volume")));
        rows.push_back(Slider(QStringLiteral("sfx"), QStringLiteral("Sound effects"), Percent(_settings->SfxVolume, 35)));
        rows.push_back(Slider(QStringLiteral("music"), QStringLiteral("Music"), Percent(_settings->MusicVolume, 50)));
        rows.push_back(Heading(QStringLiteral("Language")));
        QStringList languages;
        int index = 0;
        for (std::size_t i = 0; i < LanguageNames.size(); ++i)
        {
            languages.push_back(QString::fromLatin1(LanguageNames[i]));
            if (_settings->Language == LanguageNames[i])
            {
                index = static_cast<int>(i);
            }
        }
        rows.push_back(Choice(QStringLiteral("language"), QStringLiteral("Text"), languages, index));
        _audio.Reset(std::move(rows));
    }

    // ----------------------------------------------------------- Keyboard

    void SettingsModel::BuildKeyboard()
    {
        std::vector<Row> rows;
        rows.push_back(Heading(QStringLiteral("Mouse")));
        rows.push_back(Slider(QStringLiteral("sensitivity"), QStringLiteral("Sensitivity"),
            SensitivityToSlider(Mods::InputSettings::MouseSensitivity()),
            [](int value) { return Q(Runtime::ToStringInvariant(SliderToSensitivity(value), "0.00")) + QStringLiteral("x"); },
            120, 1, 300, 1));
        rows.push_back(Toggle(QStringLiteral("invertY"), QStringLiteral("Invert vertical aim"),
            Mods::InputSettings::InvertMouseY()));
        rows.push_back(Toggle(QStringLiteral("invertX"), QStringLiteral("Invert horizontal aim"),
            Mods::InputSettings::InvertMouseX()));
        // Off is modern PC aim; on reproduces the DS's own (stylus at 30 Hz).
        rows.push_back(Toggle(QStringLiteral("classicAim"), QStringLiteral("Classic DS aim"),
            Mods::InputSettings::ClassicAim()));

        Row advanced = Button(QStringLiteral("advanced"), QStringLiteral("Advanced"), QStringLiteral("slate"), 8, 4);
        advanced.Clicked = [this]() { _keyboardAdvanced = !_keyboardAdvanced; };
        rows.push_back(std::move(advanced));
        Row scroll = Toggle(QStringLiteral("scrollAll"), QStringLiteral("Wheel cycles every weapon"),
            Mods::InputSettings::ScrollAllWeapons());
        scroll.Shown = [this]() { return _keyboardAdvanced; };
        rows.push_back(std::move(scroll));

        rows.push_back(Heading(QStringLiteral("Keys")));
        const auto keyName = [](Mods::InputKey key)
        {
            return static_cast<int>(key) == static_cast<int>(::OpenTK::Windowing::GraphicsLibraryFramework::Keys::Unknown)
                ? QStringLiteral("none") : Q(Mods::InputSettings::KeyName(key));
        };
        Row chat;
        chat.Type = QStringLiteral("key");
        chat.Id = QStringLiteral("key.chat");
        chat.Label = QStringLiteral("Chat");
        chat.Live = [this, keyName]()
        {
            return _keyHintRow == IndexOf(_keyboard, QStringLiteral("key.chat"))
                ? QStringLiteral("Keyboard only; configure sticks under Gamepad") : keyName(Mods::InputSettings::ChatKey());
        };
        chat.Binding = -2;
        rows.push_back(std::move(chat));
        Row clip;
        clip.Type = QStringLiteral("key");
        clip.Id = QStringLiteral("key.clip");
        clip.Label = QStringLiteral("Save clip");
        clip.Live = [this, keyName]()
        {
            return _keyHintRow == IndexOf(_keyboard, QStringLiteral("key.clip"))
                ? QStringLiteral("Keyboard only; configure sticks under Gamepad") : keyName(Mods::InputSettings::ClipKey());
        };
        clip.Binding = -3;
        rows.push_back(std::move(clip));

        QStringList lengths;
        int selected = 0;
        for (std::size_t i = 0; i < std::size(Mods::Network::DemoClip::Lengths); ++i)
        {
            const std::int32_t seconds = Mods::Network::DemoClip::Lengths[i];
            lengths.push_back(QString::number(seconds) + QStringLiteral(" seconds"));
            if (seconds == Mods::Network::DemoClip::Seconds())
            {
                selected = static_cast<int>(i);
            }
        }
        rows.push_back(Choice(QStringLiteral("clipSeconds"), QStringLiteral("Clip length"), lengths, selected));

        const auto& bindings = Mods::InputSettings::Bindings();
        for (std::size_t i = 0; i < bindings.size(); ++i)
        {
            Row key;
            key.Type = QStringLiteral("key");
            key.Id = QStringLiteral("key.") + Q(bindings[i].Name);
            key.Label = Q(Mods::InputSettings::ActionName(bindings[i]));
            key.Binding = static_cast<int>(i);
            const QString id = key.Id;
            key.Live = [this, i, id]()
            {
                if (_keyHintRow >= 0 && _keyHintRow == IndexOf(_keyboard, id))
                {
                    return QStringLiteral("Keyboard only; configure sticks under Gamepad");
                }
                const auto& property = Mods::InputSettings::Bindings()[i];
                return Q(Mods::InputSettings::Describe(Mods::InputSettings::Bind(property)));
            };
            rows.push_back(std::move(key));
        }
        _keyboard.Reset(std::move(rows));
    }

    void SettingsModel::listenKey(int row)
    {
        if (row < 0 || row >= _keyboard.Count() || _keyboard.Rows()[static_cast<std::size_t>(row)].Type != QStringLiteral("key"))
        {
            return;
        }
        _keyRow = row;
        Input::KeyCapture::Listening(true);
        _keyHintRow = -1;
        (void)_keyEdges->Update(Input::GamepadManager::Snapshot());
        _keyTimer.start();
        emit listeningChanged();
        _keyboard.Refresh();
    }

    void SettingsModel::KeyTick()
    {
        if (_keyRow < 0)
        {
            _keyTimer.stop();
            return;
        }
        const Input::GamepadButtons pressed = _keyEdges->Update(Input::GamepadManager::Snapshot());
        if (pressed != Input::GamepadButtons::None)
        {
            KeyToPad(_keyRow, static_cast<std::int32_t>(pressed));
        }
    }

    void SettingsModel::keyToPad(int row)
    {
        KeyToPad(row, 0);
    }

    void SettingsModel::KeyToPad(int row, std::int32_t pressedValue)
    {
        if (row < 0 || row >= _keyboard.Count())
        {
            return;
        }
        const Row& key = _keyboard.Rows()[static_cast<std::size_t>(row)];
        const std::string binding = key.Binding >= 0
            ? std::string(Mods::InputSettings::Bindings()[static_cast<std::size_t>(key.Binding)].Name)
            : key.Binding == -2 ? std::string("Chat") : std::string("SaveClip");
        using Action = Input::PadAction;
        static const std::vector<std::pair<const char*, Action>> actions{
            {"Shoot", Action::Shoot}, {"AltAttack", Action::Shoot}, {"Jump", Action::Jump}, {"Boost", Action::Jump},
            {"Zoom", Action::Zoom}, {"Morph", Action::Morph}, {"Scan", Action::Scan}, {"ScanVisor", Action::ScanVisor},
            {"WeaponMenu", Action::WeaponWheel}, {"Pause", Action::Scoreboard}, {"NextWeapon", Action::NextWeapon},
            {"PrevWeapon", Action::PrevWeapon}, {"Missile", Action::Missile}, {"PowerBeam", Action::PowerBeam},
            {"Chat", Action::Chat}, {"VoltDriver", Action::VoltDriver}, {"Battlehammer", Action::Battlehammer},
            {"Imperialist", Action::Imperialist}, {"Judicator", Action::Judicator}, {"Magmaul", Action::Magmaul},
            {"ShockCoil", Action::ShockCoil}, {"OmegaCannon", Action::OmegaCannon},
            {"AffinitySlot", Action::AffinitySlot}};
        const auto found = std::find_if(actions.begin(), actions.end(),
            [&binding](const auto& entry) { return binding == entry.first; });
        stopKey();
        if (found == actions.end())
        {
            _keyHintRow = row;
            _keyboard.Refresh();
            return;
        }
        int padRow = -1;
        for (std::size_t i = 0; i < _gamepad.Rows().size(); ++i)
        {
            const Row& candidate = _gamepad.Rows()[i];
            if (candidate.Type == QStringLiteral("pad") && candidate.Binding == static_cast<int>(found->second))
            {
                padRow = static_cast<int>(i);
                break;
            }
        }
        if (padRow < 0)
        {
            return;
        }
        emit padRowRequested(padRow);
        padListen(padRow);
        const auto pressed = static_cast<Input::GamepadButtons>(pressedValue);
        if (pressed != Input::GamepadButtons::None)
        {
            for (const Input::GamepadButtons button : Input::GamepadButtonValues)
            {
                if (button != Input::GamepadButtons::None && (pressed & button) != Input::GamepadButtons::None)
                {
                    PadChooseButton(padRow, static_cast<std::int32_t>(button));
                    break;
                }
            }
        }
    }

    void SettingsModel::stopKey()
    {
        if (_keyRow < 0)
        {
            return;
        }
        _keyRow = -1;
        Input::KeyCapture::Listening(false);
        _keyTimer.stop();
        emit listeningChanged();
        _keyboard.Refresh();
    }

    bool SettingsModel::pressKey(int qtKey, quint32 scanCode, quint32 virtualKey, int modifiers, const QString& text)
    {
        if (_keyRow < 0)
        {
            return false;
        }
        const Row& row = _keyboard.Rows()[static_cast<std::size_t>(_keyRow)];
        using GlfwKeys = ::OpenTK::Windowing::GraphicsLibraryFramework::Keys;
        if (qtKey == ::Qt::Key_Escape)
        {
            stopKey();
            return true;
        }
        int key = -1;
        if (qtKey == ::Qt::Key_Backspace || qtKey == ::Qt::Key_Delete)
        {
            key = static_cast<int>(GlfwKeys::Unknown);
        }
        else
        {
            const QKeyEvent event(QEvent::KeyPress, qtKey, ::Qt::KeyboardModifiers(modifiers), scanCode, virtualKey,
                0, text);
            key = GlfwKey(event);
            if (key < 0)
            {
                return true;
            }
        }
        const auto chosen = static_cast<Mods::InputKey>(key);
        if (row.Binding == -2)
        {
            Mods::InputSettings::ChatKey(chosen);
        }
        else if (row.Binding == -3)
        {
            Mods::InputSettings::ClipKey(chosen);
        }
        else if (row.Binding >= 0)
        {
            Mods::InputSettings::Rebind(Mods::InputSettings::Bindings()[static_cast<std::size_t>(row.Binding)],
                ::MphRead::Entities::ButtonType::Key, chosen,
                ::OpenTK::Windowing::GraphicsLibraryFramework::MouseButton::Left);
        }
        stopKey();
        return true;
    }

    void SettingsModel::pressMouse(int button)
    {
        if (_keyRow < 0)
        {
            return;
        }
        const Row& row = _keyboard.Rows()[static_cast<std::size_t>(_keyRow)];
        if (row.Binding < 0)
        {
            // Chat and clip keys take keys only.
            return;
        }
        using Mouse = ::OpenTK::Windowing::GraphicsLibraryFramework::MouseButton;
        Mouse chosen = Mouse::Left;
        switch (button)
        {
        case ::Qt::LeftButton: chosen = Mouse::Left; break;
        case ::Qt::RightButton: chosen = Mouse::Right; break;
        case ::Qt::MiddleButton: chosen = Mouse::Middle; break;
        case ::Qt::BackButton: chosen = Mouse::Button4; break;
        case ::Qt::ForwardButton: chosen = Mouse::Button5; break;
        default: return;
        }
        Mods::InputSettings::Rebind(Mods::InputSettings::Bindings()[static_cast<std::size_t>(row.Binding)],
            ::MphRead::Entities::ButtonType::Mouse, ::OpenTK::Windowing::GraphicsLibraryFramework::Keys::Unknown,
            chosen);
        stopKey();
    }

    void SettingsModel::wheel(bool up)
    {
        if (_keyRow < 0)
        {
            return;
        }
        const Row& row = _keyboard.Rows()[static_cast<std::size_t>(_keyRow)];
        if (row.Binding < 0)
        {
            return;
        }
        Mods::InputSettings::Rebind(Mods::InputSettings::Bindings()[static_cast<std::size_t>(row.Binding)],
            up ? ::MphRead::Entities::ButtonType::ScrollUp : ::MphRead::Entities::ButtonType::ScrollDown,
            ::OpenTK::Windowing::GraphicsLibraryFramework::Keys::Unknown,
            ::OpenTK::Windowing::GraphicsLibraryFramework::MouseButton::Left);
        stopKey();
    }

    // ------------------------------------------------------------ Gamepad

    void SettingsModel::BuildGamepad()
    {
        _profileRevision = Input::GamepadProfiles::Revision();
        _runtimeRevision = Input::GamepadManager::Snapshot().Revision;
        _deviceList.clear();
        std::vector<Row> rows;
        rows.push_back(Heading(QStringLiteral("Controller")));
        rows.push_back(Choice(QStringLiteral("controller.device"), QStringLiteral("Controller"),
            {QStringLiteral("Automatic (last used)")}, 0));

        const auto number = [&rows](const QString& id, const QString& label, float value, float min, float max,
                                std::function<void(float)> changed, bool advanced, bool* open)
        {
            Row row = Slider(id, label, Runtime::MathRoundToInt32(static_cast<double>(value) * 100.0),
                [](int current) { return Q(Runtime::ToString(static_cast<float>(current) / 100.0F, "0.00")); },
                210, static_cast<int>(min * 100), static_cast<int>(max * 100), 1);
            row.Changed = [changed = std::move(changed)](Row& r) { changed(static_cast<float>(r.Value) / 100.0F); };
            // GamepadSettingsPanel stacks its rows eight points apart; the
            // page's own two are already between them.
            row.Top = 6;
            if (advanced)
            {
                row.Shown = [open]() { return *open; };
            }
            rows.push_back(std::move(row));
        };
        const auto flag = [&rows](const QString& id, const QString& label, bool value, std::function<void(bool)> changed,
                              bool advanced, bool* open)
        {
            Row row = Toggle(id, label, value);
            row.Changed = [changed = std::move(changed)](Row& r) { changed(r.On); };
            // GamepadSettingsPanel stacks its rows eight points apart; the
            // page's own two are already between them.
            row.Top = 6;
            if (advanced)
            {
                row.Shown = [open]() { return *open; };
            }
            rows.push_back(std::move(row));
        };
        const auto choice = [&rows](const QString& id, const QString& label, QStringList options, int selected,
                                std::function<void(int)> changed, bool advanced, bool* open)
        {
            Row row = Choice(id, label, std::move(options), selected);
            row.Changed = [changed = std::move(changed)](Row& r) { changed(r.Index); };
            // GamepadSettingsPanel stacks its rows eight points apart; the
            // page's own two are already between them.
            row.Top = 6;
            if (advanced)
            {
                row.Shown = [open]() { return *open; };
            }
            rows.push_back(std::move(row));
        };
        bool* const open = &_gamepadAdvanced;

        QStringList presets;
        int presetIndex = 0;
        for (std::size_t i = 0; i < Presets.size(); ++i)
        {
            presets.push_back(QString::fromLatin1(Presets[i]));
            if (Input::PadBindings::Preset() == Presets[i])
            {
                presetIndex = static_cast<int>(i);
            }
        }
        choice(QStringLiteral("controller.control_layout"), QStringLiteral("Control layout"), presets, presetIndex,
            [this](int index)
            {
                Input::PadBindings::ApplyPreset(Presets[static_cast<std::size_t>(index)]);
                QTimer::singleShot(0, this, [this]() { BuildGamepad(); });
            },
            false, open);
        number(QStringLiteral("controller.horizontal_sensitivity"), QStringLiteral("Horizontal sensitivity"),
            Input::GamepadOptions::LookX(), .1F, 5, [](float v) { Input::GamepadOptions::LookX(v); }, false, open);
        number(QStringLiteral("controller.vertical_sensitivity"), QStringLiteral("Vertical sensitivity"),
            Input::GamepadOptions::LookY(), .1F, 5, [](float v) { Input::GamepadOptions::LookY(v); }, false, open);
        number(QStringLiteral("controller.stick_deadzone"), QStringLiteral("Stick dead zone"),
            std::max(Input::GamepadOptions::LeftInner(), Input::GamepadOptions::RightInner()), 0, .9F,
            [](float v)
            {
                Input::GamepadOptions::LeftInner(v);
                Input::GamepadOptions::RightInner(v);
            },
            false, open);
        flag(QStringLiteral("controller.invert_vertical"), QStringLiteral("Invert vertical aim"),
            Input::GamepadOptions::InvertY(), [](bool v) { Input::GamepadOptions::InvertY(v); }, false, open);
        flag(QStringLiteral("controller.vibration"), QStringLiteral("Vibration"), Input::GamepadOptions::Vibration(),
            [](bool v)
            {
                Input::GamepadOptions::Vibration(v);
                if (!v)
                {
                    Input::GamepadHaptics::Stop();
                }
            },
            false, open);

        Row advanced = Button(QStringLiteral("controller.advanced"), QStringLiteral("Advanced"), QStringLiteral("slate"),
            6, 0);
        advanced.Clicked = [this]() { _gamepadAdvanced = !_gamepadAdvanced; };
        rows.push_back(std::move(advanced));

        Row monitor;
        monitor.Type = QStringLiteral("monitor");
        monitor.Top = 6;
        monitor.Shown = [open]() { return *open; };
        rows.push_back(std::move(monitor));
        choice(QStringLiteral("controller.button_labels"), QStringLiteral("Button labels"),
            {QStringLiteral("Automatic"), QStringLiteral("Xbox"), QStringLiteral("PlayStation"),
                QStringLiteral("Nintendo"), QStringLiteral("Generic")},
            static_cast<int>(Input::GamepadOptions::GlyphStyle()),
            [](int v) { Input::GamepadOptions::GlyphStyle(static_cast<Input::GamepadFamily>(v)); }, true, open);
        Row explanation;
        explanation.Type = QStringLiteral("text");
        explanation.Text = QStringLiteral(
            "Advanced settings are per controller. Calibration, profiles and mappings stay with the selected device.");
        explanation.Top = 6;
        explanation.Shown = [open]() { return *open; };
        rows.push_back(std::move(explanation));
        number(QStringLiteral("controller.scoped_horizontal_multiplier"), QStringLiteral("Scoped horizontal multiplier"),
            Input::GamepadOptions::ScopedX(), .1F, 3, [](float v) { Input::GamepadOptions::ScopedX(v); }, true, open);
        number(QStringLiteral("controller.scoped_vertical_multiplier"), QStringLiteral("Scoped vertical multiplier"),
            Input::GamepadOptions::ScopedY(), .1F, 3, [](float v) { Input::GamepadOptions::ScopedY(v); }, true, open);
        flag(QStringLiteral("controller.toggle_weapon_wheel"), QStringLiteral("Toggle weapon wheel"),
            Input::GamepadOptions::WheelToggle(), [](bool v) { Input::GamepadOptions::WheelToggle(v); }, true, open);
        number(QStringLiteral("controller.wheel_selection_threshold"), QStringLiteral("Wheel selection threshold"),
            Input::GamepadOptions::WheelThreshold(), .1F, .95F, [](float v) { Input::GamepadOptions::WheelThreshold(v); },
            true, open);

        QStringList modifiers;
        int modifierIndex = -1;
        for (std::size_t i = 0; i < Input::GamepadButtonValues.size(); ++i)
        {
            modifiers.push_back(Q(Input::PadBindings::Describe(Input::GamepadButtonValues[i])));
            if (Input::GamepadButtonValues[i] == Input::GamepadOptions::BindingModifier())
            {
                modifierIndex = static_cast<int>(i);
            }
        }
        choice(QStringLiteral("controller.modifier_for_new_bindings"), QStringLiteral("Modifier for new bindings"),
            modifiers, modifierIndex,
            [](int v) { Input::GamepadOptions::BindingModifier(Input::GamepadButtonValues[static_cast<std::size_t>(v)]); },
            true, open);
        Row modifierNote = NoteRow(QStringLiteral(
            "Hold the modifier first, then press the action button. Modifier combinations are reserved during gameplay."));
        modifierNote.Top = 6;
        modifierNote.Shown = [open]() { return *open; };
        rows.push_back(std::move(modifierNote));

        const QStringList weapons{QStringLiteral("Volt Driver"), QStringLiteral("Battlehammer"),
            QStringLiteral("Imperialist"), QStringLiteral("Judicator"), QStringLiteral("Magmaul"),
            QStringLiteral("Shock Coil")};
        for (int position = 0; position < 6; ++position)
        {
            choice(QStringLiteral("controller.wheel.") + QString::number(position),
                QStringLiteral("Wheel position ") + QString::number(position + 1), weapons,
                (*Input::GamepadOptions::WheelOrder())[static_cast<std::size_t>(position)],
                [this, position](int slot)
                {
                    Input::GamepadOptions::SetWheelSlot(position, slot);
                    QTimer::singleShot(0, this, [this]() { BuildGamepad(); });
                },
                true, open);
        }
        number(QStringLiteral("controller.left_inner_deadzone"), QStringLiteral("Left inner deadzone"),
            Input::GamepadOptions::LeftInner(), 0, .9F, [](float v) { Input::GamepadOptions::LeftInner(v); }, true, open);
        number(QStringLiteral("controller.left_outer_deadzone"), QStringLiteral("Left outer deadzone"),
            Input::GamepadOptions::LeftOuter(), 0, .5F, [](float v) { Input::GamepadOptions::LeftOuter(v); }, true, open);
        number(QStringLiteral("controller.right_inner_deadzone"), QStringLiteral("Right inner deadzone"),
            Input::GamepadOptions::RightInner(), 0, .9F, [](float v) { Input::GamepadOptions::RightInner(v); }, true, open);
        number(QStringLiteral("controller.right_outer_deadzone"), QStringLiteral("Right outer deadzone"),
            Input::GamepadOptions::RightOuter(), 0, .5F, [](float v) { Input::GamepadOptions::RightOuter(v); }, true, open);
        choice(QStringLiteral("controller.aim_curve"), QStringLiteral("Aim curve"),
            {QStringLiteral("Linear"), QStringLiteral("Classic"), QStringLiteral("Precision"), QStringLiteral("Dynamic")},
            static_cast<int>(Input::GamepadOptions::Curve()),
            [](int v) { Input::GamepadOptions::Curve(static_cast<Input::GamepadCurve>(v)); }, true, open);
        flag(QStringLiteral("controller.invert_horizontal"), QStringLiteral("Invert horizontal aim"),
            Input::GamepadOptions::InvertX(), [](bool v) { Input::GamepadOptions::InvertX(v); }, true, open);
        flag(QStringLiteral("controller.southpaw"), QStringLiteral("Southpaw sticks"), Input::GamepadOptions::Southpaw(),
            [](bool v) { Input::GamepadOptions::Southpaw(v); }, true, open);
        number(QStringLiteral("controller.trigger_actuation"), QStringLiteral("Gameplay trigger actuation"),
            Input::GamepadOptions::TriggerThreshold(), .05F, .95F,
            [](float v) { Input::GamepadOptions::TriggerThreshold(v); }, true, open);
        number(QStringLiteral("controller.device_activity_threshold"), QStringLiteral("Device activity threshold"),
            Input::GamepadOptions::ActivityThreshold(), .2F, .95F,
            [](float v) { Input::GamepadOptions::ActivityThreshold(v); }, true, open);
        number(QStringLiteral("controller.vibration_strength"), QStringLiteral("Vibration strength"),
            Input::GamepadOptions::VibrationStrength(), 0, 1,
            [](float v)
            {
                Input::GamepadOptions::VibrationStrength(v);
                if (v <= 0)
                {
                    Input::GamepadHaptics::Stop();
                }
            },
            true, open);

        AddSetupRows(rows, [open]() { return *open; });
        AddProfileRows(rows, [open]() { return *open; });

        rows.push_back(Heading(QStringLiteral("Controller buttons")));
        for (const Input::PadAction action : Input::PadBindings::Actions())
        {
            Row pad;
            pad.Type = QStringLiteral("pad");
            pad.Label = Q(Input::PadBindings::Name(action));
            pad.Binding = static_cast<int>(action);
            pad.Index = 0;
            const int at = static_cast<int>(rows.size());
            pad.Live = [this, action, at]()
            {
                const PadCapture& capture = *_pad;
                if (capture.Row == at && capture.Conflict.has_value())
                {
                    return QStringLiteral("Choose how to use ") + Q(Input::PadBindings::Describe(capture.Pending));
                }
                if (capture.Row == at && capture.Picking)
                {
                    return QStringLiteral("< ")
                        + Q(Input::PadBindings::Describe(
                            Input::GamepadButtonValues[static_cast<std::size_t>(capture.PickIndex)]))
                        + QStringLiteral(" >  Accept / Back");
                }
                if (capture.Row == at && capture.Listening)
                {
                    return capture.Modifier == Input::GamepadButtons::None
                        ? QStringLiteral("Press a button")
                        : QStringLiteral("Choose a button for ") + Q(Input::PadBindings::ButtonName(capture.Modifier))
                            + QStringLiteral(" + button");
                }
                if (capture.Row == at && capture.Message.has_value())
                {
                    return Q(*capture.Message);
                }
                return QString();
            };
            pad.Extra = [this, action, at]()
            {
                const PadCapture& capture = *_pad;
                const bool mine = capture.Row == at;
                QVariantMap map;
                map.insert(QStringLiteral("listening"), mine && capture.Listening);
                map.insert(QStringLiteral("conflict"),
                    mine && capture.Conflict.has_value() ? Q(*capture.Conflict) : QString());
                map.insert(QStringLiteral("choice"), mine ? capture.Choice : 0);
                map.insert(QStringLiteral("primary"), Q(Input::PadBindings::DescribeSlot(action, 0)));
                map.insert(QStringLiteral("secondary"), Q(Input::PadBindings::DescribeSlot(action, 1)));
                map.insert(QStringLiteral("hint"),
                    Q(Input::PadBindings::ButtonName(Input::GamepadButtons::B)) + QStringLiteral(" cancel   ")
                        + Q(Input::PadBindings::ButtonName(Input::GamepadButtons::Back)) + QStringLiteral(" clear   ")
                        + Q(Input::PadBindings::ButtonName(Input::GamepadButtons::Start))
                        + QStringLiteral(" choose button"));
                map.insert(QStringLiteral("resolutions"),
                    QStringList{QStringLiteral("Swap"), QStringLiteral("Replace"), QStringLiteral("Keep Both"),
                        QStringLiteral("Cancel")});
                return map;
            };
            rows.push_back(std::move(pad));
        }

        Row reset = Button(QStringLiteral("controller.reset"), QStringLiteral("Reset to defaults"),
            QStringLiteral("brass"), 10, 0);
        reset.Clicked = [this]() { QTimer::singleShot(0, this, [this]() { ResetControls(); }); };
        rows.push_back(std::move(reset));
        _pad->Row = -1;
        _gamepad.Reset(std::move(rows));
        RefreshDevices();
    }

    void SettingsModel::RefreshDevices()
    {
        const std::vector<Input::GamepadDeviceSnapshot> devices = Input::GamepadManager::Devices();
        std::string signature;
        for (const Input::GamepadDeviceSnapshot& device : devices)
        {
            signature += device.DeviceId() + "|";
        }
        const std::optional<std::string> selectedId = Input::GamepadManager::SelectedDeviceId();
        if (selectedId.has_value())
        {
            signature += *selectedId;
        }
        if (signature == _deviceList && !_deviceList.empty())
        {
            return;
        }
        _deviceList = signature.empty() ? std::string("-") : signature;
        Row* const row = _gamepad.Find(QStringLiteral("controller.device"));
        if (row == nullptr)
        {
            return;
        }
        QStringList labels{QStringLiteral("Automatic (last used)")};
        std::vector<std::string> ids;
        int selected = 0;
        for (std::size_t i = 0; i < devices.size(); ++i)
        {
            labels.push_back(Q(devices[i].Name()));
            ids.push_back(devices[i].DeviceId());
            if (selectedId == std::optional<std::string>(devices[i].DeviceId()))
            {
                selected = static_cast<int>(i + 1);
            }
        }
        row->Options = labels;
        row->Index = selected;
        row->Changed = [ids](Row& r)
        {
            Input::GamepadManager::SelectDevice(r.Index == 0 ? std::nullopt
                : std::optional<std::string>(ids[static_cast<std::size_t>(r.Index - 1)]));
        };
        _gamepad.Refresh();
    }

    void SettingsModel::ResetControls()
    {
        Mods::InputSettings::Reset();
        if (Row* row = _keyboard.Find(QStringLiteral("sensitivity")))
        {
            row->Value = SensitivityToSlider(Mods::InputSettings::MouseSensitivity());
        }
        if (Row* row = _keyboard.Find(QStringLiteral("invertY")))
        {
            row->On = Mods::InputSettings::InvertMouseY();
        }
        if (Row* row = _keyboard.Find(QStringLiteral("invertX")))
        {
            row->On = Mods::InputSettings::InvertMouseX();
        }
        if (Row* row = _keyboard.Find(QStringLiteral("classicAim")))
        {
            row->On = Mods::InputSettings::ClassicAim();
        }
        if (Row* row = _keyboard.Find(QStringLiteral("scrollAll")))
        {
            row->On = Mods::InputSettings::ScrollAllWeapons();
        }
        if (Row* row = _stylus.Find(QStringLiteral("stylusMode")))
        {
            row->On = Input::PointerInput::StylusMode();
        }
        if (Row* row = _stylus.Find(QStringLiteral("repositionFilter")))
        {
            row->On = Input::PointerInput::GuardJumps();
        }
        if (Row* row = _stylus.Find(QStringLiteral("stylusZone")))
        {
            row->On = Input::StylusZone::Wanted();
        }
        if (Row* row = _stylus.Find(QStringLiteral("stylusOpacity")))
        {
            row->Value = Runtime::MathRoundToInt32(static_cast<double>(Input::StylusZone::Opacity() * 100.0F));
        }
        _keyboard.Refresh();
        _stylus.Refresh();
        BuildGamepad();
    }

    void SettingsModel::padSlot(int row, int slot)
    {
        if (row < 0 || row >= _gamepad.Count())
        {
            return;
        }
        _gamepad.Rows()[static_cast<std::size_t>(row)].Index = std::clamp(slot, 0, 1);
        _gamepad.Refresh();
    }

    void SettingsModel::padListen(int row)
    {
        if (row < 0 || row >= _gamepad.Count() || _gamepad.Rows()[static_cast<std::size_t>(row)].Type != QStringLiteral("pad"))
        {
            return;
        }
        if (_pad->Listening && _pad->Row != row)
        {
            PadDone(_pad->Row);
        }
        PadCapture& capture = *_pad;
        capture = PadCapture{};
        capture.Row = row;
        capture.Action = static_cast<Input::PadAction>(_gamepad.Rows()[static_cast<std::size_t>(row)].Binding);
        capture.Listening = true;
        capture.Modifier = Input::GamepadOptions::BindingModifier();
        Input::GamepadContexts::Capturing(true);
        const Input::GamepadSnapshot snapshot = Input::GamepadManager::Snapshot();
        capture.DeviceRevision = snapshot.Revision;
        capture.Baseline = snapshot.State.Buttons;
        _padTimer.start();
        _gamepad.Refresh();
    }

    void SettingsModel::padKey(int row, int qtKey)
    {
        PadCapture& capture = *_pad;
        if (capture.Row != row || !capture.Listening)
        {
            return;
        }
        if (capture.Conflict.has_value())
        {
            if (qtKey == ::Qt::Key_Left) capture.Choice = std::max(0, capture.Choice - 1);
            if (qtKey == ::Qt::Key_Right) capture.Choice = std::min(3, capture.Choice + 1);
            if (qtKey == ::Qt::Key_Return || qtKey == ::Qt::Key_Enter)
            {
                PadResolve(row);
                return;
            }
            if (qtKey == ::Qt::Key_Escape)
            {
                PadDone(row);
                return;
            }
            _gamepad.Refresh();
            return;
        }
        if (qtKey == ::Qt::Key_Escape)
        {
            PadDone(row);
            return;
        }
        if (qtKey == ::Qt::Key_Backspace || qtKey == ::Qt::Key_Delete)
        {
            Input::PadBindings::SetSlot(capture.Action, _gamepad.Rows()[static_cast<std::size_t>(row)].Index,
                Input::GamepadButtons::None);
            PadDone(row);
        }
    }

    void SettingsModel::padChoose(int row, int choice)
    {
        PadCapture& capture = *_pad;
        if (capture.Row != row || !capture.Conflict.has_value())
        {
            return;
        }
        capture.Choice = std::clamp(choice, 0, 3);
        PadResolve(row);
    }

    void SettingsModel::padLeave(int row)
    {
        if (_pad->Row == row && _pad->Listening)
        {
            PadDone(row);
        }
    }

    void SettingsModel::PadTick()
    {
        PadCapture& capture = *_pad;
        if (!capture.Listening)
        {
            _padTimer.stop();
            return;
        }
        const Input::GamepadSnapshot snapshot = Input::GamepadManager::Snapshot();
        if (!Input::GamepadContexts::Focused() || !snapshot.State.Connected || snapshot.Revision != capture.DeviceRevision)
        {
            capture.Message = !Input::GamepadContexts::Focused() ? "Focus lost - try again"
                : !snapshot.State.Connected ? "Connect a controller, then try again" : "Controller changed - try again";
            PadDone(capture.Row);
            return;
        }
        using Buttons = Input::GamepadButtons;
        const Buttons pressed = snapshot.State.Buttons & ~capture.Baseline;
        capture.Baseline = snapshot.State.Buttons;
        if (pressed == Buttons::None)
        {
            return;
        }
        const auto has = [pressed](Buttons button) { return (pressed & button) != Buttons::None; };
        if (capture.Conflict.has_value())
        {
            if (has(Buttons::DpadLeft)) capture.Choice = std::max(0, capture.Choice - 1);
            if (has(Buttons::DpadRight)) capture.Choice = std::min(3, capture.Choice + 1);
            if (has(Buttons::A))
            {
                PadResolve(capture.Row);
                return;
            }
            if (has(Buttons::B))
            {
                PadDone(capture.Row);
                return;
            }
            _gamepad.Refresh();
            return;
        }
        if (has(Buttons::B))
        {
            PadDone(capture.Row);
            return;
        }
        const int count = static_cast<int>(Input::GamepadButtonValues.size());
        if (capture.Picking)
        {
            if (has(Buttons::DpadLeft)) capture.PickIndex = (capture.PickIndex + count - 1) % count;
            if (has(Buttons::DpadRight)) capture.PickIndex = (capture.PickIndex + 1) % count;
            if (has(Buttons::A))
            {
                PadChooseButton(capture.Row,
                    static_cast<std::int32_t>(Input::GamepadButtonValues[static_cast<std::size_t>(capture.PickIndex)]));
                return;
            }
            _gamepad.Refresh();
            return;
        }
        if (has(Buttons::Start))
        {
            capture.Picking = true;
            capture.PickIndex = 1;
            _gamepad.Refresh();
            return;
        }
        if (has(Buttons::Back))
        {
            Input::PadBindings::SetSlot(capture.Action, _gamepad.Rows()[static_cast<std::size_t>(capture.Row)].Index,
                Buttons::None);
            PadDone(capture.Row);
            return;
        }
        for (const Buttons button : Input::GamepadButtonValues)
        {
            if (button != Buttons::None && has(button))
            {
                PadChooseButton(capture.Row, static_cast<std::int32_t>(button));
                return;
            }
        }
    }

    void SettingsModel::PadChooseButton(int row, std::int32_t value)
    {
        PadCapture& capture = *_pad;
        const auto button = static_cast<Input::GamepadButtons>(value);
        if (button != Input::GamepadButtons::None && button == capture.Modifier)
        {
            return;
        }
        capture.Picking = false;
        const int slot = _gamepad.Rows()[static_cast<std::size_t>(row)].Index;
        const std::vector<Input::PadAction> conflicts
            = Input::PadBindings::Conflicts(capture.Action, button, capture.Modifier);
        if (conflicts.empty())
        {
            Input::PadBindings::SetSlot(capture.Action, slot, button, capture.Modifier);
            PadDone(row);
            return;
        }
        capture.Pending = button;
        capture.Choice = 0;
        std::string message = capture.Modifier == Input::GamepadButtons::None
            ? std::string{} : Input::PadBindings::ButtonName(capture.Modifier) + " + ";
        message += Input::PadBindings::ButtonName(button) + " is assigned to ";
        for (std::size_t i = 0; i < conflicts.size(); ++i)
        {
            if (i != 0)
            {
                message += " / ";
            }
            message += Input::PadBindings::Name(conflicts[i]);
        }
        capture.Conflict = std::move(message);
        _gamepad.Refresh();
    }

    void SettingsModel::PadResolve(int row)
    {
        PadCapture& capture = *_pad;
        Input::PadBindings::Assign(capture.Action, _gamepad.Rows()[static_cast<std::size_t>(row)].Index, capture.Pending,
            Resolutions[static_cast<std::size_t>(capture.Choice)], capture.Modifier);
        PadDone(row);
    }

    void SettingsModel::PadDone(int row)
    {
        PadCapture& capture = *_pad;
        capture.Listening = false;
        capture.Conflict.reset();
        capture.Picking = false;
        capture.Row = row;
        Input::GamepadContexts::Capturing(false);
        _padTimer.stop();
        _gamepad.Refresh();
    }

    // ----------------------------------------------- Controller setup

    void SettingsModel::AddSetupRows(std::vector<Row>& rows, const std::function<bool()>& open)
    {
        const auto word = [&rows, &open](const QString& id, const QString& text, std::function<void()> action)
        {
            Row row = Word(id, text);
            row.TextSize = 13;
            row.Top = 6;
            row.Shown = open;
            row.Clicked = std::move(action);
            rows.push_back(std::move(row));
            return rows.size() - 1;
        };
        word(QStringLiteral("setup.calibrate"), QStringLiteral("Calibrate sticks and triggers"), [this]() { SetupStart(false); });
        if (!Runtime::IsAndroid())
        {
            word(QStringLiteral("setup.map"), QStringLiteral("Map controller buttons and axes"), [this]() { SetupStart(true); });
            word(QStringLiteral("setup.reset"), QStringLiteral("Reset custom controller mappings"), [this]()
            {
                try
                {
                    Input::GamepadMappings::ResetOverrides();
                    SetupStop(QStringLiteral("Custom mappings reset. Restart the game to restore platform mappings."));
                }
                catch (const std::exception& ex)
                {
                    _setupStatus = QString::fromUtf8(ex.what());
                }
            });
        }
        const std::size_t apply = word(QStringLiteral("setup.apply"), QStringLiteral("Apply measured setup"),
            [this]() { SetupApply(); });
        rows[apply].Enabled = [this]() { return _setup->CanApply; };
        word(QStringLiteral("setup.cancel"), QStringLiteral("Cancel setup"),
            [this]() { SetupStop(QStringLiteral("Setup canceled. Settings unchanged.")); });
        Row status = NoteRow(QString());
        status.Top = 6;
        status.Shown = open;
        status.Live = [this]() { return _setupStatus; };
        rows.push_back(std::move(status));
    }

    void SettingsModel::SetupStart(bool mapping)
    {
        SetupStop(QString());
        const Input::GamepadSnapshot snapshot = Input::GamepadManager::Snapshot();
        if (!snapshot.DeviceId.has_value())
        {
            _setupStatus = QStringLiteral("Connect and select a controller first.");
            return;
        }
        PadSetup& setup = *_setup;
        setup.Buttons = snapshot.State.Buttons;
        setup.Device = snapshot.DeviceId;
        setup.Revision = snapshot.Revision;
        setup.MappingMode = mapping;
        setup.Started = Runtime::EnvironmentTickCount64();
        setup.Complete = false;
        setup.Calibration = mapping ? nullptr : std::make_unique<Input::GamepadCalibration>();
        setup.Mapping.reset();
        Input::GamepadContexts::Capturing(true);
        if (mapping)
        {
            Input::GamepadMappingWizard::Latest.reset();
            Input::GamepadMappingWizard::RequestedDevice = setup.Device;
        }
        _setupStatus = QStringLiteral("Release all controls. Keep both sticks centered. Esc or Cancel stops setup.");
        _setupTimer.start();
    }

    void SettingsModel::SetupTick()
    {
        PadSetup& setup = *_setup;
        if (!setup.Device.has_value() || setup.Complete)
        {
            return;
        }
        const Input::GamepadSnapshot snapshot = Input::GamepadManager::Snapshot();
        if (!Input::GamepadContexts::Focused() || snapshot.DeviceId != setup.Device || snapshot.Revision != setup.Revision)
        {
            SetupStop(QStringLiteral("Controller or focus changed. Restart setup."));
            return;
        }
        const std::int64_t elapsed = Runtime::EnvironmentTickCount64() - setup.Started;
        const Input::GamepadButtons pressed = snapshot.State.Buttons & ~setup.Buttons;
        setup.Buttons = snapshot.State.Buttons;
        if (!setup.MappingMode && Input::Any(pressed & Input::GamepadButtons::B))
        {
            SetupStop(QStringLiteral("Calibration canceled. Settings unchanged."));
            return;
        }
        if (setup.MappingMode)
        {
            const std::shared_ptr<Input::GamepadRawSample> sample = Input::GamepadMappingWizard::Latest;
            if (sample == nullptr || sample->DeviceId != *setup.Device)
            {
                return;
            }
            if (setup.Mapping == nullptr)
            {
                if (std::any_of(sample->Buttons.begin(), sample->Buttons.end(), [](bool value) { return value; })
                    || std::any_of(sample->Hats.begin(), sample->Hats.end(), [](std::uint8_t value) { return value != 0; }))
                {
                    setup.Started = Runtime::EnvironmentTickCount64();
                    return;
                }
                if (elapsed < 1500)
                {
                    return;
                }
                setup.Mapping = std::make_unique<Input::GamepadMappingWizard>(*sample);
            }
            try
            {
                setup.Mapping->Sample(*sample);
            }
            catch (const std::exception& ex)
            {
                SetupStop(QString::fromUtf8(ex.what()));
                return;
            }
            _setupStatus = Q(setup.Mapping->Prompt()) + QStringLiteral(" Esc or Cancel stops setup.");
            setup.Complete = setup.Mapping->Complete();
        }
        else
        {
            const std::optional<Input::GamepadDeviceSnapshot> device = Input::GamepadManager::ActiveDevice();
            if (!device.has_value() || elapsed < 1000)
            {
                // The button that opened setup is released before rest is measured.
                return;
            }
            setup.Calibration->Sample(device->RawState(), elapsed < 3500);
            if (elapsed < 3500)
            {
                _setupStatus = QStringLiteral("Keep sticks and triggers released. Measuring rest\u2026 B cancels.");
            }
            else
            {
                const std::int64_t seconds = std::max<std::int64_t>(0, (10500 - elapsed) / 1000);
                _setupStatus = QStringLiteral("Rotate both sticks fully and squeeze/release both triggers. ")
                    + QString::number(seconds) + QStringLiteral(" seconds remaining. B cancels.");
            }
            if (elapsed >= 10500)
            {
                setup.Complete = true;
                _setupStatus = Q(setup.Calibration->Summary());
            }
        }
        if (setup.Complete)
        {
            _setupTimer.stop();
            Input::GamepadContexts::Capturing(false);
            Input::GamepadMappingWizard::RequestedDevice.reset();
            setup.CanApply = setup.MappingMode || setup.Calibration->Valid();
        }
        _gamepad.Refresh();
    }

    void SettingsModel::SetupApply()
    {
        PadSetup& setup = *_setup;
        if (!setup.Complete)
        {
            return;
        }
        if (Input::GamepadManager::Snapshot().DeviceId != setup.Device)
        {
            SetupStop(QStringLiteral("Controller changed. Restart setup."));
            return;
        }
        try
        {
            if (setup.MappingMode)
            {
                Input::GamepadMappings::SaveOverride(setup.Mapping->Mapping());
            }
            else
            {
                setup.Calibration->Apply();
            }
            SetupStop(QStringLiteral("Setup applied. Save settings or a named profile to retain calibration."));
            QTimer::singleShot(0, this, [this]() { BuildGamepad(); });
        }
        catch (const std::exception& ex)
        {
            _setupStatus = QStringLiteral("Could not apply setup: ") + QString::fromUtf8(ex.what());
        }
    }

    void SettingsModel::SetupStop(const QString& message)
    {
        PadSetup& setup = *_setup;
        if (setup.Device.has_value())
        {
            Input::GamepadContexts::Capturing(false);
        }
        setup.Device.reset();
        setup.Complete = false;
        setup.CanApply = false;
        _setupTimer.stop();
        Input::GamepadMappingWizard::RequestedDevice.reset();
        Input::GamepadMappingWizard::Latest.reset();
        _setupStatus = message;
        _gamepad.Refresh();
    }

    bool SettingsModel::escape()
    {
        if (!_setup->Device.has_value())
        {
            return false;
        }
        SetupStop(QStringLiteral("Setup canceled. Settings unchanged."));
        return true;
    }

    // ----------------------------------------------- Controller profiles

    void SettingsModel::AddProfileRows(std::vector<Row>& rows, const std::function<bool()>& open)
    {
        Input::GamepadProfiles::Initialize();
        if (_profileStatus.isEmpty())
        {
            _profileStatus = Q(Input::GamepadProfiles::Status());
        }
        const auto add = [&rows, &open](Row row)
        {
            row.Top = 6;
            row.Shown = open;
            rows.push_back(std::move(row));
        };
        add(NoteRow(QStringLiteral("Controller profiles \u2014 ") + Q(Input::GamepadProfiles::ActiveName())));
        QStringList names;
        for (const Input::GamepadProfile& profile : Input::GamepadProfiles::Profiles())
        {
            names.push_back(Q(profile.Name));
        }
        if (names.isEmpty())
        {
            names.push_back(QStringLiteral("No saved profiles"));
        }
        add(Choice(QStringLiteral("profile.saved"), QStringLiteral("Saved profile"), names, 0));
        add(Field(QStringLiteral("profile.name"), QStringLiteral("Profile name"), QStringLiteral("My controller"), 260));
        add(Field(QStringLiteral("profile.file"), QStringLiteral("Import / export file"),
            Q(Runtime::PathCombine(LauncherPrefs::Directory(), "controller-profile.json")), 360));

        const auto value = [this](const char* id)
        {
            const Row* row = _gamepad.Find(QString::fromLatin1(id));
            if (row == nullptr)
            {
                return std::string();
            }
            return row->Type == QStringLiteral("choice") ? row->Options.value(row->Index).toStdString()
                                                         : row->Text.toStdString();
        };
        const auto word = [this, &add](const QString& id, const QString& text, std::function<void()> action)
        {
            Row row = Word(id, text);
            row.TextSize = 13;
            row.Clicked = [this, action = std::move(action)]()
            {
                try
                {
                    action();
                    _profileStatus = QStringLiteral("Done.");
                }
                catch (const std::exception& ex)
                {
                    _profileStatus = QString::fromUtf8(ex.what());
                }
            };
            add(std::move(row));
        };
        const auto rebuild = [this]() { QTimer::singleShot(0, this, [this]() { BuildGamepad(); }); };
        const auto active = []()
        {
            const std::optional<Input::GamepadDeviceSnapshot> device = Input::GamepadManager::ActiveDevice();
            if (!device.has_value())
            {
                throw std::runtime_error("Connect and select a controller first.");
            }
            return *device;
        };
        word(QStringLiteral("profile.save"), QStringLiteral("Save current as named profile"),
            [value, rebuild]() { Input::GamepadProfiles::Save(value("profile.name")); rebuild(); });
        word(QStringLiteral("profile.load"), QStringLiteral("Load selected profile"),
            [value, rebuild]() { Input::GamepadProfiles::Load(value("profile.saved")); rebuild(); });
        word(QStringLiteral("profile.assign"), QStringLiteral("Use selected profile for this controller"),
            [value, rebuild, active]() { Input::GamepadProfiles::Assign(value("profile.saved"), active()); rebuild(); });
        word(QStringLiteral("profile.unassign"), QStringLiteral("Remove automatic profile assignment"),
            [rebuild, active]() { Input::GamepadProfiles::Unassign(active()); rebuild(); });
        word(QStringLiteral("profile.export"), QStringLiteral("Export selected profile to file"),
            [value]() { Input::GamepadProfiles::Export(value("profile.saved"), value("profile.file")); });
        word(QStringLiteral("profile.import"), QStringLiteral("Import profile from file"),
            [value, rebuild]() { Input::GamepadProfiles::Import(value("profile.file")); rebuild(); });
        Row status = NoteRow(QString());
        status.Live = [this]() { return _profileStatus; };
        add(std::move(status));
        add(NoteRow(QStringLiteral("Profiles contain controller settings only. Save replaces a profile with the same name. "
                                   "Import adds a profile; load it to apply. Desktop automatic selection identifies the "
                                   "controller model and firmware; identical controllers share that assignment."), QColor(), 0));
    }

    // ------------------------------------------------------------- Stylus

    void SettingsModel::BuildStylus()
    {
        std::vector<Row> rows;
        rows.push_back(Heading(QStringLiteral("Pen tablet")));
        rows.push_back(Toggle(QStringLiteral("stylusMode"), QStringLiteral("Stylus mode"),
            Input::PointerInput::StylusMode()));
#if !defined(__ANDROID__)
        const auto enabled = [this]()
        {
            const Row* row = _stylus.Find(QStringLiteral("stylusMode"));
            return row != nullptr && row->On;
        };
        Row zone = Toggle(QStringLiteral("stylusZone"), QStringLiteral("DS touch-screen zone"),
            Input::StylusZone::Wanted());
        zone.Shown = enabled;
        rows.push_back(std::move(zone));
        Row place = Button(QStringLiteral("stylus.configure_zone"), QStringLiteral("Configure stylus zone"),
            QStringLiteral("slate"), 8, 0);
        place.Shown = enabled;
        place.Clicked = [this]()
        {
            Input::StylusZone::BeginPlacement();
            emit stylusPlacementRequested();
        };
        rows.push_back(std::move(place));
        Row advanced = Button(QStringLiteral("stylus.advanced"), QStringLiteral("Advanced"), QStringLiteral("slate"), 8, 4);
        advanced.Shown = enabled;
        advanced.Clicked = [this]() { _stylusAdvanced = !_stylusAdvanced; };
        rows.push_back(std::move(advanced));
        const auto open = [this, enabled]() { return enabled() && _stylusAdvanced; };
        Row filter = Toggle(QStringLiteral("repositionFilter"), QStringLiteral("Reposition filtering"),
            Input::PointerInput::GuardJumps());
        filter.Shown = open;
        rows.push_back(std::move(filter));
        Row opacity = Slider(QStringLiteral("stylusOpacity"), QStringLiteral("Overlay opacity"),
            Runtime::MathRoundToInt32(static_cast<double>(Input::StylusZone::Opacity() * 100.0F)),
            [](int value) { return QString::number(value) + QStringLiteral("%"); }, 120, 4, 60, 2);
        opacity.Shown = open;
        rows.push_back(std::move(opacity));
        Row note = NoteRow(QStringLiteral("Reposition filtering ignores tablet jumps after lift/re-contact. The overlay "
                                          "opacity only affects the DS touch-screen guide."));
        note.Shown = open;
        rows.push_back(std::move(note));
#endif
        _stylus.Reset(std::move(rows));
    }

    // ------------------------------------------------------------ Profile

    void SettingsModel::BuildProfile()
    {
        std::vector<Row> rows;
        rows.push_back(Heading(QStringLiteral("You")));
        rows.push_back(Field(QStringLiteral("name"), QStringLiteral("Your name"), Q(LauncherPrefs::PlayerName()), 200));

        QStringList hunters;
        for (std::int32_t i = 0; i < 7; ++i)
        {
            hunters.push_back(Q(::MphRead::ToString(static_cast<::MphRead::Hunter>(i))));
        }
        hunters.push_back(Q(::MphRead::ToString(::MphRead::Hunter::Random)));
        rows.push_back(Choice(QStringLiteral("hunter"), QStringLiteral("Hunter"), hunters,
            std::max(0, static_cast<int>(hunters.indexOf(Q(::MphRead::ToString(LauncherPrefs::LastHunter())))))));
        QStringList colours;
        for (std::int32_t i = 1; i <= Mods::Network::PlayerColors::Count; ++i)
        {
            colours.push_back(QString::number(i));
        }
        rows.push_back(Choice(QStringLiteral("suit"), QStringLiteral("Suit colour"), colours,
            Mods::Network::PlayerColors::Clamp(LauncherPrefs::LastColor())));

        rows.push_back(Heading(QStringLiteral("Servers")));
        rows.push_back(Field(QStringLiteral("server"), QStringLiteral("Default server"),
            Q(LauncherPrefs::ServerAddress()) + QStringLiteral(":") + QString::number(LauncherPrefs::ServerPort()), 220));
        rows.push_back(Field(QStringLiteral("master"), QStringLiteral("Server directory"),
            Q(LauncherPrefs::MasterHost()) + QStringLiteral(":") + QString::number(LauncherPrefs::MasterPort()), 220));
        rows.push_back(Toggle(QStringLiteral("autoUpdate"), QStringLiteral("Check for updates on startup"),
            LauncherPrefs::AutoUpdate()));

        rows.push_back(Heading(QStringLiteral("Game files")));
        Row files = Word(QStringLiteral("gameFiles"), QStringLiteral("Game files"));
        files.Clicked = [this]() { emit gameFilesRequested(); };
        rows.push_back(std::move(files));
        rows.push_back(NoteRow(Q(GameFiles::Describe()), GameFiles::Ready() ? Good : Warm));

        rows.push_back(Heading(QStringLiteral("Debugging")));
        Row logs = Toggle(QStringLiteral("debugLogs"), QStringLiteral("Write a debugging log"), LauncherPrefs::DebugLogs());
        logs.Changed = [](Row& row)
        {
            LauncherPrefs::DebugLogs(row.On);
            LauncherPrefs::Save();
            if (row.On)
            {
                Mods::DebugLog::Attach();
                Mods::DebugLog::Line("launcher", "debug logging turned on from the settings");
            }
            else
            {
                Mods::DebugLog::Line("launcher", "debug logging turned off from the settings");
                Mods::DebugLog::Detach();
            }
        };
        rows.push_back(std::move(logs));
        Row where = NoteRow(QString());
        where.Live = []() { return LogLocation(); };
        rows.push_back(std::move(where));
        Row share = Word(QStringLiteral("shareLogs"), QStringLiteral("↗ Share logs"), TextDim);
        share.Shown = []() { return Mods::LogShare::Available(); };
        rows.push_back(std::move(share));

        rows.push_back(Heading(QStringLiteral("Hunter")));
        const std::string saved = ::MphRead::ToString(LauncherPrefs::LastHunter());
        int standIndex = 0;
        for (std::size_t i = 0; i < StandNames.size(); ++i)
        {
            if (saved == StandNames[i])
            {
                standIndex = static_cast<int>(i);
            }
        }
        QStringList standNames;
        for (const char* name : StandNames)
        {
            standNames.push_back(QString::fromLatin1(name));
        }
        rows.push_back(Choice(QStringLiteral("defaultHunter"), QStringLiteral("Default hunter"), standNames, standIndex));
        Row stand;
        stand.Type = QStringLiteral("stand");
        stand.Top = 4;
        stand.Bottom = 4;
        stand.Extra = [this]()
        {
            const Row* row = _profile.Find(QStringLiteral("defaultHunter"));
            return QVariantMap{{QStringLiteral("hunter"), row != nullptr ? row->Index : 0}};
        };
        rows.push_back(std::move(stand));

        rows.push_back(Heading(QStringLiteral("Account")));
        Row signIn = Word(QStringLiteral("signIn"), QStringLiteral("Sign in"), Accent);
        signIn.Clicked = [this]()
        {
            if (Row* note = _profile.Find(QStringLiteral("signInNote")))
            {
                note->Text = QStringLiteral("Ranking feature will be coming soon!");
                note->On = true;
            }
        };
        rows.push_back(std::move(signIn));
        Row signInNote = NoteRow(QString());
        signInNote.Id = QStringLiteral("signInNote");
        signInNote.Shown = [this]()
        {
            const Row* row = _profile.Find(QStringLiteral("signInNote"));
            return row != nullptr && row->On;
        };
        rows.push_back(std::move(signInNote));
        _profile.Reset(std::move(rows));
    }

    void SettingsModel::ShareLogs()
    {
        if (_sharing)
        {
            return;
        }
        const std::shared_ptr<Mods::ILogShare> sharer = Mods::LogShare::Current();
        if (sharer == nullptr)
        {
            return;
        }
        _sharing = true;
        _profile.Refresh();
        const std::u16string name = Mods::LogArchive::FileName();
        const std::weak_ptr<int> alive = _lifetime;
        std::thread([this, alive, sharer, name]()
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
                error = Runtime::Utf8ToUtf16(exception.what());
            }
            if (error.empty())
            {
                built = Mods::LogArchive::Create(path, error);
            }
            QMetaObject::invokeMethod(this, [this, alive, sharer, name, path, error, built]() mutable
            {
                if (alive.expired())
                {
                    return;
                }
                if (built)
                {
                    built = sharer->Share(path, name, error);
                }
                _sharing = false;
                _shareError = built ? QString() : QString::fromStdU16String(error);
                _profile.Refresh();
            }, ::Qt::QueuedConnection);
        }).detach();
    }

    // ------------------------------------------------------------ Credits

    void SettingsModel::BuildCredits()
    {
        std::vector<Row> rows;
        rows.push_back(Heading(QStringLiteral("Credits")));
        rows.push_back(NoteRow(Q(Mods::Credits::Summary())));
        rows.push_back(Caption(Q(Mods::Credits::Author)));
        rows.push_back(NoteRow(Q(Mods::Credits::ForkWork)));
        Row support = Word(QStringLiteral("support"), QStringLiteral("☕ Support this project"));
        support.Clicked = [this]()
        {
            if (!Mods::Update::Updater::OpenLink(std::string(Mods::Credits::SupportUrl)))
            {
                if (Row* url = _credits.Find(QStringLiteral("supportUrl")))
                {
                    url->On = true;
                }
            }
        };
        rows.push_back(std::move(support));
        Row url = NoteRow(Q(Mods::Credits::SupportUrl));
        url.Id = QStringLiteral("supportUrl");
        url.Shown = [this]()
        {
            const Row* row = _credits.Find(QStringLiteral("supportUrl"));
            return row != nullptr && row->On;
        };
        rows.push_back(std::move(url));

        rows.push_back(Heading(QStringLiteral("Built on")));
        for (const Mods::Credits::Entry& entry : Mods::Credits::Entries())
        {
            rows.push_back(Caption(Q(entry.Who().value_or(""))));
            std::string what = entry.What().value_or("");
            const std::string where = entry.Where().value_or("");
            if (!where.empty())
            {
                what += "\n" + where;
            }
            rows.push_back(NoteRow(Q(what)));
        }
        _credits.Reset(std::move(rows));
    }

    // --------------------------------------------------------------- Save

    void SettingsModel::cancel()
    {
        stopKey();
        if (_pad->Listening)
        {
            PadDone(_pad->Row);
        }
        if (!_saved)
        {
            Mods::RenderOptions::FieldOfView(Mods::RenderOptions::ParseFov(
                std::string_view(_settings->FieldOfView), Mods::RenderOptions::DefaultFov));
            LauncherPrefs::LowLatency(static_cast<NativeRuntime::Rhi::LowLatencyMode>(_originalLowLatency));
        }
        emit closed(_saved);
    }

    bool SettingsModel::save()
    {
        try
        {
            Commit();
        }
        catch (const std::exception& exception)
        {
            _error = QStringLiteral("Could not save: ") + QString::fromUtf8(exception.what());
            emit errorChanged();
            return false;
        }
        _saved = true;
        if (ShellBridge* const bridge = ShellBridge::Current())
        {
            bridge->refreshProfile();
        }
        cancel();
        return true;
    }

    void SettingsModel::Commit()
    {
        ::MphRead::MenuSettings& settings = *_settings;
        const auto on = [](RowModel& model, const char* id)
        {
            const Row* row = model.Find(QString::fromLatin1(id));
            return row != nullptr && row->On;
        };
        const auto value = [](RowModel& model, const char* id)
        {
            const Row* row = model.Find(QString::fromLatin1(id));
            return row != nullptr ? row->Value : 0;
        };
        const auto index = [](RowModel& model, const char* id)
        {
            const Row* row = model.Find(QString::fromLatin1(id));
            return row != nullptr ? row->Index : 0;
        };
        const auto chosen = [](RowModel& model, const char* id)
        {
            const Row* row = model.Find(QString::fromLatin1(id));
            return row != nullptr && !row->Options.isEmpty() ? row->Options[row->Index].toStdString() : std::string();
        };
        const auto text = [](RowModel& model, const char* id)
        {
            const Row* row = model.Find(QString::fromLatin1(id));
            return row != nullptr ? row->Text.toStdString() : std::string();
        };

        LauncherPrefs::LowLatency(static_cast<NativeRuntime::Rhi::LowLatencyMode>(
            std::clamp(index(_display, "lowLatency"), 0, 2)));
        {
            const int choice = std::clamp(index(_display, "renderer"), 0, 1);
            // Left on the default: stays "auto", which keeps the OpenGL
            // fallback should Vulkan fail to start on this device.
            const char* const chosen = LauncherPrefs::Renderer() == "auto" && choice == RendererChoice() ? "auto"
                : choice == 1 ? "vulkan" : "opengl";
            LauncherPrefs::Renderer(chosen);
            NativeRuntime::Rhi::SceneBackendRequest request{};
            if (NativeRuntime::Rhi::ParseSceneBackendRequest(chosen, request))
            {
#if !defined(__ANDROID__)
                Mods::Launcher::Gui::Shell::RequestRenderer(request, true);
#else
                Droid::RequestAndroidRenderer(request);
#endif
            }
        }
        if (_display.Find(QStringLiteral("window")) != nullptr)
        {
            const Row* windowed = _display.Find(QStringLiteral("windowedFullscreen"));
            LauncherPrefs::WindowedFullscreen(windowed != nullptr && windowed->On);
            Mods::WindowMode::FullscreenKind(LauncherPrefs::FullscreenKind());
            const Mods::WindowStartMode mode = index(_display, "window") == 0 ? Mods::WindowStartMode::Windowed
                : LauncherPrefs::FullscreenKind();
            LauncherPrefs::WindowMode(mode);
            Mods::WindowMode::Startup(mode);
            if (mode != Mods::WindowMode::Active())
            {
                Mods::PauseMenu::RequestApplyWindowMode();
            }
        }
        {
            const int clip = std::clamp(index(_keyboard, "clipSeconds"), 0,
                static_cast<int>(std::size(Mods::Network::DemoClip::Lengths)) - 1);
            Mods::Network::DemoClip::Seconds(Mods::Network::DemoClip::Lengths[clip]);
        }

        settings.ResolutionScale = Runtime::ToStringInvariant(std::max(Mods::RenderOptions::MinScale, value(_display, "scale")));
        Mods::RenderOptions::FieldOfView(value(_display, "fov"));
        settings.FieldOfView = Runtime::ToStringInvariant(value(_display, "fov"));
        settings.Lighting = std::string(Mods::RenderOptions::OnOff(on(_display, "lighting")));
        settings.Fog = std::string(Mods::RenderOptions::OnOff(on(_display, "fog")));
        settings.TextureFiltering = std::string(Mods::RenderOptions::OnOff(on(_display, "filtering")));
        settings.ShowFps = std::string(Mods::RenderOptions::OnOff(on(_display, "fps")));
        const int fpsIndex = std::clamp(value(_display, "fpsLimit"), 0, static_cast<int>(_fpsLimitCaps.size()) - 1);
        const std::int32_t cap = _fpsLimitCaps[static_cast<std::size_t>(fpsIndex)];
        Render::FrameTiming::SetFrameRateCap(cap);
        settings.FrameRateCap = Render::FrameTiming::CapString(cap);
        Render::FrameTiming::SetVSync(on(_display, "vsync"));
        settings.VSync = std::string(Mods::RenderOptions::OnOff(Render::FrameTiming::VSync()));
        settings.CelShading = std::string(Mods::RenderOptions::OnOff(on(_display, "cel")));
        Mods::RenderOptions::PerformanceMode(on(_display, "performance"));
        settings.PerformanceMode = std::string(Mods::RenderOptions::OnOff(Mods::RenderOptions::PerformanceMode()));
        settings.CelBands = "8";
        settings.CelEdge = "50";
        ::MphRead::Features::ProHud(on(_display, "proHud"));
        Render::Crosshair::Size = static_cast<Render::CrosshairSize>(index(_display, "crosshairSize"));
        Render::Crosshair::Style = static_cast<Render::CrosshairStyle>(index(_display, "crosshairStyle"));
        ::MphRead::Features::ProHudFixedWeapon(index(_display, "weapon") == 0);
        Render::Radar::Enabled = on(_display, "radar");
        Render::Radar::ShowBackground = on(_display, "radarBackground");
        Render::Radar::ShowOutlines = on(_display, "radarOutlines");

        settings.SfxVolume = Runtime::ToStringInvariant(static_cast<float>(value(_audio, "sfx")) / 100.0F);
        settings.MusicVolume = Runtime::ToStringInvariant(static_cast<float>(value(_audio, "music")) / 100.0F);
        settings.Language = chosen(_audio, "language");

        Mods::InputSettings::MouseSensitivity(SliderToSensitivity(value(_keyboard, "sensitivity")));
        Mods::InputSettings::InvertMouseY(on(_keyboard, "invertY"));
        Mods::InputSettings::InvertMouseX(on(_keyboard, "invertX"));
        Mods::InputSettings::ClassicAim(on(_keyboard, "classicAim"));
        Input::PointerInput::StylusMode(on(_stylus, "stylusMode"));
        if (_stylus.Find(QStringLiteral("repositionFilter")) != nullptr)
        {
            Input::PointerInput::GuardJumps(on(_stylus, "repositionFilter"));
        }
        if (_stylus.Find(QStringLiteral("stylusZone")) != nullptr)
        {
            Input::StylusZone::Enabled(on(_stylus, "stylusZone"));
            Input::StylusZone::Opacity(std::clamp(static_cast<float>(value(_stylus, "stylusOpacity")) / 100.0F, 0.02F, 1.0F));
        }
        Mods::InputSettings::ScrollAllWeapons(on(_keyboard, "scrollAll"));
        Mods::InputSettings::Save();
        Mods::InputSettings::ApplyToPlayers();

        const std::string playerName = Runtime::StringTrim(text(_profile, "name"));
        if (!playerName.empty())
        {
            LauncherPrefs::PlayerName(playerName);
        }
        ::MphRead::Hunter hunter{};
        if (!::MphRead::TryParse(chosen(_profile, "hunter"), false, hunter))
        {
            throw std::invalid_argument("Requested value was not found.");
        }
        LauncherPrefs::LastHunter(hunter);
        std::int32_t suit = 0;
        if (Runtime::Int32TryParseCurrentCulture(chosen(_profile, "suit"), suit))
        {
            LauncherPrefs::LastColor(Mods::Network::PlayerColors::Clamp(suit - 1));
        }
        Mods::RespawnChoice::Request(LauncherPrefs::LastHunter(), LauncherPrefs::LastColor());

        std::string host = LauncherPrefs::ServerAddress();
        std::int32_t port = LauncherPrefs::ServerPort();
        if (ParseEndpoint(text(_profile, "server"), host, port))
        {
            LauncherPrefs::ServerAddress(host);
            LauncherPrefs::ServerPort(port);
        }
        std::string masterHost = LauncherPrefs::MasterHost();
        std::int32_t masterPort = LauncherPrefs::MasterPort();
        if (ParseEndpoint(text(_profile, "master"), masterHost, masterPort))
        {
            LauncherPrefs::MasterHost(masterHost);
            LauncherPrefs::MasterPort(masterPort);
        }
        LauncherPrefs::AutoUpdate(on(_profile, "autoUpdate"));

        ::MphRead::GameState::CommitSettings(_settings);
        LauncherPrefs::Save();
        Mods::GameSettings::Apply(_settings);
    }
}
