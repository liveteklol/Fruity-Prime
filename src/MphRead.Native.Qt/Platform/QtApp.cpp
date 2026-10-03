#include "QtApp.hpp"

#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

#include <memory>

namespace MphRead::Qt
{
    namespace
    {
        std::unique_ptr<QGuiApplication>& Application()
        {
            static std::unique_ptr<QGuiApplication> app;
            return app;
        }
    }

    void ShutdownApplication()
    {
        // Before static destruction: Qt's own thread storage is gone by then.
        Application().reset();
    }

    void EnsureApplication()
    {
        if (QCoreApplication::instance() != nullptr)
        {
            return;
        }
        // Qt keeps the argc reference for the application's lifetime.
        static int argc = 1;
        static char name[] = "FruityPrime";
        static char* argv[] = {name, nullptr};
        QCoreApplication::setApplicationName(QStringLiteral("Fruity Prime"));
        QCoreApplication::setOrganizationName(QStringLiteral("FruityPrime"));
        // The menus render through QRhi into a texture the game composites, on
        // the same API as the game: OpenGL until the RHI's Vulkan backend.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        // Glyphs rasterised by the font engine at their size, as Skia does
        // for Avalonia: the distance-field default softens a pixel face at
        // the launcher's small sizes until it reads as blurred.
        const QByteArray text = qgetenv("FP_QT_TEXT");
        QQuickWindow::setTextRenderType(text == "qt" ? QQuickWindow::QtTextRendering
                                        : text == "curve" ? QQuickWindow::CurveTextRendering
                                                          : QQuickWindow::NativeTextRendering);
        // Wayland's client-side decorations move an OpenGL window's default
        // framebuffer off 0 into Qt's own FBO; the renderer draws to 0.
        if (!qEnvironmentVariableIsSet("QT_WAYLAND_DISABLE_WINDOWDECORATION"))
        {
            qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
        }
        Application() = std::make_unique<QGuiApplication>(argc, argv);
        // Grey antialiasing with hinting: aliased glyphs read as pixelated
        // (FP_QT_TEXT_AA=0 brings them back). Never subpixel colour, which
        // would sit on the wrong pixels of a texture blended over the game.
        QFont font = QGuiApplication::font();
        font.setStyleStrategy(qgetenv("FP_QT_TEXT_AA") == "0"
            ? QFont::StyleStrategy(QFont::NoAntialias | QFont::NoSubpixelAntialias)
            : QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
        font.setHintingPreference(QFont::PreferVerticalHinting);
        QGuiApplication::setFont(font);
    }
}
