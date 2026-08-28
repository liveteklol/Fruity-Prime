using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using MphRead.Entities;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace MphRead.Mods
{
    /// <summary>
    /// Keys and mouse feel, kept where a settings screen can edit them and a
    /// player can keep them.
    ///
    /// Upstream builds a fresh <see cref="PlayerControls"/> per player from
    /// <see cref="PlayerControls.GetDefault"/> and never reads a file, so
    /// rebinding anything lasted exactly as long as the process. This holds
    /// one canonical set, applies it to every set upstream creates, and writes
    /// it to controls.txt beside the executable -- deliberately its own file
    /// rather than a corner of MenuSettings, for the same reason launcher.txt
    /// is.
    ///
    /// Mouse sensitivity was a literal in the aim path with an "itodo" beside
    /// it. One multiplier lives here instead, where 1.0 is exactly the feel
    /// that literal gave.
    /// </summary>
    public static class InputSettings
    {
        private static string Path => System.IO.Path.Combine(AppContext.BaseDirectory, "controls.txt");

        /// <summary>Multiplier on mouse movement. 1.0 is the original feel.</summary>
        public static float MouseSensitivity { get; set; } = 1;

        public static bool InvertMouseY { get; set; }
        public static bool InvertMouseX { get; set; }

        /// <summary>
        /// Whether the wheel cycles every weapon or only the affinity slots.
        ///
        /// On by default, which upstream's constant was not, and the
        /// difference is not a nicety: the cycling code runs only when this is
        /// set *or* the equipped weapon is neither the Power Beam nor the
        /// Missile, so with it off the wheel was dead in the hand every player
        /// spawns with -- "the scroll wheel does not change weapons", exactly.
        /// </summary>
        public static bool ScrollAllWeapons { get; set; } = true;

        private static bool _creating;
        private static PlayerControls? _current;

        /// <summary>
        /// The bindings every player is created with. The settings screen
        /// edits this set; <see cref="Apply"/> copies it onto each set upstream
        /// creates, and <see cref="ApplyToPlayers"/> onto the ones that already
        /// exist.
        /// </summary>
        public static PlayerControls Current
        {
            get
            {
                if (_current == null)
                {
                    // GetDefault calls Apply, which asks for Current: build the
                    // canonical set without letting that come back around.
                    _creating = true;
                    _current = PlayerControls.GetDefault();
                    _creating = false;
                }
                return _current;
            }
        }

        /// <summary>Every rebindable control, in the order a screen should list them.</summary>
        public static IReadOnlyList<PropertyInfo> Bindings => _bindings ??= FindBindings();

        private static PropertyInfo[]? _bindings;

        /// <summary>
        /// The ones worth putting first. Everything else -- the nine weapon
        /// slots, the roll and aim keys -- follows in declaration order, so a
        /// control added upstream shows up without being listed here.
        /// </summary>
        private static readonly string[] _order =
        {
            nameof(PlayerControls.MoveUp), nameof(PlayerControls.MoveDown),
            nameof(PlayerControls.MoveLeft), nameof(PlayerControls.MoveRight),
            nameof(PlayerControls.Jump), nameof(PlayerControls.Boost),
            nameof(PlayerControls.Shoot), nameof(PlayerControls.Zoom),
            nameof(PlayerControls.Morph), nameof(PlayerControls.AltAttack),
            nameof(PlayerControls.NextWeapon), nameof(PlayerControls.PrevWeapon),
            nameof(PlayerControls.WeaponMenu), nameof(PlayerControls.ScanVisor),
            nameof(PlayerControls.Pause), nameof(PlayerControls.HudOverlay)
        };

        private static PropertyInfo[] FindBindings()
        {
            PropertyInfo[] all = typeof(PlayerControls)
                .GetProperties(BindingFlags.Public | BindingFlags.Instance)
                .Where(p => p.PropertyType == typeof(Keybind))
                .ToArray();
            return all
                .OrderBy(p =>
                {
                    int index = Array.IndexOf(_order, p.Name);
                    return index < 0 ? _order.Length : index;
                })
                .ToArray();
        }

        public static Keybind Bind(PropertyInfo property)
        {
            return (Keybind)property.GetValue(Current)!;
        }

        /// <summary>"Left Shift", "Mouse left", "Scroll up", "1".</summary>
        public static string Describe(Keybind bind)
        {
            switch (bind.Type)
            {
                case ButtonType.Mouse:
                    // OpenTK's enum names these Button1..Button8 and aliases
                    // the first three; ToString picks the number, which is not
                    // what anybody calls them.
                    return bind.MouseButton switch
                    {
                        MouseButton.Left => "Mouse left",
                        MouseButton.Right => "Mouse right",
                        MouseButton.Middle => "Mouse middle",
                        _ => $"Mouse {(int)bind.MouseButton + 1}"
                    };
                case ButtonType.ScrollUp:
                    return "Scroll up";
                case ButtonType.ScrollDown:
                    return "Scroll down";
                default:
                    return bind.Key == Keys.Unknown ? "unbound" : KeyName(bind.Key);
            }
        }

        /// <summary>"D1" -> "1", "LeftShift" -> "Left shift", "KeyPad4" -> "Key pad 4".</summary>
        public static string KeyName(Keys key)
        {
            string name = key.ToString();
            if (name.Length == 2 && name[0] == 'D' && Char.IsDigit(name[1]))
            {
                return name[1].ToString();
            }
            var builder = new StringBuilder(name.Length + 4);
            for (int i = 0; i < name.Length; i++)
            {
                if (i > 0 && Char.IsUpper(name[i]) && !Char.IsUpper(name[i - 1]))
                {
                    builder.Append(' ');
                    builder.Append(Char.ToLowerInvariant(name[i]));
                }
                else
                {
                    builder.Append(name[i]);
                }
            }
            return builder.ToString();
        }

        /// <summary>"Humanise" a control's name for a screen: "AltAttack" -> "Alt attack".</summary>
        public static string ActionName(PropertyInfo property)
        {
            string name = property.Name == nameof(PlayerControls.Pause)
                ? "Scoreboard"
                : property.Name == nameof(PlayerControls.RolltLeft) ? "Roll left" : property.Name;
            var builder = new StringBuilder(name.Length + 4);
            for (int i = 0; i < name.Length; i++)
            {
                if (i > 0 && Char.IsUpper(name[i]) && !Char.IsUpper(name[i - 1]))
                {
                    builder.Append(' ');
                    builder.Append(Char.ToLowerInvariant(name[i]));
                }
                else
                {
                    builder.Append(i == 0 ? Char.ToUpperInvariant(name[i]) : name[i]);
                }
            }
            return builder.ToString();
        }

        /// <summary>Point a control at a key, a mouse button or the wheel.</summary>
        public static void Rebind(PropertyInfo property, ButtonType type, Keys key,
            MouseButton button)
        {
            Keybind bind = Bind(property);
            bind.Type = type;
            bind.Key = type == ButtonType.Key ? key : Keys.Unknown;
            bind.MouseButton = button;
        }

        /// <summary>
        /// Copy the canonical bindings onto a set upstream just created. Called
        /// from GetDefault, so it covers every player in every match.
        /// </summary>
        public static void Apply(PlayerControls controls)
        {
            if (_creating || _current == null)
            {
                return;
            }
            foreach (PropertyInfo property in Bindings)
            {
                var source = (Keybind)property.GetValue(_current)!;
                var target = (Keybind)property.GetValue(controls)!;
                target.Type = source.Type;
                target.Key = source.Key;
                target.MouseButton = source.MouseButton;
            }
            controls.ScrollAllWeapons = ScrollAllWeapons;
        }

        /// <summary>
        /// Push the current bindings onto players that already exist.
        ///
        /// <see cref="Apply"/> copies values into each player's own Keybind
        /// objects, so editing this set afterwards reaches nobody -- and a
        /// rebind made from the pause menu is by definition made in the middle
        /// of a match, where waiting for the next one is not an answer.
        /// </summary>
        public static void ApplyToPlayers()
        {
            try
            {
                for (int i = 0; i < PlayerEntity.Players.Count; i++)
                {
                    Apply(PlayerEntity.Players[i].Controls);
                }
            }
            catch (Exception)
            {
                // The pause menu's settings window runs on its own thread, so
                // this can land in the middle of a room load rebuilding the
                // player list. Losing the push is nothing -- the bindings are
                // saved, and PlayerControls.GetDefault applies them to every
                // set the load is creating anyway.
            }
        }

        public static void Load()
        {
            if (!File.Exists(Path))
            {
                return;
            }
            try
            {
                foreach (string raw in File.ReadAllLines(Path))
                {
                    string line = raw.Trim();
                    int split = line.IndexOf('=');
                    if (line.Length == 0 || line[0] == '#' || split <= 0)
                    {
                        continue;
                    }
                    string key = line[..split].Trim();
                    string value = line[(split + 1)..].Trim();
                    if (key == "sensitivity")
                    {
                        if (Single.TryParse(value, NumberStyles.Float,
                            CultureInfo.InvariantCulture, out float parsed))
                        {
                            MouseSensitivity = Math.Clamp(parsed, 0.05f, 10f);
                        }
                        continue;
                    }
                    if (key == "invert_y" && Boolean.TryParse(value, out bool invertY))
                    {
                        InvertMouseY = invertY;
                        continue;
                    }
                    if (key == "invert_x" && Boolean.TryParse(value, out bool invertX))
                    {
                        InvertMouseX = invertX;
                        continue;
                    }
                    if (key == "scroll_all_weapons" && Boolean.TryParse(value, out bool scrollAll))
                    {
                        ScrollAllWeapons = scrollAll;
                        continue;
                    }
                    PropertyInfo? property = Bindings.FirstOrDefault(p => p.Name == key);
                    if (property != null)
                    {
                        ParseBind(property, value);
                    }
                }
            }
            catch (Exception)
            {
                // Bindings are a convenience; an unreadable file must not stop
                // the game from starting. Every exception, not only IOException:
                // a folder the user cannot read raises
                // UnauthorizedAccessException, which is not one -- and an
                // install under Program Files is exactly where that happens.
            }
        }

        private static void ParseBind(PropertyInfo property, string value)
        {
            string[] parts = value.Split(':', 2);
            string type = parts[0].Trim();
            string name = parts.Length > 1 ? parts[1].Trim() : "";
            if (type == "ScrollUp")
            {
                Rebind(property, ButtonType.ScrollUp, Keys.Unknown, MouseButton.Left);
            }
            else if (type == "ScrollDown")
            {
                Rebind(property, ButtonType.ScrollDown, Keys.Unknown, MouseButton.Left);
            }
            else if (type == "Mouse" && Enum.TryParse(name, out MouseButton button))
            {
                Rebind(property, ButtonType.Mouse, Keys.Unknown, button);
            }
            else if (type == "Key" && Enum.TryParse(name, out Keys key))
            {
                Rebind(property, ButtonType.Key, key, MouseButton.Left);
            }
        }

        public static void Save()
        {
            try
            {
                var lines = new List<string>
                {
                    $"# {Branding.Name} controls. Delete a line to go back to the default.",
                    $"sensitivity={MouseSensitivity.ToString("0.###", CultureInfo.InvariantCulture)}",
                    $"invert_y={InvertMouseY.ToString().ToLowerInvariant()}",
                    $"invert_x={InvertMouseX.ToString().ToLowerInvariant()}",
                    $"scroll_all_weapons={ScrollAllWeapons.ToString().ToLowerInvariant()}"
                };
                foreach (PropertyInfo property in Bindings)
                {
                    Keybind bind = Bind(property);
                    string value = bind.Type switch
                    {
                        ButtonType.Mouse => $"Mouse:{bind.MouseButton}",
                        ButtonType.ScrollUp => "ScrollUp",
                        ButtonType.ScrollDown => "ScrollDown",
                        _ => $"Key:{bind.Key}"
                    };
                    lines.Add($"{property.Name}={value}");
                }
                File.WriteAllLines(Path, lines);
            }
            catch (Exception)
            {
                // Same reason as Load: the folder beside the executable is not
                // guaranteed to be writable, and losing a rebind is a far
                // smaller thing than taking down the window that made it --
                // which, from the pause menu, is the thread the menu runs on.
            }
        }

        /// <summary>Put everything back the way it shipped.</summary>
        public static void Reset()
        {
            _creating = true;
            _current = PlayerControls.GetDefault();
            _creating = false;
            MouseSensitivity = 1;
            InvertMouseY = false;
            InvertMouseX = false;
            ScrollAllWeapons = true;
        }
    }
}
