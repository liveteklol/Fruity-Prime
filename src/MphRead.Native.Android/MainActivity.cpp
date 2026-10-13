#include "AndroidQuick.hpp"
#include "MainActivity.hpp"

#include "AndroidConsole.hpp"
#include "AndroidGlContextGate.hpp"
#include "AndroidHunterShot.hpp"
#include "AndroidLogShare.hpp"
#include "AndroidMaps.hpp"
#include "AndroidPng.hpp"
#include "AndroidUiSurface.hpp"
#include "AndroidUpdateInstaller.hpp"
#include "AndroidWebLink.hpp"
#include "GamepadBridge.hpp"
#include "OffscreenGl.hpp"

#include "../MphRead.Native/Entities/Players/PlayerEntity.hpp"
#include "../MphRead.Native/GameState.hpp"
#include "../MphRead.Native/Mods/DebugLog.hpp"
#include "../MphRead.Native/Mods/EndScreen.hpp"
#include "../MphRead.Native/Mods/Input/GamepadInput.hpp"
#include "../MphRead.Native/Mods/Input/InputSourceTracker.hpp"
#include "../MphRead.Native/Mods/Input/GamepadUiRouter.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/GameFiles.hpp"
#include "../MphRead.Native/Mods/Launcher/Portable/LauncherPrefs.hpp"
#include "../MphRead.Native/Mods/LogShare.hpp"
#include "Mods/Network/Replay/DemoPlayback.hpp"
#include "Mods/Network/Integration/Launch/NetHostSession.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "../MphRead.Native/Mods/SpectatorMode.hpp"
#include "../MphRead.Native/Mods/ScreenCapture.hpp"
#include "../MphRead.Native/Mods/ThumbnailGenerator.hpp"
#include "../MphRead.Native/Mods/ThumbnailHost.hpp"
#include "../MphRead.Native/Mods/Update/UpdateInstall.hpp"

#include <bit>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr std::int32_t KeyActionDown = 0;
    constexpr std::int32_t KeyActionUp = 1;
    constexpr std::int32_t MotionEventActionDown = 0;

    [[nodiscard]] std::int64_t UncheckedSubtract(
        std::int64_t left,
        std::int64_t right
    ) noexcept
    {
        return std::bit_cast<std::int64_t>(
            static_cast<std::uint64_t>(left)
            - static_cast<std::uint64_t>(right)
        );
    }

    class ScopedJniEnv final
    {
    public:
        explicit ScopedJniEnv(JavaVM* javaVm)
            : _javaVm(javaVm)
        {
            if (_javaVm == nullptr)
            {
                throw std::runtime_error("Android Java VM is not available");
            }

            const jint result = _javaVm->GetEnv(
                reinterpret_cast<void**>(&_env),
                JNI_VERSION_1_6
            );
            if (result == JNI_EDETACHED)
            {
                if (_javaVm->AttachCurrentThread(&_env, nullptr) != JNI_OK)
                {
                    throw std::runtime_error(
                        "could not attach the current thread to the Android Java VM"
                    );
                }
                _attached = true;
            }
            else if (result != JNI_OK || _env == nullptr)
            {
                throw std::runtime_error(
                    "could not obtain the Android JNI environment"
                );
            }
        }

        ~ScopedJniEnv()
        {
            if (_attached)
            {
                _javaVm->DetachCurrentThread();
            }
        }

        ScopedJniEnv(const ScopedJniEnv&) = delete;
        ScopedJniEnv& operator=(const ScopedJniEnv&) = delete;

        [[nodiscard]] JNIEnv* Get() const noexcept
        {
            return _env;
        }

    private:
        JavaVM* _javaVm = nullptr;
        JNIEnv* _env = nullptr;
        bool _attached = false;
    };

    void DeleteGlobalRefNoThrow(JavaVM* javaVm, jobject value) noexcept
    {
        if (javaVm == nullptr || value == nullptr)
        {
            return;
        }

        JNIEnv* env = nullptr;
        bool attached = false;
        const jint result = javaVm->GetEnv(
            reinterpret_cast<void**>(&env),
            JNI_VERSION_1_6
        );
        if (result == JNI_EDETACHED)
        {
            if (javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK)
            {
                return;
            }
            attached = true;
        }
        else if (result != JNI_OK || env == nullptr)
        {
            return;
        }

        env->DeleteGlobalRef(value);
        if (attached)
        {
            javaVm->DetachCurrentThread();
        }
    }

    void WriteConsoleLine(const std::string& line)
    {
        std::cout << line << '\n';
    }

    template <typename T>
    class LocalJniRef final
    {
    public:
        LocalJniRef(JNIEnv* env, T value) noexcept
            : _env(env), _value(value)
        {
        }

        ~LocalJniRef()
        {
            if (_env != nullptr && _value != nullptr)
            {
                _env->DeleteLocalRef(_value);
            }
        }

        LocalJniRef(const LocalJniRef&) = delete;
        LocalJniRef& operator=(const LocalJniRef&) = delete;

        LocalJniRef(LocalJniRef&& other) noexcept
            : _env(other._env), _value(other._value)
        {
            other._env = nullptr;
            other._value = nullptr;
        }

        [[nodiscard]] T Get() const noexcept
        {
            return _value;
        }

    private:
        JNIEnv* _env = nullptr;
        T _value = nullptr;
    };

    void CheckJavaCall(JNIEnv* env, const char* operation)
    {
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            throw std::runtime_error(operation);
        }
    }

    LocalJniRef<jobject> GetApplicationContext(
        JNIEnv* env,
        jobject activity
    )
    {
        LocalJniRef<jclass> activityClass(
            env,
            env->GetObjectClass(activity)
        );
        CheckJavaCall(env, "could not inspect the Android Activity");
        if (activityClass.Get() == nullptr)
        {
            throw std::runtime_error("could not inspect the Android Activity");
        }

        const jmethodID getApplicationContext = env->GetMethodID(
            activityClass.Get(),
            "getApplicationContext",
            "()Landroid/content/Context;"
        );
        CheckJavaCall(env, "Android Context.getApplicationContext was not found");
        if (getApplicationContext == nullptr)
        {
            throw std::runtime_error(
                "Android Context.getApplicationContext was not found"
            );
        }

        LocalJniRef<jobject> context(
            env,
            env->CallObjectMethod(activity, getApplicationContext)
        );
        CheckJavaCall(env, "could not get the Android Application Context");
        if (context.Get() == nullptr)
        {
            throw std::runtime_error(
                "could not get the Android Application Context"
            );
        }
        return context;
    }


}

namespace MphRead::Droid
{
    std::atomic<MainActivity*> MainActivity::_instance{nullptr};

    MainActivity::MainActivity(JNIEnv* env, jobject activity)
    {
        if (env == nullptr)
        {
            throw std::invalid_argument(
                "Android JNI environment must not be null"
            );
        }
        if (activity == nullptr)
        {
            throw std::invalid_argument(
                "Android Activity must not be null"
            );
        }
        if (env->GetJavaVM(&_javaVm) != JNI_OK || _javaVm == nullptr)
        {
            throw std::runtime_error("Android Java VM is not available");
        }

        _activity = env->NewGlobalRef(activity);
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            _activity = nullptr;
            throw std::runtime_error(
                "could not retain the Android Activity"
            );
        }
        if (_activity == nullptr)
        {
            throw std::bad_alloc();
        }
    }

    MainActivity::~MainActivity()
    {
        DeleteGlobalRefNoThrow(_javaVm, _activity);
    }

    MainActivity* MainActivity::Instance() noexcept
    {
        return _instance.load(std::memory_order_relaxed);
    }

    bool MainActivity::InMatch() const noexcept
    {
        return _gameView.load(std::memory_order_relaxed) != nullptr;
    }

    double MainActivity::DisplayDensity() const
    {
        return GetMainActivityOwner().DisplayDensity(
            const_cast<MainActivity&>(*this));
    }

    MainActivityAppBuilderRef MainActivity::CustomizeAppBuilder(
        MainActivityAppBuilderRef builder
    )
    {
        MainActivityOwner& owner = GetMainActivityOwner();

        AndroidConsole::Install();

        const std::string root = ChooseRoot();
        if (!root.empty())
        {
            MphRead::Mods::Launcher::LauncherPrefs::Directory(root);
            MphRead::Mods::Launcher::GameFiles::Root(root);
            try
            {
                owner.SetCurrentDirectory(root);
            }
            catch (const std::exception&)
            {
                WriteConsoleLine(
                    "[android] could not use " + root
                    + " as the working directory: "
                    + owner.ExceptionMessage(std::current_exception())
                );
            }
        }

        JNIEnv* env = CurrentEnv();
        const std::u16string root16 = owner.ToUtf16(root);
        AndroidMaps::Install(env, owner.Assets(env, *this), root16);

        MphRead::Mods::ThumbnailHost::Current(
            std::make_shared<AndroidThumbnailHost>()
        );

        MphRead::Mods::Update::UpdateInstall::Current(
            std::make_shared<AndroidUpdateInstaller>()
        );

        const LocalJniRef<jobject> applicationContext =
            GetApplicationContext(env, _activity);
        MphRead::Mods::LogShare::Current(
            std::make_shared<AndroidLogShare>(env, applicationContext.Get())
        );

        MphRead::Mods::Platform::WebLink::Current(
            std::make_shared<AndroidWebLink>(env, applicationContext.Get())
        );

        (void)AndroidHunterShot::Install();
        MphRead::Mods::ScreenCapture::PngWriter(AndroidPng::Write);

        return builder;
    }

    std::string MainActivity::ChooseRoot()
    {
        MainActivityOwner& owner = GetMainActivityOwner();
        std::vector<std::string> candidates;

        const std::optional<std::string> external =
            owner.ExternalFilesPath(*this);
        if (external.has_value() && !external->empty())
        {
            candidates.push_back(*external);
        }

        const std::optional<std::string> internal =
            owner.InternalFilesPath(*this);
        if (internal.has_value() && !internal->empty())
        {
            candidates.push_back(*internal);
        }

        for (const std::string& candidate : candidates)
        {
            if (Writable(candidate)
                && owner.FileExists(
                    owner.CombinePath(candidate, "paths.txt")
                ))
            {
                return candidate;
            }
        }

        for (const std::string& candidate : candidates)
        {
            if (Writable(candidate))
            {
                if (candidate != candidates[0])
                {
                    WriteConsoleLine(
                        "[android] " + candidates[0]
                        + " cannot be written to; using " + candidate
                    );
                }
                return candidate;
            }
        }

        return {};
    }

    bool MainActivity::Writable(std::string_view directory)
    {
        MainActivityOwner& owner = GetMainActivityOwner();
        try
        {
            owner.CreateDirectory(directory);
            const std::string probe =
                owner.CombinePath(directory, ".write-probe");
            owner.WriteEmptyFile(probe);
            owner.DeleteFile(probe);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    void MainActivity::OnCreate(jobject savedInstanceState)
    {
        _instance.store(this, std::memory_order_relaxed);

        MainActivityOwner& owner = GetMainActivityOwner();
        owner.BaseOnCreate(*this, savedInstanceState);

        GamepadBridge::Start(CurrentEnv(), [this, &owner]()
        {
            if (_inputDevices)
            {
                owner.UnregisterInputDeviceListener(_inputDevices, *this);
                _inputDevices = {};
            }
            _inputDevices = owner.InputManager(*this);
            if (_inputDevices)
            {
                owner.RegisterInputDeviceListener(_inputDevices, *this);
            }
        });
        MphRead::Mods::Input::GamepadContexts::MenuVisible(true);

        owner.RunBackground([]()
        {
            AndroidMaps::EnsureBuilt();
        });

        _content = owner.ContentViewGroup(*this);
        _launcherView = _content
            ? owner.ChildAt(_content, 0)
            : MainActivityObjectRef{};
    }

    std::shared_future<int> MainActivity::RenderPreviews(
        std::vector<std::string> rooms,
        std::function<void(const std::string&)> report
    )
    {
        MainActivityOwner& owner = GetMainActivityOwner();

        if (rooms.empty()
            || _renderingPreviews.load(std::memory_order_acquire))
        {
            return owner.CompletedIntTask(0);
        }

        _renderingPreviews.store(true, std::memory_order_release);

        auto reportOnUi = [this, report = std::move(report)](
            const std::string& line)
        {
            GetMainActivityOwner().RunOnUiThread(
                [report, line]()
                {
                    report(line);
                }
            );
        };

        owner.RunOnUiThread([this]()
        {
            AndroidApp::SuspendBackdrop(true);
            GetMainActivityOwner().AddKeepScreenOn(*this);
        });

        return owner.RunBackgroundInt(
            [this, rooms = std::move(rooms), reportOnUi]() -> int
            {
                const auto clock = std::chrono::steady_clock::now();
                std::int32_t result = 0;
                std::exception_ptr escaped{};

                try
                {
                    try
                    {
                        MphRead::Mods::ThumbnailGenerator::
                            EnsureCacheDirectory();

                        ScopedJniEnv scopedEnv(_javaVm);
                        std::int32_t written = PreviewWorkers::Run(
                            scopedEnv.Get(),
                            _activity,
                            rooms,
                            PreviewWidth,
                            PreviewHeight,
                            reportOnUi
                        );

                        std::vector<std::string> left;
                        for (const std::string& room : rooms)
                        {
                            if (!MphRead::Mods::ThumbnailGenerator::
                                    Exists(room))
                            {
                                left.push_back(room);
                            }
                        }

                        if (!left.empty())
                        {
                            reportOnUi(
                                "[thumbnails] "
                                + std::to_string(left.size())
                                + " left to render here"
                            );
                            written += RenderHere(left, reportOnUi);
                        }

                        const double elapsed =
                            std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - clock
                            ).count();
                        reportOnUi(
                            "[thumbnails] "
                            + std::to_string(written)
                            + "/"
                            + std::to_string(rooms.size())
                            + " in "
                            + GetMainActivityOwner()
                                .FormatDoubleFixed1Current(elapsed)
                            + "s"
                        );
                        result = written;
                    }
                    catch (const std::exception&)
                    {
                        const std::exception_ptr error =
                            std::current_exception();
                        MainActivityOwner& workerOwner =
                            GetMainActivityOwner();
                        WriteConsoleLine(
                            "[thumbnails] the run failed: "
                            + workerOwner.ExceptionToString(error)
                        );
                        reportOnUi(
                            "[thumbnails] "
                            + workerOwner.ExceptionMessage(error)
                        );
                        result = 0;
                    }
                }
                catch (...)
                {
                    escaped = std::current_exception();
                }

                _renderingPreviews.store(
                    false,
                    std::memory_order_release
                );

                GetMainActivityOwner().RunOnUiThread([this]()
                {
                    if (!InMatch())
                    {
                        AndroidApp::SuspendBackdrop(false);
                        GetMainActivityOwner()
                            .ClearKeepScreenOn(*this);
                    }
                });

                if (escaped)
                {
                    std::rethrow_exception(escaped);
                }

                return result;
            }
        );
    }

    std::int32_t MainActivity::RenderHere(
        const std::vector<std::string>& rooms,
        const std::function<void(const std::string&)>& report
    )
    {
        if (InMatch() || _pending)
        {
            report("[thumbnails] not while a match is running");
            return 0;
        }

        _renderingHere.store(true, std::memory_order_release);
        _stopPreviews.store(false, std::memory_order_release);

        std::int32_t result = 0;
        std::exception_ptr escaped{};

        try
        {
            try
            {
                // A persistent hunter-preview worker may still own its
                // offscreen context. Retire it before taking the global GLES
                // lease for room-preview rendering.
                AndroidHunterShot::RetireCurrent();
                AndroidGlContextLease glContextLease;
                std::shared_ptr<OffscreenGl> gl =
                    OffscreenGl::Create(PreviewWidth, PreviewHeight);

                std::int32_t rendered = 0;
                std::exception_ptr renderError{};
                try
                {
                    rendered = GetMainActivityOwner().PreviewRunRender(
                        rooms,
                        PreviewWidth,
                        PreviewHeight,
                        report,
                        [this]()
                        {
                            return _stopPreviews.load(
                                std::memory_order_acquire
                            );
                        }
                    );
                }
                catch (...)
                {
                    renderError = std::current_exception();
                }

                if (gl)
                {
                    gl->Dispose();
                }

                if (renderError)
                {
                    std::rethrow_exception(renderError);
                }

                result = rendered;
            }
            catch (const std::exception&)
            {
                const std::exception_ptr error =
                    std::current_exception();
                MainActivityOwner& owner = GetMainActivityOwner();
                WriteConsoleLine(
                    "[thumbnails] the offscreen context failed: "
                    + owner.ExceptionToString(error)
                );
                report(
                    "[thumbnails] "
                    + owner.ExceptionMessage(error)
                );
                result = 0;
            }
        }
        catch (...)
        {
            escaped = std::current_exception();
        }

        _renderingHere.store(false, std::memory_order_release);
        _stopPreviews.store(false, std::memory_order_release);
        // Match start/cancel/end are UI-thread state transitions. Decide
        // whether hunter previews may resume on that same thread so thumbnail
        // completion cannot race StartMatch() and clear a newly asserted
        // retirement barrier.
        GetMainActivityOwner().RunOnUiThread(
            [this]()
            {
                if (!_pending && !InMatch())
                {
                    AndroidHunterShot::ResumeCurrent();
                }
            }
        );

        if (escaped)
        {
            std::rethrow_exception(escaped);
        }

        return result;
    }

    std::int32_t MainActivity::CurrentRotation()
    {
        try
        {
            MainActivityOwner& owner = GetMainActivityOwner();
            if (owner.IsAndroidVersionAtLeast30())
            {
                return owner.DisplayRotation(*this);
            }
            return owner.DefaultDisplayRotation(*this);
        }
            catch (const std::exception&)
            {
                return -1;
        }
    }

    void MainActivity::OnDisplayAdded(std::int32_t displayId)
    {
        (void)displayId;
    }

    void MainActivity::OnDisplayRemoved(std::int32_t displayId)
    {
        (void)displayId;
    }

    void MainActivity::OnDisplayChanged(std::int32_t displayId)
    {
        (void)displayId;

        const std::int32_t rotation = CurrentRotation();
        if (rotation < 0 || rotation == _lastRotation)
        {
            return;
        }

        const std::int32_t before = _lastRotation;
        _lastRotation = rotation;

        if (before < 0)
        {
            return;
        }

        MainActivityOwner& owner = GetMainActivityOwner();
        const std::string beforeText =
            owner.FormatInt32Current(before);
        const std::string rotationText =
            owner.FormatInt32Current(rotation);
        const std::string widthText =
            owner.FormatInt32Current(ContentSize().Width);
        const std::string heightText =
            owner.FormatInt32Current(ContentSize().Height);
        WriteConsoleLine(
            "[android] display rotation "
            + beforeText
            + " -> "
            + rotationText
            + " at "
            + widthText
            + "x"
            + heightText
        );

        AfterRotation();

        if (_content)
        {
            owner.PostDelayed(
                _content,
                [this]()
                {
                    AfterRotation();
                },
                SettleMs
            );
        }
    }

    void MainActivity::OnInputDeviceAdded(std::int32_t deviceId)
    {
        GamepadBridge::DeviceAdded(CurrentEnv(), deviceId);
    }

    void MainActivity::OnInputDeviceChanged(std::int32_t deviceId)
    {
        GamepadBridge::DeviceChanged(CurrentEnv(), deviceId);
    }

    void MainActivity::OnInputDeviceRemoved(std::int32_t deviceId)
    {
        GamepadBridge::DeviceRemoved(deviceId);
    }

    void MainActivity::AfterRotation()
    {
        _controls.ReleaseEverything();

        MainActivityOwner& owner = GetMainActivityOwner();
        if (_overlay)
        {
            owner.TouchOverlayRefresh(_overlay);
        }
        if (_content)
        {
            owner.RequestLayout(_content);
            owner.Invalidate(_content);
        }
        owner.RequestDecorLayout(*this);

        GoImmersive(true);
    }

    void MainActivity::OnConfigurationChanged(jobject newConfig)
    {
        GetMainActivityOwner().BaseOnConfigurationChanged(
            *this, newConfig);
        OnDisplayChanged(0);
    }

    void MainActivity::OnPause()
    {
        MainActivityOwner& owner = GetMainActivityOwner();

        if (_displays)
        {
            owner.UnregisterDisplayListener(_displays, *this);
            _displays = {};
        }

        if (MainActivityObjectRef gameView = LoadGameView();
            gameView)
        {
            owner.GameViewOnPause(gameView);
        }

        owner.BaseOnPause(*this);
    }

    void MainActivity::OnResume()
    {
        MainActivityOwner& owner = GetMainActivityOwner();

        owner.BaseOnResume(*this);

        GoImmersive(true);
        _lastRotation = CurrentRotation();

        if (!_displays)
        {
            MainActivityObjectRef manager =
                owner.DisplayManager(*this);
            if (manager)
            {
                _displays = manager;
                owner.RegisterDisplayListener(_displays, *this);
            }
        }

        if (MainActivityObjectRef gameView = LoadGameView();
            gameView)
        {
            owner.GameViewOnResume(gameView);
        }
    }

    bool MainActivity::DispatchKeyEvent(jobject event)
    {
        MainActivityOwner& owner = GetMainActivityOwner();

        const bool down = event != nullptr
            && owner.KeyEventAction(event) == KeyActionDown;

        if (event != nullptr
            && (down || owner.KeyEventAction(event) == KeyActionUp))
        {
            JNIEnv* env = CurrentEnv();
            if (GamepadBridge::HandleKey(
                    owner.KeyEventKeyCode(event),
                    event,
                    down,
                    env))
            {
                if (down)
                {
                    _controls.NotePadActivity();
                }
                return true;
            }
        }

        return owner.BaseDispatchKeyEvent(*this, event);
    }

    bool MainActivity::DispatchGenericMotionEvent(jobject event)
    {
        JNIEnv* env = event == nullptr ? nullptr : CurrentEnv();
        if (GamepadBridge::HandleMotion(event, env))
        {
            if (MphRead::Mods::Input::GamepadInput::InUse())
            {
                _controls.NotePadActivity();
            }
            return true;
        }

        return GetMainActivityOwner()
            .BaseDispatchGenericMotionEvent(*this, event);
    }

    bool MainActivity::DispatchTouchEvent(jobject event)
    {
        MainActivityOwner& owner = GetMainActivityOwner();
        if (event != nullptr
            && owner.MotionEventActionMasked(event)
                == MotionEventActionDown)
        {
            MphRead::Mods::Input::InputSourceTracker::Note(
                MphRead::Mods::Input::InputSource::Touch
            );
        }
        return owner.BaseDispatchTouchEvent(*this, event);
    }

    void MainActivity::OnWindowFocusChanged(bool hasFocus)
    {
        GetMainActivityOwner().BaseOnWindowFocusChanged(
            *this, hasFocus);
        MphRead::Mods::Input::GamepadContexts::Focused(hasFocus);
        if (!hasFocus)
        {
            GamepadBridge::Clear();
        }
        if (hasFocus)
        {
            GoImmersive(true);
        }
    }

    void MainActivity::OnDestroy()
    {
        if (_instance.load(std::memory_order_relaxed) == this)
        {
            _instance.store(nullptr, std::memory_order_relaxed);
        }

        if (MainActivityObjectRef gameView = LoadGameView();
            gameView)
        {
            GetMainActivityOwner().GameViewStop(gameView);
        }

        MainActivityOwner& owner = GetMainActivityOwner();
        if (_inputDevices)
        {
            owner.UnregisterInputDeviceListener(_inputDevices, *this);
            _inputDevices = {};
        }
        GamepadBridge::Stop();

        owner.BaseOnDestroy(*this);
    }

    void MainActivity::ShowLauncher(bool visible)
    {
        if (!_launcherView) return;
        auto& owner = GetMainActivityOwner();
        if (visible) owner.SetViewVisible(_launcherView);
        else owner.SetViewGone(_launcherView);
    }

    void MainActivity::OnBackPressed()
    {
        if (InMatch())
        {
            TogglePauseMenu();
            return;
        }

        if (_pending)
        {
            CancelPending("the player went back");
            return;
        }

        if (std::shared_ptr<AndroidLauncherPage> home =
                AndroidApp::Home();
            home && home->GoBack())
        {
            return;
        }

        GetMainActivityOwner().BaseOnBackPressed(*this);
    }

    void MainActivity::StartMatch(
        const MphRead::Mods::Launcher::LaunchPlan& plan
    )
    {
        if (std::shared_ptr<
                AndroidLauncherPage> home =
                AndroidApp::Home();
            home)
        {
            home->SuspendLobby();
        }

        if (!_content || InMatch())
        {
            return;
        }

        if (_pending)
        {
            WriteConsoleLine(
                "[android] a match is already starting; ignoring"
            );
            return;
        }

        if (_renderingHere.load(std::memory_order_acquire))
        {
            WriteConsoleLine(
                "[android] stopping the preview run: a match was asked for"
            );
            _stopPreviews.store(true, std::memory_order_release);
        }

        AndroidHunterShot::RetireCurrent();

        std::shared_ptr<AndroidInput> input =
            std::make_shared<AndroidInput>();

        _controls.ReleaseEverything();
        _controls.SetSpectator(false, false);

        MainActivityOwner& owner = GetMainActivityOwner();
        _orientationBefore = owner.RequestedOrientation(*this);
        owner.RequestedOrientation(
            *this,
            MainActivityOrientation::SensorLandscape
        );
        owner.AddKeepScreenOn(*this);
        GoImmersive(true);

        _pending = std::make_unique<PendingMatch>(
            PendingMatch{plan, input}
        );

        _waitingSince = owner.UptimeMillis();
        if (!_endPanelTickScheduled)
        {
            _endPanelTickScheduled = true;
            owner.PostDelayed(
                _content,
                [this]()
                {
                    TickEndPanel();
                },
                100
            );
        }
        _lastSize = ContentSize();
        _sizeSettledAt = _waitingSince;

        const std::string& roomKey = RequireRoomKey(plan);
        ShowNotice(
            !roomKey.empty()
                ? "Loading " + roomKey + "..."
                : "Loading your game..."
        );

        const std::string startWidth =
            owner.FormatInt32Current(_lastSize.Width);
        const std::string startHeight =
            owner.FormatInt32Current(_lastSize.Height);
        WriteConsoleLine(
            "[android] starting "
            + roomKey
            + " from "
            + startWidth
            + "x"
            + startHeight
        );

        WaitForSteadyWindow();
    }

    MainActivitySize MainActivity::ContentSize() const
    {
        if (!_content)
        {
            return {};
        }
        return GetMainActivityOwner().ViewSize(_content);
    }

    MainActivityObjectRef MainActivity::LoadGameView() const noexcept
    {
        return MainActivityObjectRef{
            _gameView.load(std::memory_order_relaxed)
        };
    }

    void MainActivity::StoreGameView(
        MainActivityObjectRef value
    ) noexcept
    {
        _gameView.store(
            std::move(value.Native),
            std::memory_order_relaxed
        );
    }

    void MainActivity::WaitForSteadyWindow()
    {
        if (!_pending || !_content || InMatch())
        {
            return;
        }

        MainActivityOwner& owner = GetMainActivityOwner();
        if (_renderingHere.load(std::memory_order_acquire))
        {
            _waitingSince = owner.UptimeMillis();
            _sizeSettledAt = _waitingSince;
            owner.PostDelayed(
                _content,
                [this]()
                {
                    WaitForSteadyWindow();
                },
                50
            );
            return;
        }

        const std::int64_t now = owner.UptimeMillis();
        const MainActivitySize size = ContentSize();

        if (!(size == _lastSize))
        {
            _lastSize = size;
            _sizeSettledAt = now;
        }

        const bool haveSize =
            size.Width > 0 && size.Height > 0;
        const bool landscape =
            size.Width > size.Height;
        const bool steady =
            haveSize
            && UncheckedSubtract(now, _sizeSettledAt) >= SettleMs;
        const std::int64_t waited =
            UncheckedSubtract(now, _waitingSince);

        if (steady && landscape)
        {
            StartPending(std::nullopt);
            return;
        }

        if (haveSize && waited >= RotateMs)
        {
            StartPending(
                landscape
                    ? std::optional<std::string>(
                        "the window was still moving after "
                        + owner.FormatInt64Current(waited)
                        + " ms")
                    : std::optional<std::string>(
                        "the display did not turn landscape in "
                        + owner.FormatInt64Current(waited)
                        + " ms")
            );
            return;
        }

        if (waited >= GiveUpMs)
        {
            CancelPending(
                "the window never took a size ("
                + owner.FormatInt32Current(size.Width)
                + "x"
                + owner.FormatInt32Current(size.Height)
                + ")"
            );
            return;
        }

        owner.PostDelayed(
            _content,
            [this]()
            {
                WaitForSteadyWindow();
            },
            50
        );
    }

    void MainActivity::CancelPending(std::string reason)
    {
        if (!_pending)
        {
            return;
        }

        WriteConsoleLine(
            "[android] the match was not started: " + reason
        );

        _pending.reset();
        HideNotice();
        _controls.ReleaseEverything();
        AndroidHunterShot::ResumeCurrent();

        MainActivityOwner& owner = GetMainActivityOwner();
        if (_launcherView)
        {
            owner.SetViewVisible(_launcherView);
            MphRead::Mods::Input::GamepadContexts::MenuVisible(true);
        }

        if (std::shared_ptr<
                AndroidLauncherPage> home =
                AndroidApp::Home();
            home)
        {
            home->Reset();
        }
        else
        {
        }

        owner.ClearKeepScreenOn(*this);
        GoImmersive(true);
        owner.RequestedOrientation(*this, _orientationBefore);

        owner.ToastLong(
            *this,
            "Could not start the match: " + reason
        );
    }

    void MainActivity::StartPending(
        std::optional<std::string> note
    )
    {
        if (!_pending || InMatch())
        {
            return;
        }

        std::unique_ptr<PendingMatch> pending =
            std::move(_pending);

        MainActivityOwner& owner = GetMainActivityOwner();
        if (_launcherView)
        {
            owner.SetViewGone(_launcherView);
            MphRead::Mods::Input::GamepadContexts::MenuVisible(false);
        }

        AndroidApp::SuspendBackdrop(true);

        if (note.has_value())
        {
            WriteConsoleLine(
                "[android] starting the match anyway: " + *note
            );
        }

        const std::string widthText =
            owner.FormatInt32Current(ContentSize().Width);
        const std::string heightText =
            owner.FormatInt32Current(ContentSize().Height);
        WriteConsoleLine(
            "[android] building the match at "
            + widthText
            + "x"
            + heightText
        );

        owner.RequestedOrientation(
            *this,
            MainActivityOrientation::Locked
        );

        BeginMatch(pending->Plan, pending->Input);
    }

    void MainActivity::ShowSoftKeyboard(bool show)
    {
        MainActivityObjectRef gameView = LoadGameView();
        if (!gameView)
        {
            return;
        }

        MainActivityOwner& owner = GetMainActivityOwner();
        MainActivityObjectRef ime =
            owner.InputMethodManager(*this);
        if (!ime)
        {
            return;
        }

        if (show)
        {
            owner.GameViewRequestFocus(gameView);
            owner.ShowSoftInput(ime, gameView);
        }
        else
        {
            owner.HideSoftInput(ime, gameView);
        }
    }

    void MainActivity::BeginMatch(
        const MphRead::Mods::Launcher::LaunchPlan& plan,
        std::shared_ptr<AndroidInput> input
    )
    {
        if (!_content)
        {
            return;
        }

        MainActivityOwner& owner = GetMainActivityOwner();

        MainActivityObjectRef gameView =
            owner.CreateGameView(
                *this,
                _controls,
                std::move(input),
                plan,
                [this]()
                {
                    GetMainActivityOwner().RunOnUiThread(
                        [this]()
                        {
                            EndMatch();
                        }
                    );
                },
                [this]()
                {
                    GetMainActivityOwner().RunOnUiThread(
                        [this]()
                        {
                            EndMatch();
                        }
                    );
                },
                [this]()
                {
                    GetMainActivityOwner().RunOnUiThread(
                        [this]()
                        {
                            MatchLoaded();
                        }
                    );
                },
                [this](std::string error)
                {
                    GetMainActivityOwner().RunOnUiThread(
                        [this, error = std::move(error)]() mutable
                        {
                            FailMatch(std::move(error));
                        }
                    );
                },
                [this]()
                {
                    GetMainActivityOwner().RunOnUiThread(
                        [this]()
                        {
                            TogglePauseMenu();
                        }
                    );
                },
                [this](bool show)
                {
                    GetMainActivityOwner().RunOnUiThread(
                        [this, show]()
                        {
                            ShowSoftKeyboard(show);
                        }
                    );
                }
            );

        StoreGameView(gameView);

        owner.GameViewSetZOrderMediaOverlay(
            gameView, true);

        _overlay = owner.CreateTouchOverlayView(
            *this, _controls);

        owner.AddView(_content, gameView);
        owner.AddView(_content, _overlay);

        ShowNotice(
            "Loading " + RequireRoomKey(plan) + "..."
        );
        if (_notice)
        {
            owner.BringToFront(_notice);
        }
    }

    void MainActivity::ShowNotice(std::string text)
    {
        if (!_content)
        {
            return;
        }

        MainActivityOwner& owner = GetMainActivityOwner();
        if (_notice)
        {
            owner.SetNoticeText(_notice, std::move(text));
            return;
        }

        _notice = owner.CreateNoticeTextView(
            *this,
            std::move(text),
            0xE6E6EAF2u,
            0xFF0A0C10u
        );
        owner.AddView(_content, _notice);
    }

    void MainActivity::MatchLoaded()
    {
        HideNotice();
        GetMainActivityOwner().RequestedOrientation(
            *this,
            MainActivityOrientation::SensorLandscape
        );
    }

    void MainActivity::HideNotice()
    {
        if (_notice && _content)
        {
            GetMainActivityOwner().RemoveView(
                _content, _notice);
            _notice = {};
        }
    }

    void MainActivity::FailMatch(std::string message)
    {
        MainActivityOwner& owner = GetMainActivityOwner();

        if (_notice)
        {
            owner.SetNoticeText(_notice, message);
        }
        else
        {
            owner.ToastLong(*this, message);
        }

        if (_content)
        {
            owner.PostDelayed(
                _content,
                [this]()
                {
                    EndMatch();
                },
                4000
            );
        }
    }

    void MainActivity::TickEndPanel()
    {
        if (!InMatch() && !_pending)
        {
            HideEndPanel();
            _endPanelTickScheduled = false;
            return;
        }

        const bool want =
            MphRead::Mods::EndScreen::Available() && !_pauseMenuOpen;
        if (want != _endPanelWanted)
        {
            _endPanelWanted = want;
            const auto boolText = [](bool value)
            {
                return value ? "True" : "False";
            };
            const std::shared_ptr<MphRead::Entities::PlayerEntity> main =
                MphRead::Entities::PlayerEntity::Main();
            std::ostringstream message;
            message
                << "end panel " << (want ? "up" : "down")
                << ": state="
                << MphRead::ToString(MphRead::GameState::MatchState())
                << " main=" << boolText(main != nullptr)
                << " mp=" << boolText(MphRead::GameState::Multiplayer())
                << " menupause="
                << boolText(MphRead::GameState::MenuPause())
                << " spectating="
                << boolText(MphRead::Mods::SpectatorMode::IsSpectating())
                << " freecam="
                << boolText(MphRead::Mods::SpectatorMode::FreeCamera())
                << " pausemenu=" << boolText(_pauseMenuOpen);
            MphRead::Mods::DebugLog::Line("ui", message.str());
        }

        MainActivityOwner& owner = GetMainActivityOwner();
        if (want && !_endPanel)
        {
            std::shared_ptr<AndroidUiSurface> surface =
                AndroidUiSurface::Ensure();
            MainActivityObjectRef gameView = LoadGameView();
            if (surface && gameView)
            {
                const MainActivitySize size = owner.ViewSize(gameView);
                if (size.Width > 0 && size.Height > 0)
                {
                    _endPanel = true;
                    MphRead::Mods::EndScreen::PanelUp(true);
                    surface->ShowEnd();
                    ShowLauncher(true);
                    _controls.ReleaseEverything();
                    if (_overlay)
                    {
                        owner.Invalidate(_overlay);
                    }
                }
            }
        }
        else if (!want && _endPanel)
        {
            HideEndPanel();
        }

        if (_endPanelTickScheduled && _content)
        {
            owner.PostDelayed(
                _content,
                [this]()
                {
                    TickEndPanel();
                },
                100
            );
        }
    }

    void MainActivity::HideEndPanel()
    {
        if (!_endPanel)
        {
            return;
        }

        _endPanel = false;
        MphRead::Mods::EndScreen::PanelUp(false);
        if (std::shared_ptr<AndroidUiSurface> surface =
                AndroidUiSurface::Current();
            surface)
        {
            surface->Hide();
            ShowLauncher(false);
        }
        _controls.ReleaseEverything();
        if (_overlay)
        {
            GetMainActivityOwner().Invalidate(_overlay);
        }
    }

    void MainActivity::TogglePauseMenu()
    {
        if (!InMatch())
        {
            return;
        }

        if (_pauseMenuOpen)
        {
            ClosePauseMenu();
            return;
        }

        _pauseMenuOpen = true;
        _controls.ReleaseEverything();
        HideEndPanel();

        MainActivityOwner& owner = GetMainActivityOwner();
        if (_overlay)
        {
            owner.SetViewGone(_overlay);
        }
        if (MainActivityObjectRef gameView = LoadGameView();
            gameView)
        {
            owner.SetViewGone(gameView);
        }
        if (_launcherView)
        {
            owner.SetViewVisible(_launcherView);
            MphRead::Mods::Input::GamepadContexts::MenuVisible(true);
        }

        GoImmersive(true);

        if (std::shared_ptr<
                AndroidLauncherPage> home =
                AndroidApp::Home();
            home)
        {
            home->ShowPauseMenu(
                [this]()
                {
                    ClosePauseMenu();
                },
                [this]()
                {
                    EndMatch();
                },
                [this]()
                {
                    Finish();
                }
            );
        }
    }

    void MainActivity::ClosePauseMenu()
    {
        if (!_pauseMenuOpen)
        {
            return;
        }

        _pauseMenuOpen = false;
        MainActivityOwner& owner = GetMainActivityOwner();

        if (_launcherView)
        {
            owner.SetViewGone(_launcherView);
            MphRead::Mods::Input::GamepadContexts::MenuVisible(false);
        }
        if (MainActivityObjectRef gameView = LoadGameView();
            gameView)
        {
            owner.SetViewVisible(gameView);
        }
        if (_overlay)
        {
            owner.SetViewVisible(_overlay);
        }

        _controls.ReleaseEverything();
        _controls.ReloadSettings();
        GoImmersive(true);
    }

    void MainActivity::EndMatch()
    {
        EndMatchCore(false);
    }

    void MainActivity::EndMatchToLobby()
    {
        EndMatchCore(true);
    }

    void MainActivity::EndMatchCore(bool keepSession)
    {
        if (!_content)
        {
            return;
        }

        _pending.reset();
        _pauseMenuOpen = false;
        HideNotice();
        HideEndPanel();
        _endPanelTickScheduled = false;

        MainActivityOwner& owner = GetMainActivityOwner();

        if (_overlay)
        {
            owner.RemoveView(_content, _overlay);
            _overlay = {};
        }

        if (MainActivityObjectRef gameView = LoadGameView();
            gameView)
        {
            owner.GameViewStop(gameView);
            owner.RemoveView(_content, gameView);
            StoreGameView({});
        }

        // GameView may still be completing teardown asynchronously, but its
        // AndroidGlContextLease remains held until that is done. Resuming here
        // only permits launcher workers to queue; they cannot enter GlEs until
        // the game context releases the lease.
        AndroidHunterShot::ResumeCurrent();

        _controls.ReleaseEverything();
        _controls.SetSpectator(false, false);

        if (_launcherView)
        {
            owner.SetViewVisible(_launcherView);
            MphRead::Mods::Input::GamepadContexts::MenuVisible(true);
        }
        AndroidApp::SuspendBackdrop(
            _renderingPreviews.load(std::memory_order_acquire)
        );

        if (keepSession)
        {
            MphRead::Mods::Network::NetSession::ResetMatchState();
            if (std::shared_ptr<
                    AndroidLauncherPage> home =
                    AndroidApp::Home();
                home)
            {
                home->ResumeLobby();
            }
        }
        else
        {
            MphRead::Mods::Network::NetSession::Stop();
            MphRead::Mods::Network::NetHostSession::Stop();
            if (std::shared_ptr<
                    AndroidLauncherPage> home =
                    AndroidApp::Home();
                home)
            {
                home->Reset();
            }
        }

        MphRead::Mods::Network::DemoPlayback::Stop();

        owner.ClearKeepScreenOn(*this);
        GoImmersive(true);
        owner.RequestedOrientation(*this, _orientationBefore);
    }

    void MainActivity::GoImmersive(bool immersive)
    {
        MainActivityOwner& owner = GetMainActivityOwner();
        MainActivityObjectRef window = owner.Window(*this);
        if (!window)
        {
            return;
        }

        if (owner.IsAndroidVersionAtLeast30())
        {
            MainActivityObjectRef controller =
                owner.WindowInsetsController(window);
            if (controller)
            {
                if (immersive)
                {
                    owner.SetTransientBarsBySwipe(controller);
                    owner.HideSystemBars(controller);
                }
                else
                {
                    owner.ShowSystemBars(controller);
                }
            }
            return;
        }

        owner.SetLegacySystemUiVisibility(window, immersive);
    }

    void MainActivity::Finish()
    {
        GetMainActivityOwner().Finish(*this);
    }

    void MainActivity::RequestRomPick()
    {
        GetMainActivityOwner().RequestRomPick(*this);
    }

    const std::string& MainActivity::RequireRoomKey(
        const MphRead::Mods::Launcher::LaunchPlan& plan
    ) const
    {
        const std::optional<std::string>& roomKey =
            plan.RoomKey();
        if (!roomKey.has_value())
        {
            GetMainActivityOwner().ThrowNullReference();
        }
        return *roomKey;
    }

    JNIEnv* MainActivity::CurrentEnv() const
    {
        if (_javaVm == nullptr)
        {
            throw std::runtime_error(
                "Android Java VM is not available"
            );
        }

        JNIEnv* env = nullptr;
        const jint result = _javaVm->GetEnv(
            reinterpret_cast<void**>(&env),
            JNI_VERSION_1_6
        );
        if (result != JNI_OK || env == nullptr)
        {
            throw std::runtime_error(
                "the current thread is not attached to the Android Java VM"
            );
        }
        return env;
    }
}
