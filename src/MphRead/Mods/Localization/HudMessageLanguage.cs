namespace MphRead.Mods.Localization
{
    internal static class HudMessageLanguage
    {
        internal static Language CaptureSourceLanguage(Language? sourceLanguage, Language currentLanguage)
        {
            return sourceLanguage ?? currentLanguage;
        }

        internal static bool IsStaleLocalizedMessage(Language? sourceLanguage, Language currentLanguage)
        {
            return sourceLanguage.HasValue && sourceLanguage.Value != currentLanguage;
        }
    }
}
