#pragma once

#include <string>

namespace MphRead::NativeRuntime
{
    // A message a person will see even when the program has no console (the
    // Windows build is a GUI binary): a native dialog on Windows, standard
    // error elsewhere. For failures that stop the window from opening at all.
    void ShowErrorDialog(const std::string& title, const std::string& message) noexcept;
}
