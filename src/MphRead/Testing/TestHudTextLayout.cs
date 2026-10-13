using System;
using MphRead.Mods.Localization;

namespace MphRead.Testing
{
    internal static class TestHudTextLayout
    {
        internal static int Run()
        {
            bool passed = true;
            passed &= Check("A\u0082\u00A0B".AsSpan(), new[] { 65, 160, 66 },
                "forward traversal decodes a Japanese packed glyph and advances past both bytes");
            passed &= Check("\u0082\n\u00A0".AsSpan(0, 1), new[] { 130 },
                "a glyph lead at the line boundary does not consume the next line");
            passed &= Check("\u0082".AsSpan(), new[] { 130 },
                "a trailing multibyte lead remains a bounded single-unit glyph");
            passed &= Check(HudMessageLanguage.IsStaleLocalizedMessage(Language.Japanese, Language.English)
                && !HudMessageLanguage.IsStaleLocalizedMessage(Language.Japanese, Language.Japanese)
                && !HudMessageLanguage.IsStaleLocalizedMessage(null, Language.English),
                "only a queued localized message from the previous language is discarded");
            passed &= Check(
                HudMessageLanguage.CaptureSourceLanguage(null, Language.Japanese) == Language.Japanese
                && HudMessageLanguage.CaptureSourceLanguage(Language.English, Language.Japanese) == Language.English,
                "queued text without an explicit locale captures the current language");
            return passed ? 0 : 1;
        }

        private static bool Check(ReadOnlySpan<char> text, int[] expected, string name)
        {
            int[] actual = new int[expected.Length];
            int count = 0;
            for (int i = 0; i < text.Length; i++)
            {
                HudTextLayout.ReadForwardGlyph(text, ref i, text.Length,
                    out int value, out _);
                if (count >= actual.Length)
                {
                    return Fail(name);
                }
                actual[count++] = value;
            }
            if (count != expected.Length)
            {
                return Fail(name);
            }
            for (int i = 0; i < count; i++)
            {
                if (actual[i] != expected[i])
                {
                    return Fail(name);
                }
            }
            return true;
        }

        private static bool Check(bool condition, string name)
        {
            return condition || Fail(name);
        }

        private static bool Fail(string name)
        {
            Console.Error.WriteLine($"FAIL {name}");
            return false;
        }
    }
}
