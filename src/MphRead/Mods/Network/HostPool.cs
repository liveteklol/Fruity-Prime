using System;
using System.Collections.Generic;
using System.Net;
using System.Threading;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// The matches a machine is running on somebody else's behalf, on ports
    /// of its own.
    ///
    /// Lifted out of <see cref="MasterServer"/>, which is where it used to
    /// live and where it did not belong. Starting a match on a box is a
    /// property of *being that box*, not of being the directory: a player who
    /// wants their game hosted in Tokyo is asking Tokyo, and only a process
    /// running there can spawn a server there. Tying it to the directory
    /// meant either one region could host, or every region had to run a
    /// second directory that listed nothing and existed purely to be asked --
    /// which is a component invented to satisfy a layering mistake.
    ///
    /// So an ordinary <see cref="DedicatedServer"/> carries one of these and
    /// answers <see cref="PacketType.HostRequest"/> on its own port. There is
    /// still exactly one directory in the world; it keeps the list, and every
    /// server on that list can be asked to open a game beside itself.
    ///
    /// The matches it starts are <c>RunsTheMatch = false</c>: a process has
    /// one static <c>NetSession</c> and can therefore simulate one match, and
    /// that one belongs to the server's own game. A hosted match is run by
    /// whichever client joins it first, exactly as it always was when the
    /// directory started them.
    /// </summary>
    public sealed class HostPool
    {
        private sealed class Hosted
        {
            public DedicatedServer Server = null!;
            public CancellationTokenSource Cancel = null!;
            public int Port;
            public string Name = "";
            /// <summary>Who asked, so a second request replaces it rather than piling up.</summary>
            public IPAddress Asker = IPAddress.None;
            public double StartedAt;
            /// <summary>When it last had anybody in it, so an abandoned game can be reaped.</summary>
            public double LastOccupied;
        }

        private readonly List<Hosted> _hosted = new();
        private readonly Dictionary<int, double> _cooling = new();
        private int _first;
        private int _last = -1;

        /// <summary>Where this pool's log lines go, so a server and a directory read alike.</summary>
        public Action<string> Log { get; init; } = _ => { };

        /// <summary>
        /// Announce a hosted match to a directory, if this machine reports to
        /// one. The games a *server* starts have to be findable the same way
        /// the server itself is.
        /// </summary>
        public Func<MasterReporter?>? ReporterFactory { get; init; }

        /// <summary>Told when a game stops, so its listing can be dropped at once.</summary>
        public Action<int>? OnStopped { get; init; }

        /// <summary>See <see cref="MasterServer"/> for why a freed port waits.</summary>
        private const double PortCooldownSeconds = 5;

        /// <summary>How long a game may sit empty before anybody has ever joined.</summary>
        private const double StartupSeconds = 180;

        /// <summary>And how long once it has been played and everyone has left.</summary>
        private const double EmptySeconds = 45;

        public void SetPorts(int first, int last)
        {
            _first = first;
            _last = last;
        }

        public bool CanHost => _last >= _first && _first > 0;

        public int Count => _hosted.Count;

        public string Describe() => CanHost
            ? $"can start games on ports {_first}-{_last} for players who cannot open one of their own"
            : "not starting games for anybody (no host port range)";

        /// <summary>
        /// Start a match for somebody, and say where it is listening.
        /// </summary>
        public HostReplyPacket Start(HostRequestPacket request, IPEndPoint asker, double now)
        {
            // One game per host. Somebody who quits and asks again is asking
            // for a *replacement*, not a second one -- and the old one is
            // sitting there empty, holding a port and a row on everybody's
            // list. Only if it is empty, though: two people behind one router
            // share an address, and the second of them starting a game must
            // not throw the first out of theirs.
            for (int i = _hosted.Count - 1; i >= 0; i--)
            {
                Hosted previous = _hosted[i];
                if (previous.Asker.Equals(asker.Address) && previous.Server.PeerCount == 0)
                {
                    Stop(previous, "the same player asked for another game");
                }
            }
            int port = FreePort(now);
            if (port < 0)
            {
                return new HostReplyPacket
                {
                    Reason = $"all {_last - _first + 1} game slots are busy"
                };
            }
            GameMode mode = Enum.IsDefined(typeof(GameMode), request.Mode)
                ? (GameMode)request.Mode
                : GameMode.Battle;
            string name = request.ServerName.Length > 0 ? request.ServerName : "Hosted game";
            // The asker's whole cycle when their launcher sent one, and the
            // single map when it did not.
            MapRotation rotation = request.Rotation != null && request.Rotation.Count > 0
                ? MapRotation.FromList(request.Rotation, request.TimeLimit, request.PointGoal)
                : MapRotation.SingleMatch(request.RoomKey, mode,
                    request.TimeLimit, request.PointGoal);
            Guid ownerToken = request.Policy == ServerSessionPolicy.Lobby ? new Guid(System.Security.Cryptography.RandomNumberGenerator.GetBytes(16)) : Guid.Empty;
            var server = new DedicatedServer(port,
                Math.Clamp((int)request.MaxPlayers, 2, MphRead.Entities.PlayerEntity.SlotCapacity),
                rotation)
            {
                ServerName = name,
                SessionPolicy = request.Policy, Format = request.Format, OwnerToken = ownerToken,
                Reporter = ReporterFactory?.Invoke(),
                // See the class note: one static NetSession per process, and
                // it belongs to this machine's own match.
                RunsTheMatch = false
            };
            server.SetSessionOptions(request.RequireReady, request.AllowJoinInProgress);
            var cancel = new CancellationTokenSource();
            var entry = new Hosted
            {
                Server = server,
                Cancel = cancel,
                Port = port,
                Name = name,
                Asker = asker.Address,
                StartedAt = now,
                LastOccupied = now
            };
            var thread = new Thread(() =>
            {
                try
                {
                    server.Run(cancel.Token);
                }
                catch (Exception ex)
                {
                    Log($"game on {port} stopped: {ex.Message}");
                }
            })
            {
                IsBackground = true,
                Name = $"FruityPrime hosted {port}"
            };
            thread.Start();
            // The socket binds a few milliseconds in, and the asker is about
            // to send a Hello at it. Its own join retries for several seconds,
            // so this only avoids the first one going into nothing.
            for (int i = 0; i < 50 && !server.Listening; i++)
            {
                Thread.Sleep(10);
            }
            if (!server.Listening)
            {
                cancel.Cancel();
                server.Stop();
                return new HostReplyPacket { Reason = $"could not listen on port {port}" };
            }
            _hosted.Add(entry);
            Log($"started \"{name}\" on port {port} for {asker.Address} "
                + $"({request.RoomKey}, {mode}, {rotation.Entries.Count} map(s))");
            return new HostReplyPacket { Started = true, Port = (ushort)port, Reason = "", OwnerToken = ownerToken };
        }

        private int FreePort(double now)
        {
            for (int port = _first; port <= _last; port++)
            {
                bool taken = false;
                for (int i = 0; i < _hosted.Count; i++)
                {
                    if (_hosted[i].Port == port)
                    {
                        taken = true;
                        break;
                    }
                }
                if (taken)
                {
                    continue;
                }
                if (_cooling.TryGetValue(port, out double freedAt))
                {
                    if (now - freedAt < PortCooldownSeconds)
                    {
                        continue;
                    }
                    _cooling.Remove(port);
                }
                return port;
            }
            return -1;
        }

        /// <summary>
        /// Shut down games nobody is playing. Without this a machine left
        /// running for a week has every port allocated to a match that ended
        /// on Tuesday.
        /// </summary>
        public void Reap(double now)
        {
            for (int i = _hosted.Count - 1; i >= 0; i--)
            {
                Hosted entry = _hosted[i];
                if (entry.Server.PeerCount > 0)
                {
                    entry.LastOccupied = now;
                    continue;
                }
                bool played = entry.Server.EverOccupied;
                double grace = played ? EmptySeconds : StartupSeconds;
                if (now - entry.LastOccupied > grace)
                {
                    Stop(entry, played ? "everyone left" : "nobody joined", now);
                }
            }
        }

        public void StopAll(string why)
        {
            for (int i = _hosted.Count - 1; i >= 0; i--)
            {
                Stop(_hosted[i], why);
            }
        }

        private void Stop(Hosted entry, string why, double now = 0)
        {
            Log($"stopping \"{entry.Name}\" on port {entry.Port}: {why}");
            entry.Cancel.Cancel();
            entry.Server.Stop();
            // Wait for the socket to actually come back before the port is
            // considered free. Handing it out while it is still bound made the
            // next game fail to start with "could not listen".
            for (int i = 0; i < 100 && entry.Server.Listening; i++)
            {
                Thread.Sleep(10);
            }
            entry.Cancel.Dispose();
            _hosted.Remove(entry);
            _cooling[entry.Port] = now;
            OnStopped?.Invoke(entry.Port);
        }
    }
}
