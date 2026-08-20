using System;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// A labelled text field, shaped like <see cref="ChoiceRow"/> so a column
    /// of the two lines up.
    ///
    /// The editing is still a real TextBox -- selection, the clipboard and
    /// the caret are not worth reimplementing -- but it is stripped of its
    /// border and sits inside a frame this control paints, because a WinForms
    /// TextBox draws a system-coloured border that no property will darken.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class FieldBox : Control
    {
        private readonly LauncherTheme _theme;
        private readonly string _label;
        private readonly int _labelWidth;
        private readonly TextBox _box = new();

        public event EventHandler? ValueChanged;

        public FieldBox(LauncherTheme theme, string label, int labelWidth = 96)
        {
            _theme = theme;
            _label = label;
            _labelWidth = theme.S(labelWidth);
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            // The parent panel is a flat colour; painting it here rather than
            // asking for a transparent background avoids the parent-repaint
            // dance WinForms does for ControlStyles.SupportsTransparentBackColor.
            BackColor = LauncherTheme.Panel;
            Height = theme.S(38);

            _box.BorderStyle = BorderStyle.None;
            _box.BackColor = LauncherTheme.PanelLight;
            _box.ForeColor = LauncherTheme.Text;
            _box.Font = theme.Body(theme.S(14), FontStyle.Bold);
            _box.TextChanged += (_, _) => ValueChanged?.Invoke(this, EventArgs.Empty);
            _box.GotFocus += (_, _) => Invalidate();
            _box.LostFocus += (_, _) => Invalidate();
            Controls.Add(_box);
        }

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public string Value
        {
            get => _box.Text.Trim();
            set => _box.Text = value;
        }

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public string Placeholder
        {
            get => _box.PlaceholderText;
            set => _box.PlaceholderText = value;
        }

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public int MaxLength
        {
            get => _box.MaxLength;
            set => _box.MaxLength = value;
        }

        public void SelectField() => _box.Focus();

        protected override void OnLayout(LayoutEventArgs e)
        {
            base.OnLayout(e);
            int pad = _theme.S(12);
            int height = _box.PreferredHeight;
            _box.SetBounds(_labelWidth + pad, (Height - height) / 2,
                Math.Max(_theme.S(40), Width - _labelWidth - pad * 2), height);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            LauncherTheme.Smooth(g);
            Font label = _theme.Body(_theme.S(11), FontStyle.Bold);
            LauncherTheme.DrawTracked(g, _label.ToUpperInvariant(), label,
                LauncherTheme.TextDim, 0, (Height - label.GetHeight(g)) / 2f, _theme.S(1));

            var frame = new RectangleF(_labelWidth, _theme.S(1),
                Width - _labelWidth - _theme.S(1), Height - _theme.S(2));
            using System.Drawing.Drawing2D.GraphicsPath path =
                LauncherTheme.RoundRect(frame, _theme.S(4));
            using var fill = new SolidBrush(LauncherTheme.PanelLight);
            g.FillPath(fill, path);
            using var pen = new Pen(_box.Focused ? LauncherTheme.Accent : LauncherTheme.Edge,
                _theme.S(1));
            g.DrawPath(pen, path);
        }
    }
}
