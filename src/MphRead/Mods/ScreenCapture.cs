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
