using System;

namespace MphRead.Mods.Localization
{
    internal static class HudTextLayout
    {
        internal static void ReadForwardGlyph(ReadOnlySpan<char> text, ref int index, int end,
            out int value, out int original)
        {
            original = text[index];
            value = original;
            if ((original & 0x80) != 0 && index + 1 < end)
            {
                char trail = text[++index];
                value = trail & 0x3F | ((original & 0x1F) << 6);
            }
        }
    }
}
