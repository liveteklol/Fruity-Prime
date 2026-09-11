using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// The one layout every screen in the launcher is built from.
    ///
    /// There used to be nine screens and each had picked its own answer to
    /// where things go: a card 400 wide over here, a 600-wide table over
    /// there, a bar across the top on one, a corner link on another. The
    /// screens were all the same product and none of them looked like it.
    ///
    /// So the answers live here and nowhere else -- the picture, the two
    /// washes over it, the column of words in the bottom-left corner, the line
    /// under it, and the two marks in the bottom corners that mean yes and no.
    /// A screen is a choice of what goes in those places, which is why there
    /// are four of them now instead of nine.
    ///
    /// The numbers are the front screen's, unchanged: it was the one screen
    /// that already read the way the rest were meant to.
    /// </summary>
    internal static class UiLayout
    {
        /// <summary>Where the column of words starts, and how far it clears the bottom edge.</summary>
        public const double ColumnLeft = 72;
        public const double ColumnBottom = 96;

        /// <summary>The corner marks, and the dim line under the column.</summary>
        public const double CornerX = 64;
        public const double CornerY = 38;
        public const double FooterBottom = 18;

        /// <summary>A menu word, and the one word a screen is named by.</summary>
        public const double WordSize = 20;
        public const double HeadingSize = 15;

        /// <summary>Where a screen's own content sits: clear of the tabs and of both corners.</summary>
        public static Thickness BodyMargin => new(ColumnLeft, 104, CornerX, 96);

        /// <summary>Where the tab strip sits: the top-left corner, above the body.</summary>
        public static Thickness TabMargin => new(ColumnLeft, 52, 0, 0);

        private static readonly Lazy<Bitmap?> _background =
            new(() => Load("Backgrounds/launcher-bg.jpg"));

        private static readonly Lazy<Bitmap?> _wordmark =
            new(() => Load("fruity-prime-logo.png"));

        private static Bitmap? Load(string asset)
        {
            try
            {
                using Stream stream = AssetLoader.Open(
                    new Uri($"avares://FruityPrime/Assets/{asset}"));
                return new Bitmap(stream);
            }
            catch (Exception)
            {
                // A build missing the asset gets no picture rather than no
                // launcher.
                return null;
            }
        }

        /// <summary>
        /// What every screen is painted on: the photo, a wash that carries the
        /// left third dark enough to read a menu over, and a corner vignette
        /// for the wordmark.
        ///
        /// Over a match it is the scrim alone. The match behind is still
        /// running -- a networked one cannot be paused -- and covering it with
        /// a photograph would be a lie about what the program is doing.
        /// </summary>
        public static Panel Backdrop(bool overGame = false)
        {
            var root = new Panel();
            if (overGame)
            {
                root.Children.Add(new Border { Background = GuiTheme.ScrimBrush });
                return root;
            }
            root.Children.Add(new Image
            {
                Source = _background.Value,
                Stretch = Stretch.UniformToFill
            });
            root.Children.Add(new Border
            {
                Background = new LinearGradientBrush
                {
                    GradientStops =
                    {
                        new GradientStop(Color.FromArgb(215, 0, 0, 0), 0),
                        new GradientStop(Color.FromArgb(110, 0, 0, 0), 0.38),
                        new GradientStop(Color.FromArgb(0, 0, 0, 0), 0.68)
                    }
                }
            });
            root.Children.Add(new Border
            {
                Background = new RadialGradientBrush
                {
                    Center = new RelativePoint(1, 1, RelativeUnit.Relative),
                    GradientOrigin = new RelativePoint(1, 1, RelativeUnit.Relative),
                    RadiusX = new RelativeScalar(0.5, RelativeUnit.Relative),
                    RadiusY = new RelativeScalar(0.7, RelativeUnit.Relative),
                    GradientStops =
                    {
                        new GradientStop(Color.FromArgb(150, 0, 0, 0), 0),
                        new GradientStop(Color.FromArgb(0, 0, 0, 0), 1)
                    }
                }
            });
            return root;
        }

        /// <summary>The column of words, anchored where the front screen's is.</summary>
        public static StackPanel Column(double spacing = 16)
        {
            return new StackPanel
            {
                Spacing = spacing,
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(ColumnLeft, 0, 0, ColumnBottom)
            };
        }

        /// <summary>
        /// The dim line under the column: the build on the front screen, what
        /// match you are in on the pause screen. One line, one corner, and
        /// never anything a player came here to read.
        /// </summary>
        public static TextBlock Footer(string text)
        {
            return new TextBlock
            {
                Text = text,
                FontFamily = GuiTheme.Display,
                FontSize = 12,
                Foreground = GuiTheme.TextDimBrush,
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(ColumnLeft - 50, 0, 0, FooterBottom)
            };
        }

        /// <summary>The mark in the opposite corner, at the size the front screen uses.</summary>
        public static Image Wordmark()
        {
            return new Image
            {
                Source = _wordmark.Value,
                Stretch = Stretch.Uniform,
                Width = 220,
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(0, 0, 32, 28),
                Opacity = 0.92
            };
        }

        /// <summary>
        /// What a screen is called, in the top-left corner over the tabs.
        /// Lower case and dim on purpose: it says where you are, and a title
        /// that shouts is a title competing with the thing it titles.
        /// </summary>
        public static TextBlock Heading(string text)
        {
            return new TextBlock
            {
                Text = text.ToLowerInvariant(),
                FontFamily = GuiTheme.Display,
                FontSize = HeadingSize,
                Foreground = GuiTheme.TextDimBrush,
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Top,
                Margin = new Thickness(ColumnLeft, 26, 0, 0)
            };
        }
    }
}
