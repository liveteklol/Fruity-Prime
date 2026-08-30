namespace MphRead.Mods
{
    /// <summary>
    /// Which Pro mode HUD layout to draw, while there is more than one to
    /// choose between.
    ///
    /// Four of them exist at once on purpose: what a HUD should look like is
    /// not a thing to argue about in prose, it is a thing to look at, so each
    /// is drawn and photographed (<c>-maptest ROOM -hudshots -shots DIR</c>)
    /// and the one that wins stays. Deliberately not a setting: it has no row
    /// in the settings screen and is not written to settings.json -- only
    /// <c>-prohudstyle N</c> sets it, which is what the capture commands pass.
    /// </summary>
    public static class ProHudStyle
    {
        public static int Current { get; set; } = 1;
    }
}
