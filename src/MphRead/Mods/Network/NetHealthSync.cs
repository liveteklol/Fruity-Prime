using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using MphRead.Entities;
using MphRead.Mods.Multiplayer;

namespace MphRead.Mods.Network
{
    // A bounded tail on the normal authoritative snapshot, not a second
    // channel. Full state repeats so loss and joining late repair themselves.
    public static class NetHealthSync
    {
        public const int MaxSpawns = 56;
        public const int HeaderSize = 3;
        public const int EntrySize = 7;
        private static readonly List<ItemSpawnEntity> _spawns = new(MaxSpawns);
        private static readonly Dictionary<short, HealthSpawnState> _states = new(MaxSpawns);
        internal static IReadOnlyList<ItemSpawnEntity> RegisteredSpawns => _spawns;
        public static bool IsReplica => NetSession.Active && !NetSession.IsAuthority;
        public static bool OwnsPickup(ItemInstanceEntity item) => !IsReplica
            || item.Owner == null || !MapResourceRules.IsHealth(item.ItemType);

        public static void BeginRoom() { _spawns.Clear(); _states.Clear(); }
        public static void Register(ItemSpawnEntity spawn)
        {
            if (!NetSession.Active || !MapResourceRules.IsHealth(spawn.Data.ItemType)) return;
            if (_spawns.Count == MaxSpawns)
                throw new ProgramException($"Network maps support at most {MaxSpawns} health spawners.");
            _spawns.Add(spawn);
        }

        public static bool TryGet(short id, out HealthSpawnState state) => _states.TryGetValue(id, out state);
        public static bool IsCurrentMatch(ReadOnlySpan<byte> src) => src.Length >= HeaderSize
            && BinaryPrimitives.ReadUInt16LittleEndian(src) == (NetSession.ServerSession?.MatchId ?? 0);

        public static int Write(Span<byte> dest)
        {
            int length = HeaderSize + EntrySize * _spawns.Count;
            if (dest.Length < length) throw new ProgramException("Health state exceeds snapshot capacity.");
            BinaryPrimitives.WriteUInt16LittleEndian(dest, NetSession.ServerSession?.MatchId ?? 0);
            dest[2] = (byte)_spawns.Count;
            int offset = HeaderSize;
            foreach (ItemSpawnEntity spawn in _spawns)
            {
                HealthSpawnState state = spawn.ModHealthState;
                BinaryPrimitives.WriteInt16LittleEndian(dest[offset..], (short)spawn.Id);
                dest[offset + 2] = (byte)((state.Available ? 1 : 0) | (state.Active ? 2 : 0));
                BinaryPrimitives.WriteUInt16LittleEndian(dest[(offset + 3)..], state.Cooldown);
                BinaryPrimitives.WriteUInt16LittleEndian(dest[(offset + 5)..], state.SpawnCount);
                offset += EntrySize;
            }
            return length;
        }

        public static bool Validate(ReadOnlySpan<byte> src)
        {
            if (src.Length < HeaderSize || src[2] > MaxSpawns
                || src.Length != HeaderSize + src[2] * EntrySize) return false;
            for (int offset = HeaderSize; offset < src.Length; offset += EntrySize)
            {
                if ((src[offset + 2] & ~3) != 0) return false;
                short id = BinaryPrimitives.ReadInt16LittleEndian(src[offset..]);
                if (id < 0) return false;
                for (int previous = HeaderSize; previous < offset; previous += EntrySize)
                    if (BinaryPrimitives.ReadInt16LittleEndian(src[previous..]) == id) return false;
            }
            return true;
        }

        public static void Receive(ReadOnlySpan<byte> src)
        {
            if (!Validate(src) || !IsCurrentMatch(src)) return;
            _states.Clear();
            for (int offset = HeaderSize; offset < src.Length; offset += EntrySize)
                _states.Add(BinaryPrimitives.ReadInt16LittleEndian(src[offset..]), new(
                    (src[offset + 2] & 1) != 0, (src[offset + 2] & 2) != 0,
                    BinaryPrimitives.ReadUInt16LittleEndian(src[(offset + 3)..]),
                    BinaryPrimitives.ReadUInt16LittleEndian(src[(offset + 5)..])));
        }
    }

    public readonly record struct HealthSpawnState(bool Available, bool Active, ushort Cooldown, ushort SpawnCount);
}
