using System;
using Android.App;
using MphRead.Mods.Update;

namespace MphRead.Droid
{
    /// <summary>
    /// The phone's half of <see cref="IUpdateInstaller"/>: everything real is
    /// in <see cref="ApkInstaller"/>, and this is the shape the front screen
    /// asks for it in.
    ///
    /// Installed in <c>MainActivity.CustomizeAppBuilder</c>, before the front
    /// screen is built, because the screen reads whether it exists while it is
    /// laying its update entry out.
    /// </summary>
    internal sealed class AndroidUpdateInstaller : IUpdateInstaller
    {
        private readonly Activity _activity;
        private string _staged = "";

        public AndroidUpdateInstaller(Activity activity)
        {
            _activity = activity;
        }

        public bool Allowed => ApkInstaller.Allowed(_activity);

        public bool RequestPermission() => ApkInstaller.RequestPermission(_activity);

        /// <summary>
        /// False: the system kills this app as it replaces it. Quitting on our
        /// own would take the screen away before the player has even answered
        /// the install dialog.
        /// </summary>
        public bool ExitAfterInstall => false;

        public Action<bool, string>? Finished
        {
            get => ApkInstaller.Finished;
            set => ApkInstaller.Finished = value;
        }

        public bool Prepare(UpdateInfo update, Action<float>? progress, out string error)
        {
            _staged = ApkInstaller.StagingPath(_activity);
            if (!UpdateDownload.Fetch(update.AssetUrl, _staged, update.AssetSize, progress))
            {
                error = UpdateDownload.LastError ?? "the download failed";
                return false;
            }
            // Before the dialog rather than after it, because Android's own
            // refusal for this is the bare words "App not installed".
            if (!ApkInstaller.SameSigner(_activity, _staged, out string? mismatch))
            {
                error = mismatch ?? "that package cannot be installed over this one";
                return false;
            }
            error = "";
            return true;
        }

        public bool Install(out string error) =>
            ApkInstaller.Commit(_activity, _staged, out error);
    }
}
