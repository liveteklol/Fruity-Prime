using System;
using System.Collections.Generic;
using System.Linq;
using MphRead.Entities;
using MphRead.Mods.Network;

namespace MphRead.Mods
{
    /// <summary>
    /// Single dispatch point for everything under Mods/.
    ///
    /// Upstream is touched in exactly one place (a call to TryHandle in
    /// Program.Main) so that pulling from NoneGiven/MphRead stays a fast
    /// forward instead of a conflict hunt. Every new mod command is added
    /// here, not in Program.cs.
    /// </summary>
    public static class ModEntry
    {
        /// <summary>
        /// Returns true if a mod command handled this invocation and the
        /// program should exit without running the normal paths.
        ///
        /// Takes the raw argv rather than Program's parsed Argument type,
        /// which is private: matching on the raw strings keeps the upstream
        /// hook to a single line and adds no coupling to internals that may
        /// be refactored later.
        /// </summary>
        /// <summary>
        /// Commands that must run before the game-file setup check, because
        /// they need neither paths.txt nor extracted assets. Kept separate
        /// from TryHandle so the dedicated server can run on a machine that
        /// has no game data at all.
        /// </summary>
        public static bool TryHandleHeadless(string[] args)
        {
            // Keys and mouse feel, before anything creates a player. Called
            // here because this runs for every invocation, launcher or not.
            InputSettings.Load();
            Update.Updater.Disabled = HasFlag(args, "noupdate");

            if (HasFlag(args, "credits"))
            {
                Credits.Print();
                return true;
            }

            // The explicit check, so there is always one command that answers
            // "am I on the latest build". Nothing is downloaded here either:
            // it prints the release page and opens it if there is a desktop to
            // open it on.
            if (HasFlag(args, "update"))
            {
                Update.Updater.Disabled = false;
                Update.UpdateInfo? update = Update.Updater.Check();
                if (update == null)
                {
                    Console.WriteLine($"[update] {Update.UpdateCheck.LastReason}");
                    return true;
                }
                Console.WriteLine($"[update] {Update.Updater.Describe(update.Value)}");
                Console.WriteLine($"[update] {update.Value.PageUrl}");
                Update.Updater.OpenPage(update.Value);
                return true;
            }
            // Before the game-file check, not after: a fresh install has no
            // paths.txt, and the check exits with "press any key" on a console
            // nobody is looking at. The launcher is the screen that fixes
            // that, so it has to be reachable first.
            // The launcher. The window first, the text screen when there is no
            // display to put it on -- an SSH login, a container, a machine with
            // no X or Wayland session. -text asks for the text one on a machine
            // that has both.
            //
            // No arguments means somebody double-clicked the binary, and on the
            // platforms where that is how a program is normally started that
            // has to be the launcher: the console menu behind it is for people
            // who typed something, and -menu is how they still get it. On Linux
            // a bare invocation has always opened upstream's console menu and
            // still does -- that is a screen people are already using, not an
            // empty spot to fill.
            bool doubleClicked = args.Length == 0
                && (OperatingSystem.IsWindows() || OperatingSystem.IsMacOS());
#if MPHREAD_SERVER
            // Except in the server package, which has no launcher of either
            // kind and ships without game files: a bare invocation there is
            // answered further down by ServerUsage, which says what the binary
            // is for. Double-clicking FruityPrimeServer.exe must not open a
            // text launcher offering matches it cannot play.
            doubleClicked = false;
#endif
            if ((HasFlag(args, "launcher") || doubleClicked) && !HasFlag(args, "menu"))
            {
#if MPHREAD_AVALONIA
                if (!HasFlag(args, "text") && Launcher.Gui.GuiLauncher.TryRun())
                {
                    return true;
                }
                // The window could not be opened. On Windows that means the
                // process has no console either -- it is a GUI binary -- so the
                // text launcher would print into nothing.
                if (OperatingSystem.IsWindows())
                {
                    Mods.ConsoleWindow.Show();
                }
#endif
                Launcher.TextLauncher.Run();
                return true;
            }
#if MPHREAD_SERVER
            // The server package, run with nothing to do. Falling through to
            // upstream's setup check would answer with "could not find
            // paths.txt, drag a ROM onto the executable" -- true of this
            // binary, and useless: it ships without game files because it
            // needs none, and it cannot play a match even with them.
            if (args.Length == 0)
            {
                ServerUsage();
                return true;
            }
#endif
            // Both servers say so at startup if they are behind, and then get
            // on with it.
            //
            // A protocol change makes a server refuse every client on an older
            // build at Hello, so a stale server is a server nobody can join,
            // and that is worth one line in the journal where an operator will
            // find it. It is a line and not an install: nothing here has a
            // person at the keyboard to decide, and a server that replaced its
            // own binary and restarted would drop whoever was playing.
            if (HasFlag(args, "masterserver") || HasFlag(args, "server")
                || HasFlag(args, "dedicated"))
            {
                Update.UpdateInfo? update = Update.Updater.Check();
                if (update != null)
                {
                    Console.WriteLine($"[update] {Update.Updater.Describe(update.Value)}");
                    Console.WriteLine($"[update] {update.Value.PageUrl}");
                    Console.WriteLine("[update] this server keeps running on "
                        + $"{Update.BuildVersion.Display}; clients on the new build "
                        + "will be refused until it is updated by hand");
                }
            }

            // The server directory: -masterserver. Same binary as the game
            // server on purpose -- the machine that runs one usually runs the
            // other, and a second thing to install is a second thing to forget
            // to restart.
            if (HasFlag(args, "masterserver"))
            {
                int masterPort = NetMasterConfig.DefaultPort;
                string? masterPortValue = ValueAfter(args, "port")
                    ?? ValueAfter(args, "masterport");
                if (masterPortValue != null && Int32.TryParse(masterPortValue, out int parsedMasterPort))
                {
                    masterPort = parsedMasterPort;
                }
                var master = new MasterServer(masterPort);
                using var masterSignals = new ShutdownSignals();
                // The ports it may start games on, for players whose routers
                // will not forward one. A range by default, because the whole
                // point of the feature is that it works without anybody being
                // asked to configure it; -hostports none turns it off.
                string hostPorts = ValueAfter(args, "hostports") ?? "27900-27919";
                if (!hostPorts.Equals("none", StringComparison.OrdinalIgnoreCase))
                {
                    string[] parts = hostPorts.Split('-', 2);
                    if (parts.Length == 2 && Int32.TryParse(parts[0], out int first)
                        && Int32.TryParse(parts[1], out int last) && first > 0 && last >= first)
                    {
                        master.SetHostPorts(first, last);
                    }
                    else
                    {
                        Console.WriteLine($"[master] ignoring -hostports {hostPorts} "
                            + "(expected e.g. 27900-27919, or none)");
                    }
                }
                // The address to hand out for servers running on this same
                // machine, which is the usual arrangement: the directory and
                // one game server on one small box. Their heartbeats arrive
                // over the loopback, and a list of loopback addresses is a
                // list of servers nobody can reach.
                string? publicHost = ValueAfter(args, "public")
                    ?? ValueAfter(args, "publicaddress");
                if (publicHost != null)
                {
                    master.SetPublicAddress(publicHost);
                }
                using var masterCancel = new System.Threading.CancellationTokenSource();
                masterSignals.OnShutdown(() =>
                {
                    masterCancel.Cancel();
                    master.Stop();
                });
                master.Run(masterCancel.Token);
                return true;
            }
            // The server list, printed. Same two calls the launcher's browser
            // makes -- ask the directory, then ask each server it named -- so
            // this is how that data path gets checked on a machine with no
            // WinForms, which is every machine that is not Windows.
            if (HasFlag(args, "servers"))
            {
                ListServers(ValueAfter(args, "master") ?? NetMasterConfig.DefaultHost,
                    ValueAfter(args, "masterport"));
                return true;
            }
            if (!HasFlag(args, "server") && !HasFlag(args, "dedicated"))
            {
                return false;
            }
            int port = NetConfig.DefaultPort;
            string? portValue = ValueAfter(args, "port");
            if (portValue != null && Int32.TryParse(portValue, out int parsedPort))
            {
                port = parsedPort;
            }
            int maxPlayers = 4;
            string? playersValue = ValueAfter(args, "players");
            if (playersValue != null && Int32.TryParse(playersValue, out int parsedPlayers))
            {
                maxPlayers = parsedPlayers;
            }

            // Rotation file lives beside the executable, the way a Quake 3
            // server keeps its config next to the binary.
            string rotationPath = ValueAfter(args, "rotation")
                ?? System.IO.Path.Combine(AppContext.BaseDirectory, "maprotation.txt");
            MapRotation rotation = MapRotation.LoadOrCreate(rotationPath);

            var server = new Network.DedicatedServer(port, maxPlayers, rotation)
            {
                ServerName = ValueAfter(args, "servername") ?? ValueAfter(args, "name")
                    ?? Environment.MachineName
            };
            // Listed by default. A dedicated server exists to be found, and a
            // server that has to be told to advertise itself is a server
            // nobody finds -- so the flag is the one that opts out.
            if (!HasFlag(args, "nomaster") && !HasFlag(args, "unlisted"))
            {
                string masterHost = ValueAfter(args, "master") ?? NetMasterConfig.DefaultHost;
                int reportPort = NetMasterConfig.DefaultPort;
                string? reportPortValue = ValueAfter(args, "masterport");
                if (reportPortValue != null && Int32.TryParse(reportPortValue, out int parsedReport))
                {
                    reportPort = parsedReport;
                }
                server.Reporter = new MasterReporter(masterHost, reportPort);
                Console.WriteLine($"[server] listing on {masterHost}:{reportPort} "
                    + $"as \"{server.ServerName}\" (-nomaster to stay private)");
            }
            using var cancel = new System.Threading.CancellationTokenSource();
            using var signals = new ShutdownSignals();
            signals.OnShutdown(() =>
            {
                cancel.Cancel();
                server.Stop();
            });
            server.Run(cancel.Token);
            return true;
        }

#if MPHREAD_SERVER
        /// <summary>
        /// What this binary is for, for somebody who started it with no
        /// arguments -- which on Windows is anybody who double-clicked it.
        /// </summary>
        private static void ServerUsage()
        {
            string exe = System.IO.Path.GetFileNameWithoutExtension(
                Environment.ProcessPath) ?? "MphReadServer";
            Console.WriteLine();
            Console.WriteLine($"{Branding.Name} dedicated server. It needs no game files.");
            Console.WriteLine();
            Console.WriteLine($"  {exe} -server -port {NetConfig.DefaultPort} -players 8 "
                + "-servername \"My server\"");
            Console.WriteLine("      run a server. Maps come from maprotation.txt, written");
            Console.WriteLine("      beside this program on first run.");
            Console.WriteLine();
            Console.WriteLine($"  {exe} -masterserver -port {NetMasterConfig.DefaultPort}");
            Console.WriteLine("      run a server directory of your own.");
            Console.WriteLine();
            Console.WriteLine($"  {exe} -servers");
            Console.WriteLine("      list the servers that are up right now.");
            Console.WriteLine();
            Console.WriteLine("A server lists itself on " + NetMasterConfig.DefaultHost
                + " so players can find it;");
            Console.WriteLine("-nomaster keeps it off every list. See SERVER.txt.");
            Console.WriteLine();
            // Double-clicked, so this window is about to close with everything
            // above it still unread.
            if (OperatingSystem.IsWindows() && ConsoleWindow.OwnsItsConsole())
            {
                Console.WriteLine("Press any key to close this window...");
                Console.ReadKey();
            }
        }
#endif

        private static void ListServers(string masterHost, string? portValue)
        {
            int port = NetMasterConfig.DefaultPort;
            if (portValue != null && Int32.TryParse(portValue, out int parsed))
            {
                port = parsed;
            }
            Console.WriteLine($"[servers] asking {masterHost}:{port}");
            MasterListResult result = NetMasterClient.Query(masterHost, port);
            if (!result.Answered)
            {
                Console.WriteLine($"[servers] no answer from {masterHost}:{port} -- "
                    + "it may be down, or UDP may not reach it");
                return;
            }
            if (result.Servers.Count == 0)
            {
                Console.WriteLine("[servers] the directory is up and has nobody listed");
                return;
            }
            Console.WriteLine($"[servers] {result.Servers.Count} listed; asking each one");
            foreach (MasterListing listing in result.Servers)
            {
                // Directly, not through the directory: the round trip that
                // matters is this machine's, and the answer also proves the
                // server is reachable from here rather than only from there.
                ServerStatus status = NetStatus.Query(listing.Address, listing.Port,
                    allowJoinProbe: false);
                string name = status.ServerName.Length > 0
                    ? status.ServerName
                    : listing.ServerName.Length > 0 ? listing.ServerName : listing.Endpoint;
                if (!status.Online)
                {
                    Console.WriteLine($"  {name,-24} {listing.Endpoint,-26} did not answer");
                    continue;
                }
                string players = status.MaxPlayers > 0
                    ? $"{status.Players}/{status.MaxPlayers}"
                    : status.Players.ToString();
                string ping = status.Latency >= 0 ? $"{status.Latency} ms" : "-- ms";
                Console.WriteLine($"  {name,-24} {listing.Endpoint,-26} "
                    + $"{status.RoomKey,-20} {NetStatus.ModeName(status.Mode),-14} "
                    + $"{players,-6} {ping}");
            }
        }

        public static bool TryHandle(string[] args)
        {
            (int width, int height) = ParseSize(args);

            // Opt-in per-second report of what this process believes about a
            // networked session -- slot occupancy, scoreboard count, which
            // remote slots have state. The failure worth catching is not
            // visible on the wire: two correctly connected clients can each
            // hold a scene containing only themselves.
            // Display flags, for the paths that never open a launcher.
            if (HasFlag(args, "fullscreen") || HasFlag(args, "borderless"))
            {
                WindowMode.Startup = WindowStartMode.BorderlessFullscreen;
            }
            else if (HasFlag(args, "windowed"))
            {
                WindowMode.Startup = WindowStartMode.Windowed;
            }
            if (HasFlag(args, "nohelmet"))
            {
                // Both of them: the helmet is drawn as three layers and the
                // visor is one of them, so zeroing only HelmetOpacity leaves a
                // tinted pane over the view that reads as a bug rather than as
                // a setting. The settings window ties the two together for the
                // same reason.
                Features.HelmetOpacity = 0;
                Features.VisorOpacity = 0;
            }
            if (HasFlag(args, "netdebug"))
            {
                Network.NetDiagnostics.Enabled = true;
                Network.MapAudit.Diagnostic = true;
            }

            // Print the game's own tables as markdown, so the mechanics
            // documentation is generated from the data rather than kept by
            // hand and quietly going stale.
            if (HasFlag(args, "mechanics"))
            {
                Network.MechanicsDump.Run();
                return true;
            }

            // The multiplayer room list, one per line, so a shell loop can
            // walk every map without hard-coding the names.
            if (HasFlag(args, "rooms"))
            {
                foreach (string room in ThumbnailGenerator.MultiplayerRooms())
                {
                    Console.WriteLine(room);
                }
                return true;
            }

            // Load one room with a full house of players and report what it
            // contains and whether it survived.
            string? dpsTest = ValueAfter(args, "dpstest");
            if (dpsTest != null)
            {
                Hunter dpsHunter = Hunter.Sylux;
                string? dpsHunterValue = ValueAfter(args, "hunter");
                if (dpsHunterValue != null && Enum.TryParse(dpsHunterValue, ignoreCase: true, out Hunter parsedDpsHunter))
                {
                    dpsHunter = parsedDpsHunter;
                }
                BeamType dpsBeam = BeamType.ShockCoil;
                string? dpsBeamValue = ValueAfter(args, "weapon");
                if (dpsBeamValue != null && Enum.TryParse(dpsBeamValue, ignoreCase: true, out BeamType parsedDpsBeam))
                {
                    dpsBeam = parsedDpsBeam;
                }
                double dpsSeconds = 10;
                string? dpsSecondsValue = ValueAfter(args, "seconds");
                if (dpsSecondsValue != null && Double.TryParse(dpsSecondsValue,
                    System.Globalization.CultureInfo.InvariantCulture, out double parsedDpsSeconds))
                {
                    dpsSeconds = parsedDpsSeconds;
                }
                float dpsDistance = 2.2f;
                string? dpsDistanceValue = ValueAfter(args, "distance");
                if (dpsDistanceValue != null && Single.TryParse(dpsDistanceValue,
                    System.Globalization.CultureInfo.InvariantCulture, out float parsedDpsDistance))
                {
                    dpsDistance = parsedDpsDistance;
                }
                Environment.ExitCode = Network.WeaponDps.Run(dpsTest, dpsHunter, dpsBeam, dpsSeconds, dpsDistance);
                return true;
            }
            string? mapTest = ValueAfter(args, "maptest");
            if (mapTest != null)
            {
                int players = 8;
                string? playerValue = ValueAfter(args, "players");
                if (playerValue != null && Int32.TryParse(playerValue, out int parsed))
                {
                    players = parsed;
                }
                double seconds = 10;
                string? secondsValue = ValueAfter(args, "seconds");
                if (secondsValue != null && Double.TryParse(secondsValue,
                    System.Globalization.CultureInfo.InvariantCulture, out double parsedSeconds))
                {
                    seconds = parsedSeconds;
                }
                GameMode mapMode = GameMode.Battle;
                string? modeValue = ValueAfter(args, "mode");
                if (modeValue != null && Enum.TryParse(modeValue, ignoreCase: true, out GameMode parsedMode))
                {
                    mapMode = parsedMode;
                }
                Environment.ExitCode = Network.MapAudit.Run(mapTest, players, seconds, mapMode,
                    bots: HasFlag(args, "bots"));
                return true;
            }

            // Ask the directory to run a match and join it. The launcher's
            // "Online, no setup" in one command -- and the only way to host
            // from a machine with no launcher, which is every machine that is
            // not Windows.
            string? hostGame = ValueAfter(args, "hostgame");
            if (hostGame != null)
            {
                string masterHost = ValueAfter(args, "master") ?? NetMasterConfig.DefaultHost;
                int masterPort = NetMasterConfig.DefaultPort;
                string? masterPortValue = ValueAfter(args, "masterport");
                if (masterPortValue != null && Int32.TryParse(masterPortValue, out int parsedMaster))
                {
                    masterPort = parsedMaster;
                }
                GameMode hostMode = GameMode.Battle;
                string? hostModeValue = ValueAfter(args, "mode");
                if (hostModeValue != null
                    && Enum.TryParse(hostModeValue, ignoreCase: true, out GameMode parsedHostMode))
                {
                    hostMode = parsedHostMode;
                }
                string hostName = ParseName(args);
                Console.WriteLine($"[net] asking {masterHost}:{masterPort} to run {hostGame}");
                HostedGame game = NetMasterClient.RequestGame(masterHost, masterPort,
                    hostGame, hostMode, timeLimit: 7 * 60, pointGoal: 7,
                    maxPlayers: PlayerEntity.SlotCapacity, serverName: $"{hostName}'s game");
                if (!game.Started)
                {
                    Console.WriteLine($"[net] it would not: {game.Reason}");
                    Environment.ExitCode = 1;
                    return true;
                }
                Console.WriteLine($"[net] running on {game.Host}:{game.Port}; joining it");
                Network.NetConnectCommand.Run(game.Host, game.Port, hostName,
                    ParseHunter(args), ParseRecolor(args));
                return true;
            }

            // Join a server from the command line, with no launcher dialog.
            // The only way to start a client on a platform without WinForms,
            // and the only practical way to start two of them side by side --
            // which is the arrangement every bug in this feature has needed.
            string? connect = ValueAfter(args, "connect");
            if (connect != null)
            {
                Network.NetConnectCommand.Run(connect, ParsePort(args), ParseName(args),
                    ParseHunter(args), ParseRecolor(args));
                return true;
            }

            // The same client, running to a script and reporting what it saw.
            string? check = ValueAfter(args, "netcheck");
            if (check != null)
            {
                string? shots = ValueAfter(args, "shots");
                double seconds = 30;
                string? secondsValue = ValueAfter(args, "seconds");
                if (secondsValue != null && Double.TryParse(secondsValue,
                    System.Globalization.CultureInfo.InvariantCulture, out double parsedSeconds))
                {
                    seconds = parsedSeconds;
                }
                Environment.ExitCode = Network.NetCheckClient.Run(check, ParsePort(args),
                    ParseName(args), ParseHunter(args), seconds, shots, width, height);
                return true;
            }

            // Worker invocation: capture exactly one room and exit. This is
            // what ThumbnailBatch spawns, but it is equally usable by hand
            // to re-shoot a single map.
            string? single = ValueAfter(args, "thumbnail");
            if (single != null)
            {
                bool ok = ThumbnailCapture.CaptureRoom(single, width, height);
                Console.WriteLine(ok
                    ? $"[thumbnails] captured {single}"
                    : $"[thumbnails] failed {single}");
                return true;
            }

            if (HasFlag(args, "thumbnails"))
            {
                GenerateThumbnails(args, width, height);
                return true;
            }
            return false;
        }

        private static void GenerateThumbnails(string[] args, int width, int height)
        {
            bool force = HasFlag(args, "force");
            IReadOnlyList<string> rooms = force
                ? ThumbnailGenerator.MultiplayerRooms()
                : ThumbnailGenerator.MissingThumbnails();
            if (rooms.Count == 0)
            {
                Console.WriteLine("[thumbnails] all previews already present in "
                    + ThumbnailGenerator.CacheDirectory);
                Console.WriteLine("[thumbnails] pass -force to re-render them");
                return;
            }
            int jobs = ThumbnailBatch.DefaultParallelism;
            string? jobsValue = ValueAfter(args, "jobs");
            if (jobsValue != null && Int32.TryParse(jobsValue, out int parsedJobs))
            {
                jobs = parsedJobs;
            }
            Console.WriteLine($"[thumbnails] rendering {rooms.Count} preview(s) at "
                + $"{width}x{height}, {jobs} at a time");
            Console.WriteLine($"[thumbnails] output: {ThumbnailGenerator.CacheDirectory}");
            int written = ThumbnailBatch.Run(rooms, jobs, width, height);
            Console.WriteLine($"[thumbnails] done -- {written}/{rooms.Count} written");
        }

        private static (int Width, int Height) ParseSize(string[] args)
        {
            string? value = ValueAfter(args, "size");
            if (value != null)
            {
                string[] parts = value.Split('x', 'X');
                if (parts.Length == 2
                    && Int32.TryParse(parts[0], out int w)
                    && Int32.TryParse(parts[1], out int h)
                    && w > 0 && h > 0)
                {
                    return (w, h);
                }
                Console.WriteLine($"[thumbnails] ignoring -size {value} (expected e.g. 1920x1440)");
            }
            return (ThumbnailGenerator.ThumbnailWidth, ThumbnailGenerator.ThumbnailHeight);
        }

        private static int ParsePort(string[] args)
        {
            string? value = ValueAfter(args, "port");
            return value != null && Int32.TryParse(value, out int port) ? port : NetConfig.DefaultPort;
        }

        private static string ParseName(string[] args)
        {
            return ValueAfter(args, "name") ?? Environment.MachineName;
        }

        private static Hunter ParseHunter(string[] args)
        {
            string? value = ValueAfter(args, "hunter");
            return value != null && Enum.TryParse(value, ignoreCase: true, out Hunter hunter)
                ? hunter
                : Hunter.Samus;
        }

        private static int ParseRecolor(string[] args)
        {
            string? value = ValueAfter(args, "recolor");
            return value != null && Int32.TryParse(value, out int recolor) ? recolor : 0;
        }

        private static bool HasFlag(string[] args, string name)
        {
            return args.Any(a => a.TrimStart('-').Equals(name, StringComparison.OrdinalIgnoreCase));
        }

        private static string? ValueAfter(string[] args, string name)
        {
            for (int i = 0; i < args.Length - 1; i++)
            {
                if (args[i].TrimStart('-').Equals(name, StringComparison.OrdinalIgnoreCase))
                {
                    return args[i + 1];
                }
            }
            return null;
        }
    }
}
