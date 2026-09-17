using System;

namespace MphRead.Mods.Network
{
    public enum SessionPhase : byte { Lobby, Starting, InMatch, PostMatch }
    public enum ServerSessionPolicy : byte { Continuous, Lobby }
    public enum MatchFormat : byte { Auto, FreeForAll, TwoVsTwo, FourVsFour, TwoVsTwoVsTwoVsTwo }

    [Flags]
    public enum SessionRules : ushort
    {
        None = 0, FriendlyFire = 1, AffinityWeapons = 2, ShadowFreeze = 4,
        RequireReady = 8, AllowJoinInProgress = 16
    }

    public readonly record struct MatchDefinition
    {
        public string RoomKey { get; init; }
        public GameMode Mode { get; init; }
        public MatchFormat Format { get; init; }
        public ushort TimeLimitSeconds { get; init; }
        public ushort PointGoal { get; init; }
        public bool FriendlyFire { get; init; }
        public bool AffinityWeapons { get; init; }
        public bool ShadowFreeze { get; init; }
        public SessionRules Rules => (FriendlyFire ? SessionRules.FriendlyFire : 0)
            | (AffinityWeapons ? SessionRules.AffinityWeapons : 0)
            | (ShadowFreeze ? SessionRules.ShadowFreeze : 0);
    }
}
