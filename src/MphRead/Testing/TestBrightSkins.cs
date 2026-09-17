using System;
using System.IO;
using MphRead.Entities;
using MphRead.Mods;
using MphRead.Mods.Launcher;
using MphRead.Mods.Render;
using OpenTK.Mathematics;

namespace MphRead.Testing
{
    public static class TestBrightSkins
    {
        public static int Run(bool assets = false)
        {
            int failures = 0;
            void Check(bool success, string label)
            {
                Console.WriteLine($"BRIGHTSKINS {(success ? "PASS" : "FAIL")} {label}");
                if (!success) failures++;
            }

            bool Applies(bool enabled = true, bool multiplayer = true, bool main = false,
                int health = 100, PlayerFlags2 flags = 0, float alpha = 1, bool status = false)
                => BrightSkins.ShouldApply(enabled, multiplayer, main, health, flags, alpha, status);

            Check(Applies(), "remote multiplayer body (also while spectating)");
            Check(!Applies(enabled: false), "disabled by default");
            Check(!Applies(multiplayer: false), "single-player excluded");
            Check(!Applies(main: true), "local player and equipment excluded");
            Check(!Applies(health: 0) && Applies(health: 1), "death and respawn");
            Check(!Applies(flags: PlayerFlags2.Cloaking), "cloak activation before alpha fades");
            Check(!Applies(alpha: 0.99f) && !Applies(alpha: 1 / 31f), "passive cloak and expiration fades");
            Check(!Applies(status: true), "damage palette, Double Damage and target-alpha precedence");
            Check(Applies(flags: PlayerFlags2.AltAttack), "alt-form attack remains eligible");

            var surface = new Vector4(0.2f, 0.7f, 0.95f, 1);
            Check(BrightSkins.ForMaterial(surface, textured: true, alpha: 0.2f) == surface,
                "textured override preserves existing material and cutout alpha");
            Check(BrightSkins.ForMaterial(surface, textured: false, alpha: 0.2f) == new Vector4(surface.Xyz, 0.2f),
                "untextured override carries material alpha");
            Check(BrightSkins.ForMaterial(null, textured: false, alpha: 0.2f) == null, "no override stays absent");

            // Mirror the fragment shader's two alpha rules, using a synthetic
            // translucent material so this needs no hunter assets or GL context.
            bool alphaPreserved = true;
            foreach (bool textured in new[] { false, true })
            foreach (bool showTextures in new[] { false, true })
            foreach (float materialAlpha in new[] { 0f, 0.2f, 1f })
            {
                Vector4 tint = BrightSkins.ForMaterial(surface, textured, materialAlpha, showTextures)!.Value;
                const float texelAlpha = 0.5f;
                bool samplesTexture = textured && showTextures;
                float resultAlpha = samplesTexture ? materialAlpha * texelAlpha * tint.W : tint.W;
                float expectedAlpha = samplesTexture ? materialAlpha * texelAlpha : materialAlpha;
                alphaPreserved &= MathF.Abs(resultAlpha - expectedAlpha) < 0.00001f;
            }
            Check(alphaPreserved, "material alpha and cutouts survive the viewer texture toggle");

            bool colorsValid = true;
            for (int red = 0; red <= 255; red += 17)
            for (int green = 0; green <= 255; green += 17)
            for (int blue = 0; blue <= 255; blue += 17)
            {
                Vector3 rgb = BrightSkins.NormalizeBright(new Vector3(red, green, blue) / 255f);
                float luminance = Vector3.Dot(rgb, new Vector3(0.2126f, 0.7152f, 0.0722f));
                colorsValid &= rgb.X >= 0 && rgb.Y >= 0 && rgb.Z >= 0
                    && rgb.X <= 1 && rgb.Y <= 1 && rgb.Z <= 1 && luminance >= 0.44999f;
            }
            Check(colorsValid, "4096 colors preserve bounds and luminance floor, including black and saturated blue");
            Check(BrightSkins.NormalizeBright(new Vector3(0.1f, 0.2f, 0.3f))
                == BrightSkins.NormalizeBright(new Vector3(0.2f, 0.4f, 0.6f)), "brightness-independent hue identity");
            for (int team = 0; team < Metadata.TeamColors.Length; team++)
            {
                Vector4 expected = new Vector4(BrightSkins.NormalizeBright(Metadata.TeamColors[team] / 31f), 1);
                Check(BrightSkins.GetTeamColor(team) == expected, $"central team definition {team}");
            }
            Check(BrightSkins.GetTeamColor(-1) == BrightSkins.GetTeamColor(Int32.MaxValue), "invalid team fallback");

            string originalDirectory = LauncherPrefs.Directory;
            bool originalEnabled = RenderOptions.BrightSkins;
            string directory = Path.Combine(Path.GetTempPath(), "fruity-brightskins-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            string path = Path.Combine(directory, "launcher.txt");
            try
            {
                LauncherPrefs.Directory = directory;
                RenderOptions.BrightSkins = false;
                LauncherPrefs.Load();
                Check(!RenderOptions.BrightSkins, "missing preferences stay off");
                File.WriteAllText(path, "# existing preferences without the new key\n");
                LauncherPrefs.Load();
                Check(!RenderOptions.BrightSkins, "legacy preferences stay off");
                File.WriteAllText(path, "bright_skins=invalid\n");
                LauncherPrefs.Load();
                Check(!RenderOptions.BrightSkins, "invalid preference stays off");
                RenderOptions.BrightSkins = true;
                LauncherPrefs.Save();
                RenderOptions.BrightSkins = false;
                LauncherPrefs.Load();
                Check(RenderOptions.BrightSkins, "enabled preference round trip");
                RenderOptions.BrightSkins = false;
                LauncherPrefs.Save();
                RenderOptions.BrightSkins = true;
                LauncherPrefs.Load();
                Check(!RenderOptions.BrightSkins, "disabled preference round trip");
            }
            finally
            {
                LauncherPrefs.Directory = originalDirectory;
                RenderOptions.BrightSkins = originalEnabled;
                File.Delete(path);
                Directory.Delete(directory);
            }

            if (assets)
            {
                Hunter[] hunters = { Hunter.Samus, Hunter.Kanden, Hunter.Spire, Hunter.Trace,
                    Hunter.Noxus, Hunter.Sylux, Hunter.Weavel };
                foreach (Hunter hunter in hunters)
                {
                    var colors = new Vector4[4];
                    bool distinct = true;
                    for (int recolor = 0; recolor < colors.Length; recolor++)
                    {
                        colors[recolor] = BrightSkins.GetSuitColor(hunter, recolor);
                        Check(colors[recolor] == BrightSkins.GetSuitColor(hunter, recolor), $"{hunter} suit {recolor} deterministic");
                        for (int previous = 0; previous < recolor; previous++)
                        {
                            distinct &= (colors[previous] - colors[recolor]).Length > 0.01f;
                        }
                    }
                    Check(distinct, $"{hunter} four resolved suits stay distinct");
                    Check(HunterSuits.Color(hunter, 4) != HunterSuits.Color(hunter, 5), $"{hunter} team palettes sampled");
                    Check(BrightSkins.GetSuitColor(hunter, -1) == BrightSkins.GetSuitColor(hunter, Int32.MaxValue),
                        $"{hunter} invalid recolor fallback");
                }
            }
            Console.WriteLine($"BRIGHTSKINS failures={failures}");
            return failures == 0 ? 0 : 1;
        }
    }
}
