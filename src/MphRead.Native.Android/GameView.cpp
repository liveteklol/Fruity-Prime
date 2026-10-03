#include "GameView.hpp"

#include "AndroidGlContextGate.hpp"
#include "AndroidMatch.hpp"
#include "GamepadBridge.hpp"
#include "AndroidUiOverlay.hpp"
#include "AndroidUiSurface.hpp"
#include "../MphRead.Native/NativeRuntime/Rhi/SceneBackend.hpp"
#include "MainActivity.hpp"
#include "TouchControls.hpp"

#include "../MphRead.Native/Entities/Players/PlayerEntity.hpp"
#include "../MphRead.Native/GameState.hpp"
#include "../MphRead.Native/Mods/Chat/ChatBox.hpp"
#include "../MphRead.Native/Mods/DebugLog.hpp"
#include "../MphRead.Native/Mods/EndScreen.hpp"
#include "../MphRead.Native/Mods/Input/GamepadInput.hpp"
#include "../MphRead.Native/Mods/InputSettings.hpp"
#include "../MphRead.Native/Mods/Network/MapVote.hpp"
#include "../MphRead.Native/Mods/Network/NetSession.hpp"
#include "../MphRead.Native/Mods/Render/EsBindings.hpp"
#include "../MphRead.Native/Mods/Render/FrameTiming.hpp"
#include "../MphRead.Native/Mods/Render/GlEs.hpp"
#include "../MphRead.Native/NativeRuntime/OpenTK/GL.hpp"
#include "../MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlDevice.hpp"
#include "../MphRead.Native/Mods/Render/HunterShot.hpp"
#include "../MphRead.Native/Mods/SpectatorMode.hpp"
#include "../MphRead.Native/Renderer.hpp"
#include "../MphRead.Native/Scene.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/api-level.h>
#include <android/keycodes.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace
{
    using Keys = OpenTK::Windowing::GraphicsLibraryFramework::Keys;

    constexpr std::int32_t KeyUnknown = AKEYCODE_UNKNOWN;
    constexpr std::int32_t KeyEscapeValue = 256;
    constexpr std::int32_t KeyEnterValue = 257;
    constexpr std::int32_t KeyBackspaceValue = 259;
    constexpr std::int32_t KeyPadEnterValue = 335;

    constexpr std::int32_t InputTypeNull = 0;
    constexpr std::int32_t ImeActionDone = 6;
    constexpr std::int32_t ImeFlagNoFullscreen = 0x02000000;
    constexpr std::int32_t ImeFlagNoExtractUi = 0x10000000;

    template <typename T>
    class LocalRef final
    {
    public:
        LocalRef() = default;

        LocalRef(JNIEnv* env, T value) noexcept
            : _env(env),
              _value(value)
        {
        }

        ~LocalRef()
        {
            Reset();
        }

        LocalRef(const LocalRef&) = delete;
        LocalRef& operator=(const LocalRef&) = delete;

        LocalRef(LocalRef&& other) noexcept
            : _env(other._env),
              _value(other._value)
        {
            other._env = nullptr;
            other._value = nullptr;
        }

        LocalRef& operator=(LocalRef&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                _env = other._env;
                _value = other._value;
                other._env = nullptr;
                other._value = nullptr;
            }
            return *this;
        }

        [[nodiscard]] T Get() const noexcept
        {
            return _value;
        }

        explicit operator bool() const noexcept
        {
            return _value != nullptr;
        }

        T Release() noexcept
        {
            T value = _value;
            _env = nullptr;
            _value = nullptr;
            return value;
        }

        void Reset() noexcept
        {
            if (_env != nullptr && _value != nullptr)
            {
                _env->DeleteLocalRef(_value);
            }
            _env = nullptr;
            _value = nullptr;
        }

    private:
        JNIEnv* _env = nullptr;
        T _value = nullptr;
    };

    class ScopedJniEnv final
    {
    public:
        explicit ScopedJniEnv(JavaVM* vm)
            : _vm(vm)
        {
            if (_vm == nullptr)
            {
                throw std::runtime_error("Android Java VM is not available");
            }

            const jint result = _vm->GetEnv(
                reinterpret_cast<void**>(&_env),
                JNI_VERSION_1_6
            );
            if (result == JNI_EDETACHED)
            {
                if (_vm->AttachCurrentThread(&_env, nullptr) != JNI_OK)
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
                _vm->DetachCurrentThread();
            }
        }

        ScopedJniEnv(const ScopedJniEnv&) = delete;
        ScopedJniEnv& operator=(const ScopedJniEnv&) = delete;

        [[nodiscard]] JNIEnv* Get() const noexcept
        {
            return _env;
        }

    private:
        JavaVM* _vm = nullptr;
        JNIEnv* _env = nullptr;
        bool _attached = false;
    };

    std::string JavaStringToUtf8(JNIEnv* env, jstring value)
    {
        if (value == nullptr)
        {
            return {};
        }

        const char* chars = env->GetStringUTFChars(value, nullptr);
        if (chars == nullptr)
        {
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
            }
            return {};
        }

        std::string result(chars);
        env->ReleaseStringUTFChars(value, chars);
        return result;
    }

    [[noreturn]] void ThrowPendingJavaException(JNIEnv* env)
    {
        LocalRef<jthrowable> throwable(
            env,
            static_cast<jthrowable>(env->ExceptionOccurred())
        );
        env->ExceptionClear();

        std::string message = "Android Java exception";
        if (throwable)
        {
            LocalRef<jclass> type(env, env->GetObjectClass(throwable.Get()));
            if (type)
            {
                jmethodID getMessage = env->GetMethodID(
                    type.Get(),
                    "getMessage",
                    "()Ljava/lang/String;"
                );
                if (getMessage != nullptr && !env->ExceptionCheck())
                {
                    LocalRef<jstring> text(
                        env,
                        static_cast<jstring>(
                            env->CallObjectMethod(throwable.Get(), getMessage)
                        )
                    );
                    if (!env->ExceptionCheck() && text)
                    {
                        std::string value = JavaStringToUtf8(env, text.Get());
                        if (!value.empty())
                        {
                            message = std::move(value);
                        }
                    }
                }

                if (env->ExceptionCheck())
                {
                    env->ExceptionClear();
                }
            }
        }

        throw std::runtime_error(message);
    }

    void CheckJavaException(JNIEnv* env)
    {
        if (env->ExceptionCheck())
        {
            ThrowPendingJavaException(env);
        }
    }

    LocalRef<jclass> FindClass(JNIEnv* env, const char* name)
    {
        LocalRef<jclass> type(env, env->FindClass(name));
        CheckJavaException(env);
        if (!type)
        {
            throw std::runtime_error(
                std::string("Android class not found: ") + name
            );
        }
        return type;
    }

    LocalRef<jclass> ObjectClass(JNIEnv* env, jobject object)
    {
        LocalRef<jclass> type(env, env->GetObjectClass(object));
        CheckJavaException(env);
        if (!type)
        {
            throw std::runtime_error("Android object class is not available");
        }
        return type;
    }

    jmethodID GetMethodId(
        JNIEnv* env,
        jclass type,
        const char* name,
        const char* signature
    )
    {
        const jmethodID method = env->GetMethodID(type, name, signature);
        CheckJavaException(env);
        if (method == nullptr)
        {
            throw std::runtime_error(
                std::string("Android method not found: ") + name
            );
        }
        return method;
    }

    jfieldID GetFieldId(
        JNIEnv* env,
        jclass type,
        const char* name,
        const char* signature
    )
    {
        const jfieldID field = env->GetFieldID(type, name, signature);
        CheckJavaException(env);
        if (field == nullptr)
        {
            throw std::runtime_error(
                std::string("Android field not found: ") + name
            );
        }
        return field;
    }

    jobject NewGlobalRefChecked(JNIEnv* env, jobject value)
    {
        if (value == nullptr)
        {
            return nullptr;
        }

        jobject result = env->NewGlobalRef(value);
        CheckJavaException(env);
        if (result == nullptr)
        {
            throw std::bad_alloc();
        }
        return result;
    }

    void DeleteGlobalRefNoThrow(JavaVM* vm, jobject value) noexcept
    {
        if (vm == nullptr || value == nullptr)
        {
            return;
        }

        JNIEnv* env = nullptr;
        bool attached = false;
        const jint status = vm->GetEnv(
            reinterpret_cast<void**>(&env),
            JNI_VERSION_1_6
        );
        if (status == JNI_EDETACHED)
        {
            if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
            {
                return;
            }
            attached = true;
        }
        else if (status != JNI_OK || env == nullptr)
        {
            return;
        }

        env->DeleteGlobalRef(value);
        if (attached)
        {
            vm->DetachCurrentThread();
        }
    }

    class JavaGlobalRef final
    {
    public:
        JavaGlobalRef(JNIEnv* env, jobject value)
        {
            if (env == nullptr)
            {
                throw std::invalid_argument(
                    "Android JNI environment must not be null"
                );
            }
            if (env->GetJavaVM(&_vm) != JNI_OK || _vm == nullptr)
            {
                throw std::runtime_error("Android Java VM is not available");
            }
            _value = NewGlobalRefChecked(env, value);
        }

        ~JavaGlobalRef()
        {
            DeleteGlobalRefNoThrow(_vm, _value);
        }

        JavaGlobalRef(const JavaGlobalRef&) = delete;
        JavaGlobalRef& operator=(const JavaGlobalRef&) = delete;

        [[nodiscard]] JavaVM* Vm() const noexcept
        {
            return _vm;
        }

        [[nodiscard]] jobject Object() const noexcept
        {
            return _value;
        }

        [[nodiscard]] bool SameObject(
            JNIEnv* env,
            const JavaGlobalRef& other
        ) const
        {
            return env->IsSameObject(_value, other._value) == JNI_TRUE;
        }

    private:
        JavaVM* _vm = nullptr;
        jobject _value = nullptr;
    };

    std::string EglHex(EGLint error)
    {
        std::ostringstream stream;
        stream
            << "0x"
            << std::uppercase
            << std::hex
            << static_cast<std::uint32_t>(error);
        return stream.str();
    }

    std::string ExceptionMessage(const std::exception& ex)
    {
        return ex.what() == nullptr ? std::string{} : std::string(ex.what());
    }

    void IncrementUnchecked(std::int32_t& value) noexcept
    {
        value = std::bit_cast<std::int32_t>(
            std::bit_cast<std::uint32_t>(value) + 1u
        );
    }

    bool EventBool(JNIEnv* env, jobject event, const char* methodName)
    {
        if (event == nullptr)
        {
            return false;
        }
        LocalRef<jclass> type = ObjectClass(env, event);
        const jmethodID method = GetMethodId(env, type.Get(), methodName, "()Z");
        const jboolean value = env->CallBooleanMethod(event, method);
        CheckJavaException(env);
        return value == JNI_TRUE;
    }

    std::int32_t EventInt(JNIEnv* env, jobject event, const char* methodName)
    {
        if (event == nullptr)
        {
            return 0;
        }
        LocalRef<jclass> type = ObjectClass(env, event);
        const jmethodID method = GetMethodId(env, type.Get(), methodName, "()I");
        const jint value = env->CallIntMethod(event, method);
        CheckJavaException(env);
        return static_cast<std::int32_t>(value);
    }

    std::int32_t EventUnicode(
        JNIEnv* env,
        jobject event,
        std::int32_t metaState
    )
    {
        if (event == nullptr)
        {
            return 0;
        }
        LocalRef<jclass> type = ObjectClass(env, event);
        const jmethodID method = GetMethodId(
            env,
            type.Get(),
            "getUnicodeChar",
            "(I)I"
        );
        const jint value = env->CallIntMethod(
            event,
            method,
            static_cast<jint>(metaState)
        );
        CheckJavaException(env);
        return static_cast<std::int32_t>(value);
    }

    LocalRef<jobject> HolderSurface(JNIEnv* env, jobject holder)
    {
        if (holder == nullptr)
        {
            return {};
        }
        LocalRef<jclass> type = ObjectClass(env, holder);
        const jmethodID method = GetMethodId(
            env,
            type.Get(),
            "getSurface",
            "()Landroid/view/Surface;"
        );
        LocalRef<jobject> surface(
            env,
            env->CallObjectMethod(holder, method)
        );
        CheckJavaException(env);
        return surface;
    }

    bool SurfaceValid(JNIEnv* env, jobject surface)
    {
        if (surface == nullptr)
        {
            return false;
        }
        LocalRef<jclass> type = ObjectClass(env, surface);
        const jmethodID method = GetMethodId(
            env,
            type.Get(),
            "isValid",
            "()Z"
        );
        const jboolean result = env->CallBooleanMethod(surface, method);
        CheckJavaException(env);
        return result == JNI_TRUE;
    }

    void SurfaceSetFrameRate(
        JNIEnv* env,
        jobject surface,
        float rate
    )
    {
        LocalRef<jclass> type = ObjectClass(env, surface);
        const jmethodID method = GetMethodId(
            env,
            type.Get(),
            "setFrameRate",
            "(FI)V"
        );
        env->CallVoidMethod(
            surface,
            method,
            static_cast<jfloat>(rate),
            static_cast<jint>(0)
        );
        CheckJavaException(env);
    }

    bool AndroidAtLeast30() noexcept
    {
        return android_get_device_api_level() >= 30;
    }
}

namespace MphRead::Droid
{
    class GameView::ViewTarget final
    {
    public:
        ViewTarget(JNIEnv* env, jobject view)
        {
            if (env == nullptr)
            {
                throw std::invalid_argument(
                    "Android JNI environment must not be null"
                );
            }
            if (view == nullptr)
            {
                throw std::invalid_argument(
                    "Android GameView peer must not be null"
                );
            }
            if (env->GetJavaVM(&_vm) != JNI_OK || _vm == nullptr)
            {
                throw std::runtime_error("Android Java VM is not available");
            }
            _view = NewGlobalRefChecked(env, view);
        }

        ~ViewTarget()
        {
            DeleteGlobalRefNoThrow(_vm, _view);
        }

        ViewTarget(const ViewTarget&) = delete;
        ViewTarget& operator=(const ViewTarget&) = delete;

        [[nodiscard]] JavaVM* Vm() const noexcept
        {
            return _vm;
        }

        [[nodiscard]] jobject Object() const noexcept
        {
            return _view;
        }

        void AddHolderCallback(JNIEnv* env)
        {
            LocalRef<jclass> viewClass = ObjectClass(env, _view);
            const jmethodID getHolder = GetMethodId(
                env,
                viewClass.Get(),
                "getHolder",
                "()Landroid/view/SurfaceHolder;"
            );
            LocalRef<jobject> holder(
                env,
                env->CallObjectMethod(_view, getHolder)
            );
            CheckJavaException(env);
            if (!holder)
            {
                return;
            }

            LocalRef<jclass> holderClass = ObjectClass(env, holder.Get());
            const jmethodID addCallback = GetMethodId(
                env,
                holderClass.Get(),
                "addCallback",
                "(Landroid/view/SurfaceHolder$Callback;)V"
            );
            env->CallVoidMethod(holder.Get(), addCallback, _view);
            CheckJavaException(env);
        }

        void MakeFocusable(JNIEnv* env)
        {
            LocalRef<jclass> viewClass = FindClass(env, "android/view/SurfaceView");
            const jmethodID setFocusable = GetMethodId(
                env,
                viewClass.Get(),
                "setFocusable",
                "(Z)V"
            );
            const jmethodID setFocusableInTouchMode = GetMethodId(
                env,
                viewClass.Get(),
                "setFocusableInTouchMode",
                "(Z)V"
            );
            const jmethodID requestFocus = GetMethodId(
                env,
                viewClass.Get(),
                "requestFocus",
                "()Z"
            );

            env->CallVoidMethod(
                _view,
                setFocusable,
                JNI_TRUE
            );
            CheckJavaException(env);
            env->CallVoidMethod(
                _view,
                setFocusableInTouchMode,
                JNI_TRUE
            );
            CheckJavaException(env);
            (void)env->CallBooleanMethod(
                _view,
                requestFocus
            );
            CheckJavaException(env);
        }

        [[nodiscard]] bool BaseOnKeyDown(
            JNIEnv* env,
            std::int32_t keyCode,
            jobject event
        )
        {
            LocalRef<jclass> viewClass = FindClass(env, "android/view/SurfaceView");
            const jmethodID method = GetMethodId(
                env,
                viewClass.Get(),
                "onKeyDown",
                "(ILandroid/view/KeyEvent;)Z"
            );
            const jboolean result = env->CallNonvirtualBooleanMethod(
                _view,
                viewClass.Get(),
                method,
                static_cast<jint>(keyCode),
                event
            );
            CheckJavaException(env);
            return result == JNI_TRUE;
        }

        [[nodiscard]] bool BaseOnKeyUp(
            JNIEnv* env,
            std::int32_t keyCode,
            jobject event
        )
        {
            LocalRef<jclass> viewClass = FindClass(env, "android/view/SurfaceView");
            const jmethodID method = GetMethodId(
                env,
                viewClass.Get(),
                "onKeyUp",
                "(ILandroid/view/KeyEvent;)Z"
            );
            const jboolean result = env->CallNonvirtualBooleanMethod(
                _view,
                viewClass.Get(),
                method,
                static_cast<jint>(keyCode),
                event
            );
            CheckJavaException(env);
            return result == JNI_TRUE;
        }

        [[nodiscard]] bool BaseOnGenericMotionEvent(
            JNIEnv* env,
            jobject event
        )
        {
            LocalRef<jclass> viewClass = FindClass(env, "android/view/SurfaceView");
            const jmethodID method = GetMethodId(
                env,
                viewClass.Get(),
                "onGenericMotionEvent",
                "(Landroid/view/MotionEvent;)Z"
            );
            const jboolean result = env->CallNonvirtualBooleanMethod(
                _view,
                viewClass.Get(),
                method,
                event
            );
            CheckJavaException(env);
            return result == JNI_TRUE;
        }

        [[nodiscard]] jobject CreateInputConnection(
            JNIEnv* env,
            jobject outAttrs
        )
        {
            if (outAttrs != nullptr)
            {
                LocalRef<jclass> attrsClass = ObjectClass(env, outAttrs);
                const jfieldID inputType = GetFieldId(
                    env,
                    attrsClass.Get(),
                    "inputType",
                    "I"
                );
                const jfieldID imeOptions = GetFieldId(
                    env,
                    attrsClass.Get(),
                    "imeOptions",
                    "I"
                );

                env->SetIntField(
                    outAttrs,
                    inputType,
                    static_cast<jint>(InputTypeNull)
                );
                CheckJavaException(env);
                env->SetIntField(
                    outAttrs,
                    imeOptions,
                    static_cast<jint>(
                        ImeActionDone
                        | ImeFlagNoFullscreen
                        | ImeFlagNoExtractUi
                    )
                );
                CheckJavaException(env);
            }

            LocalRef<jclass> connectionClass = FindClass(
                env,
                "android/view/inputmethod/BaseInputConnection"
            );
            const jmethodID constructor = GetMethodId(
                env,
                connectionClass.Get(),
                "<init>",
                "(Landroid/view/View;Z)V"
            );
            LocalRef<jobject> result(
                env,
                env->NewObject(
                    connectionClass.Get(),
                    constructor,
                    _view,
                    JNI_FALSE
                )
            );
            CheckJavaException(env);
            return result.Release();
        }

    private:
        JavaVM* _vm = nullptr;
        jobject _view = nullptr;
    };

    class GameView::RenderLoop final
        : public std::enable_shared_from_this<GameView::RenderLoop>
    {
    public:
        static std::shared_ptr<RenderLoop> Create(
            JavaVM* vm,
            TouchControls& controls,
            std::shared_ptr<AndroidInput> input,
            Build build,
            Action onEnd,
            Action onLoaded,
            ErrorAction onError,
            Action onPauseMenu,
            BoolAction onSoftKeyboard
        )
        {
            auto loop = std::shared_ptr<RenderLoop>(
                new RenderLoop(
                    vm,
                    controls,
                    std::move(input),
                    std::move(build),
                    std::move(onEnd),
                    std::move(onLoaded),
                    std::move(onError),
                    std::move(onPauseMenu),
                    std::move(onSoftKeyboard)
                )
            );
            loop->Start();
            return loop;
        }

        RenderLoop(const RenderLoop&) = delete;
        RenderLoop& operator=(const RenderLoop&) = delete;

        [[nodiscard]] MphRead::Scene* SceneValue() const noexcept
        {
            return _publishedScene.load(std::memory_order_acquire);
        }

        ~RenderLoop()
        {
            if (_nativeWindow != nullptr)
            {
                ANativeWindow_release(_nativeWindow);
                _nativeWindow = nullptr;
            }
        }

        void RequestStop()
        {
            std::lock_guard<std::mutex> guard(_lock);
            _stopping = true;
            _changed.notify_all();
        }

        void SetPaused(bool paused)
        {
            std::lock_guard<std::mutex> guard(_lock);
            _paused = paused;
            _changed.notify_all();
        }

        void SurfaceReady(
            JNIEnv* env,
            jobject holder,
            std::int32_t width,
            std::int32_t height
        )
        {
            if (width <= 0 || height <= 0)
            {
                return;
            }

            std::shared_ptr<JavaGlobalRef> holderRef;
            if (holder != nullptr)
            {
                holderRef = std::make_shared<JavaGlobalRef>(env, holder);
            }

            std::lock_guard<std::mutex> guard(_lock);
            _holder = std::move(holderRef);
            _wanted = OpenTK::Mathematics::Vector2i(width, height);
            _changed.notify_all();
        }

        void SurfaceGone()
        {
            std::unique_lock<std::mutex> guard(_lock);
            _holder.reset();
            _changed.notify_all();

            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(SurfaceReleaseMs);
            while (_holdingSurface)
            {
                if (_released.wait_until(guard, deadline)
                    == std::cv_status::timeout)
                {
                    if (_holdingSurface)
                    {
                        std::cout
                            << "[android] the surface went away while the GL thread "
                            << "was busy; carrying on without waiting for it"
                            << std::endl;
                    }
                    break;
                }
            }
        }

    private:
        static constexpr double MinFrameSeconds =
            1.0 / MphRead::Mods::Render::FrameTiming::MaxCap;
        static constexpr float AimScale = 1.0F;
        static constexpr EGLint OpenGlEs3Bit = 0x40;
        static constexpr std::int32_t SurfaceReleaseMs = 2000;

        RenderLoop(
            JavaVM* vm,
            TouchControls& controls,
            std::shared_ptr<AndroidInput> input,
            Build build,
            Action onEnd,
            Action onLoaded,
            ErrorAction onError,
            Action onPauseMenu,
            BoolAction onSoftKeyboard
        )
            : _vm(vm),
              _controls(&controls),
              _input(std::move(input)),
              _build(std::move(build)),
              _onEnd(std::move(onEnd)),
              _onLoaded(std::move(onLoaded)),
              _onError(std::move(onError)),
              _onPauseMenu(std::move(onPauseMenu)),
              _onSoftKeyboard(std::move(onSoftKeyboard))
        {
        }

        void Start()
        {
            std::shared_ptr<RenderLoop> self = shared_from_this();
            std::thread(
                [self = std::move(self)]()
                {
                    (void)pthread_setname_np(
                        pthread_self(),
                        "FruityPrime GL"
                    );
                    self->Run();
                }
            ).detach();
        }

        void Run()
        {
            // Hold exclusive ownership of the process-global GlEs shim for the
            // complete non-shared EGL context lifetime, including teardown.
            AndroidGlContextLease glContextLease;
            try
            {
                try
                {
                    Loop();
                }
                catch (const std::exception& ex)
                {
                    std::cout
                        << "[android] the render thread stopped: "
                        << GetMainActivityOwner().ExceptionToString(
                            std::current_exception()
                        )
                        << std::endl;
                    if (!_ended)
                    {
                        _ended = true;
                        _onError(ExceptionMessage(ex));
                    }
                }
            }
            catch (...)
            {
                ReleaseGlEsContext();
                ReleaseSurface();
                DestroyContext();
                throw;
            }

            ReleaseGlEsContext();
            ReleaseSurface();
            DestroyContext();
        }

        void ReleaseGlEsContext() noexcept
        {
            if (_vulkan)
            {
                // Nothing is context-local: the device, the scene's GPU
                // resources and the game all outlive the surface.
                return;
            }
            // Dynamic shim objects are context-local. Delete them only while
            // this exact context is current; if ordinary surface loss already
            // unbound it, clear the process-global bookkeeping and let context
            // destruction reclaim the objects.
            if (_contextAssigned
                && _context != EGL_NO_CONTEXT
                && eglGetCurrentContext() == _context)
            {
                MphRead::Mods::Render::GlEs::ReleaseContext();
            }
            else
            {
                MphRead::Mods::Render::GlEs::Reset();
            }
            // GL's Android wrapper keeps its own per-context bindings and
            // element data; they die with this context as well.
            OpenTK::Graphics::OpenGL::GL::ResetAndroidState();
            // The RHI device's textures lived in that context too.
            MphRead::NativeRuntime::Rhi::OpenGL::ResetContextDevice();
        }

        void Loop()
        {
            while (true)
            {
                std::shared_ptr<JavaGlobalRef> holder;
                OpenTK::Mathematics::Vector2i wanted;

                {
                    std::unique_lock<std::mutex> guard(_lock);
                    while (!_stopping && (!_holder || _paused))
                    {
                        if (!_holder && _holdingSurface)
                        {
                            guard.unlock();
                            try
                            {
                                ReleaseSurface();
                            }
                            catch (...)
                            {
                                guard.lock();
                                throw;
                            }
                            guard.lock();
                            _released.notify_all();
                            continue;
                        }
                        _changed.wait(guard);
                    }

                    if (_stopping)
                    {
                        break;
                    }

                    holder = _holder;
                    wanted = _wanted;
                }

                if (!BindSurface(holder, wanted))
                {
                    continue;
                }

                if (_scene == nullptr)
                {
                    if (_ended)
                    {
                        break;
                    }
                    BuildScene();
                    continue;
                }

                if (!DrawFrame())
                {
                    break;
                }
            }

            MphRead::Scene* scene = _scene.get();
            if (scene != nullptr)
            {
                End(*scene);
            }
        }

        bool BoundTo(
            const std::shared_ptr<JavaGlobalRef>& holder
        )
        {
            if (!_boundTo || !holder)
            {
                return false;
            }
            ScopedJniEnv scoped(_vm);
            return _boundTo->SameObject(scoped.Get(), *holder);
        }

        bool BindSurface(
            const std::shared_ptr<JavaGlobalRef>& holder,
            OpenTK::Mathematics::Vector2i wanted
        )
        {
            if (!_displayAssigned && !CreateContext())
            {
                return false;
            }

            if (!BoundTo(holder) || !_surfaceAssigned)
            {
                ReleaseSurface();
                if (!CreateSurface(holder))
                {
                    return false;
                }
            }

            if (wanted != _size)
            {
                _size = wanted;
                if (_vulkan)
                {
                    if (_swapchain)
                        _swapchain->Resize(static_cast<std::uint32_t>(std::max(_size.X, 1)),
                            static_cast<std::uint32_t>(std::max(_size.Y, 1)));
                }
                else
                {
                    glViewport(0, 0, _size.X, _size.Y);
                }
                if (_scene != nullptr)
                {
                    _scene->Size(_size);
                    _scene->OnResize();
                }
            }
            return true;
        }

        bool CreateContext()
        {
            // Which backend draws: resolved once, before anything is made for
            // it. An explicit Vulkan that cannot start throws, and the error
            // reaches the player through _onError rather than becoming GLES.
            _vulkan = MphRead::NativeRuntime::Rhi::SelectedSceneBackend()
                == MphRead::NativeRuntime::Rhi::GraphicsBackend::Vulkan;
            if (_vulkan)
            {
                // The device is made with the first surface.
                _displayAssigned = true;
                std::cout << "[android] renderer: vulkan" << std::endl;
                return true;
            }
            _display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            _displayAssigned = true;
            if (_display == EGL_NO_DISPLAY)
            {
                return Fail("no EGL display");
            }

            EGLint version[2] = { 0, 0 };
            if (eglInitialize(
                    _display,
                    &version[0],
                    &version[1]
                ) != EGL_TRUE)
            {
                return Fail(
                    "eglInitialize failed ("
                    + EglHex(eglGetError())
                    + ")"
                );
            }

            const EGLint attributes[] =
            {
                EGL_RENDERABLE_TYPE, OpenGlEs3Bit,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 0,
                EGL_DEPTH_SIZE, 24,
                EGL_STENCIL_SIZE, 8,
                EGL_NONE
            };

            EGLConfig configs[1] = { nullptr };
            EGLint found = 0;
            if (eglChooseConfig(
                    _display,
                    attributes,
                    &configs[0],
                    1,
                    &found
                ) != EGL_TRUE
                || found < 1
                || configs[0] == nullptr)
            {
                return Fail(
                    "no EGL config with a window, depth and stencil"
                );
            }
            _config = configs[0];

            const EGLint contextAttributes[] =
            {
                EGL_CONTEXT_CLIENT_VERSION, 3,
                EGL_NONE
            };
            _context = eglCreateContext(
                _display,
                _config,
                EGL_NO_CONTEXT,
                contextAttributes
            );
            _contextAssigned = true;
            if (_context == EGL_NO_CONTEXT)
            {
                return Fail(
                    "eglCreateContext failed ("
                    + EglHex(eglGetError())
                    + ")"
                );
            }

            return true;
        }

        bool CreateSurface(
            const std::shared_ptr<JavaGlobalRef>& holder
        )
        {
            if (_vulkan)
            {
                return CreateVulkanSurface(holder);
            }
            if (!_displayAssigned
                || _display == EGL_NO_DISPLAY
                || _config == nullptr
                || !_contextAssigned
                || _context == EGL_NO_CONTEXT
                || !holder)
            {
                return false;
            }

            ScopedJniEnv scoped(_vm);
            JNIEnv* const env = scoped.Get();
            LocalRef<jobject> window = HolderSurface(
                env,
                holder->Object()
            );
            if (!window || !SurfaceValid(env, window.Get()))
            {
                return false;
            }

            ANativeWindow* nativeWindow =
                ANativeWindow_fromSurface(env, window.Get());
            CheckJavaException(env);
            if (nativeWindow == nullptr)
            {
                return false;
            }

            const EGLint attributes[] = { EGL_NONE };
            _eglSurface = eglCreateWindowSurface(
                _display,
                _config,
                nativeWindow,
                attributes
            );
            if (_eglSurface == EGL_NO_SURFACE)
            {
                _eglSurface = EGL_NO_SURFACE;
                _surfaceAssigned = false;
                ANativeWindow_release(nativeWindow);
                std::cout
                    << "[android] eglCreateWindowSurface failed ("
                    << EglHex(eglGetError())
                    << ")"
                    << std::endl;
                return false;
            }
            _surfaceAssigned = true;
            _nativeWindow = nativeWindow;

            if (eglMakeCurrent(
                    _display,
                    _eglSurface,
                    _eglSurface,
                    _context
                ) != EGL_TRUE)
            {
                return Fail(
                    "eglMakeCurrent failed ("
                    + EglHex(eglGetError())
                    + ")"
                );
            }

            {
                std::lock_guard<std::mutex> guard(_lock);
                _boundTo = holder;
                _holdingSurface = true;
            }

            if (_scene == nullptr)
            {
                MphRead::Mods::Render::EsBindings::Load();
                MphRead::Mods::Render::GlEs::Reset();
                OpenTK::Graphics::OpenGL::GL::ResetAndroidState();
                MphRead::NativeRuntime::Rhi::OpenGL::ResetContextDevice();
                glClearColor(
                    10.0F / 255.0F,
                    12.0F / 255.0F,
                    16.0F / 255.0F,
                    1.0F
                );
                glClear(GL_COLOR_BUFFER_BIT);
                (void)eglSwapBuffers(_display, _eglSurface);
            }

            _size = OpenTK::Mathematics::Vector2i{};
            return true;
        }

        // The surface came (back): a VkSurface on it, and a swapchain. The
        // device is made with the first one and kept for every later one, so
        // a pause, a rotation or a trip to the home screen loses nothing but
        // the swapchain and its images.
        bool CreateVulkanSurface(const std::shared_ptr<JavaGlobalRef>& holder)
        {
            if (!holder)
            {
                return false;
            }
            ScopedJniEnv scoped(_vm);
            JNIEnv* const env = scoped.Get();
            LocalRef<jobject> window = HolderSurface(env, holder->Object());
            if (!window || !SurfaceValid(env, window.Get()))
            {
                return false;
            }
            ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, window.Get());
            CheckJavaException(env);
            if (nativeWindow == nullptr)
            {
                return false;
            }
            try
            {
                MphRead::NativeRuntime::Rhi::AttachSceneSurface(nativeWindow);
                MphRead::NativeRuntime::Rhi::SwapchainDesc desc{};
                desc.width = static_cast<std::uint32_t>(std::max(ANativeWindow_getWidth(nativeWindow), 1));
                desc.height = static_cast<std::uint32_t>(std::max(ANativeWindow_getHeight(nativeWindow), 1));
                desc.format = MphRead::NativeRuntime::Rhi::TextureFormat::RGBA8Unorm;
                _swapchain = MphRead::NativeRuntime::Rhi::CreateSceneSurfaceSwapchain(desc);
            }
            catch (...)
            {
                _swapchain.reset();
                MphRead::NativeRuntime::Rhi::DetachSceneSurface();
                ANativeWindow_release(nativeWindow);
                throw;
            }
            _surfaceAssigned = true;
            _nativeWindow = nativeWindow;
            {
                std::lock_guard<std::mutex> guard(_lock);
                _boundTo = holder;
                _holdingSurface = true;
            }
            std::cout << "[android] vulkan surface "
                << MphRead::NativeRuntime::Rhi::DescribeSceneBackend(_swapchain.get()) << std::endl;
            _size = OpenTK::Mathematics::Vector2i{};
            return true;
        }

        void ReleaseSurface() noexcept
        {
            if (_vulkan)
            {
                ANativeWindow* nativeWindow = nullptr;
                {
                    std::lock_guard<std::mutex> guard(_lock);
                    nativeWindow = _nativeWindow;
                    _surfaceAssigned = false;
                    _nativeWindow = nullptr;
                    _boundTo.reset();
                    _holdingSurface = false;
                }
                // Swapchain-dependent objects first, then the surface; the
                // device and the game stay.
                _swapchain.reset();
                MphRead::NativeRuntime::Rhi::DetachSceneSurface();
                if (nativeWindow != nullptr)
                {
                    ANativeWindow_release(nativeWindow);
                }
                {
                    std::lock_guard<std::mutex> guard(_lock);
                    _released.notify_all();
                }
                return;
            }
            EGLSurface surface = EGL_NO_SURFACE;
            ANativeWindow* nativeWindow = nullptr;

            {
                std::lock_guard<std::mutex> guard(_lock);
                surface = _eglSurface;
                nativeWindow = _nativeWindow;
                _eglSurface = EGL_NO_SURFACE;
                _surfaceAssigned = false;
                _nativeWindow = nullptr;
                _boundTo.reset();
                _holdingSurface = false;
                _released.notify_all();
            }

            if (!_displayAssigned
                || _display == EGL_NO_DISPLAY
                || surface == EGL_NO_SURFACE)
            {
                if (nativeWindow != nullptr)
                {
                    ANativeWindow_release(nativeWindow);
                }
                return;
            }

            try
            {
                (void)eglMakeCurrent(
                    _display,
                    EGL_NO_SURFACE,
                    EGL_NO_SURFACE,
                    EGL_NO_CONTEXT
                );
                (void)eglDestroySurface(_display, surface);
                if (nativeWindow != nullptr)
                {
                    ANativeWindow_release(nativeWindow);
                }
            }
            catch (const std::exception& ex)
            {
                if (nativeWindow != nullptr)
                {
                    ANativeWindow_release(nativeWindow);
                }
                std::cout
                    << "[android] releasing the surface failed: "
                    << ex.what()
                    << std::endl;
            }
        }

        void DestroyContext() noexcept
        {
            if (_vulkan)
            {
                // The Vulkan device lives for the process, as the scene
                // backend's does on the desktop.
                _displayAssigned = false;
                return;
            }
            if (!_displayAssigned)
            {
                return;
            }

            try
            {
                (void)eglMakeCurrent(
                    _display,
                    EGL_NO_SURFACE,
                    EGL_NO_SURFACE,
                    EGL_NO_CONTEXT
                );
                if (_contextAssigned)
                {
                    (void)eglDestroyContext(_display, _context);
                }
                (void)eglTerminate(_display);
            }
            catch (const std::exception& ex)
            {
                std::cout
                    << "[android] tearing the context down failed: "
                    << ex.what()
                    << std::endl;
            }

            _context = EGL_NO_CONTEXT;
            _contextAssigned = false;
            _config = nullptr;
            _display = EGL_NO_DISPLAY;
            _displayAssigned = false;
        }

        bool Fail(std::string message)
        {
            std::cout
                << "[android] "
                << message
                << std::endl;

            if (!_ended)
            {
                _ended = true;
                _onError(message);
            }

            {
                std::lock_guard<std::mutex> guard(_lock);
                _stopping = true;
            }
            return false;
        }

        void BuildScene()
        {
            if (_size.X <= 0 || _size.Y <= 0)
            {
                return;
            }

            try
            {
                _scene = _build(*_input, _size);
                if (_scene == nullptr)
                {
                    throw std::runtime_error(
                        "Object reference not set to an instance of an object."
                    );
                }
                _publishedScene.store(
                    _scene.get(),
                    std::memory_order_release
                );
                _scene->OnLoad();
                MphRead::Mods::Network::NetSession::MarkMatchLoaded();
            }
            catch (const std::exception& ex)
            {
                std::cout
                    << "[android] the match could not start: "
                    << GetMainActivityOwner().ExceptionToString(
                        std::current_exception()
                    )
                    << std::endl;
                MphRead::Mods::Network::NetSession::ReportMatchLoadFailed(
                    ExceptionMessage(ex)
                );
                _publishedScene.store(nullptr, std::memory_order_release);
                _scene.reset();
                _ended = true;
                _onError(ExceptionMessage(ex));
                std::lock_guard<std::mutex> guard(_lock);
                _stopping = true;
                return;
            }

            _clockStarted = true;
            _clockStart = std::chrono::steady_clock::now();
            _nextFrame = ClockSeconds();
            _lastFrameStart = _nextFrame;
            MphRead::Mods::Render::FrameTiming::Reset();
            _onLoaded();
        }

        bool DrawFrame()
        {
            MphRead::Scene& scene = *_scene;
            const double elapsed = WaitForTick();

            MphRead::GameState::ApplyPause();
            const std::int32_t steps =
                MphRead::Mods::Render::FrameTiming::Advance(elapsed);
            for (std::int32_t i = 0; i < steps; ++i)
            {
                ApplyInput();
                scene.OnSimulationFrame();
                if (MphRead::Mods::Network::NetSession::Refused()
                    || MphRead::Mods::Network::NetSession::SessionTimedOut())
                {
                    End(scene);
                    return false;
                }
                if (MphRead::Mods::Network::NetSession::PersistentLobby()
                    && MphRead::Mods::Network::NetSession::IsInLobby())
                {
                    End(scene, true);
                    return false;
                }
            }

            RequestFrameRate();
            scene.OnDrawFrame();
            if (!scene.OnRenderFrame())
            {
                End(scene);
                return false;
            }
            scene.AfterRenderFrame();
            DrawUi(scene);

            if (_vulkan)
            {
                if (_surfaceAssigned && _swapchain)
                {
                    MphRead::NativeRuntime::Rhi::PresentSceneWindow(*_swapchain);
                }
                return true;
            }

            if (_displayAssigned
                && _display != EGL_NO_DISPLAY
                && _surfaceAssigned
                && _eglSurface != EGL_NO_SURFACE
                && eglSwapBuffers(_display, _eglSurface) != EGL_TRUE)
            {
                std::cout
                    << "[android] the surface stopped accepting frames; "
                    << "waiting for another ("
                    << EglHex(eglGetError())
                    << ")"
                    << std::endl;
                ReleaseSurface();
            }

            return true;
        }

        void SayUi()
        {
            if (!MphRead::Mods::EndScreen::PanelUp())
            {
                return;
            }

            const std::int64_t now = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count();
            if (_uiSaid == 0)
            {
                _uiSaid = now;
                return;
            }
            if (now - _uiSaid < 1000)
            {
                return;
            }

            _uiSaid = now;
            std::ostringstream message;
            message
                << "end panel drawn " << _uiDrawn
                << ", skipped " << _uiSkipped
                << ", hole " << _uiHole;
            MphRead::Mods::DebugLog::Line("ui", message.str());
            _uiDrawn = 0;
            _uiSkipped = 0;
            _uiHole = 0;
        }

        void DrawUi(MphRead::Scene& scene)
        {
            std::shared_ptr<AndroidUiSurface> surface =
                AndroidUiSurface::Current();
            SayUi();
            if (!surface || !surface->Visible())
            {
                IncrementUnchecked(_uiSkipped);
                AndroidUiOverlay::Visible(false);
                MphRead::Scene::LauncherPreview = false;
                return;
            }

            IncrementUnchecked(_uiDrawn);
            if (MphRead::Mods::Render::HunterShot::HoleWanted)
            {
                IncrementUnchecked(_uiHole);
            }

            std::int32_t width = 0;
            std::int32_t height = 0;
            if (surface->TakeFrame(_uiPixels, _uiVersion, width, height))
            {
                AndroidUiOverlay::Upload(_uiPixels, width, height);
            }
            AndroidUiOverlay::Visible(true);
            AndroidUiOverlay::Draw(_size.X, _size.Y);

            if (MphRead::Mods::Render::HunterShot::HoleWanted)
            {
                MphRead::Scene::LauncherPreview = true;
                MphRead::Scene::LauncherHunter =
                    MphRead::Mods::Render::HunterShot::HoleHunter;
                MphRead::Scene::LauncherSuit =
                    MphRead::Mods::Render::HunterShot::HoleSuit;
                MphRead::Scene::PreviewWanted(true);
                MphRead::Scene::PreviewLeft(
                    MphRead::Mods::Render::HunterShot::HoleLeft
                );
                MphRead::Scene::PreviewTop(
                    MphRead::Mods::Render::HunterShot::HoleTop
                );
                MphRead::Scene::PreviewRight(
                    MphRead::Mods::Render::HunterShot::HoleRight
                );
                MphRead::Scene::PreviewBottom(
                    MphRead::Mods::Render::HunterShot::HoleBottom
                );
                (void)scene.ModDrawPreviewAlone(_size);
            }
            else
            {
                MphRead::Scene::LauncherPreview = false;
                MphRead::Scene::PreviewWanted(false);
            }
        }

        void RequestFrameRate()
        {
            const std::int32_t cap =
                MphRead::Mods::Render::FrameTiming::FrameRateCap();
            if (cap == _requestedFrameRate || !AndroidAtLeast30())
            {
                return;
            }

            _requestedFrameRate = cap;

            std::shared_ptr<JavaGlobalRef> bound;
            {
                std::lock_guard<std::mutex> guard(_lock);
                bound = _boundTo;
            }

            if (!bound)
            {
                _requestedFrameRate = -1;
                return;
            }

            try
            {
                ScopedJniEnv scoped(_vm);
                JNIEnv* const env = scoped.Get();
                LocalRef<jobject> window = HolderSurface(
                    env,
                    bound->Object()
                );
                if (!window || !SurfaceValid(env, window.Get()))
                {
                    _requestedFrameRate = -1;
                    return;
                }

                SurfaceSetFrameRate(
                    env,
                    window.Get(),
                    cap == MphRead::Mods::Render::FrameTiming::DisplayRate
                        ? 0.0F
                        : static_cast<float>(cap)
                );
            }
            catch (const std::exception& ex)
            {
                std::cout
                    << "[android] the display would not be asked for "
                    << cap
                    << " fps: "
                    << ex.what()
                    << std::endl;
            }
        }

        void End(MphRead::Scene& scene, bool keepSession = false)
        {
            _ended = true;
            scene.DoCleanup();
            _publishedScene.store(nullptr, std::memory_order_release);
            _scene.reset();

            try
            {
                AndroidMatch::Finish();
            }
            catch (const std::exception& ex)
            {
                std::cout
                    << "[android] the save could not be written: "
                    << GetMainActivityOwner().ExceptionToString(
                        std::current_exception()
                    )
                    << std::endl;
            }

            if (keepSession)
            {
                MainActivity* activity = MainActivity::Instance();
                if (activity != nullptr)
                {
                    GetMainActivityOwner().RunOnUiThread(
                        []()
                        {
                            if (MainActivity* current = MainActivity::Instance())
                            {
                                current->EndMatchToLobby();
                            }
                        }
                    );
                }
            }
            else
            {
                _onEnd();
            }
        }

        [[nodiscard]] double ClockSeconds() const noexcept
        {
            if (!_clockStarted)
            {
                return 0.0;
            }
            return std::chrono::duration<double>(
                std::chrono::steady_clock::now() - _clockStart
            ).count();
        }

        double WaitForTick()
        {
            double now = ClockSeconds();
            const std::int32_t cap =
                MphRead::Mods::Render::FrameTiming::FrameRateCap();
            const double interval =
                cap == MphRead::Mods::Render::FrameTiming::DisplayRate
                    ? MinFrameSeconds
                    : std::max(
                        MinFrameSeconds,
                        1.0 / static_cast<double>(cap)
                    );

            const double wait = _nextFrame - now;
            if (wait > 0.001)
            {
                const auto milliseconds = static_cast<std::int32_t>(
                    wait * 1000.0
                );
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(milliseconds)
                );
                now = ClockSeconds();
            }

            _nextFrame += interval;
            if (_nextFrame < now)
            {
                _nextFrame = now + interval;
            }

            const double elapsed = now - _lastFrameStart;
            _lastFrameStart = now;
            return elapsed;
        }

        void ApplyInput()
        {
            HandleChat();

            _controls->ForceVisible(MphRead::GameState::DialogPause());

            std::shared_ptr<MphRead::Entities::PlayerEntity> main =
                MphRead::Entities::PlayerEntity::Main();
            if (!main
                || (main->LoadFlags()
                    & MphRead::Entities::LoadFlags::Active)
                    != MphRead::Entities::LoadFlags::Active)
            {
                return;
            }

            _input->BeginFrame();
            try
            {
                CollectInput(*main);
            }
            catch (...)
            {
                _input->CommitFrame();
                throw;
            }
            _input->CommitFrame();
        }

        void Spectate()
        {
            const bool freeCamera = MphRead::Mods::SpectatorMode::FreeCamera();
            _controls->SetSpectator(true, freeCamera);

            const bool cycle =
                _controls->IsHeld(TouchAction::Shoot);
            if (cycle && !_spectateCycleHeld)
            {
                MphRead::Mods::SpectatorMode::CycleNext();
            }
            _spectateCycleHeld = cycle;

            const bool view =
                _controls->IsHeld(TouchAction::ScanVisor);
            if (view && !_spectateViewHeld)
            {
                MphRead::Mods::SpectatorMode::ToggleView();
            }
            _spectateViewHeld = view;

            const bool menuHeld =
                _controls->IsHeld(TouchAction::Pause);
            if (menuHeld && !_menuWasHeld)
            {
                _onPauseMenu();
            }
            _menuWasHeld = menuHeld;

            _input->Apply(
                MphRead::Mods::InputSettings::Current().Pause(),
                _controls->IsHeld(TouchAction::Scoreboard)
            );

            if (freeCamera)
            {
                const TouchControls::Dir dir = _controls->Direction();
                _input->ApplyKey(
                    Keys::W,
                    (dir & TouchControls::Dir::Up)
                        != TouchControls::Dir::None
                );
                _input->ApplyKey(
                    Keys::S,
                    (dir & TouchControls::Dir::Down)
                        != TouchControls::Dir::None
                );
                _input->ApplyKey(
                    Keys::A,
                    (dir & TouchControls::Dir::Left)
                        != TouchControls::Dir::None
                );
                _input->ApplyKey(
                    Keys::D,
                    (dir & TouchControls::Dir::Right)
                        != TouchControls::Dir::None
                );
                _input->ApplyKey(
                    Keys::Space,
                    _controls->IsHeld(TouchAction::Jump)
                );
                _input->ApplyKey(
                    Keys::V,
                    _controls->IsHeld(TouchAction::Morph)
                );

                const TouchControls::AimDelta look =
                    _controls->TakeAimDelta();
                if (look.X != 0.0F || look.Y != 0.0F)
                {
                    if (_scene != nullptr)
                    {
                        _scene->OnMouseMove(
                            look.X * AimScale,
                            look.Y * AimScale
                        );
                    }
                }
            }
            else
            {
                (void)_controls->TakeAimDelta();
            }

            (void)_controls->TakeSwipeBoost();
            (void)_controls->TakeDoubleTapJump();
            _controls->ScanVisorActive(false);
            _controls->SwipeBoostEnabled(false);
        }

        void HandleChat()
        {
            _controls->ChatEnabled(
                MphRead::Mods::Chat::ChatBox::Available()
                && MphRead::Mods::Network::NetSession::Active()
            );

            if (MphRead::Mods::Chat::ChatBox::Composing()
                && MphRead::Mods::Input::GamepadInput::TakePress(
                    MphRead::Mods::Input::GamepadButtons::B
                    | MphRead::Mods::Input::GamepadButtons::Start
                ))
            {
                MphRead::Mods::Chat::ChatBox::Cancel();
            }

            if (MphRead::Mods::Input::GamepadInput::TakeMenuPress())
            {
                _onPauseMenu();
            }

            const bool chat =
                _controls->IsHeld(TouchAction::Chat);
            if (chat && !_chatWasHeld)
            {
                MphRead::Mods::Chat::ChatBox::Open(false);
            }
            _chatWasHeld = chat;

            const bool wanted =
                MphRead::Mods::Chat::ChatBox::Composing();
            if (wanted != _keyboardShown)
            {
                _keyboardShown = wanted;
                _onSoftKeyboard(wanted);
            }
        }

        void CollectInput(MphRead::Entities::PlayerEntity& main)
        {
            const bool endScreen =
                MphRead::Mods::EndScreen::Available();
            _controls->SetEndScreen(endScreen);

            _controls->SetTapTargets(
                MphRead::Mods::Network::MapVote::TouchTargets()
            );
            const TouchControls::TapResult tap = _controls->TakeTap();
            if (tap.Got)
            {
                MphRead::Mods::EndScreen::NotePointer(tap.X, tap.Y);
                if (!MphRead::Mods::Network::MapVote::HandleClick())
                {
                    (void)MphRead::Mods::EndScreen::HandleClick();
                }
            }

            if (MphRead::Mods::SpectatorMode::IsSpectating())
            {
                Spectate();
                return;
            }

            _controls->SetSpectator(false, false);
            _spectateCycleHeld = false;
            _spectateViewHeld = false;

            MphRead::Entities::PlayerControls& controls =
                main.Controls();
            const TouchControls::Dir dir = _controls->Direction();
            const bool up =
                (dir & TouchControls::Dir::Up)
                    != TouchControls::Dir::None;
            const bool down =
                (dir & TouchControls::Dir::Down)
                    != TouchControls::Dir::None;
            const bool left =
                (dir & TouchControls::Dir::Left)
                    != TouchControls::Dir::None;
            const bool right =
                (dir & TouchControls::Dir::Right)
                    != TouchControls::Dir::None;

            _input->Apply(controls.MoveUp(), up);
            _input->Apply(controls.MoveDown(), down);
            _input->Apply(controls.MoveLeft(), left);
            _input->Apply(controls.MoveRight(), right);
            _input->Apply(controls.RollUp(), up);
            _input->Apply(controls.RollDown(), down);
            _input->Apply(controls.RolltLeft(), left);
            _input->Apply(controls.RollRight(), right);

            const bool doubleTap = _controls->TakeDoubleTapJump();
            const bool jump =
                _controls->IsHeld(TouchAction::Jump)
                || (doubleTap
                    && !MphRead::GameState::DialogPause()
                    && !_controls->IsHeld(TouchAction::WeaponMenu));

            const bool fire =
                _controls->IsHeld(TouchAction::Shoot);
            const bool altForm =
                main.IsAltForm()
                || _controls->IsHeld(TouchAction::Morph);

            _input->Apply(
                controls.Shoot(),
                fire && !main.IsAltForm()
            );
            _input->Apply(
                controls.AltAttack(),
                fire && altForm
            );
            _input->Apply(controls.Jump(), jump);
            _input->Apply(controls.Boost(), jump);

            _controls->SwipeBoostEnabled(main.IsAltForm());
            const TouchControls::SwipeBoostResult swipe =
                _controls->TakeSwipeBoost();
            if (swipe.Fired && main.IsAltForm())
            {
                main.SetSwipeBoostRequested(true);
                main.SetSwipeBoostX(swipe.X);
                main.SetSwipeBoostY(swipe.Y);
            }

            _input->Apply(
                controls.Morph(),
                _controls->IsHeld(TouchAction::Morph)
            );

            _controls->ScanVisorActive(main.ScanVisor());
            _input->Apply(
                controls.ScanVisor(),
                _controls->IsHeld(TouchAction::ScanVisor)
            );
            _input->Apply(
                controls.Scan(),
                _controls->IsHeld(TouchAction::Scan)
            );
            _input->Apply(
                controls.Zoom(),
                _controls->IsHeld(TouchAction::Zoom)
            );

            const bool missile =
                _controls->IsHeld(TouchAction::Missile);
            if (missile && !_missileWasHeld)
            {
                _input->Apply(
                    main.CurrentWeapon() == MphRead::BeamType::Missile
                        ? controls.PowerBeam()
                        : controls.Missile(),
                    true
                );
            }
            _missileWasHeld = missile;

            _input->Apply(
                controls.Pause(),
                _controls->IsHeld(TouchAction::Scoreboard)
            );

            const bool menu =
                _controls->IsHeld(TouchAction::Pause);
            if (menu && !_menuWasHeld)
            {
                _onPauseMenu();
            }
            _menuWasHeld = menu;

            if (MphRead::GameState::DialogPause())
            {
                _controls->PointerIsAbsolute(true);
                const TouchControls::PositionResult position =
                    _controls->AimPosition();
                if (position.Down)
                {
                    _input->PlacePointer(position.X, position.Y);
                }
                _input->ApplyButton(
                    AndroidInput::MouseButton::Left,
                    position.Down
                );
                _dialogClickDown = position.Down;
                (void)_controls->TakeAimDelta();
                return;
            }

            _controls->PointerIsAbsolute(
                _controls->IsHeld(TouchAction::WeaponMenu)
            );
            if (_dialogClickDown)
            {
                _dialogClickDown = false;
            }

            const bool weaponMenu =
                _controls->IsHeld(TouchAction::WeaponMenu);
            _input->Apply(controls.WeaponMenu(), weaponMenu);

            if (weaponMenu)
            {
                const TouchControls::PositionResult position =
                    _controls->WeaponWheelPosition();
                if (position.Down)
                {
                    _input->PlacePointer(position.X, position.Y);
                }
                (void)_controls->TakeAimDelta();
            }
            else
            {
                const TouchControls::AimDelta delta =
                    _controls->TakeAimDelta();
                _input->MovePointer(
                    delta.X * AimScale,
                    delta.Y * AimScale
                );
            }
        }

        JavaVM* _vm = nullptr;
        TouchControls* _controls = nullptr;
        std::shared_ptr<AndroidInput> _input;
        Build _build;
        Action _onEnd;
        Action _onLoaded;
        ErrorAction _onError;

        mutable std::mutex _lock;
        std::condition_variable _changed;
        std::condition_variable _released;

        std::shared_ptr<JavaGlobalRef> _holder;
        OpenTK::Mathematics::Vector2i _wanted{};
        bool _paused = false;
        bool _stopping = false;
        bool _holdingSurface = false;
        bool _ended = false;
        bool _dialogClickDown = false;

        Action _onPauseMenu;
        BoolAction _onSoftKeyboard;
        bool _menuWasHeld = false;
        bool _spectateCycleHeld = false;
        bool _spectateViewHeld = false;
        bool _missileWasHeld = false;
        bool _chatWasHeld = false;
        bool _keyboardShown = false;

        EGLDisplay _display = EGL_NO_DISPLAY;
        bool _displayAssigned = false;
        EGLConfig _config = nullptr;
        EGLSurface _eglSurface = EGL_NO_SURFACE;
        bool _surfaceAssigned = false;
        EGLContext _context = EGL_NO_CONTEXT;
        bool _contextAssigned = false;
        ANativeWindow* _nativeWindow = nullptr;
        std::shared_ptr<JavaGlobalRef> _boundTo;
        OpenTK::Mathematics::Vector2i _size{};

        bool _clockStarted = false;
        std::chrono::steady_clock::time_point _clockStart{};
        double _nextFrame = 0.0;
        double _lastFrameStart = 0.0;
        std::int32_t _requestedFrameRate = -1;

        std::vector<std::uint8_t> _uiPixels;
        std::int32_t _uiVersion = 0;
        std::int32_t _uiDrawn = 0;
        std::int32_t _uiSkipped = 0;
        std::int32_t _uiHole = 0;
        std::int64_t _uiSaid = 0;

        std::unique_ptr<MphRead::Scene> _scene;
        // The Vulkan path: the surface's swapchain on the scene device.
        bool _vulkan = false;
        std::unique_ptr<MphRead::NativeRuntime::Rhi::Swapchain> _swapchain;
        std::atomic<MphRead::Scene*> _publishedScene{nullptr};
    };

    GameView::GameView(
        JNIEnv* env,
        jobject view,
        TouchControls& controls,
        std::shared_ptr<AndroidInput> input,
        Build build,
        Action onEnd,
        Action onLoaded,
        ErrorAction onError,
        Action onPauseMenu,
        BoolAction onSoftKeyboard
    )
        : _view(std::make_shared<ViewTarget>(env, view))
    {
        _loop = RenderLoop::Create(
            _view->Vm(),
            controls,
            std::move(input),
            std::move(build),
            std::move(onEnd),
            std::move(onLoaded),
            std::move(onError),
            std::move(onPauseMenu),
            std::move(onSoftKeyboard)
        );

        _view->AddHolderCallback(env);
        _view->MakeFocusable(env);
    }

    GameView::~GameView() = default;

    bool GameView::OnCheckIsTextEditor() const
    {
        return MphRead::Mods::Chat::ChatBox::Composing();
    }

    jobject GameView::OnCreateInputConnection(
        JNIEnv* env,
        jobject outAttrs
    )
    {
        return _view->CreateInputConnection(env, outAttrs);
    }

    bool GameView::OnKeyDown(
        JNIEnv* env,
        std::int32_t keyCode,
        jobject event
    )
    {
        if (GamepadBridge::HandleKey(
                keyCode,
                event,
                true,
                env
            ))
        {
            return true;
        }

        if (HandleKey(env, keyCode, event))
        {
            _keyTaken = keyCode;
            return true;
        }

        return _view->BaseOnKeyDown(env, keyCode, event);
    }

    bool GameView::OnKeyUp(
        JNIEnv* env,
        std::int32_t keyCode,
        jobject event
    )
    {
        if (GamepadBridge::HandleKey(
                keyCode,
                event,
                false,
                env
            ))
        {
            return true;
        }

        if (_keyTaken == keyCode)
        {
            _keyTaken = KeyUnknown;
            return true;
        }

        return _view->BaseOnKeyUp(env, keyCode, event);
    }

    bool GameView::OnGenericMotionEvent(
        JNIEnv* env,
        jobject event
    )
    {
        if (GamepadBridge::HandleMotion(event, env))
        {
            return true;
        }
        return _view->BaseOnGenericMotionEvent(env, event);
    }

    bool GameView::HandleKey(
        JNIEnv* env,
        std::int32_t keyCode,
        jobject event
    )
    {
        const bool composing =
            MphRead::Mods::Chat::ChatBox::Composing();
        const bool control =
            event == nullptr
                ? false
                : EventBool(env, event, "isCtrlPressed");
        const bool alt =
            event == nullptr
                ? false
                : EventBool(env, event, "isAltPressed");

        if (!composing)
        {
            if (MphRead::Mods::EndScreen::HandleKeyDown(
                    Map(keyCode)
                ))
            {
                return true;
            }

            return MphRead::Mods::Chat::ChatBox::HandleKeyDown(
                Map(keyCode),
                control,
                alt,
                Scene() != nullptr,
                false
            );
        }

        const Keys key = Map(keyCode);
        if (key == static_cast<Keys>(KeyEnterValue)
            || key == static_cast<Keys>(KeyEscapeValue)
            || key == static_cast<Keys>(KeyBackspaceValue))
        {
            (void)MphRead::Mods::Chat::ChatBox::HandleKeyDown(
                key,
                control,
                alt,
                false
            );
            return true;
        }

        const std::int32_t metaState =
            event == nullptr
                ? 0
                : EventInt(env, event, "getMetaState");
        const std::int32_t unicode =
            event == nullptr
                ? 0
                : EventUnicode(env, event, metaState);
        if (unicode != 0)
        {
            MphRead::Mods::Chat::ChatBox::HandleText(unicode);
        }

        return true;
    }

    Keys GameView::Map(std::int32_t keyCode) noexcept
    {
        if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z)
        {
            return static_cast<Keys>(
                static_cast<std::int32_t>(Keys::A)
                + (keyCode - AKEYCODE_A)
            );
        }

        if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9)
        {
            return static_cast<Keys>(
                static_cast<std::int32_t>(Keys::D0)
                + (keyCode - AKEYCODE_0)
            );
        }

        switch (keyCode)
        {
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER:
            return static_cast<Keys>(KeyEnterValue);
        case AKEYCODE_ESCAPE:
        case AKEYCODE_BACK:
            return static_cast<Keys>(KeyEscapeValue);
        case AKEYCODE_DEL:
            return static_cast<Keys>(KeyBackspaceValue);
        case AKEYCODE_SPACE:
            return Keys::Space;
        case AKEYCODE_TAB:
            return Keys::Tab;
        case AKEYCODE_DPAD_LEFT:
            return Keys::Left;
        case AKEYCODE_DPAD_RIGHT:
            return Keys::Right;
        case AKEYCODE_DPAD_UP:
            return Keys::Up;
        case AKEYCODE_DPAD_DOWN:
            return Keys::Down;
        default:
            return Keys::Unknown;
        }
    }

    MphRead::Scene* GameView::Scene() const noexcept
    {
        return _loop == nullptr
            ? nullptr
            : _loop->SceneValue();
    }

    void GameView::Stop()
    {
        _loop->RequestStop();
    }

    void GameView::OnPause()
    {
        _loop->SetPaused(true);
    }

    void GameView::OnResume()
    {
        _loop->SetPaused(false);
    }

    void GameView::SurfaceCreated(
        JNIEnv*,
        jobject
    )
    {
    }

    void GameView::SurfaceChanged(
        JNIEnv* env,
        jobject holder,
        std::int32_t,
        std::int32_t width,
        std::int32_t height
    )
    {
        _loop->SurfaceReady(
            env,
            holder,
            width,
            height
        );
    }

    void GameView::SurfaceDestroyed(
        JNIEnv*,
        jobject
    )
    {
        _loop->SurfaceGone();
    }
}
