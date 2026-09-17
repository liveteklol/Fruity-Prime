using MphRead.Formats;

namespace MphRead.Entities
{
    public partial class PlayerEntity
    {
        private void ModClearAltFlick()
        {
            AltFlickRequested = false;
            AltFlickX = 0;
            AltFlickY = 0;
        }

        private bool ModConsumeAltFlick(out float x, out float y)
        {
            bool acceptsFlick = IsAltForm && !IsMorphing && !IsUnmorphing
                && _health > 0 && _frozenTimer == 0
                && (_abilities.TestFlag(AbilityFlags.Boost)
                    || _abilities.TestFlag(AbilityFlags.SpireAltAttack));
            if (acceptsFlick && !_abilities.TestFlag(AbilityFlags.SpireAltAttack))
            {
                ModCheckMouseFlick();
            }
            bool flick = acceptsFlick && AltFlickRequested;
            x = AltFlickX;
            y = AltFlickY;
            ModClearAltFlick();
            return flick;
        }

        private void ModPrepareSpireFlick()
        {
            if (!_abilities.TestFlag(AbilityFlags.SpireAltAttack)) return;
            if (IsAltForm && !IsMorphing && !IsUnmorphing && _health > 0 && _frozenTimer == 0)
            {
                ModCheckMouseFlick();
                if (AltFlickRequested)
                {
                    // Network press history is captured after this input pass, before
                    // ProcessAlt. An edge created in ProcessAlt never reaches the wire.
                    Controls.AltAttack.IsPressed = true;
                    Input.HasInput = true;
                }
            }
            ModClearAltFlick();
        }

        /// <summary>
        /// Read this frame's mouse movement as an alt-form flick, using the
        /// same gesture state as the touch head.
        ///
        /// A partial rather than a dozen lines inside <c>ProcessAlt</c>: the
        /// gesture wants the player's own <c>Input</c> deltas and its flags,
        /// both private, and the alternative is widening four of them. What
        /// upstream carries is the one call.
        ///
        /// The gates are all "is the mouse currently saying something else":
        /// the weapon wheel is answered by dragging, the boost bind being
        /// held is a charge the player is building on purpose and a flick
        /// would cut it short at a charge they did not choose, and a camera
        /// sequence or a frame-advance step is not a frame anybody moved a
        /// mouse in. There is no gate for the form, the hunter or the
        /// ability, because the shared consumer checks all three.
        ///
        /// Every player runs <c>ProcessAlt</c> -- bots, and the puppets
        /// standing in for other people -- and exactly one of them has a
        /// mouse, so the others leave before they can clear the history the
        /// main player is building. See <see cref="Mods.Input.MouseFlick"/>.
        /// </summary>
        private void ModCheckMouseFlick()
        {
            if (!IsMainPlayer || IsBot)
            {
                return;
            }
            bool chargingBoost = _abilities.TestFlag(AbilityFlags.Boost) && Controls.Boost.IsDown;
            if (!Controls.MouseAim || chargingBoost
                || Flags1.TestFlag(PlayerFlags1.NoAimInput)
                || Flags1.TestFlag(PlayerFlags1.WeaponMenuOpen)
                || Mods.SpectatorMode.IsSpectating
                || _scene.FrameAdvance || _scene.FrameAdvanceLastFrame
                || CameraSequence.Current?.Flags.TestFlag(CamSeqFlags.BlockInput) == true)
            {
                Mods.Input.MouseFlick.Reset();
                return;
            }
            if (Mods.Input.MouseFlick.Check(Input.MouseDeltaX, Input.MouseDeltaY,
                _scene.FrameCount, out float dirX, out float dirY))
            {
                AltFlickRequested = true;
                AltFlickX = dirX;
                AltFlickY = dirY;
            }
        }
    }
}
