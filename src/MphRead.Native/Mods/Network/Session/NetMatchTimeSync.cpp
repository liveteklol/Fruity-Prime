#include "Mods/Network/Session/NetMatchTimeSync.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"

#include <cmath>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    static_assert(NetMatchTimeSync::Size == ::MphRead::Entities::PlayerEntity::SlotCapacity * 4 * 2);

    void NetMatchTimeSync::Write(std::span<std::uint8_t> dest)
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(::MphRead::Entities::PlayerEntity::SlotCapacity); ++i)
        {
            Runtime::WriteSingleLittleEndian(Runtime::SpanSlice(dest, i * 8), ::MphRead::GameState::Time()[i]);
            Runtime::WriteSingleLittleEndian(Runtime::SpanSlice(dest, i * 8 + 4), ::MphRead::GameState::TeamTime()[i]);
        }
    }

    bool NetMatchTimeSync::Validate(std::span<const std::uint8_t> src)
    {
        if (src.size() != static_cast<std::size_t>(Size))
        {
            return false;
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(Size); i += 4)
        {
            const float value = Runtime::ReadSingleLittleEndian(Runtime::SpanSlice(src, i));
            if (!std::isfinite(value) || value < -1)
            {
                return false;
            }
        }
        return true;
    }

    void NetMatchTimeSync::Receive(std::span<const std::uint8_t> src)
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(::MphRead::Entities::PlayerEntity::SlotCapacity); ++i)
        {
            ::MphRead::GameState::Time()[i] = Runtime::ReadSingleLittleEndian(Runtime::SpanSlice(src, i * 8));
            ::MphRead::GameState::TeamTime()[i] = Runtime::ReadSingleLittleEndian(Runtime::SpanSlice(src, i * 8 + 4));
        }
    }
}
