#include "PresentConformanceCheck.hpp"
#include "../Branding.hpp"
#include "../../Renderer.hpp"
#include "../../NativeRuntime/Rhi/BackendSession.hpp"
#include "../../NativeRuntime/Rhi/CommandList.hpp"
#include "../../NativeRuntime/Rhi/GraphicsDevice.hpp"
#include "../../NativeRuntime/Rhi/Swapchain.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

// The presentation contract, the same scenario on every backend through the
// shared session and swapchain: what -vulkanpresentcheck proves for Vulkan
// natively, asked of OpenGL as well. A visible window; frames acquired and
// presented; a resize; each present mode the surface offers; a minimised
// window whose acquire must answer at once rather than block; a clean
// shutdown.
namespace MphRead::Mods::Diagnostics
{
    namespace
    {
        namespace Rhi = NativeRuntime::Rhi;

        void Expect(bool value, const std::string& message)
        {
            if (!value) throw std::runtime_error(message);
        }

        // One frame as the game loop makes one: draw into the window target,
        // then present, which acquires the drawable itself. Returns what the
        // present said.
        Rhi::PresentationStatus Frame(Rhi::BackendSession& session, Rhi::Swapchain& swapchain,
            const Rhi::ClearColor& color)
        {
            auto commands = session.Device().CreateCommandList();
            commands->Begin();
            Rhi::RenderingColorAttachment attachment{};
            attachment.loadOp = Rhi::LoadOp::Clear;
            attachment.clearValue = color;
            Rhi::RenderingInfo target{};
            target.swapchain = true;
            target.width = swapchain.Desc().width;
            target.height = swapchain.Desc().height;
            target.colorAttachments = std::span(&attachment, 1);
            commands->BeginRendering(target);
            commands->EndRendering();
            commands->End();
            const Rhi::PresentResult presented = session.Present(swapchain);
            Expect(!presented.failure && presented.status != Rhi::PresentationStatus::DeviceLost
                && presented.status != Rhi::PresentationStatus::SurfaceLost, "A present reported a loss.");
            return presented.status;
        }

        void Pump(int milliseconds)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
            while (std::chrono::steady_clock::now() < until)
            {
                RendererPlatform::ProcessEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        void CheckBackend(Rhi::GraphicsBackend backend)
        {
            const char* name = backend == Rhi::GraphicsBackend::OpenGl ? "OpenGL" : "Vulkan";
            const auto* provider = Rhi::FindBackendProvider(backend);
            if (provider == nullptr) { std::cout << "[present conformance] " << name << " not built; skipped\n"; return; }
            RendererPlatform::WindowSettings settings{};
            settings.ClientSize = {160, 120};
            settings.StartVisible = true;
            settings.Title = std::string(Branding::Name) + " presentation conformance";
            settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
            settings.ApiMajor = 4; settings.ApiMinor = 5;
            settings.GraphicsMode = backend == Rhi::GraphicsBackend::OpenGl
                ? RendererPlatform::GraphicsWindowMode::OpenGL : RendererPlatform::GraphicsWindowMode::NoApi;
            auto window = RendererPlatform::CreateWindow(settings);
            Pump(50);
            auto session = provider->CreateSession({true});
            Rhi::SwapchainDesc desc{};
            const auto size = window->Size();
            desc.width = static_cast<std::uint32_t>(size.X); desc.height = static_cast<std::uint32_t>(size.Y);
            desc.presentMode = Rhi::PresentMode::Fifo;
            {
                auto swapchain = session->CreateSwapchain(*window, desc);
                // The session makes its device with the first swapchain.
                if (backend == Rhi::GraphicsBackend::Vulkan)
                    Expect(session->ValidationEnabled(), "Presentation conformance requires Vulkan validation.");
                const Rhi::PresentationCapabilities caps = swapchain->PresentationCaps();
                Expect(caps.fifo, "FIFO, the one mode every surface has, is not reported.");
                Expect(swapchain->RequestedPresentMode() == Rhi::PresentMode::Fifo, "The requested present mode was not kept.");
                unsigned frames = 0;
                for (int i = 0; i < 8; ++i)
                {
                    Expect(Frame(*session, *swapchain, {0.1F * i, 0.2F, 0.3F, 1}) == Rhi::PresentationStatus::Ready,
                        "A visible window did not acquire.");
                    ++frames; Pump(1);
                }
                // A resize: the window first, then the swapchain to match.
                window->ClientSize({200, 140});
                Pump(80);
                const auto resized = window->Size();
                swapchain->Resize(static_cast<std::uint32_t>(resized.X), static_cast<std::uint32_t>(resized.Y));
                Expect(swapchain->Desc().width == static_cast<std::uint32_t>(resized.X)
                    && swapchain->Desc().height == static_cast<std::uint32_t>(resized.Y), "Resize did not reach the swapchain.");
                for (int i = 0; i < 4; ++i, ++frames)
                    Expect(Frame(*session, *swapchain, {0, 0.5F, 0, 1}) == Rhi::PresentationStatus::Ready,
                        "The resized window did not acquire.");
                // Each mode the surface offers; the request is what is reported back.
                unsigned modes = 0;
                for (const auto [mode, offered] : {std::pair{Rhi::PresentMode::Immediate, caps.immediate},
                         std::pair{Rhi::PresentMode::Mailbox, caps.mailbox}, std::pair{Rhi::PresentMode::Fifo, caps.fifo}})
                {
                    if (!offered) continue;
                    swapchain->SetPresentMode(mode);
                    Expect(swapchain->RequestedPresentMode() == mode, "A present-mode request was not kept.");
                    for (int i = 0; i < 3; ++i, ++frames)
                        Expect(Frame(*session, *swapchain, {0, 0, 0.5F, 1}) == Rhi::PresentationStatus::Ready,
                            "A frame did not present after a present-mode change.");
                    ++modes;
                }
                // Minimised: the acquire answers immediately, whatever it says,
                // and the window draws again once restored.
                window->WindowStateMinimized();
                Pump(150);
                const auto start = std::chrono::steady_clock::now();
                struct { Rhi::PresentationStatus status; } hidden{Frame(*session, *swapchain, {1, 1, 1, 1})};
                const auto took = std::chrono::steady_clock::now() - start;
                Expect(took < std::chrono::milliseconds(250), "A frame on a minimised window blocked.");
                Expect(hidden.status == Rhi::PresentationStatus::Ready || hidden.status == Rhi::PresentationStatus::TemporarilyUnavailable,
                    "A minimised window reported a lost surface or device.");
                window->WindowStateNormal();
                Pump(150);
                const auto restored = window->Size();
                swapchain->Resize(static_cast<std::uint32_t>(restored.X), static_cast<std::uint32_t>(restored.Y));
                bool drew = false;
                for (int i = 0; i < 20 && !drew; ++i)
                {
                    drew = Frame(*session, *swapchain, {0.4F, 0.4F, 0.4F, 1}) == Rhi::PresentationStatus::Ready;
                    if (!drew) Pump(20);
                }
                Expect(drew, "The restored window did not draw again.");
                session->Device().WaitIdle();
                std::cout << "[present conformance] " << name << " PASS; " << frames << " frames; resize; "
                    << modes << " present mode(s) offered and kept; minimised frame "
                    << (hidden.status == Rhi::PresentationStatus::Ready ? "ready" : "temporarily unavailable")
                    << " without blocking; restored\n";
            }
            session->Shutdown();
            Expect(session->ValidationErrors() == 0, "Presentation conformance reported validation errors.");
        }
    }

    int RunPresentConformanceCheck()
    {
        try
        {
            for (const auto backend : {Rhi::GraphicsBackend::OpenGl, Rhi::GraphicsBackend::Vulkan}) CheckBackend(backend);
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "[present conformance] FAIL: " << error.what() << '\n';
            return 1;
        }
    }
}
