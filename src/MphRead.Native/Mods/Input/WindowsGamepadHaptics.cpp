#include "WindowsGamepadHaptics.hpp"

#include "GamepadAnalog.hpp"
#include "../../NativeRuntime/System/Exceptions.hpp"

#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MphRead::Mods::Input
{
    namespace
    {
#if defined(_WIN32)
        struct XInputState
        {
            std::uint32_t Packet;
            std::uint16_t Buttons;
            std::uint8_t LeftTrigger;
            std::uint8_t RightTrigger;
            std::int16_t LeftX;
            std::int16_t LeftY;
            std::int16_t RightX;
            std::int16_t RightY;
        };

        struct XInputVibration
        {
            std::uint16_t Low;
            std::uint16_t High;
        };

        using GetStateFn = DWORD(WINAPI*)(DWORD, XInputState*);
        using SetStateFn = DWORD(WINAPI*)(DWORD, XInputVibration*);

        // [DllImport("xinput1_4.dll")]: resolved on first use, and absent is
        // DllNotFoundException / EntryPointNotFoundException.
        struct XInput final
        {
            GetStateFn GetState = nullptr;
            SetStateFn SetState = nullptr;
        };

        const XInput& Library()
        {
            static const XInput library = []()
            {
                XInput result{};
                if (const HMODULE module = ::LoadLibraryW(L"xinput1_4.dll"))
                {
                    result.GetState = reinterpret_cast<GetStateFn>(reinterpret_cast<void*>(::GetProcAddress(module, "XInputGetState")));
                    result.SetState = reinterpret_cast<SetStateFn>(reinterpret_cast<void*>(::GetProcAddress(module, "XInputSetState")));
                }
                return result;
            }();
            return library;
        }
#endif
    }

    WindowsGamepadHaptics::WindowsGamepadHaptics(std::uint32_t index)
        : _index(index), _timer([this]() { TimerLoop(); })
    {
    }

    WindowsGamepadHaptics::~WindowsGamepadHaptics()
    {
        {
            const std::lock_guard<std::mutex> guard(_timerLock);
            _timerExit = true;
        }
        _timerSignal.notify_all();
        if (_timer.joinable())
        {
            if (_timer.get_id() == std::this_thread::get_id())
            {
                _timer.detach();
            }
            else
            {
                _timer.join();
            }
        }
    }

    void WindowsGamepadHaptics::TimerLoop()
    {
        std::unique_lock<std::mutex> lock(_timerLock);
        while (!_timerExit)
        {
            if (!_due.has_value())
            {
                _timerSignal.wait(lock);
                continue;
            }
            if (_timerSignal.wait_until(lock, *_due) == std::cv_status::timeout
                && _due.has_value() && std::chrono::steady_clock::now() >= *_due)
            {
                _due.reset();
                lock.unlock();
                Stop();
                lock.lock();
            }
        }
    }

    void WindowsGamepadHaptics::SetVibration(std::uint16_t low, std::uint16_t high)
    {
#if defined(_WIN32)
        XInputVibration vibration{low, high};
        if (Library().SetState == nullptr)
        {
            throw ::System::EntryPointNotFoundException(
                "Unable to find XInputSetState in xinput1_4.dll.");
        }
        Library().SetState(_index, &vibration);
#else
        static_cast<void>(low);
        static_cast<void>(high);
#endif
    }

    void WindowsGamepadHaptics::Synchronize(const std::optional<std::string>& requested)
    {
#if defined(_WIN32)
        // Called every frame. With no XInput pad asked for and none bound
        // the scan below can only end in "nothing to do", and
        // XInputGetState on an empty slot is slow: four of them a frame
        // were 3-4 % of the frame at 180 FPS. Otherwise rescan when the
        // request changes, or once a second for a pad that moved slot.
        if (!requested.has_value() && !_id.has_value())
        {
            return;
        }
        static std::optional<std::string> scannedFor;
        static std::chrono::steady_clock::time_point scannedAt{};
        const auto now = std::chrono::steady_clock::now();
        if (requested == scannedFor && now - scannedAt < std::chrono::seconds(1))
        {
            return;
        }
        scannedFor = requested;
        scannedAt = now;
        std::optional<std::string> uniqueId = requested;
        std::optional<std::uint32_t> index;
        if (Library().GetState == nullptr)
        {
            uniqueId = std::nullopt;
        }
        else
        {
            for (std::uint32_t i = 0; i < 4; i++)
            {
                XInputState state{};
                if (Library().GetState(i, &state) == 0)
                {
                    if (index.has_value())
                    {
                        uniqueId = std::nullopt;
                        break;
                    }
                    index = i;
                }
            }
        }
        if (!index.has_value())
        {
            uniqueId = std::nullopt;
        }
        if (_id == uniqueId && (_current == nullptr || _current->_index == index))
        {
            return;
        }
        if (_id.has_value())
        {
            GamepadHaptics::Unregister(*_id);
        }
        if (_current != nullptr)
        {
            _current->Dispose();
        }
        _current = nullptr;
        _id = uniqueId;
        if (_id.has_value() && index.has_value())
        {
            _current.reset(new WindowsGamepadHaptics(*index));
            GamepadHaptics::Register(*_id, _current);
        }
#else
        static_cast<void>(requested);
#endif
    }

    void WindowsGamepadHaptics::Rumble(float lowFrequency, float highFrequency, std::chrono::milliseconds duration)
    {
        const std::lock_guard<std::recursive_mutex> guard(_gate);
        if (_disposed)
        {
            return;
        }
        SetVibration(static_cast<std::uint16_t>(GamepadAnalog::Finite(lowFrequency, 0, 1) * 65535),
            static_cast<std::uint16_t>(GamepadAnalog::Finite(highFrequency, 0, 1) * 65535));
        const auto due = std::clamp(static_cast<std::int32_t>(duration.count()), 1, 500);
        {
            const std::lock_guard<std::mutex> timer(_timerLock);
            _due = std::chrono::steady_clock::now() + std::chrono::milliseconds(due);
        }
        _timerSignal.notify_all();
    }

    void WindowsGamepadHaptics::Dispose()
    {
        const std::lock_guard<std::recursive_mutex> guard(_gate);
        Stop();
        _disposed = true;
        {
            const std::lock_guard<std::mutex> timer(_timerLock);
            _due.reset();
        }
        _timerSignal.notify_all();
    }

    void WindowsGamepadHaptics::Stop()
    {
        const std::lock_guard<std::recursive_mutex> guard(_gate);
        if (_disposed)
        {
            return;
        }
        {
            const std::lock_guard<std::mutex> timer(_timerLock);
            _due.reset();
        }
        SetVibration(0, 0);
    }
}
