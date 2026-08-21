namespace MphRead.Mods.Launcher
{
    public enum LaunchKind
    {
        None,
        Online,
        Offline,
        Host,
        Adventure
    }

    /// <summary>
    /// What a front screen decided, carried back to the code that starts the
    /// match.
    ///
    /// Lives here rather than beside <c>HomeForm</c> because there are two
    /// front screens now -- the WinForms one on Windows and
    /// <see cref="TextLauncher"/> everywhere else -- and this is the whole of
    /// what they have to agree on. <see cref="MatchStart"/> is the other half:
    /// it takes one of these and does the same thing with it whichever screen
    /// produced it, so the two launchers cannot drift into starting subtly
    /// different matches.
    /// </summary>
    public readonly struct LaunchPlan
    {
        public LaunchKind Kind { get; init; }
        public Hunter Hunter { get; init; }
        public string RoomKey { get; init; }
        public GameMode Mode { get; init; }
        public int Bots { get; init; }
        public int BotLevel { get; init; }
        public int Port { get; init; }
        public string PlayerName { get; init; }

        /// <summary>Adventure only: which save slot, 1-based. 0 is no slot.</summary>
        public byte SaveSlot { get; init; }

        /// <summary>Adventure only: start over rather than resume the slot.</summary>
        public bool NewGame { get; init; }
    }
}
