using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Threading;

namespace MphRead.Mods.Network
{
    public readonly struct ReceivedPacket
    {
        public readonly IPEndPoint Sender;
        public readonly byte[] Data;
        public readonly int Length;

        public ReceivedPacket(IPEndPoint sender, byte[] data, int length)
        {
            Sender = sender;
            Data = data;
            Length = length;
        }

        public PacketType Type => Length > 0 ? (PacketType)Data[0] : default;
        public ReadOnlySpan<byte> Payload => Data.AsSpan(1, Length - 1);
    }

    /// <summary>
    /// UDP transport on a dedicated worker thread.
    ///
    /// The game loop never touches a socket: it only drains a bounded
    /// concurrent queue. This mirrors the threading decision documented in
    /// ndsrecomp's wifi_net.cpp, and it matters for the same reason -- a
    /// blocking recv on the simulation thread turns a network hiccup into a
    /// frame hitch. Bounded, because an unbounded queue converts a flood
    /// into unbounded memory growth instead of dropped packets.
    /// </summary>
    public sealed class NetTransport : IDisposable
    {
        private readonly UdpClient _socket;
        private readonly Thread _worker;
        private readonly ConcurrentQueue<ReceivedPacket> _inbox = new();
        private readonly CancellationTokenSource _cancel = new();
        private volatile bool _running;
        private int _inboxCount;

        /// <summary>
        /// How many received packets may wait for the game loop.
        ///
        /// The number that matters is how long a frame can take while the
        /// queue still holds everything that arrived during it. Eight clients
        /// on one machine produce roughly two thousand packets a second
        /// between them, so 256 covers a 130 ms frame -- which sounds
        /// generous until eight copies of the engine share one CPU and a
        /// frame takes exactly that long. At that point the queue overflows,
        /// and what was dropped was a player's aim.
        /// </summary>
        private const int MaxQueuedPackets = 2048;

        /// <summary>
        /// Bytes the OS may hold before the worker thread gets to them. The
        /// default (64 KB) is the other half of the same problem: the worker
        /// sleeps a millisecond at a time, and a millisecond of eight clients
        /// is not the issue -- a scheduling hiccup of fifty is.
        /// </summary>
        private const int SocketBufferBytes = 1 << 20;

        public int LocalPort { get; }
        public long PacketsDropped { get; private set; }

        /// <summary>
        /// Packets dropped by every transport in this process, for the test
        /// harness: "the other client never saw me turn" has two very
        /// different causes, and this is what tells them apart.
        /// </summary>
        public static long TotalPacketsDropped;

        /// <summary>
        /// Packets sent by any transport in this process. Used by the
        /// headless client tests to prove the authority is actually
        /// publishing rather than silently doing nothing.
        /// </summary>
        public static long TotalPacketsSent;

        public NetTransport(int port)
        {
            _socket = new UdpClient(AddressFamily.InterNetwork);
            if (OperatingSystem.IsWindows())
            {
                // SIO_UDP_CONNRESET. Without it, a peer that vanishes makes
                // Windows raise ConnectionReset on the *next* receive, which
                // would kill the worker. The control code is Windows-only and
                // throws PlatformNotSupportedException elsewhere, so it is
                // guarded rather than swallowed.
                _socket.Client.IOControl(unchecked((int)0x9800000C), new byte[] { 0, 0, 0, 0 }, null);
            }
            try
            {
                _socket.Client.ReceiveBufferSize = SocketBufferBytes;
                _socket.Client.SendBufferSize = SocketBufferBytes;
            }
            catch (SocketException)
            {
                // A system that refuses the size keeps its default; the
                // session still works, it just tolerates less of a stall.
            }
            _socket.Client.Bind(new IPEndPoint(IPAddress.Any, port));
            LocalPort = ((IPEndPoint)_socket.Client.LocalEndPoint!).Port;
            _running = true;
            _worker = new Thread(ReceiveLoop)
            {
                IsBackground = true,
                Name = "MphRead net"
            };
            _worker.Start();
        }

        private void ReceiveLoop()
        {
            var any = new IPEndPoint(IPAddress.Any, 0);
            while (_running)
            {
                try
                {
                    if (_socket.Available == 0)
                    {
                        Thread.Sleep(1);
                        continue;
                    }
                    IPEndPoint sender = any;
                    byte[] data = _socket.Receive(ref sender);
                    if (data.Length == 0)
                    {
                        continue;
                    }
                    if (Volatile.Read(ref _inboxCount) >= MaxQueuedPackets)
                    {
                        // Drop rather than grow -- but drop the *oldest*, not
                        // this one. In a real-time protocol the newest packet
                        // is the one worth having: it carries where the player
                        // is aiming now. Discarding arrivals while a queue of
                        // stale ones drains is how a backlogged client ends up
                        // seeing a third of an opponent's turn.
                        if (_inbox.TryDequeue(out _))
                        {
                            Interlocked.Decrement(ref _inboxCount);
                        }
                        PacketsDropped++;
                        Interlocked.Increment(ref TotalPacketsDropped);
                    }
                    Interlocked.Increment(ref _inboxCount);
                    _inbox.Enqueue(new ReceivedPacket(sender, data, data.Length));
                }
                catch (SocketException)
                {
                    // Transient: an ICMP unreachable from a peer that left.
                    // Keep serving the peers that are still here.
                }
                catch (ObjectDisposedException)
                {
                    break;
                }
            }
        }

        /// <summary>Drain everything received since the last call. Called once per frame.</summary>
        public IEnumerable<ReceivedPacket> Drain()
        {
            while (_inbox.TryDequeue(out ReceivedPacket packet))
            {
                Interlocked.Decrement(ref _inboxCount);
                yield return packet;
            }
        }

        private static readonly IPEndPoint _playbackSender = new(IPAddress.Loopback, 0);

        /// <summary>
        /// Feeds a packet read back from a demo file into the same queue a
        /// real receive would have used, so <see cref="Drain"/> and
        /// everything downstream of it (<c>NetSession.Handle</c> and every
        /// packet-type handler) runs completely unchanged during playback --
        /// the sender endpoint is never checked by any of it, only the
        /// bytes. The socket this transport opened is never used for real
        /// traffic in that mode; it exists only because the constructor
        /// always binds one.
        /// </summary>
        public void EnqueueForPlayback(byte[] data, int length)
        {
            if (Volatile.Read(ref _inboxCount) >= MaxQueuedPackets)
            {
                return;
            }
            Interlocked.Increment(ref _inboxCount);
            _inbox.Enqueue(new ReceivedPacket(_playbackSender, data, length));
        }

        public void Send(IPEndPoint target, PacketType type, ReadOnlySpan<byte> payload)
        {
            Span<byte> buffer = stackalloc byte[NetConfig.MaxPacketSize];
            buffer[0] = (byte)type;
            payload.CopyTo(buffer[1..]);
            try
            {
                _socket.Send(buffer[..(payload.Length + 1)], target);
                Interlocked.Increment(ref TotalPacketsSent);
            }
            catch (SocketException)
            {
                // Same rationale as above: one unreachable peer must not
                // take down the session for everyone else.
            }
        }

        public void Dispose()
        {
            _running = false;
            _cancel.Cancel();
            _socket.Dispose();
            if (!_worker.Join(TimeSpan.FromSeconds(1)))
            {
                // Background thread; the process can exit regardless.
            }
            _cancel.Dispose();
        }
    }
}
