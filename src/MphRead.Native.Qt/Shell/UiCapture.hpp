#pragma once

#include <QtCore/QString>
#include <QtCore/QVariantList>

namespace MphRead::Qt
{
    class UiCapture final
    {
    public:
        UiCapture() = delete;

        // Writes DIR/<screen>.png for each screen -uishot knows; 0 when all were.
        [[nodiscard]] static int Run(const QString& directory);
    };
}
