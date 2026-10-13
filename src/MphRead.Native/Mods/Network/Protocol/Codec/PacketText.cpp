#include "Mods/Network/Protocol/Codec/PacketText.hpp"

#include "Mods/Network/Protocol/Codec/WirePrimitives.hpp"

#include <algorithm>
#include <vector>

namespace MphRead::Mods::Network
{
    void PacketText::Write(std::span<std::uint8_t> dest, const std::optional<std::string>& value)
    {
        Protocol::Wire::Clear(dest);
        if (!value.has_value() || value->empty())
        {
            return;
        }
        const std::vector<std::uint16_t> units = Protocol::Wire::U16(*value);
        const std::size_t count = std::min(units.size(), dest.size());
        for (std::size_t index = 0; index < count; index++)
        {
            const std::uint16_t unit = units[index];
            dest[index] = unit < 32U || unit > 126U
                ? static_cast<std::uint8_t>('?') : static_cast<std::uint8_t>(unit);
        }
    }

    std::string PacketText::Read(std::span<const std::uint8_t> src)
    {
        std::size_t length = 0;
        while (length < src.size() && src[length] != 0)
        {
            length++;
        }
        return length == 0 ? std::string{} : Protocol::Wire::DA(src.first(length));
    }
}
