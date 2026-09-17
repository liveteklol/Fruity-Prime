using System;
using MphRead.Entities;
using MphRead.Hud;
using MphRead.Mods.Chat;
using MphRead.Mods.Network;
using OpenTK.Mathematics;

namespace MphRead.Mods.Replay
{
    internal static class ReplayHud
    {
        private static Scene? _scene;
        private static HudObjectInstance? _font;
        private static readonly bool[] Markers = new bool[154];
        private static long _textAt;
        private static string _status = "", _watching = "";
        private static ReplayState _state;
        internal static void Reset() { _scene = null; _font = null; }
        private static readonly ColorRgba[] Palette = { new ColorRgba(), new ColorRgba(255, 255, 255, 255) };
        public static string Time(uint frame) => $"{frame / 3600:00}:{frame / 60 % 60:00}";
        public static void Draw(Scene scene)
        {
            if (!DemoPlayback.IsActive) return;
            if (_scene != scene)
            {
                _scene = scene;
                _font = new HudObjectInstance(width: ChatFont.Cell, height: ChatFont.Cell);
                _font.SetPaletteData(Palette, scene);
                _font.SetCharacterData(ChatFont.Pixels, scene);
                _font.Enabled = true;
                _textAt = 0;
                Array.Clear(Markers);
                foreach (ReplayEvent marker in DemoPlayback.Events)
                {
                    if (marker.Type is not (ReplayEventType.Kill or ReplayEventType.PlayerDeath or ReplayEventType.MatchEnded)) continue;
                    int at = ReplayController.DurationFrames == 0 ? 0 : (int)(153UL * marker.Frame / ReplayController.DurationFrames);
                    Markers[Math.Clamp(at, 0, 153)] = true;
                }
            }
            float alpha = ReplayController.State == ReplayState.Playing
                && Environment.TickCount64 - ReplayController.LastInteraction > 4000 ? 0.45f : 1;
            scene.DrawHudFlatBox(46, 159, 210, 188, new Vector4(0, 0, 0, alpha * 0.7f));
            if (Environment.TickCount64 - _textAt >= 100 || _state != ReplayController.State)
            {
                _state = ReplayController.State;
                _textAt = Environment.TickCount64;
                string state = _state == ReplayState.Ended ? "Replay finished"
                    : _state == ReplayState.Error ? "Replay damaged" : _state.ToString();
                _status = $"{state}   {Time(ReplayController.CurrentFrame)} / {Time(ReplayController.DurationFrames)}   {ReplayController.PlaybackRate:0.##}x";
                int slot = PlayerEntity.MainPlayerIndex;
                _watching = ReplayCamera.Mode is ReplayCameraMode.Chase or ReplayCameraMode.Orbit ? $"{ReplayCamera.Mode}: {GameState.Nicknames[Math.Clamp(slot, 0, GameState.Nicknames.Length - 1)]}" : SpectatorMode.FreeCamera ? "Free camera" : $"Watching: {(slot >= 0 && slot < GameState.Nicknames.Length ? GameState.Nicknames[slot] : "")}";
            }
            Text(scene, 49, 163, _status, alpha);
            float progress = ReplayController.DurationFrames == 0 ? 0 : ReplayController.CurrentFrame / (float)ReplayController.DurationFrames;
            scene.DrawHudFlatBox(51, 173, 205, 175, new Vector4(0.4f, 0.4f, 0.4f, alpha));
            scene.DrawHudFlatBox(51, 173, 51 + 154 * progress, 175, new Vector4(0.4f, 1, 0.6f, alpha));
            for (int i = 0; i < Markers.Length; i++)
                if (Markers[i]) scene.DrawHudFlatBox(51 + i, 172, 52 + i, 176, new Vector4(1, 0.65f, 0.3f, alpha));
            Text(scene, 49, 178, _watching, alpha);
            if (ReplayController.AtEnd || ReplayController.State == ReplayState.Error)
                Text(scene, 49, 184, "Home: restart   Esc: exit replay", alpha);
        }
        private static void Text(Scene scene, float x, float y, string text, float alpha)
        {
            var font = _font!;
            font.Alpha = alpha;
            foreach (char ch in text)
            {
                int index = ChatFont.Index(ch);
                if (index < 0) continue;
                font.PositionX = x / 256;
                font.PositionY = y / 192;
                font.SetData(index, new ColorRgba(210, 255, 225, 255), scene);
                scene.DrawHudObject(font, mode: 1, scale: 0.55f);
                x += ChatFont.Widths[index] * 0.55f;
                if (x > 207) break;
            }
        }
    }
}
