#include "AndroidQuick.hpp"
#include "MainActivity.hpp"
#include "../MphRead.Native.Qt/Shell/QmlTypes.hpp"
#include "../MphRead.Native.Qt/Shell/ShellBridge.hpp"
#include "../MphRead.Native.Qt/Shell/FocusNav.hpp"
#include "../MphRead.Native/Mods/Input/GamepadManager.hpp"
#include "../MphRead.Native/Mods/Input/GamepadUiRouter.hpp"
#include "../MphRead.Native/NativeRuntime/System/Runtime.hpp"
#include "../MphRead.Native/GameState.hpp"
#include "../MphRead.Native/Mods/GameSettings.hpp"
#include "../MphRead.Native/Mods/InputSettings.hpp"
#include "../MphRead.Native/Mods/ThumbnailGenerator.hpp"
#include "../MphRead.Native/Mods/ThumbnailHost.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/GameFiles.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/NativeFilePicker.hpp"
#include "../MphRead.Native/Mods/MapGen/CustomRooms.hpp"
#include "../MphRead.Native/Metadata/Metadata.hpp"

#include <QtCore/QMetaObject>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

#include <android/log.h>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    using Bridge = MphRead::Qt::ShellBridge;
    std::unique_ptr<Bridge> bridge;
    std::function<void()> resume;
    std::function<void()> leave;
    std::function<void()> quit;

    void OnQt(std::function<void()> action)
    {
        if (!QCoreApplication::instance()) throw std::runtime_error("Android Qt runtime is not ready");
        QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(action), ::Qt::QueuedConnection);
    }
    std::atomic<std::int32_t> startupFlags{0};
    std::mutex startupErrorLock;
    std::string startupError;

    void LogStartup(const char* phase, const std::string& detail = {})
    {
        __android_log_print(ANDROID_LOG_INFO, "FruityStartup", "[android-startup] %s%s%s", phase,
            detail.empty() ? "" : " ", detail.c_str());
    }

    // Qt's own warnings -- QML import errors, QRhi, EGL, scene graph -- go to
    // logcat under one tag, whatever the platform plugin would have done.
    void QtToLogcat(QtMsgType type, const QMessageLogContext&, const QString& message)
    {
        const int priority = type == QtDebugMsg ? ANDROID_LOG_DEBUG : type == QtInfoMsg ? ANDROID_LOG_INFO
            : type == QtWarningMsg ? ANDROID_LOG_WARN : ANDROID_LOG_ERROR;
        __android_log_print(priority, "FruityQt", "%s", message.toUtf8().constData());
    }

    const char* GraphicsApiName(QSGRendererInterface::GraphicsApi api)
    {
        switch (api)
        {
        case QSGRendererInterface::OpenGL: return "opengl";
        case QSGRendererInterface::Vulkan: return "vulkan";
        case QSGRendererInterface::Software: return "software";
        case QSGRendererInterface::Null: return "null";
        default: return "other";
        }
    }

    // READY means the QML source loaded, not that anybody saw a frame. Watch
    // every Quick window for its first swapped frame and record it once.
    void WatchFirstFrame(QQuickWindow* window, const char* container)
    {
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [window, container]
        {
            if (startupFlags.fetch_or(MphRead::Droid::StartupFirstFrame) & MphRead::Droid::StartupFirstFrame) return;
            const auto* renderer = window->rendererInterface();
            LogStartup("first_qt_frame_presented", std::string("api=")
                + (renderer ? GraphicsApiName(renderer->graphicsApi()) : "unknown")
                + " container=" + container + " size=" + std::to_string(window->width()) + "x"
                + std::to_string(window->height()) + " qt=" + qVersion());
        }, ::Qt::DirectConnection);
    }

    void OnActivity(std::function<void(MphRead::Droid::MainActivity&)> action)
    {
        MphRead::Droid::GetMainActivityOwner().RunOnUiThread([action = std::move(action)]
        {
            if (auto* activity = MphRead::Droid::MainActivity::Instance()) action(*activity);
        });
    }
}

namespace MphRead::Droid
{
    void StartupPhase(const char* phase, const std::string& detail) { LogStartup(phase, detail); }
    void StartupMark(std::int32_t flag) noexcept { startupFlags.fetch_or(flag); }
    std::int32_t StartupFlags() noexcept { return startupFlags.load(); }
    std::string StartupError() { std::lock_guard lock(startupErrorLock); return startupError; }

    void QuickStartup(std::string state, std::string error)
    {
        if (!QCoreApplication::instance()) return;
        OnQt([state = std::move(state), error = std::move(error)]
        {
            if (bridge) bridge->SetStartup(QString::fromStdString(state), QString::fromStdString(error));
        });
    }

    void StartupFail(const std::string& error)
    {
        { std::lock_guard lock(startupErrorLock); startupError = error; }
        startupFlags.fetch_or(StartupFailed);
        LogStartup("startup_failed", error);
        QuickStartup("Failed", error);
    }

    namespace
    {
        // The pieces of the front screen, in the order a first frame needs
        // them. Each runs on the Qt thread, where the bridge lives.
        void LoadLauncherState()
        {
            Mods::Launcher::LauncherPrefs::Load();
            Mods::InputSettings::Load();
            auto settings = GameState::LoadSettings();
            Mods::GameSettings::Apply(settings);
            bridge->SetSettings(std::move(settings));
        }
        bool LoadGameFileState()
        {
            const bool ready = Mods::Launcher::GameFiles::Ready();
            if (ready) Mods::Launcher::GameFiles::ApplyPaths();
            return ready;
        }
        void PublishFront(bool ready)
        {
            // The room list is not needed to draw the front screen (or the
            // setup screen it opens without game files): publish what the
            // first frame needs, and the list a turn of the loop later.
            bridge->SetRooms({}, ready);
            bridge->SetPage(QStringLiteral("front"));
            if (!(startupFlags.fetch_or(StartupFrontPublished) & StartupFrontPublished))
                LogStartup("front_published", ready ? "game_files=ready" : "game_files=missing");
            bridge->SetStartup(QStringLiteral("FrontReady"));
        }
        void BuildRoomList(bool ready)
        {
            if (ready) bridge->SetRooms(Mods::ThumbnailGenerator::MultiplayerRooms(), true);
        }
        // Game files copied by hand, an extraction interrupted, a cache
        // cleared: previews still missing at start are drawn in the
        // background, once per process, and the deck reloads when done.
        void RenderMissingPreviews(bool ready)
        {
            // Again whenever a room without a preview turns up that was not
            // tried before (a map just downloaded), never twice for the same
            // room: one that will not render must not be redrawn at every
            // return to the front screen. Called on the Qt thread only.
            static std::atomic<bool> running{false};
            static std::set<std::string> tried;
            if (!ready || running.load()) return;
            bool fresh = false;
            for (const std::string& room : Mods::ThumbnailGenerator::MissingThumbnails())
            {
                fresh = tried.insert(room).second || fresh;
            }
            if (!fresh || running.exchange(true)) return;
            auto task = Mods::ThumbnailHost::RenderMissingAsync([](const std::string& line)
            {
                __android_log_write(ANDROID_LOG_INFO, "FruityPrime", line.c_str());
            });
            std::thread([task = std::move(task)]
            {
                try { (void)task.get(); }
                catch (const std::exception& error)
                {
                    __android_log_write(ANDROID_LOG_WARN, "FruityPrime", error.what());
                }
                running.store(false);
                OnQt([] { BuildRoomList(Mods::Launcher::GameFiles::Ready()); });
            }).detach();
        }
    }

    void QuickFront()
    {
        OnQt([]
        {
            if (!(startupFlags.load() & StartupFrontPublished)) LogStartup("quick_front_begin");
            try
            {
                LoadLauncherState();
                const bool ready = LoadGameFileState();
                PublishFront(ready);
                OnQt([ready]
                {
                    try { BuildRoomList(ready); RenderMissingPreviews(ready); }
                    catch (const std::exception& error) { LogStartup("room_list_failed", error.what()); }
                });
            }
            catch (const std::exception& error)
            {
                StartupFail(std::string("The front screen could not be prepared: ") + error.what());
            }
            catch (...)
            {
                StartupFail("The front screen could not be prepared.");
            }
        });
    }
    void QuickLobby() { OnQt([] { bridge->SetPage(QStringLiteral("front")); bridge->OpenLobby(); }); }
    void QuickPage(std::string page)
    {
        OnQt([page = std::move(page)] { bridge->SetPage(QString::fromStdString(page)); });
    }
    void QuickPause(std::function<void()> onResume, std::function<void()> onLeave, std::function<void()> onQuit)
    {
        OnQt([onResume = std::move(onResume), onLeave = std::move(onLeave), onQuit = std::move(onQuit)]
        {
            resume = onResume; leave = onLeave; quit = onQuit;
            bridge->SetPage(QStringLiteral("pause"));
        });
    }
    void QuickGoBack() { OnQt([] { emit bridge->backRequested(); }); }
    void QuickSuspendBackdrop(bool value) { OnQt([value] { bridge->SetBackdropSuspended(value); }); }
}

// QtQuickView starts Qt's GUI thread and calls this entry point. It owns the
// QML engine and the Android View, while the existing Activity owns the match.
int main(int argc, char** argv)
{
    qInstallMessageHandler(QtToLogcat);
    // Composite the menus in the Activity's view tree above the game's
    // SurfaceView, including transparent pause/results pages (TextureView).
    // FRUITY_SURFACE_CONTAINER=default, set by the Activity from an intent
    // extra, leaves Qt's own default for a one-variable A/B on a device whose
    // launcher stays black; it breaks the transparent in-match overlay.
    const char* override = std::getenv("FRUITY_SURFACE_CONTAINER");
    const bool textureView = !override || std::string_view(override) != "default";
    if (textureView) qputenv("QT_ANDROID_SURFACE_CONTAINER_TYPE", "1");
    static const char* container = textureView ? "textureview" : "default";
    qputenv("QSG_INFO", "1");
    QGuiApplication application(argc, argv);
    LogStartup("qt_main_started", std::string("container=") + container + " qt=" + qVersion());
    MphRead::Qt::RegisterQmlTypes();
    Bridge::Actions actions;
    actions.Launch = [](Bridge::LaunchPlan plan) { OnActivity([plan](auto& activity) { activity.StartMatch(plan); }); };
    // The setup screen's "choose your .nds file" -- the system document picker.
    MphRead::Mods::Launcher::NativeFilePicker::AndroidRequest([] { OnActivity([](auto& activity) { activity.RequestRomPick(); }); });
    actions.Quit = [] { OnActivity([](auto& activity) { activity.Finish(); }); };
    actions.Resume = [] { OnActivity([callback = resume](auto& activity) { if (callback) callback(); else activity.OnBackPressed(); }); };
    actions.Leave = [] { OnActivity([callback = leave](auto& activity) { if (callback) callback(); else activity.EndMatch(); }); };
    actions.QuitMatch = [] { OnActivity([callback = quit](auto& activity) { if (callback) callback(); else activity.Finish(); }); };
    actions.GameFilesChanged = []
    {
        // As the desktop does: a pack that arrived is read, its rooms added to
        // the game's lists (made once, when it started) and built; the front
        // screen then lists them. Without this the pack sat on the disk and
        // the Custom set stayed empty until the next start.
        try
        {
            if (MphRead::Mods::MapGen::CustomRooms::TakeDownloaded() > 0)
            {
                MphRead::Mods::MapGen::CustomRooms::Rescan();
                (void)MphRead::Metadata::AppendLateRooms();
                MphRead::Mods::MapGen::CustomRooms::StartGenerating();
            }
        }
        catch (const std::exception& error)
        {
            __android_log_write(ANDROID_LOG_WARN, "FruityPrime", error.what());
        }
        MphRead::Droid::QuickFront();
    };
    bridge = std::make_unique<Bridge>(std::move(actions));
    bridge->SetStartup(QStringLiteral("BootingQt"));
    // Every Quick window the view creates is watched for its first frame.
    QTimer windows;
    QObject::connect(&windows, &QTimer::timeout, [&windows]
    {
        for (QWindow* candidate : QGuiApplication::allWindows())
            if (auto* quick = qobject_cast<QQuickWindow*>(candidate); quick && !quick->property("fruityWatched").toBool())
            {
                quick->setProperty("fruityWatched", true);
                WatchFirstFrame(quick, container);
            }
        if (startupFlags.load() & MphRead::Droid::StartupFirstFrame) windows.stop();
    });
    windows.start(10);
    namespace Input = MphRead::Mods::Input;
    Input::GamepadUiRouter router;
    router.Action.Add([](Input::UiAction action)
    {
        QQuickWindow* window = nullptr;
        for (QWindow* candidate : QGuiApplication::allWindows())
            if (auto* quick = qobject_cast<QQuickWindow*>(candidate); quick && quick->isVisible()) { window = quick; break; }
        if (!window) return;
        using Action = Input::UiAction;
        int key = 0;
        switch (action)
        {
        case Action::PreviousTab: bridge->StepTabs(-1); return;
        case Action::NextTab: bridge->StepTabs(1); return;
        case Action::PageUp: MphRead::Qt::FocusNav::Page(*window, false); return;
        case Action::PageDown: MphRead::Qt::FocusNav::Page(*window, true); return;
        case Action::Up: key = ::Qt::Key_Up; break;
        case Action::Down: key = ::Qt::Key_Down; break;
        case Action::Left: key = ::Qt::Key_Left; break;
        case Action::Right: key = ::Qt::Key_Right; break;
        case Action::Back: key = ::Qt::Key_Escape; break;
        case Action::Accept:
            if (MphRead::Qt::FocusNav::PadAccept(*window)) { bridge->KeyboardDriving(); return; }
            key = ::Qt::Key_Return; break;
        }
        QKeyEvent press(QEvent::KeyPress, key, ::Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        if (!press.isAccepted()) MphRead::Qt::FocusNav::Unhandled(*window, key);
        QKeyEvent release(QEvent::KeyRelease, key, ::Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        bridge->KeyboardDriving();
    });
    QTimer input;
    QObject::connect(&input, &QTimer::timeout, [&]
    {
        if (!bridge->Showing() || !Input::GamepadContexts::Focused()) { router.Reset(); return; }
        router.Update(Input::GamepadManager::Snapshot(), Input::GamepadContexts::Capturing()
            ? Input::GamepadContext::BindingCapture : Input::GamepadContext::Menu,
            MphRead::NativeRuntime::EnvironmentTickCount64());
    });
    input.start(16);
    const int result = application.exec();
    bridge.reset();
    return result;
}
