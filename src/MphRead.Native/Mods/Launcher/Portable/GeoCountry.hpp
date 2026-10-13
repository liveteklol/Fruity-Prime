#pragma once

#include "../../../NativeRuntime/System/Net.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::Launcher::Gui
{
    // Which country an address is in, answered from a table that ships
    // (DB-IP Lite, CC BY 4.0), so nothing leaves the machine.
    class GeoCountry final
    {
    public:
        GeoCountry() = delete;

        // The two-letter code, or an empty string.
        [[nodiscard]] static std::string Of(const std::optional<::MphRead::NativeRuntime::Address>& address);
        [[nodiscard]] static bool IsPrivate(const ::MphRead::NativeRuntime::Address& address);

    private:
        static void Load();
        [[nodiscard]] static std::vector<std::uint8_t> ReadTable();

        static inline std::vector<std::uint32_t> _first{};
        static inline std::vector<std::uint8_t> _index{};
        static inline std::vector<std::string> _codes{};
        static inline bool _tried = false;
        static inline int _failures = 0;
    };
}
