#pragma once

#include "Submission.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The GPU lifetime contract, the same for every backend.
//
// Frame slots govern reusable per-frame storage. Resource lifetime follows
// actual completed submissions, independently of those slots. Vulkan uses a
// graphics-queue timeline; OpenGL uses GLsync stream markers. WaitIdle is an
// explicit release boundary, not required for ordinary destruction or resize.
namespace MphRead::NativeRuntime::Rhi
{
    inline constexpr std::uint32_t FramesInFlight = 2;

    struct FrameContext final
    {
        // The frame's number; this is not a submission serial.
        std::uint64_t Number = 0;
        // Which of the FramesInFlight per-frame slots it uses.
        std::uint32_t Slot = 0;
    };

    struct GpuResourceStatistics final
    {
        std::uint32_t Textures = 0;
        std::uint32_t Buffers = 0;
        std::uint32_t Renderbuffers = 0;
        std::uint32_t Shaders = 0;
        std::uint32_t Programs = 0;
        std::uint32_t Framebuffers = 0;
        std::uint32_t Samplers = 0;
        std::uint32_t VertexArrays = 0;
        std::uint32_t TimestampSets = 0;
        std::uint32_t Retired = 0;
        std::uint64_t CompletedFrame = 0;
        // Times the CPU has stopped to wait for the GPU (a fence or the whole
        // device). Implicit driver waits in upload/map APIs are not counted here.
        std::uint64_t HostWaits = 0;
        // Actual queue progress, not a simulation/presentation frame counter.
        SubmissionSerial Submitted{};
        SubmissionSerial Completed{};
        std::uint64_t DeviceWideWaits = 0;
        // Reusable GPU-only staging the backend keeps for its own transfers
        // (Vulkan's RGB copy scratch). Internal, so not in LiveObjects: a
        // caller cannot release it, and a stable count is the reuse proof.
        std::uint32_t TransferScratchPages = 0;

        bool operator==(const GpuResourceStatistics&) const = default;
        [[nodiscard]] std::uint64_t LiveObjects() const noexcept
        {
            return static_cast<std::uint64_t>(Textures) + Buffers + Renderbuffers + Shaders
                + Programs + Framebuffers + Samplers + VertexArrays + TimestampSets;
        }
    };

    // Native objects waiting for the GPU to finish with them. Backend
    // neutral: T is whatever a backend destroys (a GL name and its kind, a
    // VkImage and its memory).
    template <typename T>
    class RetirementQueue final
    {
    public:
        void Retire(T object, SubmissionSerial lastUse)
        {
            _entries.push_back(Entry{std::move(object), lastUse});
        }

        // Destroy everything whose last-use value the GPU has completed.
        template <typename Destroy>
        std::size_t Collect(SubmissionSerial completedValue, Destroy&& destroy)
        {
            std::size_t destroyed = 0;
            auto keep = _entries.begin();
            for (auto it = _entries.begin(); it != _entries.end(); ++it)
            {
                if (it->LastUse <= completedValue)
                {
                    destroy(it->Object);
                    ++destroyed;
                }
                else
                {
                    if (keep != it)
                    {
                        *keep = std::move(*it);
                    }
                    ++keep;
                }
            }
            _entries.erase(keep, _entries.end());
            return destroyed;
        }

        template <typename Destroy>
        std::size_t CollectAll(Destroy&& destroy)
        {
            for (Entry& entry : _entries)
            {
                destroy(entry.Object);
            }
            const std::size_t destroyed = _entries.size();
            _entries.clear();
            return destroyed;
        }

        // Take back objects that are about to be used again rather than
        // destroyed (a texture re-created under the handle it had).
        template <typename Matches>
        std::size_t Cancel(Matches&& matches)
        {
            const auto end = std::remove_if(_entries.begin(), _entries.end(),
                [&matches](const Entry& entry) { return matches(entry.Object); });
            const auto cancelled = static_cast<std::size_t>(std::distance(end, _entries.end()));
            _entries.erase(end, _entries.end());
            return cancelled;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return _entries.size(); }

    private:
        struct Entry final
        {
            T Object;
            SubmissionSerial LastUse;
        };

        std::vector<Entry> _entries{};
    };
}
