using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using MphRead.Entities;
using MphRead.Mods.Launcher;
using MphRead.Mods.Launcher.Gui;
using MphRead.Mods.Network;

namespace MphRead.Droid
{
    /// <summary>
    /// The screen on a phone: who you are, which servers are up, and an honest
    /// line about what this build can and cannot do here.
    ///
    /// Built out of the same painted controls as the desktop front screen
    /// (<see cref="MenuEntry"/>, <see cref="Caption"/>, the rows in Rows.cs) so
    /// that the two are one product rather than two that look alike, and over
    /// the same <see cref="LauncherPrefs"/> and the same directory client -- a
    /// server list here is the same query the desktop browser makes, answered
    /// by the same servers.
    ///
    /// What is missing is the match. The engine draws through OpenTK and takes
    /// its input from GLFW, and neither exists on Android; until it has a
    /// mobile renderer and touch controls, a "play" button here would be a
    /// button that cannot work, so there is not one.
    /// </summary>
    internal sealed class AndroidHomeView : UserControl
    {
        private readonly StackPanel _servers = new() { Spacing = 4 };
        private readonly Note _serverNote = new("");
        private readonly FieldRow _name;
        private readonly ChoiceRow _hunter;
        private readonly FieldRow _master;
        private MenuEntry _refresh = null!;

        private static readonly string[] _hunters = Enumerable.Range(0, 7)
            .Select(i => ((Hunter)i).ToString())
            .Append(Hunter.Random.ToString()).ToArray();

        public AndroidHomeView()
        {
            LauncherPrefs.Load();
            Background = GuiTheme.PanelBrush;

            _name = new FieldRow("Your name", LauncherPrefs.PlayerName, boxWidth: 180);
            _hunter = new ChoiceRow("Hunter", _hunters,
                Math.Max(0, Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString())));
            _master = new FieldRow("Server directory",
                $"{LauncherPrefs.MasterHost}:{LauncherPrefs.MasterPort}", boxWidth: 200);

            var body = new StackPanel { Spacing = 4, Margin = new Thickness(18, 14, 18, 24) };
            body.Children.Add(Wordmark());
            body.Children.Add(new Note($"{Mods.Branding.NameAndVersion} on Android",
                GuiTheme.TextDim));

            body.Children.Add(new Caption("You") { Height = 30 });
            body.Children.Add(_name);
            body.Children.Add(_hunter);
            var save = new MenuEntry("Save", titleSize: 15) { Primary = true, Height = 42 };
            save.Click += (_, _) => SavePrefs();
            body.Children.Add(save);

            body.Children.Add(new Caption("Servers") { Height = 30 });
            body.Children.Add(_master);
            _refresh = new MenuEntry("Find servers", "Ask the directory who is up",
                titleSize: 15);
            _refresh.Click += async (_, _) => await ReloadServers();
            body.Children.Add(_refresh);
            body.Children.Add(_serverNote);
            body.Children.Add(_servers);

            body.Children.Add(new Caption("Playing here") { Height = 30 });
            body.Children.Add(new Note(
                "This build does not play a match on Android yet. The launcher, the "
                + "preferences and the server directory are the same code as on the "
                + "desktop; the game itself draws through OpenTK and reads its input "
                + "from GLFW, neither of which exists on this platform. What is left "
                + "to do is a mobile renderer and touch controls -- not a port of the "
                + "game logic, which builds here already.", GuiTheme.Warm));
            body.Children.Add(new Note(
                "Until then this screen is useful for what it can answer: who you "
                + "are, and which servers are up to play on from a desktop.",
                GuiTheme.TextDim));

            Content = new ScrollViewer
            {
                Content = body,
                Background = GuiTheme.PanelBrush,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            };
        }

        private static Control Wordmark()
        {
            try
            {
                using Stream stream = AssetLoader.Open(
                    new Uri("avares://FruityPrime/Assets/fruity-prime-logo.png"));
                return new Image
                {
                    Source = new Bitmap(stream),
                    Height = 90,
                    HorizontalAlignment = HorizontalAlignment.Left,
                    Margin = new Thickness(0, 8, 0, 8)
                };
            }
            catch (Exception)
            {
                // A build without the asset gets the name, not a crash on the
                // first screen.
                return new TextBlock
                {
                    Text = Mods.Branding.Name,
                    FontFamily = GuiTheme.Display,
                    FontSize = 28,
                    Foreground = GuiTheme.TextBrush
                };
            }
        }

        private void SavePrefs()
        {
            if (_name.Value.Trim().Length > 0)
            {
                LauncherPrefs.PlayerName = _name.Value.Trim();
            }
            LauncherPrefs.LastHunter = Enum.Parse<Hunter>(_hunter.Value);
            LauncherPrefs.Save();
        }

        /// <summary>
        /// Ask the directory who is up, then ask each of them directly -- the
        /// same two calls the desktop browser makes, and for the same reason:
        /// an answer proves the server is reachable from *this* device rather
        /// than only from the directory.
        /// </summary>
        private async Task ReloadServers()
        {
            _servers.Children.Clear();
            _refresh.IsEnabled = false;
            string host = LauncherPrefs.MasterHost;
            int port = LauncherPrefs.MasterPort;
            ParseEndpoint(_master.Value, ref host, ref port);
            _serverNote.Text = $"Asking {host}...";
            _serverNote.Foreground = GuiTheme.TextDimBrush;

            MasterListResult result = await Task.Run(() => NetMasterClient.Query(host, port));
            _refresh.IsEnabled = true;
            if (!result.Answered)
            {
                _serverNote.Text = "The directory did not answer. It may be down, or UDP "
                    + "may not reach it from this network.";
                _serverNote.Foreground = GuiTheme.WarmBrush;
                return;
            }
            if (result.Servers.Count == 0)
            {
                _serverNote.Text = "The directory is up and has nobody listed.";
                _serverNote.Foreground = GuiTheme.WarmBrush;
                return;
            }
            _serverNote.Text = $"{result.Servers.Count} listed.";
            _serverNote.Foreground = GuiTheme.TextDimBrush;
            foreach (MasterListing listing in result.Servers)
            {
                AddServerRow(listing);
            }
        }

        private void AddServerRow(MasterListing listing)
        {
            string name = listing.ServerName.Length > 0 ? listing.ServerName : listing.Endpoint;
            var entry = new MenuEntry(name, listing.Endpoint, titleSize: 15);
            _servers.Children.Add(entry);
            Task.Run(() =>
            {
                ServerStatus status = NetStatus.Query(listing.Address, listing.Port,
                    allowJoinProbe: false);
                Dispatcher.UIThread.Post(() =>
                {
                    entry.Subtitle = status.Online
                        ? $"{listing.Endpoint} -- {status.RoomKey} "
                            + $"{NetStatus.ModeName(status.Mode)} {status.Players} players"
                        : $"{listing.Endpoint} -- did not answer";
                    entry.SubtitleColor = status.Online ? GuiTheme.TextDim : GuiTheme.Warm;
                });
            });
        }

        /// <summary>host, or host:port. Leaves both alone on anything else.</summary>
        private static void ParseEndpoint(string text, ref string host, ref int port)
        {
            text = text.Trim();
            if (text.Length == 0)
            {
                return;
            }
            int colon = text.LastIndexOf(':');
            if (colon <= 0)
            {
                host = text;
                return;
            }
            if (Int32.TryParse(text[(colon + 1)..], out int parsed)
                && parsed >= 1 && parsed <= 65535)
            {
                host = text[..colon];
                port = parsed;
            }
        }
    }
}
