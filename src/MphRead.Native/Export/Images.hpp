#pragma once

#include "NativeRuntime/System/AtomicSharedPtr.hpp"
#include "../Formats/Types.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead
{
    class Model;
}

namespace MphRead::NativeRuntime::Rhi { class CommandList; }

namespace MphRead::Export
{
    class Images final
    {
    public:
        static void Screenshot(
            NativeRuntime::Rhi::CommandList& commands,
            std::int32_t width,
            std::int32_t height,
            std::optional<std::string> name = std::nullopt);
        static void Record(NativeRuntime::Rhi::CommandList& commands, std::int32_t width, std::int32_t height, const std::string& name);
        static void StopRecording();
        // Device-thread pump: nonblocking GPU polls, then immutable CPU leases
        // are handed to the PNG worker. Session-close cancellation is reported.
        static void PollReadbacks();
        static void ExportImages(const Model& model);
        static void ExportPalettes(const Model& model);
        static void SaveTexture(
            const std::string& directory,
            const std::string& filename,
            std::uint16_t width,
            std::uint16_t height,
            const std::vector<ColorRgba>& pixels);
        static void ExportHudLayers();
        static void ExportHudObjects();

        Images() = delete;
        Images(const Images&) = delete;
        Images& operator=(const Images&) = delete;
        Images(Images&&) = delete;
        Images& operator=(Images&&) = delete;

    private:
        class TaskState;
        class QueueState;

        static ::MphRead::NativeRuntime::AtomicSharedPtr<TaskState> _task;
        static std::atomic<bool> _recording;
        static QueueState _queue;

        static void ProcessQueue();
        static bool CaptureFits(std::int32_t width, std::int32_t height);
    };
}
