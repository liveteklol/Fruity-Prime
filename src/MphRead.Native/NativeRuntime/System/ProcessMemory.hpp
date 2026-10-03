#pragma once

#include <cstdint>

namespace MphRead::NativeRuntime::System
{
    // The process's committed private memory, in KiB: what a CPU-side leak
    // across load/release cycles shows up in. 0 where it cannot be read.
    [[nodiscard]] std::uint64_t PrivateKiB() noexcept;
}

