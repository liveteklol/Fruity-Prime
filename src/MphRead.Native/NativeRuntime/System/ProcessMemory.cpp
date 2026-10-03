#include "ProcessMemory.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <cstdio>
#include <unistd.h>
#endif

namespace MphRead::NativeRuntime::System
{
    std::uint64_t PrivateKiB() noexcept
    {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS_EX counters{};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
            return counters.PrivateUsage / 1024U;
        return 0;
#elif defined(__linux__)
        unsigned long size = 0, resident = 0, shared = 0;
        std::FILE* file = std::fopen("/proc/self/statm", "r");
        if (file == nullptr) return 0;
        const int read = std::fscanf(file, "%lu %lu %lu", &size, &resident, &shared);
        std::fclose(file);
        if (read != 3 || resident < shared) return 0;
        return static_cast<std::uint64_t>(resident - shared) * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE)) / 1024U;
#else
        return 0;
#endif
    }
}

