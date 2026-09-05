using System;
using System.Diagnostics;
using System.Globalization;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// A bad line, on purpose.
    ///
    /// Three of the faults reported from real matches -- a shot that misses
    /// and kills a second later, a player frozen on one screen and walking on
    /// another, movement that stutters and then does not -- are all invisible
    /// on loopback and hard to hold still on the internet, where the number
    /// changes while you are measuring it. The test rig can already put a
    /// proxy in front of a *local* server (<c>udp-lag.py</c>), but that
    /// measures a server on this machine: the interesting one is the Pi, over
    /// the real internet, with a chosen amount of latency added on top of
    /// whatever the wire is already doing.
    ///
    /// So the client carries it. <c>-netlag 200</c> adds 200 ms to the round
    /// trip -- half on the way out and half on the way back, because a
    /// one-sided delay is not what a link does and the halves are what decide
    /// whether an intent or a snapshot is the stale one. <c>-netloss 5</c>
    /// throws away one datagram in twenty on top of that.
    ///
    /// Never on by default, and every report that carries a measurement says
    /// so when it is: a run with this set is a reproduction, not a
    /// measurement of anything real.
    /// </summary>
    public static class NetLag
    {
        /// <summary>Milliseconds added to the round trip. 0 = off.</summary>
        public static int RoundTripMs { get; private set; }

        /// <summary>
        /// Milliseconds of random extra hold, on top of the fixed half. Only
        /// ever adds, so nothing is reordered by it -- what a queue of these
        /// reproduces is a link that breathes, not one that shuffles.
        /// </summary>
        public static int JitterMs { get; private set; }

        /// <summary>Datagrams thrown away, as a percentage, each way.</summary>
        public static double LossPercent { get; private set; }

        public static bool Active => RoundTripMs > 0 || LossPercent > 0;

        /// <summary>Half the round trip, in stopwatch ticks, plus this call's jitter.</summary>
        internal static long HoldTicks()
        {
            if (RoundTripMs <= 0 && JitterMs <= 0)
            {
                return 0;
            }
            double ms = RoundTripMs / 2.0;
            if (JitterMs > 0)
            {
                ms += _random.NextDouble() * JitterMs;
            }
            return (long)(ms * Stopwatch.Frequency / 1000.0);
        }

        /// <summary>Whether this datagram is one of the ones the line eats.</summary>
        internal static bool Drops()
        {
            return LossPercent > 0 && _random.NextDouble() * 100 < LossPercent;
        }

        // Not Rng: that one is the game's own LCG, its state is replicated
        // between machines, and drawing from it here would make a simulated
        // dropped packet change what every player's weapon does.
        private static readonly Random _random = new Random();

        /// <summary>
        /// "200", or "200:40" for two hundred milliseconds give or take
        /// forty. Returns false for anything else, so a typo is refused
        /// rather than quietly meaning zero.
        /// </summary>
        public static bool Configure(string? value)
        {
            if (String.IsNullOrWhiteSpace(value))
            {
                return false;
            }
            string[] parts = value.Split(':', ',');
            if (!Int32.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture,
                out int rtt) || rtt < 0 || rtt > 10000)
            {
                return false;
            }
            int jitter = 0;
            if (parts.Length > 1 && (!Int32.TryParse(parts[1], NumberStyles.Integer,
                CultureInfo.InvariantCulture, out jitter) || jitter < 0 || jitter > 5000))
            {
                return false;
            }
            RoundTripMs = rtt;
            JitterMs = jitter;
            return true;
        }

        public static bool ConfigureLoss(string? value)
        {
            if (!Double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture,
                out double percent) || percent < 0 || percent > 100)
            {
                return false;
            }
            LossPercent = percent;
            return true;
        }

        /// <summary>One line for a report, or null when the line is the real one.</summary>
        public static string? Describe()
        {
            if (!Active)
            {
                return null;
            }
            string text = RoundTripMs > 0
                ? $"+{RoundTripMs} ms round trip"
                : "no added latency";
            if (JitterMs > 0)
            {
                text += $" (jitter up to {JitterMs} ms each way)";
            }
            if (LossPercent > 0)
            {
                text += $", {LossPercent:0.##}% packet loss each way";
            }
            return text;
        }
    }
}
