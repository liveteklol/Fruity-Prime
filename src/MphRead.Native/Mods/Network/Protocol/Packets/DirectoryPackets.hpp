#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "NativeRuntime/System/Guid.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::Network
{
    struct HostRequestPacket
    {
        static constexpr std::int32_t MaxRoomBytes = 40;
        static constexpr std::int32_t MaxNameBytes = 32;
        static constexpr std::int32_t Size = 1 + 1 + 1 + 2 + 2 + MaxRoomBytes + MaxNameBytes;

        // How many maps a requested rotation may carry, and what one costs on
        // the wire. The cap is the datagram rather than a policy.
        static constexpr std::int32_t MaxRotation = 16;
        static constexpr std::int32_t RotationEntrySize = MaxRoomBytes + 1;

        std::uint8_t Protocol = 0;
        std::uint8_t MaxPlayers = 0;
        std::uint8_t Mode = 0;
        std::uint16_t TimeLimit = 0;
        std::uint16_t PointGoal = 0;
        // The C# parameterless constructor sets both to "" and the two flags
        // to true; `default` (ZeroInitialized) leaves them null and false.
        std::optional<std::string> RoomKey = std::string();
        std::optional<std::string> ServerName = std::string();

        // Every map the asker wants played, in order, or nothing. Written
        // *after* the fixed block, so a directory from before rotations reads
        // exactly Size bytes and plays RoomKey on a loop.
        std::optional<std::vector<std::pair<std::string, ::MphRead::GameMode>>> Rotation{};

        ServerSessionPolicy Policy = ServerSessionPolicy::Continuous;
        bool AllowJoinInProgress = true;
        bool RequireReady = true;
        MatchFormat Format = MatchFormat::Auto;

        // How many bytes this request takes, tail included.
        [[nodiscard]] std::int32_t Length() const noexcept;

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static HostRequestPacket Read(std::span<const std::uint8_t> src);
        // default(HostRequestPacket): every field zero, bypassing the
        // constructor's defaults.
        [[nodiscard]] static HostRequestPacket ZeroInitialized();

    private:
        [[nodiscard]] static std::optional<std::vector<std::pair<std::string, ::MphRead::GameMode>>>
            ReadRotation(std::span<const std::uint8_t> src);
    };

    struct HostReplyPacket
    {
        static constexpr std::int32_t MaxReasonBytes = 96;
        static constexpr std::int32_t Size = 1 + 2 + MaxReasonBytes + 16;

        ::MphRead::NativeRuntime::Guid OwnerToken{};
        bool Started = false;
        std::uint16_t Port = 0;
        std::optional<std::string> Reason{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static HostReplyPacket Read(std::span<const std::uint8_t> src);
    };

    class NetText final
    {
    public:
        static void Write(std::span<std::uint8_t> dest, const std::optional<std::string>& value);
        [[nodiscard]] static std::string Read(std::span<const std::uint8_t> src);

        NetText() = delete;
    };

    struct MasterEntryPacket
    {
        static constexpr std::int32_t MaxNameBytes = 32;
        static constexpr std::int32_t MaxRoomBytes = 40;
        static constexpr std::int32_t Size = 4 + 2 + 1 + 1 + 1 + 1 + MaxNameBytes + MaxRoomBytes;

        std::uint32_t Address = 0;
        std::uint16_t Port = 0;
        std::uint8_t Players = 0;
        std::uint8_t MaxPlayers = 0;
        std::uint8_t Mode = 0;
        std::uint8_t Protocol = 0;
        std::optional<std::string> ServerName{};
        std::optional<std::string> RoomKey{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static MasterEntryPacket Read(std::span<const std::uint8_t> src);
    };

    struct MasterHeartbeatPacket
    {
        static constexpr std::int32_t Size
            = 1 + 2 + 1 + 1 + 1 + MasterEntryPacket::MaxNameBytes + MasterEntryPacket::MaxRoomBytes;

        std::uint8_t Protocol = 0;
        std::uint16_t Port = 0;
        std::uint8_t Players = 0;
        std::uint8_t MaxPlayers = 0;
        std::uint8_t Mode = 0;
        std::optional<std::string> ServerName{};
        std::optional<std::string> RoomKey{};

        void Write(std::span<std::uint8_t> dest) const;
        [[nodiscard]] static MasterHeartbeatPacket Read(std::span<const std::uint8_t> src);
    };

}
