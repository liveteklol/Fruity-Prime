#include "PresentationSleep.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace MphRead::NativeRuntime::Rhi
{
    void SleepForPresentation(PresentationScheduler::Time deadline)
    {
        const auto remaining = deadline - PresentationScheduler::Clock::now();
        if (remaining <= PresentationScheduler::Clock::duration::zero()) return;
#if defined(_WIN32)
        struct Timer final
        {
            HANDLE Handle = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
            Timer() { if (!Handle) Handle = CreateWaitableTimerW(nullptr, FALSE, nullptr); }
            ~Timer() { if (Handle) CloseHandle(Handle); }
        };
        static thread_local Timer timer;
        LARGE_INTEGER due;
        due.QuadPart = -std::max<LONGLONG>(1, (std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() + 99) / 100);
        if (timer.Handle && SetWaitableTimer(timer.Handle, &due, 0, nullptr, nullptr, FALSE))
        {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
            if (WaitForSingleObject(timer.Handle, static_cast<DWORD>(std::clamp<std::int64_t>(ms + 2, 1, 1000))) == WAIT_OBJECT_0) return;
        }
#endif
        std::this_thread::sleep_until(deadline);
    }
}
