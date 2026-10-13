#include "PlayerEntity.hpp"
#include "../../Scene.hpp"
#include "../../Mods/Input/HostTouch.hpp"
#include "../../Mods/Input/GamepadInput.hpp"
#include "../../Mods/Input/StylusZone.hpp"
#include "../../Mods/Gameplay/NativeGameplayClock.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include "Mods/Network/Testing/Harness/NetTestScript.hpp"
#include "../../Entities/CamSeq/CameraSequence.hpp"
#include "../../Mods/SpectatorMode.hpp"
#include <cmath>

namespace MphRead::Entities
{
    namespace Input = Mods::Input;
    void PlayerEntity::PrepareAimInput()
    {
        const auto previous = _aimFrame.Source;
        _aimFrame = {};
        if (_isBot)
        {
            _aimFrame.Owner = Input::AimOwner::Bot;
            _aimFrame.Source = Input::AimSource::Dual;
        }
        else if (Mods::Network::NetSession::Active() && _slotIndex != Mods::Network::NetHooks::LocalSlot())
        {
            _aimFrame.Owner = Input::AimOwner::Remote;
            _aimFrame.Source = Input::AimSource::Network;
        }
        else if (Mods::SpectatorMode::IsSpectating())
        {
            _aimFrame.Owner = Input::AimOwner::Spectator;
        }
        else if (this != Main().get())
        {
            _aimFrame.Owner = Input::AimOwner::Remote;
        }
        else if (Mods::Network::NetTestScript::Enabled())
        {
            _aimFrame.Source = Input::AimSource::Script;
        }
        else
        {
            const bool blocked = TestFlag(_flags1, PlayerFlags1::NoAimInput)
                || (Formats::CameraSequence::Current() && TestFlag(Formats::CameraSequence::Current()->Flags(), Formats::CamSeqFlags::BlockInput))
                || _scene->FrameAdvance() || _scene->FrameAdvanceLastFrame();
            // Classic aim applies the DS's rules on every 60 Hz step. Only the
            // strict ROM cadence (-nativeaim) reads the 30 Hz touch sample or
            // makes the keys wait; mouse, touch and pen never wait otherwise.
            const bool classic = _controls.NativeAim();
            const bool rom = classic && _controls.NativeControl().RomCadence;
            const bool nativeTouch = rom && ((_controls.NativeControl().Flags & 2U)
                || Input::HostTouch::Published()
                || (Input::PointerDevice::Active() && Input::StylusZone::Enabled()));
            if (!blocked && nativeTouch)
            {
                _aimFrame.Source = Input::AimSource::Touch;
                _aimFrame.Native = true;
            }
            else if (rom && !nativeTouch)
            {
                _aimFrame.Source = Input::AimSource::Dual;
                _aimFrame.Native = true; // Dual consumes old state even with NoAimInput.
            }
            else if (!blocked)
            {
                const bool keys = _controls.KeyboardAim() && (_controls.AimLeft().IsDown() || _controls.AimRight().IsDown()
                    || _controls.AimUp().IsDown() || _controls.AimDown().IsDown());
                const bool mouse = _controls.MouseAim() && (_input.MouseDeltaX() != 0 || _input.MouseDeltaY() != 0);
                if (keys || (!_controls.MouseAim() && _controls.KeyboardAim()))
                    _aimFrame.Source = Input::AimSource::Dual;
                else if (mouse) _aimFrame.Source = Input::AimSource::Mouse;
                else if (Input::GamepadInput::AimDeltaX() != 0 || Input::GamepadInput::AimDeltaY() != 0)
                    _aimFrame.Source = Input::AimSource::Gamepad;
                else if (previous == Input::AimSource::Dual
                    && (std::fabs(_nativeDual.X) >= 0.000001F || std::fabs(_nativeDual.Y) >= 0.000001F))
                    _aimFrame.Source = Input::AimSource::Dual;
                else if (_controls.MouseAim()) _aimFrame.Source = Input::AimSource::Mouse;
                else if (_controls.KeyboardAim()) _aimFrame.Source = Input::AimSource::Dual;
                // Neither mouse nor keys aim: the pad is the only aim device left.
                else _aimFrame.Source = Input::AimSource::Gamepad;
                // The source is the PC's; what it does to the aim is the DS's.
                _aimFrame.Native = classic && _aimFrame.Source != Input::AimSource::None;
            }
            _aimFrame.Exact = !_aimFrame.Native || (_controls.NativeControl().Flags & 0x20U) != 0;
        }
        if (_aimFrame.Source != Input::AimSource::Dual || previous != Input::AimSource::Dual)
        {
            _nativeDual = {};
            if (!_isBot) _buttonAimX = _buttonAimY = 0;
        }
        if (_aimTrace.Enabled)
        {
            _aimTrace.Owner = _aimFrame.Owner; _aimTrace.Source = _aimFrame.Source;
            _aimTrace.SimulationStep = _scene->FrameCount();
            _aimTrace.NativeSequence = _input.TouchSample().NativeSampleSequence();
            _aimTrace.NativeSample = _input.TouchSample().NewNativeSampleThisStep();
            _aimTrace.NativeGameplayTick = Mods::Gameplay::NativeGameplayClock::IsNativeTick(_scene->FrameCount());
        }
    }

    void PlayerEntity::ApplyLocalAim(bool alt)
    {
        if (_aimFrame.Owner != Input::AimOwner::Local) return;
        if (_aimFrame.Source == Input::AimSource::Touch) ApplyTouchAim();
        else if (_aimFrame.Source == Input::AimSource::Dual) ApplyDualAim(alt);
        else if (_aimFrame.Source == Input::AimSource::Mouse) ApplyMouseAim();
        else if (_aimFrame.Source == Input::AimSource::Gamepad) ApplyGamepadAim();
    }
}
