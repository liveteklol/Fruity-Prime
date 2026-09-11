using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// One question and the two marks that answer it.
    ///
    /// Quitting, leaving a match, forgetting every keybind and wiping a save
    /// slot are four different consequences and one screen: what changes
    /// between them is a sentence. Built the way OpenQuake3/defrag's own
    /// confirm screen is -- there is one of those too, and everything
    /// irreversible goes through it.
    /// </summary>
    internal sealed class ConfirmScreen : UserControl
    {
        /// <summary>Raised with what was answered. False is also what Escape means.</summary>
        public event EventHandler<bool>? Answered;

        private readonly UiMark _no;

        public ConfirmScreen(string question, string yes = "yes", string no = "no",
            bool overGame = false)
        {
            Background = Brushes.Transparent;
            Focusable = true;

            var prompt = new TextBlock
            {
                Text = question,
                FontFamily = GuiTheme.Display,
                FontSize = 26,
                Foreground = GuiTheme.TextBrush,
                TextWrapping = TextWrapping.Wrap,
                MaxWidth = 560,
                TextAlignment = TextAlignment.Center,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            };

            _no = new UiMark(UiMark.Shape.Cancel, no)
            {
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(UiLayout.CornerX, 0, 0, UiLayout.CornerY)
            };
            _no.Click += (_, _) => Answered?.Invoke(this, false);
            var ok = new UiMark(UiMark.Shape.Accept, yes)
            {
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(0, 0, UiLayout.CornerX, UiLayout.CornerY)
            };
            ok.Click += (_, _) => Answered?.Invoke(this, true);

            Panel root = UiLayout.Backdrop(overGame);
            root.Children.Add(prompt);
            root.Children.Add(_no);
            root.Children.Add(ok);
            Content = root;
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            // On "no", every time. A confirm screen that opens on the
            // destructive answer is one an accidental Enter goes through.
            Dispatcher.UIThread.Post(() => _no.Focus(), DispatcherPriority.Background);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                Answered?.Invoke(this, false);
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }
    }
}
