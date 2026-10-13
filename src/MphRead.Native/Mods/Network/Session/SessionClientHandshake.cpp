#include "Mods/Network/Session/SessionClientHandshake.hpp"

#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"
#include "Mods/Network/Session/ClientLobbyState.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionPlayerIdentity.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Gameplay/Player/PlayerColors.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "NativeRuntime/System/Encoding.hpp"
#include "NativeRuntime/System/Random.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::uint32_t SessionClientHandshake::NewClientId()
    {
        std::array<std::uint8_t, 4> bytes{};
        Runtime::RandomNumberGeneratorFill(bytes.data(), bytes.size());
        const std::uint32_t id = Runtime::ReadUInt32LittleEndian(std::span<const std::uint8_t>(bytes));
        return id == 0 ? 1U : id;
    }

    ConnectionSecret SessionClientHandshake::NewSecret()
    {
        ConnectionSecret secret{};
        while (!IsSet(secret))
        {
            Runtime::RandomNumberGeneratorFill(secret.data(), secret.size());
        }
        return secret;
    }

    namespace
    {
        [[nodiscard]] std::vector<std::uint8_t> AsciiBytes(std::string_view text)
        {
            std::vector<std::uint8_t> result;
            result.reserve(text.size());
            for (std::size_t index = 0; index < text.size();)
            {
                const Runtime::Utf8Scalar unit = Runtime::DecodeUtf8Scalar(text, index);
                if (!unit.Valid())
                {
                    result.push_back(static_cast<std::uint8_t>('?'));
                    index += unit.Length;
                    continue;
                }
                if (unit.Value <= 0x7FU)
                {
                    result.push_back(static_cast<std::uint8_t>(unit.Value));
                }
                else
                {
                    // Encoding.ASCII replaces an unsupported UTF-16 code unit
                    // with '?'. A supplementary scalar occupies a surrogate
                    // pair, so the C# encoder emits two replacement bytes.
                    result.push_back(static_cast<std::uint8_t>('?'));
                    if (unit.Value > 0xFFFFU)
                    {
                        result.push_back(static_cast<std::uint8_t>('?'));
                    }
                }
                index += unit.Length;
            }
            return result;
        }
    }

    void SessionClientHandshake::SendHello(SessionConnection& connection,
        const SessionRuntimeState& runtime, const ClientLobbyState& lobby,
        std::uint32_t clientId, const ConnectionSecret& secret)
    {
        if (!connection.IsOpen() || !connection.HasHostEndpoint())
        {
            return;
        }
        HelloPacket hello{};
        hello.Protocol = NetConfig::ProtocolVersion;
        hello.Slot = runtime.LocalSlot() >= 0 && runtime.LocalSlot() < HelloPacket::AnySlot
            ? static_cast<std::uint8_t>(runtime.LocalSlot())
            : HelloPacket::AnySlot;
        hello.ClientId = clientId;
        hello.OwnerToken = lobby.OwnerToken();
        hello.Secret = secret;
        hello.Write(connection.Buffer());
        connection.SendToHost(PacketType::Hello,
            std::span<const std::uint8_t>(connection.Buffer()).first(HelloPacket::Size));
    }

    void SessionClientHandshake::SendIdentify(SessionConnection& connection,
        const SessionPlayerIdentity& identity)
    {
        if (!connection.IsOpen() || !connection.HasHostEndpoint())
        {
            return;
        }
        const std::vector<std::uint8_t> name = AsciiBytes(identity.Name());
        const std::size_t count = std::min(name.size(), static_cast<std::size_t>(RosterPacket::MaxNameBytes));
        connection.Buffer()[0] = static_cast<std::uint8_t>(identity.SelectedHunter());
        connection.Buffer()[1] = static_cast<std::uint8_t>(PlayerColors::Clamp(identity.Color()));
        std::copy_n(name.begin(), count, connection.Buffer().begin() + 2);
        connection.SendToHost(PacketType::Identify,
            std::span<const std::uint8_t>(connection.Buffer()).first(count + 2));
    }
}
