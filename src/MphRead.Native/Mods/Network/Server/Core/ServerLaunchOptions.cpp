#include "Mods/Network/Server/Core/ServerLaunchOptions.hpp"
#include "Mods/Network/Server/Core/DedicatedServer.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    bool ServerLaunchOptions::ParseOwnerToken(const std::string& text, Runtime::Guid& token)
    {
        if (text.size() != 32) { return false; }
        const auto hex = [](char c) -> int
        {
            if (c >= '0' && c <= '9') { return c - '0'; }
            if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
            return -1;
        };
        std::array<std::uint8_t, 16> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            const int a = hex(text[i * 2]);
            const int b = hex(text[i * 2 + 1]);
            if (a < 0 || b < 0) { return false; }
            bytes[i] = static_cast<std::uint8_t>(a * 16 + b);
        }
        std::reverse(bytes.begin(), bytes.begin() + 4);
        std::reverse(bytes.begin() + 4, bytes.begin() + 6);
        std::reverse(bytes.begin() + 6, bytes.begin() + 8);
        token = Runtime::Guid(bytes);
        return true;
    }

    void ServerLaunchOptions::Apply(DedicatedServer& server, std::span<const std::string> arguments)
    {
        const auto value = [&](const std::string& key) -> std::optional<std::string>
        {
            for (std::size_t i = 0; i < arguments.size(); ++i)
            {
                if (Runtime::StringEqualsOrdinalIgnoreCase(arguments[i], "-" + key))
                {
                    return i + 1 < arguments.size() ? arguments[i + 1] : std::string{};
                }
            }
            return std::nullopt;
        };
        for (const auto& argument : arguments)
        {
            if (Runtime::StringEqualsOrdinalIgnoreCase(argument, "-lobby"))
            {
                server.SessionPolicy(ServerSessionPolicy::Lobby);
            }
        }
        if (const auto token = value("ownertoken"); token.has_value())
        {
            Runtime::Guid parsed{};
            if (!ParseOwnerToken(*token, parsed)) { throw std::invalid_argument("invalid lobby owner token"); }
            server.OwnerToken(parsed);
        }
        if (const auto format = value("format"); format.has_value())
        {
            bool found = false;
            for (int i = 0; i <= static_cast<int>(MatchFormat::Custom); ++i)
            {
                const auto candidate = static_cast<MatchFormat>(i);
                if (Runtime::StringEqualsOrdinalIgnoreCase(*format, ToString(candidate)))
                {
                    server.Format(candidate);
                    found = true;
                    break;
                }
            }
            if (!found) { throw std::invalid_argument("invalid match format"); }
        }
        const auto flag = [&](const std::string& key, bool fallback)
        {
            const auto text = value(key);
            if (!text.has_value()) { return fallback; }
            if (Runtime::StringEqualsOrdinalIgnoreCase(*text, "on")) { return true; }
            if (Runtime::StringEqualsOrdinalIgnoreCase(*text, "off")) { return false; }
            throw std::invalid_argument("invalid " + key + ": expected on or off");
        };
        server.SetSessionOptions(flag("requireready", server.RequireReady()),
            flag("joininprogress", server.AllowJoinInProgress()));
    }
}
