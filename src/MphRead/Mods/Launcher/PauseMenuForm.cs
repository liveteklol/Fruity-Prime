using System;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;
using MphRead.Mods;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// What Escape shows during a match: resume, the window mode, the
    /// settings, and the two ways out.
    ///
    /// Deliberately small and centred rather than full-screen: the match is
    /// still running behind it, which is the point -- a networked match cannot
    /// be paused, and watching it carry on is more honest than pretending it
    /// stopped.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class PauseMenuForm : Form
    {
        private readonly LauncherTheme _theme;
        private MenuButton _windowButton = null!;

        [ThreadStatic]
        private static PauseMenuForm? _instance;
        private static PauseMenuForm? _shared;

        /// <summary>Entry point for the menu's own thread.</summary>
        public static void RunLoop()
        {
            try
            {
                try
                {
                    ApplicationConfiguration.Initialize();
                }
                catch (InvalidOperationException)
                {
                    // Already done by whichever thread showed the first window:
                    // SetCompatibleTextRenderingDefault throws once a form
                    // exists, and swallowing that is the whole fix -- without
                    // it the pause menu threw before it ever appeared.
                }
                using var form = new PauseMenuForm();
                _instance = form;
                _shared = form;
                Application.Run(form);
            }
            catch (Exception)
            {
                // A menu that fails to open must not take the match with it.
            }
            finally
            {
                _instance = null;
                _shared = null;
                PauseMenu.MarkClosed();
            }
        }

        /// <summary>Close from the game's thread, safely.</summary>
        public static void CloseIfOpen()
        {
            PauseMenuForm? form = _shared;
            if (form == null || form.IsDisposed)
            {
                return;
            }
            try
            {
                if (form.IsHandleCreated)
                {
                    form.BeginInvoke(() => form.Close());
                }
            }
            catch (Exception)
            {
            }
        }

        private PauseMenuForm()
        {
            _theme = new LauncherTheme(DeviceDpi);
            Text = "MphRead";
            FormBorderStyle = FormBorderStyle.None;
            AutoScaleMode = AutoScaleMode.None;
            // Over the game window rather than over the middle of the desktop:
            // the match is still running behind it and that is where the
            // player is looking.
            StartPosition = FormStartPosition.Manual;
            BackColor = LauncherTheme.Panel;
            ForeColor = LauncherTheme.Text;
            TopMost = true;
            ShowInTaskbar = false;
            KeyPreview = true;
            ClientSize = new Size(_theme.S(340), _theme.S(392));
            Rectangle screen = Screen.PrimaryScreen?.WorkingArea ?? new Rectangle(0, 0, 1280, 800);
            int x = PauseMenu.WindowWidth > 0
                ? PauseMenu.WindowX + (PauseMenu.WindowWidth - ClientSize.Width) / 2
                : screen.X + (screen.Width - ClientSize.Width) / 2;
            int y = PauseMenu.WindowHeight > 0
                ? PauseMenu.WindowY + (PauseMenu.WindowHeight - ClientSize.Height) / 2
                : screen.Y + (screen.Height - ClientSize.Height) / 2;
            Location = new Point(Math.Max(screen.X, x), Math.Max(screen.Y, y));

            var stack = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                BackColor = LauncherTheme.Panel,
                Padding = new Padding(_theme.S(22), _theme.S(18), _theme.S(22), _theme.S(18))
            };
            stack.MouseDown += (_, e) =>
            {
                if (e.Button == MouseButtons.Left)
                {
                    WindowChrome.DragFrom(this);
                }
            };
            Controls.Add(stack);

            int width = _theme.S(340 - 44);
            stack.Controls.Add(new Caption(_theme, "Paused", 24, LauncherTheme.Text, 1, display: true)
            {
                Width = width,
                Margin = new Padding(0, 0, 0, _theme.S(10))
            });

            Add(stack, width, "Resume", "Escape", () => Close());
            _windowButton = Add(stack, width, WindowLabel(), "F11 or Alt+Enter", () =>
            {
                PauseMenu.RequestFullscreenToggle();
                // The game thread does it on the next frame; reflect it here
                // straight away so the label is not a lie for 16 milliseconds.
                _windowButton.Text = WindowMode.IsFullscreen ? "Fullscreen" : "Windowed";
                Close();
            });
            Add(stack, width, "Settings", "Controls, display, audio", OpenSettings);
            Add(stack, width, "Leave match", "Back to the launcher",
                () => { PauseMenu.RequestLeave(); Close(); });
            Add(stack, width, "Quit", "Close MphRead",
                () => { PauseMenu.RequestQuit(); Close(); });
        }

        private static string WindowLabel()
        {
            return WindowMode.IsFullscreen ? "Windowed" : "Fullscreen";
        }

        private MenuButton Add(FlowLayoutPanel stack, int width, string text, string note,
            Action action)
        {
            var button = new MenuButton(_theme, text, note, titleSize: 17)
            {
                Width = width,
                Margin = new Padding(0, 0, 0, _theme.S(4))
            };
            button.Click += (_, _) => action();
            stack.Controls.Add(button);
            return button;
        }

        /// <summary>
        /// Open the settings over the pause menu, not under it.
        ///
        /// This menu is TopMost, because the game behind it may be borderless
        /// fullscreen and a menu that disappears behind the window it belongs
        /// to is not a menu. Windows keeps every topmost window above every
        /// ordinary one, and a modal dialog is only guaranteed to sit above
        /// its *owner* -- so the settings window opened, took the keyboard,
        /// and drew underneath the small panel that had just launched it.
        /// Nothing on it could be reached.
        ///
        /// Both halves are needed. The dialog is made topmost so it clears the
        /// game window; this menu drops out of the topmost band while the
        /// dialog is up so that two topmost windows are not left arguing about
        /// which of them is in front.
        /// </summary>
        private void OpenSettings()
        {
            bool wasTopMost = TopMost;
            TopMost = false;
            try
            {
                MenuSettings settings = GameState.LoadSettings();
                using var form = new SettingsForm(settings, inGame: true);
                form.ShowDialog(this);
            }
            catch (Exception ex)
            {
                // Anything this window could not do -- an unreadable
                // settings file, a preview cache it cannot count -- would
                // otherwise reach WinForms as an unhandled exception, and the
                // dialog that produces is over a match still being played.
                MessageBox.Show(this, $"The settings could not be opened:\n\n{ex.Message}",
                    "MphRead", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            finally
            {
                TopMost = wasTopMost;
                if (!IsDisposed)
                {
                    // Back in front of the game, and focused: the player came
                    // out of a dialog and the menu is what they are looking at.
                    Activate();
                }
            }
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            WindowChrome.RoundCorners(Handle);
        }

        protected override bool ProcessCmdKey(ref System.Windows.Forms.Message message, Keys keyData)
        {
            if (keyData == Keys.Escape)
            {
                Close();
                return true;
            }
            return base.ProcessCmdKey(ref message, keyData);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _theme.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
