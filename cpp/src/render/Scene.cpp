#include "Scene.h"

#include "formats/Metadata.h"

#include <QtGlobal>

#include <cmath>
#include <numbers>

namespace fp {

Mat4 ModelInstance::modelTransform(double seconds) const
{
    Mat4 result = includeModelScale ? Mat4::scale(model->scale()) : Mat4::identity();
    if (spinDegreesPerSecond == 0 && !floats) {
        return result * transform;
    }
    const double spin = std::fmod(spinStartDegrees + seconds * spinDegreesPerSecond, 360.0);
    const float radians = static_cast<float>(spin * std::numbers::pi / 180.0);
    if (spinDegreesPerSecond != 0) {
        // Matrix.GetTransformSRT with only the rotation set: X, then Y, then Z.
        result *= Mat4::rotationX(spinAxis[0] * radians) * Mat4::rotationY(spinAxis[1] * radians)
            * Mat4::rotationZ(spinAxis[2] * radians);
    }
    result *= transform;
    if (floats) {
        result.m[3][1] += (std::sin(radians) + 1.0f) / 8.0f;
    }
    return result;
}

const Model* Scene::model(const std::filesystem::path& root, const std::string& name)
{
    if (auto it = m_byName.find(name); it != m_byName.end()) {
        return it->second;
    }
    const Model* loaded = nullptr;
    if (const ModelMetadata* meta = findModel(name)) {
        try {
            models.push_back(std::make_unique<Model>(Model::load(root, *meta)));
            loaded = models.back().get();
        } catch (const std::exception& e) {
            qWarning("model %s: %s", name.c_str(), e.what());
        }
    } else {
        qWarning("model %s: not in the metadata table", name.c_str());
    }
    m_byName[name] = loaded;
    return loaded;
}

} // namespace fp
