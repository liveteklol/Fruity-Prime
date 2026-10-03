#pragma once
#include "PresentationScheduler.hpp"

namespace MphRead::NativeRuntime::Rhi
{
    // Sleep the frame loop until a presentation deadline, with a high
    // resolution timer where the platform has one. Shared by every window
    // platform's frame loop (GLFW and Qt).
    void SleepForPresentation(PresentationScheduler::Time deadline);
}
