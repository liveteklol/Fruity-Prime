#pragma once
#include <memory>

namespace MphRead { class Scene; }
namespace MphRead::Mods::Diagnostics
{
    // Read-only witness at the actual transition, before GPU release and after
    // rebuild, with no intervening simulation. Not a future-time actor predicate.
    class RendererSwitchWitness final
    {
    public:
        explicit RendererSwitchWitness(Scene& scene);
        ~RendererSwitchWitness();
        void Validate(Scene& scene, bool report = true) const;
        // Diagnostic negative controls: perturb and restore fields before any
        // simulation/draw, proving that the witness rejects semantic changes.
        void CheckRejections(Scene& scene) const;
    private:
        struct State;
        std::unique_ptr<State> _state;
    };
}
