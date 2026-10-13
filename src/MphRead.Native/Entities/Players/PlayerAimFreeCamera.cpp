#include "PlayerEntity.hpp"
#include "../../Scene.hpp"
#include "../../Mods/InputSettings.hpp"
#include <tuple>
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Integration/Hooks/NetHooks.hpp"
#include <algorithm>
#include <cmath>

namespace MphRead::Entities
{
    namespace Native = Mods::Input::NativeAim;
    void PlayerEntity::ApplyNativeFreeCamera(::MphRead::Entities::CameraInfo& camera)
    {
        if (_isBot || (Mods::Network::NetSession::Active() && _slotIndex != Mods::Network::NetHooks::LocalSlot())) return;
        const auto& control = _controls.NativeControl();
        // A camera is not gameplay: keys respond on every 60 Hz step, with
        // half-step rates so a held key turns as fast as on the DS tick.
        // EU1.1 0201ABB0: FreeCamera makes this step's velocity first.
        _nativeDual.Produce(ReadNativeAimButtons(), control, Native::Form::FreeCamera, true);
        float x = _nativeDual.X * 0.5F, y = _nativeDual.Y * 0.5F;
        if (_controls.InvertAimX()) x = -x;
        if (_controls.InvertAimY()) y = -y;
        const auto pitch = OpenTK::Mathematics::MathHelper::DegreesToRadians(std::clamp(
            OpenTK::Mathematics::MathHelper::RadiansToDegrees(std::asin(std::clamp(camera.Facing.Y, -1.0F, 1.0F))) + y, -80.0F, 80.0F));
        const auto yaw = std::atan2(camera.Facing.X, camera.Facing.Z) + OpenTK::Mathematics::MathHelper::DegreesToRadians(x);
        camera.Facing = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    }

    void PlayerEntity::ReadFreeCameraPointer(float& aimX, float& aimY)
    {
        if (!Controls().MouseAim() || IsBot()) return;
        aimY = -_input.MouseDeltaY() / 4.0F * Mods::InputSettings::MouseSensitivity();
        aimX = -_input.MouseDeltaX() / 4.0F * Mods::InputSettings::MouseSensitivity();
    }

    void PlayerEntity::ReadDirectFreeCameraAim(float& aimX, float& aimY)
    {
        if (Controls().MouseAim() && !IsBot())
        {
            if (!Controls().KeyboardAim()
                || (!Controls().AimUp().IsDown() && !Controls().AimDown().IsDown()))
            {
                aimY = -_input.MouseDeltaY() / 4.0F
                    * Mods::InputSettings::MouseSensitivity();
            }
            if (!Controls().KeyboardAim()
                || (!Controls().AimLeft().IsDown() && !Controls().AimRight().IsDown()))
            {
                aimX = -_input.MouseDeltaX() / 4.0F
                    * Mods::InputSettings::MouseSensitivity();
            }
        }

        if (Controls().KeyboardAim() || IsBot())
        {
            const float maxAimX = _maxButtonAimX * 30.0F;
            const float aimStepX = maxAimX * 0.01F;
            const float maxAimY = _maxButtonAimY * 30.0F;
            const float aimStepY = maxAimY * 0.01F;

            if (Controls().AimRight().IsDown())
            {
                std::tie(_buttonAimX, aimX) = ConstantAcceleration(
                    -aimStepX, _buttonAimX, -maxAimX, -maxAimX * 0.4F);
            }
            else if (Controls().AimLeft().IsDown())
            {
                std::tie(_buttonAimX, aimX) = ConstantAcceleration(
                    aimStepX, _buttonAimX, maxAimX * 0.4F, maxAimX);
            }
            else if (_buttonAimX != 0.0F)
            {
                if ((_buttonAimX > 0.0F && _buttonAimX < 0.000001F)
                    || (_buttonAimX < 0.0F && _buttonAimX > -0.000001F))
                {
                    _buttonAimX = 0.0F;
                }
                else
                {
                    float updateAimX;
                    std::tie(_buttonAimX, updateAimX) = Drag(0.4F, _buttonAimX);
                    if (aimX == 0.0F)
                    {
                        aimX = updateAimX;
                    }
                }
            }

            if (Controls().AimUp().IsDown())
            {
                std::tie(_buttonAimY, aimY) = ConstantAcceleration(
                    aimStepY, _buttonAimY, maxAimY * 0.4F, maxAimY);
            }
            else if (Controls().AimDown().IsDown())
            {
                std::tie(_buttonAimY, aimY) = ConstantAcceleration(
                    -aimStepY, _buttonAimY, -maxAimY, -maxAimY * 0.4F);
            }
            else if (_buttonAimY != 0.0F)
            {
                if ((_buttonAimY > 0.0F && _buttonAimY < 0.000001F)
                    || (_buttonAimY < 0.0F && _buttonAimY > -0.000001F))
                {
                    _buttonAimY = 0.0F;
                }
                else
                {
                    float updateAimY;
                    std::tie(_buttonAimY, updateAimY) = Drag(0.4F, _buttonAimY);
                    if (aimY == 0.0F)
                    {
                        aimY = updateAimY;
                    }
                }
            }
        }

    }

}
