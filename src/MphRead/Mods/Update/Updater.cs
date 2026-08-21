using System;
using System.Threading;

namespace MphRead.Mods.Update
{
    /// <summary>
    /// When an update is looked for, and what is done about one.
    ///
    /// The policy differs by what is running, and both cases are chosen for the
    /// same reason: the moment to replace a program is one where nothing is
    /// depending on it staying up.
    ///
    /// - The **launcher** checks while the front screen is on display and
    ///   installs before a match starts. Nobody is playing yet, so there is
    ///   nothing to interrupt.
    /// - The **dedicated server** checks before it binds its socket, installs,
    ///   and exits for its supervisor to restart. A server that updated itself
    ///   mid-match would drop everybody in it, so once it is up it is left
    ///   alone until <see cref="ShouldUpdateNow"/> says it is empty.
    /// </summary>
    public static class Updater
    {
        /// <summary>Set by -noupdate, for anybody who wants none of this.</summary>
        public static bool Disabled { get; set; }

        /// <summary>
        /// Look for an update and install it. Returns the binary to run, or
        /// null when there was nothing to do or it did not work.
        /// </summary>
        public static string? CheckAndApply(Action<string> report,
            CancellationToken cancel = default)
        {
            if (Disabled)
            {
                return null;
            }
            UpdateInfo? found = UpdateCheck.Latest(cancel);
            if (found == null)
            {
                report($"[update] {UpdateCheck.LastReason}");
                return null;
            }
            UpdateInfo update = found.Value;
            report($"[update] {update.Tag} is available "
                + $"(this is {BuildVersion.Display})");
            return UpdateInstall.Apply(update, report, cancel);
        }

        /// <summary>
        /// The dedicated server's startup path: update, then exit so that
        /// systemd or NSSM starts the new build. Returns true when the caller
        /// should stop rather than carry on and bind a port.
        /// </summary>
        public static bool UpdatedBeforeStart()
        {
            if (Disabled)
            {
                return false;
            }
            string? installed = CheckAndApply(Console.WriteLine);
            if (installed == null)
            {
                return false;
            }
            Console.WriteLine("[update] installed; restarting");
            // Deliberately not relaunching: a service manager restarts what it
            // started, and a child spawned from here would be a second server
            // on the same port that the manager knows nothing about. Under a
            // manager this exit is the restart; run by hand, it is a prompt.
            return true;
        }

        /// <summary>
        /// Whether a running server may take an update now.
        ///
        /// Empty only. Everything about this feature exists because a client on
        /// a different build is refused at Hello, and the cure for that must
        /// not be to disconnect the people who are already playing.
        /// </summary>
        public static bool ShouldUpdateNow(int playersConnected) =>
            !Disabled && playersConnected == 0;
    }
}
