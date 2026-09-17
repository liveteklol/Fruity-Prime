using System.Collections.Generic;
using MphRead.Mods.Network;

namespace MphRead.Mods.Chat
{
    public static class NetChat
    {
        private static readonly List<string> _history = new();
        public static IReadOnlyList<string> History => _history;
        public static int Revision { get; private set; }
        public static void Clear() { _history.Clear(); Revision++; }
        public static void Receive(ChatPacket packet)
        {
            Remember(packet);
            ChatBox.Receive(packet);
        }
        public static void Remember(ChatPacket packet)
        {
            if (_history.Count == 64) _history.RemoveAt(0);
            _history.Add(packet.Kind == ChatPacket.KindSystem ? packet.Text : $"{packet.Name}: {packet.Text}");
            Revision++;
        }
        public static void Send(string text)
        {
            if (string.IsNullOrWhiteSpace(text)) return;
            NetSession.SendChat(text);
        }
    }
}
