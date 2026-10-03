#include "GpuLifetimeCheck.hpp"
#include "../Branding.hpp"

#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../GameState.hpp"
#include "../../NativeRuntime/Rhi/BackendFactory.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlDevice.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../Renderer.hpp"
#include "../../Scene.hpp"
#include "../Network/NetLaunch.hpp"
#include "../../NativeRuntime/System/ProcessMemory.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
    namespace
    {
        using OpenTK::Mathematics::Vector2i;

        class LifetimeWindow final : public RendererPlatform::WindowEvents
        {
        public:
            LifetimeWindow(std::string room, std::int32_t cycles, std::int32_t frames)
                : _room(std::move(room)), _cycles(cycles), _frames(frames)
            {
                RendererPlatform::WindowSettings settings{};
                settings.UpdateFrequency = 60;
                settings.ClientSize = Vector2i(320, 180);
                settings.Title = std::string(Branding::Name) + " GPU lifetime check";
                settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
                settings.Flags = RendererPlatform::WindowSettings::ContextFlags::Default;
                settings.ApiMajor = 3;
                settings.ApiMinor = 2;
                settings.StartVisible = false;
                settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::OpenGL;
                _window = RendererPlatform::CreateWindow(settings);
                NativeRuntime::Rhi::SwapchainDesc desc{};
                desc.width = 320;
                desc.height = 180;
                _swapchain = NativeRuntime::Rhi::BackendFactory::CreateSwapchain(
                    NativeRuntime::Rhi::GraphicsBackend::OpenGl, *_window, desc);
            }

            void Run() { _window->Run(*this); }

            void Dispose()
            {
                _scene.reset();
                _swapchain.reset();
                _window.reset();
            }

            void OnLoad() override
            {
                _window->BaseOnLoad();
                StartCycle();
            }

            void OnRenderFrame(const RendererPlatform::FrameEventArgs& args) override
            {
                try
                {
                    if (_scene)
                    {
                        GameState::ApplyPause();
                        _scene->OnSimulationFrame();
                        _scene->OnDrawFrame();
                        (void)_scene->OnRenderFrame();
                        _swapchain->Present();
                        _scene->AfterRenderFrame();
                        // Loading is over by the halfway frame: from there to the
                        // end is what drawing alone costs.
                        if (++_frame == _frames / 2) _mid = NativeRuntime::Rhi::SceneDevice().Statistics();
                        if (_frame >= _frames)
                        {
                            EndCycle();
                            if (static_cast<std::int32_t>(_after.size()) < _cycles)
                            {
                                StartCycle();
                            }
                            else
                            {
                                _window->Close();
                            }
                        }
                    }
                }
                catch (const std::exception& ex)
                {
                    std::cout << "GPULIFETIME crashed in cycle " << (_after.size() + 1) << ": " << ex.what() << '\n';
                    _failed = true;
                    _window->Close();
                }
                _window->BaseOnRenderFrame(args);
            }

            [[nodiscard]] std::int32_t Report() const
            {
                bool pass = !_failed && static_cast<std::int32_t>(_after.size()) == _cycles;
                for (std::size_t i = 0; i < _after.size(); ++i)
                {
                    const NativeRuntime::Rhi::GpuResourceStatistics& s = _after[i];
                    const bool steady = s.Textures == _after.front().Textures
                        && s.Buffers == _after.front().Buffers
                        && s.Renderbuffers == _after.front().Renderbuffers
                        && s.Framebuffers == _after.front().Framebuffers
                        && s.Shaders == _after.front().Shaders
                        && s.Programs == _after.front().Programs
                        && s.Samplers == _after.front().Samplers
                        && s.VertexArrays == _after.front().VertexArrays;
                    const bool drained = s.Retired == 0 && s.LiveObjects() == 0 && s.Completed == s.Submitted;
                    pass = pass && steady && drained;
                    std::cout << "GPULIFETIME " << _room << " | cycle " << (i + 1)
                        << " | while drawing: " << _during[i].Textures << " textures, "
                        << _during[i].Framebuffers << " framebuffers, " << _during[i].Shaders << " shaders, "
                        << _during[i].Programs << " programs, " << _during[i].Buffers << " buffers, "
                        << _during[i].Renderbuffers << " renderbuffers, " << _during[i].Samplers
                        << " samplers, " << _during[i].VertexArrays << " vertex arrays"
                        << " | after release: " << s.Textures << " textures, " << s.Framebuffers
                        << " framebuffers, " << s.Shaders << " shaders, " << s.Programs << " programs, "
                        << s.Buffers << " buffers, " << s.Renderbuffers << " renderbuffers, "
                        << s.Samplers << " samplers, " << s.VertexArrays << " vertex arrays, "
                        << s.Retired << " retired | frames completed " << s.CompletedFrame
                        << " | submissions completed " << s.Completed.Value << "/" << s.Submitted.Value
                        << " | host waits over the last " << (_frames - _frames / 2) << " frames: "
                        << _steadyWaits[i]
                        << " | private " << (i < _privateKiB.size() ? _privateKiB[i] / 1024U : 0U) << " MB"
                        << (steady ? "" : " | GREW") << (drained ? "" : " | NOT DRAINED") << '\n';
                }
                // CPU memory: after a warm-up quarter, the peak of the last
                // quarter against the peak of the second, per cycle between
                // them. A room's worth leaked per load is tens of MB a cycle;
                // under 1 MB is the allocator's band and the GL driver's own
                // bookkeeping for programs relinked every cycle.
                if (_privateKiB.size() >= 8 && _privateKiB.front() != 0)
                {
                    const std::size_t quarter = _privateKiB.size() / 4;
                    const std::uint64_t early = *std::max_element(_privateKiB.begin() + static_cast<std::ptrdiff_t>(quarter),
                        _privateKiB.begin() + static_cast<std::ptrdiff_t>(2 * quarter));
                    const std::uint64_t late = *std::max_element(_privateKiB.end() - static_cast<std::ptrdiff_t>(quarter),
                        _privateKiB.end());
                    const double span = static_cast<double>(_privateKiB.size() - 2 * quarter);
                    const double perCycle = late > early ? static_cast<double>(late - early) / 1024.0 / span : 0.0;
                    const bool flat = perCycle < 1.0;
                    std::cout << "GPULIFETIME " << _room << " | private memory peak " << early / 1024U << " MB -> "
                        << late / 1024U << " MB, " << perCycle << " MB a cycle" << (flat ? "" : " | GREW") << std::endl;
                    pass = pass && flat;
                }
                std::cout << "GPULIFETIME " << _room << " | " << _after.size() << "/" << _cycles
                    << " cycles | " << (pass ? "PASS" : "FAIL") << '\n';
                return pass ? 0 : 1;
            }

        private:
            void StartCycle()
            {
                _frame = 0;
                _scene = std::make_shared<Scene>(_window->Size(), _window->Keyboard(), _window->Mouse(),
                    [](auto&&) { }, [this]() { _window->Close(); });
                constexpr std::int32_t players = 4;
                Entities::PlayerEntity::SetMaxPlayers(std::max(Entities::PlayerEntity::MaxPlayers(), players));
                for (std::int32_t i = 0; i < players; ++i)
                {
                    _scene->AddPlayer(static_cast<Hunter>(i % 7), 0, -1);
                }
                Entities::PlayerEntity::SetPlayerCount(players);
                Entities::PlayerEntity::SetMainPlayerIndex(0);
                _scene->AddRoom(_room, GameMode::Battle, Network::NetLaunch::RoomPlayerCount());
                _scene->Size(_window->Size());
                _scene->OnLoad();
                _scene->OnResize();
            }

            void EndCycle()
            {
                NativeRuntime::Rhi::GraphicsDevice& device = NativeRuntime::Rhi::SceneDevice();
                _during.push_back(device.Statistics());
                _steadyWaits.push_back(_during.back().HostWaits - _mid.HostWaits);
                _scene->DoCleanup();
                _scene->ReleaseGpuResources();
                _scene.reset();
                _after.push_back(device.Statistics());
                _privateKiB.push_back(NativeRuntime::System::PrivateKiB());
            }

            std::string _room;
            std::int32_t _cycles;
            std::int32_t _frames;
            std::int32_t _frame = 0;
            bool _failed = false;
            std::shared_ptr<RendererPlatform::Window> _window{};
            std::unique_ptr<NativeRuntime::Rhi::Swapchain> _swapchain{};
            std::shared_ptr<Scene> _scene{};
            std::vector<NativeRuntime::Rhi::GpuResourceStatistics> _during{};
            NativeRuntime::Rhi::GpuResourceStatistics _mid{};
            std::vector<std::uint64_t> _steadyWaits{};
            std::vector<NativeRuntime::Rhi::GpuResourceStatistics> _after{};
            std::vector<std::uint64_t> _privateKiB{};
        };
    }

    std::int32_t GpuLifetimeCheck::Run(const std::string& room, std::int32_t cycles, std::int32_t frames)
    {
        std::unique_ptr<LifetimeWindow> window;
        std::int32_t result = 1;
        try
        {
            window = std::make_unique<LifetimeWindow>(room, std::max(1, cycles), std::max(2, frames));
            window->Run();
            result = window->Report();
        }
        catch (const std::exception& ex)
        {
            std::cout << "GPULIFETIME " << room << " | crashed: " << ex.what() << '\n';
            result = 1;
        }
        if (window)
        {
            window->Dispose();
        }
        return result;
    }
}
