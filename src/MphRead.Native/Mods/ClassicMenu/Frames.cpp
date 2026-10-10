#include "Frames.hpp"

#include "ClassicMenu.hpp"
#include "MenuData.hpp"

#include "../../NativeRuntime/System/IO.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <vector>

namespace MphRead::Export::ImagesInterop
{
    void WritePngRgb(
        std::span<const std::uint8_t> buffer,
        std::int32_t width,
        std::int32_t height,
        std::ostream& stream);
}

namespace MphRead::Mods::ClassicMenu
{
    namespace
    {
        [[nodiscard]] std::vector<std::string> Split(const std::string& text, char separator)
        {
            std::vector<std::string> parts;
            std::stringstream stream(text);
            std::string part;
            while (std::getline(stream, part, separator))
            {
                if (!part.empty()) parts.push_back(part);
            }
            return parts;
        }

        void Write(const std::filesystem::path& path, const std::vector<std::uint32_t>& pixels)
        {
            std::vector<std::uint8_t> rgb;
            rgb.reserve(pixels.size() * 3);
            for (const std::uint32_t p : pixels)
            {
                rgb.push_back(static_cast<std::uint8_t>(p));
                rgb.push_back(static_cast<std::uint8_t>(p >> 8));
                rgb.push_back(static_cast<std::uint8_t>(p >> 16));
            }
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            Export::ImagesInterop::WritePngRgb(rgb, Facade::ScreenWidth, Facade::ScreenHeight * 2, stream);
        }
    }

    int RunFrames(const std::string& directory, const std::string& script)
    {
        const std::filesystem::path dir = NativeRuntime::PathFromUtf8(directory);
        std::filesystem::create_directories(dir);
        static const int owner = 0;
        if (!Facade::SetActive(true, &owner))
        {
            std::fprintf(stderr, "[classicframes] the DS menus could not load: %s\n", Facade::LastError().c_str());
            return 1;
        }
        std::vector<std::uint32_t> pixels;
        long long tick = 0;
        int written = 0;
        const auto run = [&](int ticks, bool capture)
        {
            for (int i = 0; i < ticks; ++i)
            {
                if (!Facade::RenderDs(1, pixels)) return false;
                ++tick;
                if (!capture) continue;
                char name[32];
                std::snprintf(name, sizeof(name), "t%05lld.png", tick);
                Write(dir / name, pixels);
                ++written;
            }
            return true;
        };
        // where things stand before the first tick
        if (!Facade::RenderDs(0, pixels)) return 1;
        Write(dir / "t00000.png", pixels);
        int failures = 0;
        for (const std::string& raw : Split(script.empty() ? "cap:300" : script, ';'))
        {
            const std::vector<std::string> parts = Split(raw, ':');
            const std::string& action = parts.at(0);
            const int count = parts.size() >= 2 ? std::atoi(parts[1].c_str()) : 1;
            bool ok = true;
            if (action == "tick") ok = run(count, false);
            else if (action == "cap") ok = run(count, true);
            else if (action == "shot" && parts.size() >= 2) { Write(dir / (parts[1] + ".png"), pixels); ++written; }
            else if (action == "a") Facade::Press(KeyA);
            else if (action == "b") Facade::Press(KeyB);
            else if (action == "start") Facade::Press(KeyStart);
            else if (action == "up") Facade::Navigate(0, 1);
            else if (action == "down") Facade::Navigate(0, -1);
            else if (action == "left") Facade::Navigate(-1, 0);
            else if (action == "right") Facade::Navigate(1, 0);
            else if (action == "touch" && parts.size() >= 3)
            {
                Facade::Touch(std::strtof(parts[1].c_str(), nullptr), std::strtof(parts[2].c_str(), nullptr));
            }
            else if (action == "page" && parts.size() >= 2) Facade::Visit(parts[1]);
            else if (action == "describe")
            {
                char name[32];
                std::snprintf(name, sizeof(name), "t%05lld.txt", tick);
                std::ofstream(dir / name) << Facade::Describe();
            }
            else
            {
                std::fprintf(stderr, "[classicframes] unknown step %s\n", raw.c_str());
                ++failures;
            }
            if (!ok)
            {
                std::fprintf(stderr, "[classicframes] the menus stopped at tick %lld: %s\n", tick, Facade::LastError().c_str());
                return 1;
            }
        }
        std::printf("[classicframes] %d picture(s), %lld tick(s), in %s\n", written, tick, directory.c_str());
        (void)Facade::SetActive(false, &owner);
        return failures == 0 ? 0 : 1;
    }
}
