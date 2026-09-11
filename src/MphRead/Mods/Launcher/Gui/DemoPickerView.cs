using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// The recordings this machine made, to pick one from.
    ///
    /// Its own list rather than the system file picker, for the reason
    /// <see cref="DemoLibrary"/> gives: on Android the folder they are written
    /// to cannot be reached through that picker at all. The picker is still
    /// here, as the last entry, for the demo that came from somewhere else.
    ///
    /// A view rather than a window, for <see cref="MapPickerView"/>'s reason:
    /// the desktop and Android both show it over the front screen, and neither
    /// should have a copy of the list.
    /// </summary>
    internal sealed class DemoPickerView : UserControl
    {
        /// <summary>The recording that was chosen, or null when it was not.</summary>
        public string? Path { get; private set; }

        /// <summary>Whether the player asked for the system file picker instead.</summary>
        public bool ImportRequested { get; private set; }

        /// <summary>Raised when this view is finished with, picked or not.</summary>
        public event EventHandler? Closed;

        private MenuEntry? _first;

        public DemoPickerView(IReadOnlyList<DemoRecording> demos, string directory)
        {
            Background = Brushes.Transparent;
            Focusable = true;

            var list = new StackPanel { Spacing = 2 };
            foreach (DemoRecording demo in demos)
            {
                var entry = new MenuEntry(
                    demo.Room.Length > 0 ? demo.Room : demo.FileName,
                    DemoLibrary.Describe(demo),
                    titleSize: 15);
                string path = demo.Path;
                entry.Click += (_, _) =>
                {
                    Path = path;
                    Closed?.Invoke(this, EventArgs.Empty);
                };
                _first ??= entry;
                list.Children.Add(entry);
            }
            if (demos.Count == 0)
            {
                // The folder, spelled out. It is the app's own directory and
                // no file manager on a modern Android can open it, so a player
                // who wants to copy a recording off the device needs the path
                // itself -- over USB or adb -- and this is the only place it
                // is ever written down.
                list.Children.Add(new TextBlock
                {
                    Text = "Nothing recorded yet. Recordings are made from the pause menu "
                        + $"during an online match, and are written to:\n{directory}",
                    Foreground = GuiTheme.TextDimBrush,
                    FontSize = 12,
                    TextWrapping = TextWrapping.Wrap,
                    Margin = new Thickness(4, 6, 4, 12)
                });
            }
            var import = new MenuEntry("Open a file...",
                "A demo from somewhere else on this device", titleSize: 13)
            {
                Accent = GuiTheme.TextDim,
                Margin = new Thickness(0, 10, 0, 0)
            };
            import.Click += (_, _) =>
            {
                ImportRequested = true;
                Closed?.Invoke(this, EventArgs.Empty);
            };
            _first ??= import;
            list.Children.Add(import);

            // A back entry as well as Escape: the same corner Settings' own
            // Back sits in, not a bar across the top.
            var back = new MenuEntry("Back", titleSize: 20)
            {
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(64, 0, 0, 96)
            };
            back.Click += (_, _) => Closed?.Invoke(this, EventArgs.Empty);
            var heading = new Caption("Clips") { Height = 34 };
            list.Children.Insert(0, heading);

            var body = new ScrollViewer
            {
                Content = list,
                Margin = new Thickness(320, 60, 64, 60),
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            };
            var root = new Panel();
            root.Children.Add(GuiTheme.Backdrop());
            root.Children.Add(body);
            root.Children.Add(back);
            Content = root;
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            Dispatcher.UIThread.Post(() => _first?.Focus(), DispatcherPriority.Background);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                Closed?.Invoke(this, EventArgs.Empty);
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }
    }
}
