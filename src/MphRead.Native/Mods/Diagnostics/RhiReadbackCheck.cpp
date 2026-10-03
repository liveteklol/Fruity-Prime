#include "RhiReadbackCheck.hpp"
#include "../../Export/Images.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/Rhi/GraphicsDevice.hpp"
#include "../../NativeRuntime/Rhi/CommandList.hpp"
#include "../../NativeRuntime/Stb/Image.hpp"
#include "../../NativeRuntime/System/IO.hpp"
#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

namespace MphRead::Mods::Diagnostics
{
    bool CheckRhiImageExports(const std::string& outputDirectory)
    {
        namespace Rhi = NativeRuntime::Rhi;
        // An odd RGB row width catches four-byte pack alignment mistakes.
        constexpr int width = 641, height = 127;
        // The device that draws the window, whichever backend that is.
        Rhi::GraphicsDevice& device = Rhi::SceneDevice();
        auto commands = device.CreateCommandList();
        // Both backends record only inside Begin/End: OpenGL refuses work on a
        // list that is not recording since the recording scopes were enforced.
        commands->Begin();
        const std::array<Rhi::ClearColor, 3> colors{{
            {1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}};
        Rhi::RenderingInfo target{};
        target.swapchain = true;
        target.width = width;
        target.height = height;
        for (int band = 0; band < 3; ++band)
        {
            Rhi::RenderingColorAttachment attachment{};
            attachment.loadOp = Rhi::LoadOp::Clear;
            attachment.clearValue = colors[band];
            target.colorAttachments = std::span(&attachment, 1);
            target.renderArea = {0, band * 42, width,
                static_cast<std::uint32_t>(band == 2 ? 43 : 42)};
            commands->BeginRendering(target);
            commands->EndRendering();
        }
        const auto directory = NativeRuntime::PathGetFullPath(outputDirectory);
        const auto screenshot = directory + "/rhi-export-screen";
        const auto recording = directory + "/rhi-export-record";
        Export::Images::Screenshot(*commands, width, height, screenshot);
        Export::Images::Record(*commands, width, height, recording);
        Export::Images::StopRecording();
        // Leave the back buffer's scissor unrestricted for subsequent frames.
        target.colorAttachments = {};
        target.renderArea = {};
        commands->BeginRendering(target);
        commands->EndRendering();
        commands->End();
        for (const auto& prefix : {screenshot, recording})
        {
            NativeRuntime::Image image;
            for (int attempt = 0; attempt < 250; ++attempt)
            {
                Export::Images::PollReadbacks();
                if (NativeRuntime::FileExists(prefix + ".png"))
                {
                    // Recording writes on another thread. On Windows the PNG
                    // exists before its writer releases the exclusive handle.
                    try
                    {
                        image = NativeRuntime::LoadPng(
                            NativeRuntime::FileReadAllBytes(prefix + ".png"), 3);
                        if (!image.Pixels.empty()) break;
                    }
                    catch (const std::exception&)
                    {
                        // Retry within the same bounded wait; an unreadable or
                        // incomplete file still fails the pixel check below.
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            bool correct = image.Width == width && image.Height == height
                && image.Pixels.size() == width * height * 3;
            for (int y = 0; correct && y < height; ++y)
            {
                const int channel = y < 43 ? 2 : y < 85 ? 1 : 0;
                for (int x = 0; correct && x < width; ++x)
                    for (int c = 0; c < 3; ++c)
                        if (image.Pixels[(y * width + x) * 3 + c] != (c == channel ? 255 : 0))
                            correct = false;
            }
            if (!correct)
            {
                std::cout << "[shellshot] RHI RGB image export failed: " << prefix << '\n';
                return false;
            }
        }
        std::cout << "[shellshot] RHI screenshot/record RGB, vertical orientation and odd row width PASS\n";
        return true;
    }
}
