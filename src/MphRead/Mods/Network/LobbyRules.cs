using System;

namespace MphRead.Mods.Network
{
    public static class LobbyRules
    {
        public static int TeamCount(MatchDefinition match) => !GameState.IsTeamMode(match.Mode) ? 0
            : match.Format == MatchFormat.TwoVsTwoVsTwoVsTwo ? 4 : 2;
        public static int TeamCapacity(MatchDefinition match) => match.Format is MatchFormat.TwoVsTwo
            or MatchFormat.TwoVsTwoVsTwoVsTwo ? 2 : 4;

        public static LobbyResultCode ValidateDefinition(MatchDefinition match, out string reason)
        {
            reason = "";
            if (String.IsNullOrWhiteSpace(match.RoomKey) || match.RoomKey.Length > HostRequestPacket.MaxRoomBytes
                || !Enum.IsDefined(match.Format) || !Enum.IsDefined(match.Mode)
                || match.Mode is GameMode.SinglePlayer or GameMode.None or GameMode.Unknown15)
                reason = "Choose a multiplayer map and mode.";
            else if (match.Format == MatchFormat.TwoVsTwoVsTwoVsTwo)
                reason = "Four-team gameplay is not supported yet.";
            else if (match.Format != MatchFormat.Auto
                && GameState.IsTeamMode(match.Mode) != (match.Format != MatchFormat.FreeForAll))
                reason = "Choose a team mode for a team format, or a free-for-all mode for FFA.";
            return reason.Length == 0 ? LobbyResultCode.Ok : LobbyResultCode.InvalidConfiguration;
        }

        public static LobbyResultCode Validate(MatchDefinition match, RosterPacket roster,
            bool requireReady, out string reason)
        {
            var result = ValidateDefinition(match, out reason);
            if (result != LobbyResultCode.Ok) return result;
            int required = match.Format == MatchFormat.TwoVsTwo ? 4
                : match.Format is MatchFormat.FourVsFour or MatchFormat.TwoVsTwoVsTwoVsTwo ? 8 : 1;
            if (roster.Count < required || (required > 1 && roster.Count != required))
            {
                reason = required == 1 ? "At least one player must join." : $"{match.Format} requires exactly {required} players.";
                return LobbyResultCode.NotEnoughPlayers;
            }
            Span<int> counts = stackalloc int[4];
            int teams = TeamCount(match);
            for (int i = 0; i < roster.Count; i++)
            {
                if (teams > 0)
                {
                    int team = roster.Teams[i];
                    if (team < 0 || team >= teams) { reason = "Every player needs a valid team."; return LobbyResultCode.InvalidTeam; }
                    counts[team]++;
                }
                if (requireReady && !roster.LobbyReady[i])
                { reason = $"Waiting for {roster.Names[i]} to ready."; return LobbyResultCode.PlayersNotReady; }
            }
            for (int team = 0; team < teams; team++)
            {
                int capacity = TeamCapacity(match);
                if (counts[team] > capacity || (required > 1 && counts[team] != capacity))
                { reason = $"Team {team + 1} needs {capacity} players (currently {counts[team]})."; return LobbyResultCode.InvalidTeam; }
            }
            reason = "Ready to start.";
            return LobbyResultCode.Ok;
        }
    }
}
