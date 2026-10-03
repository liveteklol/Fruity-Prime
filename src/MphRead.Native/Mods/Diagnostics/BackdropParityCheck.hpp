#pragma once
#include <string>
namespace MphRead::Mods::Diagnostics
{
    // Actual launcher draw paths, fixed fixture/time, RGBA8 readback.
    int RunBackdropParityCheck(const std::string& directory, bool observeOnly);
}
