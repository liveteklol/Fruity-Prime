#include "PreviewCamera.hpp"

#include "../../Scene.hpp"

namespace MphRead
{
    void Scene::SetPreviewCamera(OpenTK::Mathematics::Vector3 position,
        OpenTK::Mathematics::Vector3 target)
    {
        using OpenTK::Mathematics::Vector3;

        _cameraMode = CameraMode::Roam;
        _inputMode = InputMode::CameraOnly;
        _cameraPosition = position;
        Vector3 facing = target - position;
        _cameraFacing = facing.LengthSquared() < 0.0001f ? -Vector3::UnitZ : facing.Normalized();
        _cameraRight = Vector3::Cross(_cameraFacing, Vector3::UnitY);
        _cameraRight = _cameraRight.LengthSquared() < 0.0001f
            ? Vector3::UnitX
            : _cameraRight.Normalized();
        _cameraUp = Vector3::Cross(_cameraRight, _cameraFacing).Normalized();
    }

    // The same, turned so that up is as near this as the facing allows: a
    // Quake portal camera's, which is not the world's.
    void Scene::SetPreviewCamera(OpenTK::Mathematics::Vector3 position,
        OpenTK::Mathematics::Vector3 target, OpenTK::Mathematics::Vector3 up)
    {
        using OpenTK::Mathematics::Vector3;

        SetPreviewCamera(position, target);
        const Vector3 right = Vector3::Cross(_cameraFacing, up);
        if (right.LengthSquared() > 1.0e-8F)
        {
            _cameraRight = right.Normalized();
            _cameraUp = Vector3::Cross(_cameraRight, _cameraFacing).Normalized();
        }
    }
}
