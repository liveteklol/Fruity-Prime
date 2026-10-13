#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"

#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Domain/PowerUps.hpp"

#include "Mods/Multiplayer/MapResourceRules.hpp"
#include "Entities/EntityBase.hpp"
#include "Entities/ItemInstanceEntity.hpp"
#include "Entities/ItemSpawnEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "NativeRuntime/System/BinaryPrimitives.hpp"
#include "Program.hpp"
#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::vector<std::shared_ptr<::MphRead::Entities::ItemSpawnEntity>> NetItemState::_spawns{};
    std::map<std::int16_t, ItemSpawnState> NetItemState::_states{};
    std::map<std::int16_t, NetItemState::Appearance> NetItemState::_appeared{};

    const std::vector<std::shared_ptr<::MphRead::Entities::ItemSpawnEntity>>&
        NetItemState::RegisteredSpawns() noexcept
    {
        return _spawns;
    }

    bool NetItemState::IsReplica()
    {
        return NetSession::Active() && !NetSession::IsAuthority();
    }

    bool NetItemState::ServerOwned(::MphRead::ItemType type) noexcept
    {
        return Multiplayer::MapResourceRules::IsHealth(type) || PowerUpOf(type).has_value();
    }

    bool NetItemState::Mirrors(const ::MphRead::Entities::ItemSpawnEntity& spawn)
    {
        return IsReplica() && ServerOwned(spawn.Data().ItemType);
    }

    PickupRoute NetItemState::Route(const ::MphRead::Entities::PlayerEntity& player,
        const ::MphRead::Entities::ItemInstanceEntity& item)
    {
        PickupContext context{};
        context.Networked = NetSession::Active();
        context.Authority = NetSession::IsAuthority();
        context.Spawned = item.Owner() != nullptr;
        context.ServerOwned = ServerOwned(item.ItemType());
        context.PowerUp = PowerUpOf(item.ItemType()).has_value();
        context.OwnPlayer = player.SlotIndex() == NetSession::LocalSlot();
        return RouteFor(context);
    }

    std::shared_ptr<::MphRead::Entities::ItemSpawnEntity> NetItemState::Find(std::int16_t id)
    {
        for (const auto& spawn : _spawns)
        {
            if (spawn != nullptr && spawn->Id == id)
            {
                return spawn;
            }
        }
        return nullptr;
    }

    void NetItemState::NoteSpawned(const ::MphRead::Entities::ItemSpawnEntity& spawn)
    {
        if (!NetSession::Active() || !NetSession::IsAuthority() || !ServerOwned(spawn.Data().ItemType))
        {
            return;
        }
        _appeared[static_cast<std::int16_t>(spawn.Id)] = Appearance{ spawn.SpawnCount(),
            std::max(1U, NetSession::NetFrame()) };
    }

    std::optional<std::uint32_t> NetItemState::SpawnedAt(std::int16_t id, std::uint16_t spawnCount)
    {
        const auto found = _appeared.find(id);
        if (found == _appeared.end() || found->second.SpawnCount != spawnCount)
        {
            return std::nullopt;
        }
        return found->second.Frame;
    }

    void NetItemState::BeginRoom()
    {
        _spawns.clear();
        _states.clear();
        _appeared.clear();
        _nextSpawn = 0;
    }

    void NetItemState::Register(::MphRead::Entities::ItemSpawnEntity& spawn)
    {
        if (!NetSession::Active() || !ServerOwned(spawn.Data().ItemType))
        {
            return;
        }
        if (static_cast<std::int32_t>(_spawns.size()) == MaxSpawns)
        {
            throw ::MphRead::ProgramException("Network maps support at most "
                + std::to_string(MaxSpawns) + " health and power-up spawners.");
        }
        _spawns.push_back(::MphRead::Entities::SharedFrom(spawn));
    }

    bool NetItemState::TryGet(std::int16_t id, ItemSpawnState& state)
    {
        const auto found = _states.find(id);
        if (found == _states.end())
        {
            state = ItemSpawnState{};
            return false;
        }
        state = found->second;
        return true;
    }

    bool NetItemState::IsCurrentMatch(std::span<const std::uint8_t> src)
    {
        return src.size() >= static_cast<std::size_t>(HeaderSize)
            && Runtime::ReadUInt16LittleEndian(src) == NetSession::CurrentMatchId();
    }

    std::int32_t NetItemState::Write(std::span<std::uint8_t> dest)
    {
        const std::size_t count = std::min(_spawns.size(), static_cast<std::size_t>(PacketEntries(dest.size())));
        const std::int32_t length = HeaderSize + EntrySize * static_cast<std::int32_t>(count);
        if (dest.size() < HeaderSize || (!_spawns.empty() && count == 0))
        {
            throw ::MphRead::ProgramException("Item state exceeds snapshot capacity.");
        }
        Runtime::WriteUInt16LittleEndian(dest, NetSession::CurrentMatchId());
        dest[2] = static_cast<std::uint8_t>(count);
        std::size_t offset = HeaderSize;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& spawn = _spawns[(_nextSpawn + i) % _spawns.size()];
            const ItemSpawnState state = spawn->ModItemState();
            const std::int32_t encodedPicker = state.PickerSlot + 1;
            if (encodedPicker < 0 || encodedPicker > ::MphRead::Entities::PlayerEntity::SlotCapacity)
            {
                throw ::MphRead::ProgramException("Invalid item pickup slot "
                    + std::to_string(state.PickerSlot) + ".");
            }
            Runtime::WriteInt16LittleEndian(Runtime::SpanSlice(dest, offset), static_cast<std::int16_t>(spawn->Id));
            dest[offset + 2] = static_cast<std::uint8_t>((state.Available ? 1 : 0) | (state.Active ? 2 : 0)
                | (encodedPicker << PickerShift));
            Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, offset + 3), state.Cooldown);
            Runtime::WriteUInt16LittleEndian(Runtime::SpanSlice(dest, offset + 5), state.SpawnCount);
            offset += EntrySize;
        }
        if (!_spawns.empty()) _nextSpawn = (_nextSpawn + count) % _spawns.size();
        return length;
    }

    bool NetItemState::Validate(std::span<const std::uint8_t> src)
    {
        if (src.size() < static_cast<std::size_t>(HeaderSize) || src[2] > MaxSpawns
            || src.size() != static_cast<std::size_t>(HeaderSize + src[2] * EntrySize))
        {
            return false;
        }
        for (std::size_t offset = HeaderSize; offset < src.size(); offset += EntrySize)
        {
            const std::uint8_t flags = src[offset + 2];
            const std::int32_t encodedPicker = (flags >> PickerShift) & PickerMask;
            if ((flags & ReservedMask) != 0 || encodedPicker > ::MphRead::Entities::PlayerEntity::SlotCapacity)
            {
                return false;
            }
            const std::int16_t id = Runtime::ReadInt16LittleEndian(Runtime::SpanSlice(src, offset));
            if (id < 0)
            {
                return false;
            }
            for (std::size_t previous = HeaderSize; previous < offset; previous += EntrySize)
            {
                if (Runtime::ReadInt16LittleEndian(Runtime::SpanSlice(src, previous)) == id)
                {
                    return false;
                }
            }
        }
        return true;
    }

    void NetItemState::Receive(std::span<const std::uint8_t> src)
    {
        if (!Validate(src) || !IsCurrentMatch(src))
        {
            return;
        }
        for (std::size_t offset = HeaderSize; offset < src.size(); offset += EntrySize)
        {
            const std::uint8_t flags = src[offset + 2];
            const auto pickerSlot = static_cast<std::int8_t>(((flags >> PickerShift) & PickerMask) - 1);
            const std::int16_t id = Runtime::ReadInt16LittleEndian(Runtime::SpanSlice(src, offset));
            ItemSpawnState state;
            state.Available = (flags & 1) != 0;
            state.Active = (flags & 2) != 0;
            state.Cooldown = Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, offset + 3));
            state.SpawnCount = Runtime::ReadUInt16LittleEndian(Runtime::SpanSlice(src, offset + 5));
            state.PickerSlot = pickerSlot;
            // Each bounded packet repeats a rotating subset. Retain the other
            // spawners until BeginRoom clears this match's cache.
            _states.insert_or_assign(id, state);
        }
    }
}
