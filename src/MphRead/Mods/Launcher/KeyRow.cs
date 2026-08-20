using System;
using System.Drawing;
using System.Reflection;
using System.Runtime.Versioning;
using System.Windows.Forms;
using MphRead.Entities;
using MphRead.Mods;
using GlfwKeys = OpenTK.Windowing.GraphicsLibraryFramework.Keys;
using GlfwMouse = OpenTK.Windowing.GraphicsLibraryFramework.MouseButton;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// One rebindable control: what it does on the left, what it is bound to
    /// on the right, click and press to change it.
    ///
    /// The awkward part is that this window is WinForms and the game is GLFW,
    /// and their key enumerations agree only about printable ASCII -- Escape
    /// is 27 in one and 256 in the other. The map below covers everything a
    /// person is likely to bind; anything unmapped is refused rather than
    /// bound to whatever key happens to share its number.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class KeyRow : Control
    {
        private readonly LauncherTheme _theme;
        private readonly PropertyInfo _property;
        private readonly int _labelWidth;
        private bool _listening;
        private bool _hot;

        public event EventHandler? Rebound;

        public KeyRow(LauncherTheme theme, PropertyInfo property, int labelWidth = 150)
        {
            _theme = theme;
            _property = property;
            _labelWidth = theme.S(labelWidth);
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw
                | ControlStyles.Selectable, true);
            TabStop = true;
            BackColor = LauncherTheme.Panel;
            Height = theme.S(32);
        }

        private Rectangle Box => new(_labelWidth, _theme.S(2),
            Math.Max(_theme.S(60), Width - _labelWidth), Height - _theme.S(4));

        protected override void OnMouseDown(MouseEventArgs e)
        {
            Focus();
            if (!_listening)
            {
                if (Box.Contains(e.Location))
                {
                    _listening = true;
                    Invalidate();
                }
                base.OnMouseDown(e);
                return;
            }
            // Already listening: this press is the new binding.
            GlfwMouse? button = e.Button switch
            {
                MouseButtons.Left => GlfwMouse.Left,
                MouseButtons.Right => GlfwMouse.Right,
                MouseButtons.Middle => GlfwMouse.Middle,
                MouseButtons.XButton1 => GlfwMouse.Button4,
                MouseButtons.XButton2 => GlfwMouse.Button5,
                _ => null
            };
            if (button != null)
            {
                InputSettings.Rebind(_property, ButtonType.Mouse, GlfwKeys.Unknown, button.Value);
                Done();
            }
            base.OnMouseDown(e);
        }

        protected override void OnMouseWheel(MouseEventArgs e)
        {
            if (_listening && e.Delta != 0)
            {
                InputSettings.Rebind(_property,
                    e.Delta > 0 ? ButtonType.ScrollUp : ButtonType.ScrollDown,
                    GlfwKeys.Unknown, GlfwMouse.Left);
                Done();
            }
            base.OnMouseWheel(e);
        }

        protected override void OnMouseEnter(EventArgs e)
        {
            _hot = true;
            Cursor = Cursors.Hand;
            Invalidate();
            base.OnMouseEnter(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hot = false;
            Invalidate();
            base.OnMouseLeave(e);
        }

        protected override bool IsInputKey(Keys keyData)
        {
            // While listening every key belongs to this control, including the
            // ones WinForms would otherwise spend on moving the focus.
            return _listening || keyData == Keys.Enter || keyData == Keys.Space
                || base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (!_listening)
            {
                if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space)
                {
                    _listening = true;
                    Invalidate();
                    e.Handled = true;
                }
                base.OnKeyDown(e);
                return;
            }
            e.Handled = true;
            e.SuppressKeyPress = true;
            if (e.KeyCode == Keys.Escape)
            {
                Done();
                return;
            }
            if (e.KeyCode == Keys.Back || e.KeyCode == Keys.Delete)
            {
                InputSettings.Rebind(_property, ButtonType.Key, GlfwKeys.Unknown, GlfwMouse.Left);
                Done();
                return;
            }
            GlfwKeys? key = Translate(e.KeyCode);
            if (key != null)
            {
                InputSettings.Rebind(_property, ButtonType.Key, key.Value, GlfwMouse.Left);
                Done();
            }
        }

        private void Done()
        {
            _listening = false;
            Invalidate();
            Rebound?.Invoke(this, EventArgs.Empty);
        }

        protected override void OnLostFocus(EventArgs e)
        {
            _listening = false;
            Invalidate();
            base.OnLostFocus(e);
        }

        protected override void OnGotFocus(EventArgs e)
        {
            Invalidate();
            base.OnGotFocus(e);
        }

        /// <summary>WinForms key to the one the game's input layer speaks.</summary>
        private static GlfwKeys? Translate(Keys key)
        {
            if (key >= Keys.A && key <= Keys.Z)
            {
                return GlfwKeys.A + (key - Keys.A);
            }
            if (key >= Keys.D0 && key <= Keys.D9)
            {
                return GlfwKeys.D0 + (key - Keys.D0);
            }
            if (key >= Keys.NumPad0 && key <= Keys.NumPad9)
            {
                return GlfwKeys.KeyPad0 + (key - Keys.NumPad0);
            }
            if (key >= Keys.F1 && key <= Keys.F12)
            {
                return GlfwKeys.F1 + (key - Keys.F1);
            }
            return key switch
            {
                Keys.Space => GlfwKeys.Space,
                Keys.Tab => GlfwKeys.Tab,
                Keys.Enter => GlfwKeys.Enter,
                Keys.ShiftKey or Keys.LShiftKey => GlfwKeys.LeftShift,
                Keys.RShiftKey => GlfwKeys.RightShift,
                Keys.ControlKey or Keys.LControlKey => GlfwKeys.LeftControl,
                Keys.RControlKey => GlfwKeys.RightControl,
                Keys.Menu or Keys.LMenu => GlfwKeys.LeftAlt,
                Keys.RMenu => GlfwKeys.RightAlt,
                Keys.Left => GlfwKeys.Left,
                Keys.Right => GlfwKeys.Right,
                Keys.Up => GlfwKeys.Up,
                Keys.Down => GlfwKeys.Down,
                Keys.Insert => GlfwKeys.Insert,
                Keys.Home => GlfwKeys.Home,
                Keys.End => GlfwKeys.End,
                Keys.PageUp => GlfwKeys.PageUp,
                Keys.PageDown => GlfwKeys.PageDown,
                Keys.CapsLock => GlfwKeys.CapsLock,
                Keys.OemMinus => GlfwKeys.Minus,
                Keys.Oemplus => GlfwKeys.Equal,
                Keys.OemOpenBrackets => GlfwKeys.LeftBracket,
                Keys.OemCloseBrackets => GlfwKeys.RightBracket,
                Keys.OemSemicolon => GlfwKeys.Semicolon,
                Keys.OemQuotes => GlfwKeys.Apostrophe,
                Keys.Oemcomma => GlfwKeys.Comma,
                Keys.OemPeriod => GlfwKeys.Period,
                Keys.OemQuestion => GlfwKeys.Slash,
                Keys.OemBackslash or Keys.OemPipe => GlfwKeys.Backslash,
                Keys.Oemtilde => GlfwKeys.GraveAccent,
                Keys.Add => GlfwKeys.KeyPadAdd,
                Keys.Subtract => GlfwKeys.KeyPadSubtract,
                Keys.Multiply => GlfwKeys.KeyPadMultiply,
                Keys.Divide => GlfwKeys.KeyPadDivide,
                _ => null
            };
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            LauncherTheme.Smooth(g);
            Font label = _theme.Body(_theme.S(12), FontStyle.Bold);
            using (var brush = new SolidBrush(LauncherTheme.Text))
            {
                g.DrawString(InputSettings.ActionName(_property), label, brush, 0,
                    (Height - label.GetHeight(g)) / 2f);
            }

            Rectangle box = Box;
            using System.Drawing.Drawing2D.GraphicsPath path =
                LauncherTheme.RoundRect(box, _theme.S(4));
            using (var fill = new SolidBrush(LauncherTheme.PanelLight))
            {
                g.FillPath(fill, path);
            }
            using (var pen = new Pen(_listening ? LauncherTheme.Warm
                : Focused || _hot ? LauncherTheme.Accent : LauncherTheme.Edge, _theme.S(1)))
            {
                g.DrawPath(pen, path);
            }
            Font value = _theme.Body(_theme.S(12), FontStyle.Bold);
            string text = _listening
                ? "press a key, a mouse button or the wheel"
                : InputSettings.Describe(InputSettings.Bind(_property));
            using var textBrush = new SolidBrush(_listening ? LauncherTheme.Warm : LauncherTheme.Text);
            using var format = new StringFormat
            {
                Alignment = StringAlignment.Center,
                LineAlignment = StringAlignment.Center,
                Trimming = StringTrimming.EllipsisCharacter,
                FormatFlags = StringFormatFlags.NoWrap
            };
            g.DrawString(text, value, textBrush, box, format);
        }
    }
}
