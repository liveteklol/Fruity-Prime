// The few pieces of the Avalonia launcher that code outside it still reaches
// for. Each goes away with the QML screen or port that replaces its caller:
//   ToUtf8           Renderer's text input (move to NativeRuntime/System)
//   KeyRow           Renderer asks whether a binding row is listening (QML settings)
//   GamepadUiChecks  the -gamepadcheck launcher walk (QML gamepad setup)
//   LauncherPhoto    drawn by the QML front page instead; never enabled here

#include "../../MphRead.Native/Mods/Launcher/Gui/GamepadUiChecks.hpp"
#include "../../MphRead.Native/Mods/Launcher/Gui/KeyRow.hpp"
#include "../../MphRead.Native/Mods/Render/LauncherPhoto.hpp"
#include "../../MphRead.Native/NativeRuntime/Avalonia/Media.hpp"

#include <QtCore/QString>

#include <iostream>

namespace MphRead::NativeRuntime::Avalonia::Media
{
    std::string ToUtf8(std::u32string_view text)
    {
        return QString::fromUcs4(text.data(), static_cast<qsizetype>(text.size())).toStdString();
    }
}

namespace MphRead::Mods::Launcher::Gui
{
    bool KeyRow::_anyListening = false;

    void GamepadUiChecks::Run(const std::optional<std::string>& shots)
    {
        (void)shots;
        std::cout << "[gamepadcheck] the launcher walk is not ported to the Qt menus yet\n";
    }
}

namespace MphRead::Mods::Render
{
    void LauncherPhoto::Enabled(bool value) noexcept
    {
        (void)value;
    }

    bool LauncherPhoto::Enabled() noexcept
    {
        return false;
    }

    void LauncherPhoto::Draw(std::int32_t width, std::int32_t height)
    {
        (void)width;
        (void)height;
    }

    void LauncherPhoto::Release() noexcept
    {
    }
}
