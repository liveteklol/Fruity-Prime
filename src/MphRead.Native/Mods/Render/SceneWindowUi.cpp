#include "SceneWindowUi.hpp"

#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/Rhi/WindowUi.hpp"

#include <memory>

namespace MphRead::Mods::Render
{
    namespace
    {
        std::unique_ptr<NativeRuntime::Rhi::WindowUi>& Instance()
        {
            static std::unique_ptr<NativeRuntime::Rhi::WindowUi> ui;
            return ui;
        }
    }

    bool SceneWindowUi::Active() noexcept
    {
        return NativeRuntime::Rhi::ScenePresentsWindow();
    }

    NativeRuntime::Rhi::WindowUi* SceneWindowUi::Get()
    {
        if (!Active()) return nullptr;
        auto& ui = Instance();
        if (!ui) ui = NativeRuntime::Rhi::CreateSceneWindowUi(NativeRuntime::Rhi::SceneDevice());
        return ui.get();
    }

    void SceneWindowUi::Release() noexcept
    {
        Instance().reset();
    }
}
