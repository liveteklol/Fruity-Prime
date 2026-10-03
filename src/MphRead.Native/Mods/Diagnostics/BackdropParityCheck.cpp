#include "BackdropParityCheck.hpp"
#include "../Branding.hpp"
#include "../../Renderer.hpp"
#include "../../NativeRuntime/Rhi/BackendSession.hpp"
#include "../../NativeRuntime/Rhi/OpenGL/OpenGlWindowDraw.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
#if !defined(__ANDROID__)
    namespace
    {
        namespace Rhi = NativeRuntime::Rhi;
        using Pixels = std::vector<unsigned char>;
        using Captures = std::map<std::string, Pixels>;
        void Expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
        void Pump()
        {
            for (int i = 0; i < 12; ++i)
            { RendererPlatform::ProcessEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        }
        void Save(const std::filesystem::path& path, const Pixels& pixels, int width, int height)
        {
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            // ReadColor uses bottom-first rows; PPM viewers use top-first rows.
            for (int y = height - 1; y >= 0; --y)
                for (int x = 0; x < width; ++x)
                    file.write(reinterpret_cast<const char*>(pixels.data() + (y * width + x) * 4), 3);
            Expect(file.good(), "Cannot save backdrop capture.");
        }
        Captures Capture(Rhi::GraphicsBackend backend, const std::filesystem::path& directory, int cycle)
        {
            const bool gl = backend == Rhi::GraphicsBackend::OpenGl;
            const std::string name = gl ? "opengl" : "vulkan";
            const auto* provider = Rhi::FindBackendProvider(backend);
            Expect(provider != nullptr, "Backdrop parity requires both backend providers.");
            RendererPlatform::WindowSettings settings{};
            settings.ClientSize = {1280, 720}; settings.StartVisible = true;
            settings.Title = std::string(Branding::Name) + " backdrop parity";
            settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
            settings.ApiMajor = 4; settings.ApiMinor = 5;
            settings.GraphicsMode = gl ? RendererPlatform::GraphicsWindowMode::OpenGL : RendererPlatform::GraphicsWindowMode::NoApi;
            auto window = RendererPlatform::CreateWindow(settings); Pump();
            auto session = provider->CreateSession({true});
            Captures captures;
            {
                Rhi::SwapchainDesc desc{}; desc.width = 1280; desc.height = 720;
                auto swapchain = session->CreateSwapchain(*window, desc);
                auto& device = session->Device();
                std::cout << "[backdrop parity] " << name << ' ' << device.AdapterDescription() << '\n';
                Expect(gl || session->ValidationEnabled(), "Backdrop parity requires Vulkan validation.");
                {
                    // Fixed non-square photo with gradients and hard edges exercises crop and orientation.
                    Pixels fixture(256 * 128 * 4);
                    for (int y = 0; y < 128; ++y) for (int x = 0; x < 256; ++x)
                    {
                        const auto i = (y * 256 + x) * 4;
                        fixture[i] = static_cast<unsigned char>(x);
                        fixture[i+1] = static_cast<unsigned char>(y * 2);
                        fixture[i+2] = ((x / 32 + y / 16) % 2) ? 192 : 64; fixture[i+3] = 255;
                    }
                    Rhi::TextureDesc photoDesc{}; photoDesc.width = 256; photoDesc.height = 128;
                    photoDesc.format = Rhi::TextureFormat::RGBA8Unorm;
                    photoDesc.usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDst;
                    auto photo = device.CreateTexture(photoDesc);
                    device.WriteTexture(*photo, {256, 128, Rhi::TextureFormat::RGBA8Unorm, fixture.data()});
                    Rhi::SamplerDesc samplerDesc{}; samplerDesc.maxLod = 0;
                    samplerDesc.addressU = samplerDesc.addressV = Rhi::SamplerAddressMode::ClampToEdge;
                    auto sampler = device.CreateSampler(samplerDesc);
                    auto reader = device.CreateCommandList();
                    auto ui = session->CreateUi(device);
                    std::unique_ptr<Rhi::OpenGL::OpenGlWindowDraw> glDraw;
                    if (gl) glDraw = std::make_unique<Rhi::OpenGL::OpenGlWindowDraw>();
                    Expect(gl || ui != nullptr, "Vulkan window UI missing.");
                    const auto frame = [&](const std::string& key, float time, float strength = 0.62F) {
                        const auto size = window->Size();
                        const int w = size.X, h = size.Y;
                        swapchain->Resize(w, h);
                        const double aspect = static_cast<double>(w) / h;
                        const float u = aspect <= 2.0 ? static_cast<float>(aspect / 2.0) : 1.0F;
                        const float v = aspect > 2.0 ? static_cast<float>(2.0 / aspect) : 1.0F;
                        const float u0 = (1-u)/2, u1 = u0+u, v0 = (1-v)/2, v1 = v0+v;
                        const std::array<Rhi::WindowQuadVertex, 4> strip{{
                            {{1,1},{u1,v0},{1,0}}, {{-1,1},{u0,v0},{0,0}},
                            {{1,-1},{u1,v1},{1,1}}, {{-1,-1},{u0,v1},{0,1}}}};
                        (void)device.BeginFrame();
                        if (gl)
                        {
                            std::array<Rhi::OpenGL::WindowVertex,4> vertices{};
                            for (int i=0;i<4;++i)
                                vertices[i] = {{strip[i].Position[0],strip[i].Position[1],0},
                                    {strip[i].TexCoord[0],strip[i].TexCoord[1]}, {strip[i].TexCoord1[0],strip[i].TexCoord1[1]}};
                            glDraw->Draw(photo->Handle().value, vertices, w, h, false, true, strength, time);
                        }
                        else { ui->Begin(w,h,true); ui->DrawBackdrop(*photo,*sampler,strip,strength,time); ui->End(); }
                        Rhi::RenderingInfo info{}; info.swapchain = true; info.width = w; info.height = h;
                        Pixels pixels(static_cast<std::size_t>(w) * h * 4);
                        reader->ReadColor(info,0,0,w,h,Rhi::TextureFormat::RGBA8Unorm,pixels.data());
                        Expect(device.DrainErrors() == 0, "Backdrop draw raised device errors.");
                        const auto presented = session->Present(*swapchain);
                        Expect(!presented.failure && presented.status == Rhi::PresentationStatus::Ready, "Backdrop did not present.");
                        Expect(session->ValidationErrors() == 0, "Backdrop draw raised validation errors.");
                        Save(directory / (name + "-" + std::to_string(cycle) + "-" + key + ".ppm"), pixels,w,h);
                        captures.emplace(key,std::move(pixels));
                    };
                    for (const auto size : {std::pair{1280,720},std::pair{1920,1080}})
                    {
                        window->ClientSize({size.first,size.second}); Pump();
                        Expect(window->Size().X == size.first && window->Size().Y == size.second, "Requested capture resolution unavailable.");
                        const std::string prefix = std::to_string(size.first) + "x" + std::to_string(size.second);
                        for (const auto time : {0.0F,0.25F,0.5F,1.0F}) frame(prefix+"-t"+std::to_string(time),time);
                        frame(prefix+"-photo",0,0);
                    }
                    window->ClientSize({1280,720}); Pump(); frame("resize-return",0);
                    Expect(captures.at("resize-return") == captures.at("1280x720-t0.000000"), "Resize changed fixed-time backdrop.");
                    // Same borderless monitor rectangle used by WindowMode::Enter.
                    const auto savedLocation = window->Location();
                    const auto monitor = window->CurrentMonitorClientArea();
                    window->WindowBorder(2); window->Location(monitor.Min);
                    window->ClientSize(monitor.Size); Pump(); frame("fullscreen",0.5F);
                    window->WindowBorder(0); window->Location(savedLocation); window->ClientSize({1280,720}); Pump();
                    frame("fullscreen-return",0);
                    Expect(captures.at("fullscreen-return") == captures.at("resize-return"), "Fullscreen return changed backdrop.");
                    for (int i = 0; i < 16; ++i) frame("animation-"+std::to_string(i),i/15.0F);
                }
                device.TrimCaches(); device.WaitIdle();
                Expect(device.DrainErrors() == 0, "Backdrop release raised device errors.");
                const auto stats = device.Statistics();
                Expect(stats.LiveObjects() == 0 && stats.Retired == 0, "Backdrop resources leaked after release.");
                std::cout << "[backdrop parity] " << name << " cycle=" << cycle << " resize/fullscreen/errors/release PASS; live=0 retired=0\n";
            }
            session->Shutdown();
            Expect(session->ValidationErrors() == 0, "Backdrop shutdown validation errors.");
            return captures;
        }
    }
#endif
    int RunBackdropParityCheck(const std::string& directory, bool observeOnly)
    {
#if !defined(__ANDROID__)
        try
        {
            const auto output = std::filesystem::u8path(directory); std::filesystem::create_directories(output);
            std::ofstream metrics(output / "metrics.csv");
            metrics << "cycle,capture,max_channel_difference,mean_channel_difference,channels_over_2\n";
            Captures reference;
            for (int cycle=0;cycle<2;++cycle)
            {
                const auto gl = Capture(Rhi::GraphicsBackend::OpenGl,output,cycle);
                const auto vk = Capture(Rhi::GraphicsBackend::Vulkan,output,cycle);
                if (cycle) Expect(gl == reference, "Session recreation changed OpenGL captures.");
                else reference = gl;
                if (!observeOnly)
                {
                    Expect(gl.at("animation-0") != gl.at("animation-15")
                        && vk.at("animation-0") != vk.at("animation-15"), "Backdrop animation is frozen or fell back to the photograph.");
                    Expect(gl.at("1280x720-photo") != gl.at("resize-return"), "Moving layer was not applied.");
                }
                for (const auto& [key,a] : gl)
                {
                    const auto& b = vk.at(key); Expect(a.size() == b.size(), "Backend extents differ.");
                    unsigned maximum=0; std::uint64_t sum=0, over=0;
                    for (std::size_t i=0;i<a.size();++i)
                    { const auto delta = static_cast<unsigned>(std::abs(int(a[i])-int(b[i]))); maximum=std::max(maximum,delta); sum+=delta; over+=delta>2; }
                    const double mean = static_cast<double>(sum)/a.size();
                    metrics << cycle << ',' << key << ',' << maximum << ',' << mean << ',' << over << '\n';
                    std::cout << "[backdrop parity] " << key << " max=" << maximum << " mean=" << mean << " over2=" << over << '\n';
                    Expect(observeOnly || (maximum <= 2 && mean <= 0.1), "Backdrop image parity failed.");
                }
            }
            Expect(metrics.good(), "Cannot save parity metrics.");
            std::cout << "[backdrop parity] " << (observeOnly ? "OBSERVED" : "PASS") << '\n'; return 0;
        }
        catch (const std::exception& error) { std::cerr << "[backdrop parity] FAIL: " << error.what() << '\n'; return 1; }
#else
        (void)directory; (void)observeOnly; return 1;
#endif
    }
}
