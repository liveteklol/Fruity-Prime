using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// The strip that says which of a screen's few faces is up: an arrow, the
    /// names with a dot between them, an arrow.
    ///
    /// It is what turned seven screens into one. Choosing a server, a map, a
    /// save slot and a recording were four separate cards with four layouts,
    /// and they are four answers to the same question -- what are we playing.
    /// A strip over one list is that question asked once.
    ///
    /// The arrows are real controls rather than decoration, because a phone
    /// has no left and right arrow keys and the strip is the only way to
    /// change face there.
    /// </summary>
    internal sealed class UiTabs : StackPanel
    {
        /// <summary>Raised after <see cref="Index"/> has already moved.</summary>
        public event EventHandler? Changed;

        private readonly List<UiWord> _words = new();
        private int _index;

        public int Index
        {
            get => _index;
            set
            {
                int clamped = _words.Count == 0 ? 0 : Math.Clamp(value, 0, _words.Count - 1);
                if (clamped == _index)
                {
                    return;
                }
                _index = clamped;
                Mark();
                Changed?.Invoke(this, EventArgs.Empty);
            }
        }

        public UiTabs(IReadOnlyList<string> names, int index = 0)
        {
            Orientation = Orientation.Horizontal;
            Spacing = 10;
            VerticalAlignment = VerticalAlignment.Center;
            _index = names.Count == 0 ? 0 : Math.Clamp(index, 0, names.Count - 1);

            Children.Add(Arrow(pointsLeft: true, () => Step(-1)));
            for (int i = 0; i < names.Count; i++)
            {
                if (i > 0)
                {
                    Children.Add(new TextBlock
                    {
                        Text = "\u00b7",
                        FontFamily = GuiTheme.Display,
                        FontSize = UiLayout.WordSize,
                        Foreground = GuiTheme.EdgeBrush,
                        VerticalAlignment = VerticalAlignment.Center
                    });
                }
                var word = new UiWord(names[i].ToUpperInvariant(), UiLayout.WordSize,
                    colour: GuiTheme.TextDim);
                int target = i;
                word.Click += (_, _) => Index = target;
                _words.Add(word);
                Children.Add(word);
            }
            Children.Add(Arrow(pointsLeft: false, () => Step(1)));
            Mark();
        }

        /// <summary>
        /// The left and right keys, wherever they were pressed on the screen.
        /// Offered to the host rather than handled here: the strip is rarely
        /// what holds the keyboard -- the list under it is.
        /// </summary>
        public bool HandleKey(Key key)
        {
            if (key == Key.Left)
            {
                Step(-1);
                return true;
            }
            if (key == Key.Right)
            {
                Step(1);
                return true;
            }
            return false;
        }

        /// <summary>
        /// Put the keyboard on the name that is up. A <see cref="StackPanel"/>
        /// cannot take focus itself, so a host that asked the strip to take it
        /// would silently focus nothing -- and the first Tab would then land
        /// on whatever the tree happened to offer first.
        /// </summary>
        public void FocusSelected()
        {
            if (_index >= 0 && _index < _words.Count)
            {
                _words[_index].Focus();
            }
        }

        /// <summary>Wrapping: the faces are few and running off the end of them is nobody's intent.</summary>
        private void Step(int direction)
        {
            if (_words.Count == 0)
            {
                return;
            }
            _index = (_index + direction + _words.Count) % _words.Count;
            Mark();
            Changed?.Invoke(this, EventArgs.Empty);
        }

        private void Mark()
        {
            for (int i = 0; i < _words.Count; i++)
            {
                _words[i].Selected = i == _index;
            }
        }

        private static Control Arrow(bool pointsLeft, Action go)
        {
            var word = new UiWord(pointsLeft ? "\u25c4" : "\u25ba", 13,
                colour: GuiTheme.TextDim);
            word.Click += (_, _) => go();
            return word;
        }
    }
}
