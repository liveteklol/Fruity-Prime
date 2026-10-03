#pragma once

class QKeyEvent;

namespace MphRead::Qt
{
    // The GLFW key code (OpenTK's Keys) for a key event, or -1. GLFW names keys
    // by their position on a US keyboard, whatever the layout, and every saved
    // binding is such a code; so the physical scan code decides where the
    // platform gives one, and Qt's layout-dependent key only as a fallback.
    [[nodiscard]] int GlfwKey(const QKeyEvent& event) noexcept;
}
