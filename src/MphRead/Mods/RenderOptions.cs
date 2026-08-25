using System;
using System.Globalization;

namespace MphRead.Mods
{
    /// <summary>
    /// The knobs that trade picture for frame rate.
    ///
    /// The engine already had all of these; they were debug keys in the model
    /// viewer this grew out of (L for lighting, G for fog, F for filtering) and
    /// so were reachable only by someone who had read the help text, and never
    /// at all on a phone, which has no keyboard. They are the things worth
    /// turning off on a device that cannot keep 60, so they belong in the
    /// settings on every platform.
    ///
    /// <see cref="ResolutionScale"/> is the new one and the one that matters
    /// most. The scene is already drawn into an offscreen target and then put
    /// on the screen as one textured quad, so rendering that target smaller and
    /// letting the quad stretch it costs nothing to arrange and saves fill rate
    /// in proportion. The HUD, the helmet and the fade are drawn afterwards,
    /// straight to the window, so they stay sharp at any scale.
    /// </summary>
    public static class RenderOptions
    {
        /// <summary>
        /// Percent of the window the 3D scene is rendered at, 25 to 100.
        /// Halving it quarters the pixels.
        /// </summary>
        public static int ResolutionScale
        {
            get => _resolutionScale;
            set => _resolutionScale = Math.Clamp(value, MinScale, 100);
        }

        private static int _resolutionScale = 100;

        public const int MinScale = 25;

        /// <summary>Per-vertex lighting. Off is flatter and cheaper.</summary>
        public static bool Lighting { get; set; } = true;

        /// <summary>
        /// Cel shading: the lighting goes to hard steps and a dark line is
        /// drawn where a surface turns away from the camera.
        ///
        /// Not the same thing as turning lighting off, which only flattens
        /// everything to full brightness. This bands the *brightness* rather
        /// than each channel, so the textures keep their colours and it is the
        /// shading that goes to steps -- and the silhouette term is what makes
        /// it read as drawn rather than merely posterised.
        /// </summary>
        public static bool CelShading { get; set; }

        /// <summary>How many steps the shading is banded into, 2 to 8.</summary>
        public static int CelBands
        {
            get => _celBands;
            set => _celBands = Math.Clamp(value, 2, 8);
        }

        private static int _celBands = 4;

        /// <summary>How dark the silhouette line goes, 0 to 1.</summary>
        public static float CelEdge
        {
            get => _celEdge;
            set => _celEdge = Math.Clamp(value, 0, 1);
        }

        private static float _celEdge = 0.75f;

        /// <summary>Distance fog, where the room asks for it.</summary>
        public static bool Fog { get; set; } = true;

        /// <summary>
        /// Linear texture filtering. The DS had none, so off is both faster and
        /// what the game looked like.
        /// </summary>
        public static bool TextureFiltering { get; set; }

        /// <summary>Apply a scale to one dimension, never below one pixel.</summary>
        public static int Scaled(int pixels)
        {
            if (_resolutionScale >= 100)
            {
                return pixels;
            }
            return Math.Max(1, pixels * _resolutionScale / 100);
        }

        public static bool ParseOnOff(string? value, bool fallback)
        {
            if (value == null)
            {
                return fallback;
            }
            string text = value.Trim().ToLowerInvariant();
            if (text == "on" || text == "true" || text == "yes")
            {
                return true;
            }
            if (text == "off" || text == "false" || text == "no")
            {
                return false;
            }
            return fallback;
        }

        public static string OnOff(bool value) => value ? "on" : "off";

        public static int ParseScale(string? value, int fallback)
        {
            if (value != null && Int32.TryParse(value.Trim().TrimEnd('%'),
                NumberStyles.Integer, CultureInfo.InvariantCulture, out int percent))
            {
                return Math.Clamp(percent, MinScale, 100);
            }
            return fallback;
        }

        /// <summary>Unclamped; the properties do their own clamping.</summary>
        public static int ParseInt(string? value, int fallback)
        {
            if (value != null && Int32.TryParse(value.Trim().TrimEnd('%'),
                NumberStyles.Integer, CultureInfo.InvariantCulture, out int number))
            {
                return number;
            }
            return fallback;
        }
    }
}
