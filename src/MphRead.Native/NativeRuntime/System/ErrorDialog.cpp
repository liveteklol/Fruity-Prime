#include "ErrorDialog.hpp"

#include <iostream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MphRead::NativeRuntime
{
    namespace
    {
#if defined(_WIN32)
        std::wstring Wide(const std::string& text)
        {
            if (text.empty()) return {};
            const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }
#endif
    }

    void ShowErrorDialog(const std::string& title, const std::string& message) noexcept
    {
        try
        {
            std::cerr << title << ": " << message << std::endl;
#if defined(_WIN32)
            ::MessageBoxW(nullptr, Wide(message).c_str(), Wide(title).c_str(), MB_OK | MB_ICONERROR);
#endif
        }
        catch (...)
        {
        }
    }
}
