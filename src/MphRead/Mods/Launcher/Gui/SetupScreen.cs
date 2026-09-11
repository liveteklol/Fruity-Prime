using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// The one thing a fresh install needs before anything else can happen:
    /// the player's own cartridge dump.
    ///
    /// Shown *instead of* the front screen while there is nothing to load,
    /// rather than as one entry among five that are all refused. A menu whose
    /// entries do nothing is a program that looks broken; one screen asking
    /// for one file is a program waiting for you.
    /// </summary>
    internal sealed class SetupScreen : UserControl
    {
        /// <summary>Raised when the files are there, or when the player backed out.</summary>
        public event EventHandler? Closed;

        private readonly Note _log = new("");
        private readonly ProgressRow _progress = new();
        private readonly UiMark _choose;
        private readonly UiMark _back;
        private readonly UiWord _previews;

        public SetupScreen()
        {
            Background = Brushes.Transparent;
            Focusable = true;

            var body = new StackPanel { Spacing = 8 };
            body.Children.Add(new Note(Mods.Branding.Name
                + " needs your own Metroid Prime Hunters cartridge dump. It unpacks what "
                + "it needs next to this program and leaves the file alone. No game data "
                + "is included in this download, and none is downloaded."));
            if (GameFiles.InProcessSetup)
            {
                body.Children.Add(new Note("The unpacked files land in " + GameFiles.Root
                    + " -- this device's own folder for the app, which shows up over USB "
                    + "under Android/data. Files already copied there are found without "
                    + "picking anything.", GuiTheme.TextDim));
            }
            // Previews are rendered here, from the files that are here. A run
            // can be interrupted and files can arrive after one, so asking for
            // the missing ones has to be possible without setting up again.
            _previews = new UiWord("Render map previews", 15, colour: GuiTheme.TextDim);
            _previews.Click += async (_, _) => await RenderPreviews();
            body.Children.Add(_previews);
            _progress.IsVisible = false;
            body.Children.Add(_progress);
            body.Children.Add(new ScrollViewer
            {
                Height = 160,
                Content = _log,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            });

            _choose = new UiMark(UiMark.Shape.Accept, "choose your .nds file")
            {
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(0, 0, UiLayout.CornerX, UiLayout.CornerY)
            };
            _choose.Click += async (_, _) => await ChooseRom();
            _back = new UiMark(UiMark.Shape.Cancel, "back")
            {
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(UiLayout.CornerX, 0, 0, UiLayout.CornerY),
                // Nothing to go back to until there is something to play.
                IsVisible = GameFiles.Ready
            };
            _back.Click += (_, _) => Closed?.Invoke(this, EventArgs.Empty);

            var holder = new ScrollViewer
            {
                Content = body,
                Margin = UiLayout.BodyMargin,
                MaxWidth = 620,
                HorizontalAlignment = HorizontalAlignment.Left,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            };
            Panel root = UiLayout.Backdrop();
            root.Children.Add(holder);
            root.Children.Add(UiLayout.Heading("game files"));
            root.Children.Add(_back);
            root.Children.Add(_choose);
            Content = root;
            RefreshPreviewEntry();
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            Dispatcher.UIThread.Post(() => _choose.Focus(), DispatcherPriority.Background);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape && _back.IsVisible)
            {
                Closed?.Invoke(this, EventArgs.Empty);
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }

        private async Task ChooseRom()
        {
            TopLevel? top = TopLevel.GetTopLevel(this);
            if (top == null)
            {
                return;
            }
            var options = new FilePickerOpenOptions
            {
                Title = "Your Metroid Prime Hunters cartridge dump",
                AllowMultiple = false
            };
            if (!OperatingSystem.IsAndroid())
            {
                // Patterns are what Windows, Linux and the browser filter on.
                // Android filters by MIME type, and .nds has none -- inventing
                // one there produces a picker in which every file is refused.
                options.FileTypeFilter = new[]
                {
                    new FilePickerFileType("Nintendo DS ROM") { Patterns = new[] { "*.nds" } },
                    new FilePickerFileType("Every file") { Patterns = new[] { "*" } }
                };
            }
            IReadOnlyList<IStorageFile> picked =
                await top.StorageProvider.OpenFilePickerAsync(options);
            if (picked.Count == 0)
            {
                return;
            }
            _choose.IsEnabled = false;
            _choose.Label = "working...";
            _log.Text = "";
            var progress = new SetupProgress();
            _progress.IsVisible = true;
            _progress.Set(0, "Starting");
            string? path = picked[0].TryGetLocalPath();
            string? scratch = null;
            if (path == null)
            {
                // Android hands back a content:// document with no path behind
                // it. Copying is the only way to give the extractor a file, and
                // it is the player's own cartridge dump, so it is copied into
                // the app's directory and deleted afterwards.
                _log.Text = "Copying the file onto this device...";
                try
                {
                    scratch = Path.Combine(GameFiles.Root, "picked.nds");
                    await using (Stream source = await picked[0].OpenReadAsync())
                    await using (var target = File.Create(scratch))
                    {
                        await source.CopyToAsync(target);
                    }
                    path = scratch;
                }
                catch (Exception ex)
                {
                    _log.Text = $"The file could not be read: {ex.Message}";
                    Ready();
                    return;
                }
            }
            // Extraction takes minutes. Off the UI thread, or the screen stops
            // answering at the exact moment it is doing the one thing a fresh
            // install needs.
            bool ok = await Task.Run(() => GameFiles.RunSetup(path, line =>
                Dispatcher.UIThread.Post(() =>
                {
                    _log.Text = Tail(_log.Text, line);
                    if (progress.Observe(line))
                    {
                        _progress.Set(progress.Fraction, progress.Stage);
                    }
                })));
            if (scratch != null)
            {
                try
                {
                    File.Delete(scratch);
                }
                catch (IOException)
                {
                    // A copy left behind is untidy, not a failure worth saying.
                }
            }
            if (ok)
            {
                await RenderMissing(progress);
            }
            progress.Finish(ok);
            _progress.Set(progress.Fraction, progress.Stage);
            Ready();
            _log.Text = Tail(_log.Text, ok ? "Ready to play." : "Setup did not finish.");
            RefreshPreviewEntry();
            if (ok)
            {
                _progress.IsVisible = false;
                _back.IsVisible = true;
                Closed?.Invoke(this, EventArgs.Empty);
            }
        }

        private void Ready()
        {
            _choose.IsEnabled = true;
            _choose.Label = "choose your .nds file";
        }

        private async Task RenderPreviews()
        {
            _previews.IsEnabled = false;
            _previews.Text = "Rendering...";
            await RenderMissing(null);
            _previews.IsEnabled = true;
            _previews.Text = "Render map previews";
            RefreshPreviewEntry();
        }

        /// <summary>
        /// The pictures are made here, on this machine, from the files that
        /// were just unpacked. Nothing is downloaded and no picture ships with
        /// the program.
        /// </summary>
        private async Task RenderMissing(SetupProgress? progress)
        {
            if (!ThumbnailHost.CanRender)
            {
                return;
            }
            _log.Text = Tail(_log.Text, "Rendering map previews...");
            await ThumbnailHost.RenderMissingAsync(line => Dispatcher.UIThread.Post(() =>
            {
                _log.Text = Tail(_log.Text, line);
                if (progress != null && progress.Observe(line))
                {
                    _progress.Set(progress.Fraction, progress.Stage);
                }
            }));
        }

        /// <summary>How many previews are still to render, or nothing to say.</summary>
        private void RefreshPreviewEntry()
        {
            if (!GameFiles.Ready || !ThumbnailHost.CanRender)
            {
                _previews.IsVisible = false;
                return;
            }
            int missing = ThumbnailGenerator.MissingThumbnails().Count;
            _previews.IsVisible = missing > 0;
            _previews.IsEnabled = missing > 0;
        }

        /// <summary>Keep the last few lines; the extraction prints hundreds.</summary>
        private static string Tail(string? existing, string line)
        {
            string[] lines = ((existing ?? "") + "\n" + line)
                .Split('\n', StringSplitOptions.RemoveEmptyEntries);
            return String.Join("\n", lines[Math.Max(0, lines.Length - 8)..]);
        }
    }
}
