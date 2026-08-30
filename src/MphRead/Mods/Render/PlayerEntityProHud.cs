using System;
using MphRead.Formats;
using MphRead.Hud;
using MphRead.Text;
using OpenTK.Mathematics;

namespace MphRead.Entities
{
    /// <summary>
    /// The readouts Pro mode HUD draws in place of the game's own.
    ///
    /// A partial of PlayerEntity for the reason the net HUD and the aim
    /// injection are: the HUD's text and box drawing are private to it, and
    /// reaching them from here costs upstream a handful of call sites in
    /// DrawHudObjects instead of opening the whole HUD up.
    ///
    /// What the stock HUD does with energy and ammo is draw them into the
    /// helmet -- a tank meter along a moulded panel, a number in the visor's
    /// green, both swaying with the idle animation. Pro mode has no helmet to
    /// draw them into, so they are drawn the way a shooter with no helmet
    /// draws them: flat, still, in the corners, in colours that say something
    /// on their own. No sprite assets are involved, only boxes and the game's
    /// own font, so this costs nothing to load and works in any room.
    /// </summary>
    public partial class PlayerEntity
    {
        /// <summary>
        /// Below these fractions of a full tank the readout goes amber, then
        /// red -- 60 and 33 of 99, which is where GetCrosshairColor turns.
        /// </summary>
        private const float ProHudWarn = 60 / 99f;
        private const float ProHudDanger = 33 / 99f;

        private static readonly Vector4 ProHudPanel = new Vector4(0, 0, 0, 0.5f);
        private static readonly Vector4 ProHudShade = new Vector4(0, 0, 0, 0.55f);
        private static readonly Vector4 ProHudTrack = new Vector4(1, 1, 1, 0.16f);
        private static readonly ColorRgba ProHudInk = new ColorRgba(235, 238, 245, 255);
        private static readonly ColorRgba ProHudDim = new ColorRgba(178, 186, 200, 255);
        private static readonly ColorRgba ProHudShadow = new ColorRgba(0, 0, 0, 255);

        /// <summary>Energy, ammo and the score, in whichever layout is being tried.</summary>
        private void DrawProHud()
        {
            switch (Mods.ProHudStyle.Current)
            {
                case 2:
                    DrawProHudCorners();
                    break;
                case 3:
                    DrawProHudColumn();
                    break;
                case 4:
                    DrawProHudCentre();
                    break;
                default:
                    DrawProHudStrip();
                    break;
            }
        }

        // ------------------------------------------------------------ pieces

        /// <summary>
        /// How full the bar is, where full is a hunter's own energy and not
        /// the most they could possibly be carrying.
        ///
        /// A multiplayer hunter spawns with 99 and can pick up to 199 -- the
        /// stock HUD draws that as two meters, one stacked on the other, for
        /// exactly this reason. Measuring against 199 instead would have a
        /// player who has taken no damage at all looking half dead, which is
        /// what it did first: 99 of 199 is amber.
        /// </summary>
        private float ProHealthFraction()
        {
            return Math.Clamp(_health / (float)ProHealthSpan(), 0, 1);
        }

        private int ProHealthSpan()
        {
            if (GameState.Multiplayer)
            {
                return Math.Max(Values.EnergyTank - 1, 1);
            }
            return Math.Max(_healthMax, 1);
        }

        /// <summary>Carrying more than one hunter's worth: Quake's overhealth, and worth saying so.</summary>
        private bool ProHealthOver()
        {
            return GameState.Multiplayer && _health > Values.EnergyTank - 1;
        }

        /// <summary>
        /// Green, amber, red -- the same three, at the same thresholds, the
        /// custom crosshair uses, so the two never disagree about how much
        /// trouble you are in. Over a full tank it goes pale blue, which is
        /// the one state the crosshair has no way to show.
        /// </summary>
        private Vector4 ProHealthColor()
        {
            if (ProHealthOver())
            {
                return new Vector4(0.45f, 0.8f, 1f, 1);
            }
            float fraction = ProHealthFraction();
            if (fraction > ProHudWarn)
            {
                return new Vector4(0.24f, 0.85f, 0.32f, 1);
            }
            if (fraction > ProHudDanger)
            {
                return new Vector4(1f, 0.68f, 0.1f, 1);
            }
            return new Vector4(0.95f, 0.18f, 0.18f, 1);
        }

        private static ColorRgba ProInk(Vector4 color)
        {
            return new ColorRgba((byte)(color.X * 255), (byte)(color.Y * 255), (byte)(color.Z * 255), 255);
        }

        /// <summary>The equipped weapon's shots left, or null where there is no such number.</summary>
        private string? ProAmmoText()
        {
            if (IsAltForm || IsMorphing || IsUnmorphing)
            {
                return null;
            }
            WeaponInfo info = EquipInfo.Weapon;
            if (info.AmmoCost == 0)
            {
                return null;
            }
            int amount = _ammo[info.AmmoType];
            return amount < 0 ? "--" : (amount / info.AmmoCost).ToString();
        }

        /// <summary>
        /// A bar, on a track, in a dark surround. Widths are in units of the
        /// screen's height, like everything else here -- see HudAspectFix --
        /// so the caller states one number and it is the same size on any
        /// window.
        ///
        /// Three layers rather than two because a HUD is drawn over whatever
        /// the room happens to be: a dark track is invisible in a dark
        /// corridor and a pale one is invisible in a lit hall, so the track is
        /// pale, the surround behind it is dark, and the pair of them read
        /// against both.
        /// </summary>
        private void ProBar(float x, float y, float width, float height, float fill, Vector4 color)
        {
            float aspect = HudAspectFix;
            float pad = 1f;
            _scene.DrawHudFlatBox(x - pad * aspect, y - pad,
                x + (width + pad) * aspect, y + height + pad, ProHudShade);
            _scene.DrawHudFlatBox(x, y, x + width * aspect, y + height, ProHudTrack);
            if (fill > 0)
            {
                _scene.DrawHudFlatBox(x, y, x + width * fill * aspect, y + height, color);
            }
        }

        /// <summary>
        /// A readout, with its own shadow under it.
        ///
        /// The stock HUD never needed this: everything it draws sits on the
        /// helmet, which is an opaque backing of its own. With the helmet gone
        /// the numbers are over the room, and a red 15 over red rock is a
        /// number you cannot read at the moment you most need to.
        /// </summary>
        private void ProNumber(float x, float y, Align align, ReadOnlySpan<char> text,
            ColorRgba color, float scale)
        {
            float aspect = HudAspectFix;
            DrawText2D(x + 0.8f * aspect, y + 0.8f, align, palette: 0, text, ProHudShadow, scale: scale);
            DrawText2D(x, y, align, palette: 0, text, color, scale: scale);
        }

        /// <summary>
        /// The mode's own score line, in two parts: what it is called, small,
        /// and what it says, large. The stock HUD draws the same two strings
        /// (see DrawModeScore), which is suppressed while this is on.
        /// </summary>
        private void ProScore(float x, float y, Align align, float scale)
        {
            string label = Strings.GetHudMessage(ProScoreMessageId());
            ProNumber(x, y, align, label, ProHudDim, 0.55f);
            ProNumber(x, y + 8, align, FormatModeScore(MainPlayerIndex), ProHudInk, scale);
        }

        /// <summary>
        /// What this mode calls its score, from the game's own strings -- the
        /// same id each mode's own DrawHud* method passes to DrawModeScore, so
        /// the label does not quietly become "points" in a mode that counts
        /// octoliths.
        /// </summary>
        private static int ProScoreMessageId()
        {
            switch (GameState.Mode)
            {
                case GameMode.Survival:
                case GameMode.SurvivalTeams:
                    return 213; // lives left
                case GameMode.PrimeHunter:
                    return 214; // prime time
                case GameMode.Bounty:
                case GameMode.BountyTeams:
                    return 215; // octoliths
                case GameMode.Capture:
                    return 216; // octoliths
                case GameMode.Defender:
                case GameMode.DefenderTeams:
                    return 217; // ring time
                case GameMode.Nodes:
                case GameMode.NodesTeams:
                    return 218; // points
                default:
                    return 212; // points
            }
        }

        // ------------------------------------------------------------ styles

        /// <summary>
        /// Style 1: the strip. A dark band across the foot of the screen with
        /// energy at the left of it and ammo at the right, and the score up in
        /// the corner. Quake 3's status bar, which is the shape the reference
        /// shot is.
        /// </summary>
        private void DrawProHudStrip()
        {
            float aspect = HudAspectFix;
            Vector4 health = ProHealthColor();
            _scene.DrawHudFlatBox(0, 168, 256, 192, ProHudPanel);
            // A block of the health colour standing in for the energy icon:
            // the same job the cross does in the reference, with no art.
            _scene.DrawHudFlatBox(52 * aspect, 173, 57 * aspect, 186, health);
            ProNumber(62 * aspect, 171, Align.Left, _health.ToString(), ProInk(health), 1.8f);
            ProBar(52 * aspect, 187, 62, 3, ProHealthFraction(), health);
            string? ammo = ProAmmoText();
            if (ammo != null)
            {
                ProNumber(256 - 8 * aspect, 171, Align.Right, ammo, ProHudInk, 1.8f);
            }
            ProScore(4 * aspect, 12, Align.Left, 1.1f);
        }

        /// <summary>
        /// Style 2: corners, and nothing else. No panel at all -- the numbers
        /// are large enough to read against the room, with a bar under the
        /// energy one carrying the same reading in a form you can take in
        /// without reading it.
        /// </summary>
        private void DrawProHudCorners()
        {
            float aspect = HudAspectFix;
            Vector4 health = ProHealthColor();
            ProNumber(52 * aspect, 162, Align.Left, _health.ToString(), ProInk(health), 2.4f);
            ProBar(52 * aspect, 183, 66, 3, ProHealthFraction(), health);
            string? ammo = ProAmmoText();
            if (ammo != null)
            {
                ProNumber(256 - 6 * aspect, 162, Align.Right, ammo, ProHudInk, 2.4f);
            }
            ProScore(4 * aspect, 12, Align.Left, 1.3f);
        }

        /// <summary>
        /// Style 3: one column. Everything the player owns reads top to bottom
        /// down the left edge -- score, then the weapon list, then energy --
        /// so there is one place to look instead of three corners.
        /// </summary>
        private void DrawProHudColumn()
        {
            float aspect = HudAspectFix;
            Vector4 health = ProHealthColor();
            _scene.DrawHudFlatBox(2 * aspect, 170, 46 * aspect, 190, ProHudPanel);
            ProNumber(6 * aspect, 172, Align.Left, _health.ToString(), ProInk(health), 1.5f);
            ProBar(4 * aspect, 186, 40, 3, ProHealthFraction(), health);
            string? ammo = ProAmmoText();
            if (ammo != null)
            {
                ProNumber(44 * aspect, 175, Align.Right, ammo, ProHudInk, 0.9f);
            }
            ProScore(4 * aspect, 12, Align.Left, 1.1f);
        }

        /// <summary>
        /// Style 4: under the crosshair. Energy on a bar in the middle of the
        /// foot of the screen, where the eye already is, with the number on
        /// one side of it and ammo on the other.
        /// </summary>
        private void DrawProHudCentre()
        {
            float aspect = HudAspectFix;
            Vector4 health = ProHealthColor();
            ProNumber(128 - 48 * aspect, 170, Align.Right, _health.ToString(), ProInk(health), 1.6f);
            ProBar(128 - 44 * aspect, 175, 88, 6, ProHealthFraction(), health);
            string? ammo = ProAmmoText();
            if (ammo != null)
            {
                ProNumber(128 + 48 * aspect, 170, Align.Left, ammo, ProHudInk, 1.6f);
            }
            ProScore(128, 12, Align.Center, 1.2f);
        }
    }
}
