using System;
using System.Buffers.Binary;

namespace MphRead.Mods.Network
{
    // Fixed, bounded control packets. TryRead is the only wire entry point.
    public struct SessionStatePacket
    {
        public const int Size = 20 + HostRequestPacket.MaxRoomBytes;
        public SessionPhase Phase;
        public ServerSessionPolicy Policy;
        public ushort Revision, MatchId;
        public byte OwnerSlot, MaxPlayers, ExpectedParticipants, LoadedParticipants;
        public SessionRules RuleFlags;
        public MatchDefinition Match;
        public bool RequireReady => RuleFlags.HasFlag(SessionRules.RequireReady);
        public bool AllowJoinInProgress => RuleFlags.HasFlag(SessionRules.AllowJoinInProgress);

        public void Write(Span<byte> dest)
        {
            dest[..Size].Clear();
            dest[0] = (byte)Phase; dest[1] = (byte)Policy;
            BinaryPrimitives.WriteUInt16LittleEndian(dest[2..], Revision);
            BinaryPrimitives.WriteUInt16LittleEndian(dest[4..], MatchId);
            dest[6] = OwnerSlot; dest[7] = MaxPlayers;
            dest[8] = (byte)Match.Format; dest[9] = (byte)Match.Mode;
            BinaryPrimitives.WriteUInt16LittleEndian(dest[10..], Match.TimeLimitSeconds);
            BinaryPrimitives.WriteUInt16LittleEndian(dest[12..], Match.PointGoal);
            BinaryPrimitives.WriteUInt16LittleEndian(dest[14..], (ushort)(RuleFlags | Match.Rules));
            dest[16] = ExpectedParticipants; dest[17] = LoadedParticipants;
            NetText.Write(dest.Slice(20, HostRequestPacket.MaxRoomBytes), Match.RoomKey);
        }

        public static bool TryRead(ReadOnlySpan<byte> src, out SessionStatePacket state)
        {
            state = default;
            if (src.Length != Size || src[0] > (byte)SessionPhase.PostMatch
                || src[1] > (byte)ServerSessionPolicy.Lobby || src[7] is < 1 or > 8
                || (src[6] != byte.MaxValue && src[6] >= src[7])
                || src[8] > (byte)MatchFormat.TwoVsTwoVsTwoVsTwo
                || !Enum.IsDefined(typeof(GameMode), src[9])) return false;
            var flags = (SessionRules)BinaryPrimitives.ReadUInt16LittleEndian(src[14..]);
            if (((ushort)flags & ~31) != 0) return false;
            state = new SessionStatePacket
            {
                Phase = (SessionPhase)src[0], Policy = (ServerSessionPolicy)src[1],
                Revision = BinaryPrimitives.ReadUInt16LittleEndian(src[2..]),
                MatchId = BinaryPrimitives.ReadUInt16LittleEndian(src[4..]),
                OwnerSlot = src[6], MaxPlayers = src[7], RuleFlags = flags,
                ExpectedParticipants = src[16], LoadedParticipants = src[17],
                Match = new MatchDefinition
                {
                    Format = (MatchFormat)src[8], Mode = (GameMode)src[9],
                    TimeLimitSeconds = BinaryPrimitives.ReadUInt16LittleEndian(src[10..]),
                    PointGoal = BinaryPrimitives.ReadUInt16LittleEndian(src[12..]),
                    RoomKey = NetText.Read(src.Slice(20, HostRequestPacket.MaxRoomBytes)),
                    FriendlyFire = flags.HasFlag(SessionRules.FriendlyFire),
                    AffinityWeapons = flags.HasFlag(SessionRules.AffinityWeapons),
                    ShadowFreeze = flags.HasFlag(SessionRules.ShadowFreeze)
                }
            };
            return true;
        }

        public static bool IsNewer(ushort value, ushort previous) => (short)(value - previous) > 0;
    }

    public enum LobbyCommandType : byte { SetReady, SetTeam, UpdateMatch, StartMatch, KickPlayer, TransferOwner }
    public enum LobbyResultCode : byte
    {
        Ok, NotOwner, InvalidPhase, StaleRevision, InvalidConfiguration, InvalidTeam,
        TeamFull, PlayersNotReady, NotEnoughPlayers, TargetNotFound, ServerBusy, MapUnavailable
    }

    public struct LobbyCommandPacket
    {
        public const int Size = 10 + SessionStatePacket.Size;
        public uint CommandId;
        public ushort ExpectedRevision;
        public LobbyCommandType Type;
        public byte TargetSlot;
        public sbyte TeamIndex;
        public bool Ready;
        public SessionStatePacket Configuration;
        public void Write(Span<byte> dest)
        {
            dest[..Size].Clear();
            BinaryPrimitives.WriteUInt32LittleEndian(dest, CommandId);
            BinaryPrimitives.WriteUInt16LittleEndian(dest[4..], ExpectedRevision);
            dest[6] = (byte)Type; dest[7] = TargetSlot;
            dest[8] = unchecked((byte)TeamIndex); dest[9] = Ready ? (byte)1 : (byte)0;
            Configuration.Write(dest[10..]);
        }
        public static bool TryRead(ReadOnlySpan<byte> src, out LobbyCommandPacket command)
        {
            command = default;
            if (src.Length != Size || src[6] > (byte)LobbyCommandType.TransferOwner || src[9] > 1) return false;
            SessionStatePacket config = default;
            if (src[6] == (byte)LobbyCommandType.UpdateMatch
                && !SessionStatePacket.TryRead(src[10..], out config)) return false;
            command = new LobbyCommandPacket
            {
                CommandId = BinaryPrimitives.ReadUInt32LittleEndian(src),
                ExpectedRevision = BinaryPrimitives.ReadUInt16LittleEndian(src[4..]),
                Type = (LobbyCommandType)src[6], TargetSlot = src[7],
                TeamIndex = unchecked((sbyte)src[8]), Ready = src[9] != 0, Configuration = config
            };
            return command.CommandId != 0;
        }
    }

    public struct LobbyCommandResultPacket
    {
        public const int Size = 7 + 96;
        public uint CommandId;
        public LobbyResultCode ResultCode;
        public ushort CurrentRevision;
        public string Reason;
        public void Write(Span<byte> dest)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(dest, CommandId);
            dest[4] = (byte)ResultCode;
            BinaryPrimitives.WriteUInt16LittleEndian(dest[5..], CurrentRevision);
            NetText.Write(dest.Slice(7, 96), Reason);
        }
        public static bool TryRead(ReadOnlySpan<byte> src, out LobbyCommandResultPacket result)
        {
            result = default;
            if (src.Length != Size || src[4] > (byte)LobbyResultCode.MapUnavailable) return false;
            result = new LobbyCommandResultPacket
            {
                CommandId = BinaryPrimitives.ReadUInt32LittleEndian(src), ResultCode = (LobbyResultCode)src[4],
                CurrentRevision = BinaryPrimitives.ReadUInt16LittleEndian(src[5..]),
                Reason = NetText.Read(src.Slice(7, 96))
            };
            return true;
        }
    }

    public readonly record struct MatchLoadedPacket(ushort MatchId)
    {
        public const int Size = 2;
        public void Write(Span<byte> dest) => BinaryPrimitives.WriteUInt16LittleEndian(dest, MatchId);
        public static bool TryRead(ReadOnlySpan<byte> src, out MatchLoadedPacket packet)
        {
            packet = src.Length == Size ? new(BinaryPrimitives.ReadUInt16LittleEndian(src)) : default;
            return src.Length == Size;
        }
    }

    public readonly record struct MatchLoadFailedPacket(ushort MatchId, string Reason)
    {
        public const int Size = 98;
        public void Write(Span<byte> dest)
        {
            BinaryPrimitives.WriteUInt16LittleEndian(dest, MatchId);
            NetText.Write(dest.Slice(2, 96), Reason);
        }
        public static bool TryRead(ReadOnlySpan<byte> src, out MatchLoadFailedPacket packet)
        {
            packet = src.Length == Size ? new(BinaryPrimitives.ReadUInt16LittleEndian(src), NetText.Read(src.Slice(2, 96))) : default;
            return src.Length == Size;
        }
    }
}
