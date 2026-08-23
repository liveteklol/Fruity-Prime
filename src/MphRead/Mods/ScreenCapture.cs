using System;
using System.IO;
using OpenTK.Graphics.OpenGL;
using ReFuel.Stb;

namespace MphRead.Mods
{
    /// <summary>
    /// Saves what the scene just drew to a PNG.
    ///
    /// Reads the scene's own offscreen target rather than the window's back
    /// buffer. A hidden window has no usable back buffer under Mesa -- which
    /// is why headless captures came out black on Linux while the identical
    /// code produced correct thumbnails on Windows -- and even a visible one
    /// leaves the back buffer undefined once SwapBuffers has run. The
    /// offscreen target is a texture the scene owns, so it is valid either
    /// way.
    /// </summary>
    public static class ScreenCapture
    {
        public static bool Save(Scene scene, string path)
        {
            try
            {
                byte[]? pixels = scene.ReadSceneTarget(out int width, out int height);
                if (pixels == null || width <= 0 || height <= 0)
                {
                    return false;
                }
                // An all-black frame is a failure that writes a file. It
                // looks like a success in every log, the caller counts it,
                // and the player ends up with a full set of black pictures
                // and nothing saying why -- which is exactly how it was
                // reported. Refusing to save it turns a silent wrong answer
                // into a loud missing one.
                if (LitFraction(pixels) < MinLitFraction)
                {
                    Console.WriteLine($"[capture] {Path.GetFileName(path)} came out black "
                        + $"({LitFraction(pixels) * 100:0.00}% lit); not saving it. "
                        + $"The scene rendered nothing -- {DescribeContext()}");
                    return false;
                }
                string? directory = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(directory))
                {
                    Directory.CreateDirectory(directory);
                }
                using FileStream stream = File.Create(path);
                StbImage.FlipVerticallyOnSave = true;
                StbImage.WritePng<byte>(pixels, width, height, StbiImageFormat.Rgb, stream);
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[capture] could not save {path}: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// How much of a picture has to be lit before it counts as a picture.
        ///
        /// "Any lit pixel at all" is not enough, and was tried: a dead context
        /// still produces the odd non-zero pixel, so a retry eventually passed
        /// and the run reported a capture it had refused to save. A room seen
        /// from its own intro camera lights up far more than a hundredth of
        /// the frame.
        /// </summary>
        private const double MinLitFraction = 0.01;

        private static double LitFraction(byte[] pixels)
        {
            int lit = 0;
            int total = 0;
            for (int i = 0; i + 2 < pixels.Length; i += 3)
            {
                total++;
                if (pixels[i] > 8 || pixels[i + 1] > 8 || pixels[i + 2] > 8)
                {
                    lit++;
                }
            }
            return total == 0 ? 0 : lit / (double)total;
        }

        /// <summary>
        /// What the driver actually handed over, which is the one thing a
        /// black render never says on its own.
        ///
        /// This engine draws in immediate mode -- GL.Begin and friends -- and
        /// those do not exist in a core profile. A driver that answers a
        /// request for 3.2 Compatability with a core context therefore fails
        /// every draw call silently and renders black, with nothing in any log
        /// to say so. It is the documented cause on Mesa and it is not unique
        /// to Mesa.
        /// </summary>
        public static string DescribeContext()
        {
            try
            {
                string vendor = GL.GetString(StringName.Vendor) ?? "?";
                string renderer = GL.GetString(StringName.Renderer) ?? "?";
                string version = GL.GetString(StringName.Version) ?? "?";
                int mask = GL.GetInteger((GetPName)All.ContextProfileMask);
                string profile = (mask & (int)All.ContextCoreProfileBit) != 0
                    ? "CORE (immediate mode is unavailable, which renders everything black)"
                    : (mask & (int)All.ContextCompatibilityProfileBit) != 0 ? "compatibility" : "unreported";
                return $"GL {version}, profile {profile}, {vendor} / {renderer}";
            }
            catch (Exception ex)
            {
                return $"could not query the GL context: {ex.Message}";
            }
        }

        /// <summary>
        /// How much of the image is not the clear colour, as a fraction.
        ///
        /// A capture that is entirely black is the failure this whole path
        /// has to be able to detect: it looks like a successful render in
        /// every log, and only reading the pixels back says otherwise.
        /// </summary>
        public static double NonBlackFraction(Scene scene)
        {
            byte[]? pixels = scene.ReadSceneTarget(out int width, out int height);
            if (pixels == null || width <= 0)
            {
                return 0;
            }
            int lit = 0;
            for (int i = 0; i < pixels.Length; i += 3)
            {
                if (pixels[i] > 8 || pixels[i + 1] > 8 || pixels[i + 2] > 8)
                {
                    lit++;
                }
            }
            return lit / (double)(width * height);
        }
    }
}
