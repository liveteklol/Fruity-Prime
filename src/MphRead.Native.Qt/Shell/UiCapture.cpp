// FP_QT_UISHOT=DIR: the Qt menus photographed without a window, at the size
// and under the names the Avalonia launcher's -uishot uses, so the two sets
// can be laid side by side. It proves the layout, not the window manager.

#include "UiCapture.hpp"

#include "FocusNav.hpp"
#include "QmlTypes.hpp"

#include "PlayModel.hpp"
#include "ShellBridge.hpp"

#include "../../MphRead.Native/Menu.hpp"
#include "../../MphRead.Native/Metadata/Metadata.hpp"
#include "../../MphRead.Native/Metadata/Rooms.hpp"
#include "../../MphRead.Native/NativeRuntime/System/Globalization.hpp"
#include "../Platform/QtApp.hpp"

#include <QtCore/QCoreApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtCore/QDir>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickGraphicsDevice>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickRenderControl>
#include <QtQuick/QQuickRenderTarget>
#include <QtQuick/QQuickWindow>

#include <iostream>
#include <memory>
#include <algorithm>
#include <vector>

namespace MphRead::Qt
{
    namespace
    {
        struct Shot
        {
            const char* Name;
            // The base under the stack: "front", "pause" or "end".
            const char* Page;
            // The screen over it, or none for the base alone.
            const char* Url;
            QVariantMap Props;
            QSize Size;
            bool Phone;
        };

        // FP_QT_UISHOT_SIZE=WxH: the desktop screens at another size, to judge
        // the text at a real window's resolution; 940x528 matches -uishot.
        const QSize Window = []() {
            const QStringList parts = qEnvironmentVariable("FP_QT_UISHOT_SIZE").split(u'x');
            const int w = parts.size() == 2 ? parts[0].toInt() : 0;
            const int h = parts.size() == 2 ? parts[1].toInt() : 0;
            return w > 0 && h > 0 ? QSize(w, h) : QSize(940, 528);
        }();
        const QSize PhonePortrait(360, 800);
        const QSize PhoneLandscape(800, 360);

        // UiCapture.RoomList: every multiplayer room the metadata knows, in
        // ordinal-ignore-case order.
        [[nodiscard]] std::vector<std::string> RoomList()
        {
            std::vector<std::string> rooms;
            for (const auto& entry : ::MphRead::Metadata::RoomMetadata)
            {
                if (entry.second != nullptr && entry.second->Multiplayer)
                {
                    rooms.push_back(entry.second->Name);
                }
            }
            std::sort(rooms.begin(), rooms.end(), ::MphRead::NativeRuntime::OrdinalIgnoreCaseLess{});
            return rooms;
        }

        [[nodiscard]] QVariantMap P(std::initializer_list<std::pair<QString, QVariant>> values)
        {
            QVariantMap map;
            for (const auto& [key, value] : values)
            {
                map.insert(key, value);
            }
            return map;
        }
    }

    int UiCapture::Run(const QString& directory)
    {
        EnsureApplication();
        QDir().mkpath(directory);

        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        auto context = std::make_unique<QOpenGLContext>();
        context->setFormat(format);
        auto surface = std::make_unique<QOffscreenSurface>();
        surface->setFormat(format);
        surface->create();
        if (!context->create() || !context->makeCurrent(surface.get()))
        {
            std::cout << "[qtuishot] no OpenGL context\n";
            return 1;
        }
        QOpenGLFunctions* const gl = context->functions();

        ShellBridge bridge(ShellBridge::Actions{});
        bridge.SetSettings(std::make_shared<::MphRead::MenuSettings>());
        bridge.SetRooms(RoomList(), true);
        PlayModel::UseSample = true;

        auto control = std::make_unique<QQuickRenderControl>();
        auto window = std::make_unique<QQuickWindow>(control.get());
        window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(context.get()));
        // -uishot photographs over the panel colour.
        window->setColor(QColor(18, 21, 28));
        if (!control->initialize())
        {
            std::cout << "[qtuishot] Qt Quick could not start\n";
            return 1;
        }
        auto engine = std::make_unique<QQmlEngine>();
        RegisterQmlTypes();
        engine->rootContext()->setContextProperty(QStringLiteral("shell"), &bridge);
        QQmlComponent component(engine.get(), QUrl(QStringLiteral("qrc:/qt/qml/FruityPrime/Ui/Main.qml")));
        std::unique_ptr<QObject> object(component.create());
        auto* const root = qobject_cast<QQuickItem*>(object.get());
        if (root == nullptr)
        {
            std::cout << "[qtuishot] " << component.errorString().toStdString() << '\n';
            return 1;
        }
        root->setParentItem(window->contentItem());
        root->setProperty("still", true);

        const std::vector<Shot> shots = {
            {"start", "front", "", {}, Window, false},
            {"start-phone-portrait", "front", "", {}, PhonePortrait, true},
            {"start-phone-landscape", "front", "", {}, PhoneLandscape, true},
            {"play-online", "front", "PlayPage.qml", P({{"face", 0}}), Window, false},
            {"play-online-phone-portrait", "front", "PlayPage.qml", P({{"face", 0}}), PhonePortrait, true},
            {"play-online-phone-landscape", "front", "PlayPage.qml", P({{"face", 0}}), PhoneLandscape, true},
            {"play-offline", "front", "PlayPage.qml", P({{"face", 1}}), Window, false},
            {"play-story", "front", "PlayPage.qml", P({{"face", 2}}), Window, false},
            {"play-clips", "front", "PlayPage.qml", P({{"face", 3}}), Window, false},
            {"play-vote", "pause", "PlayPage.qml", P({{"face", 4}, {"overGame", true}}), Window, false},
            {"create-server", "front", "CreateServerPage.qml", {}, Window, false},
            {"create-server-dedicated", "front", "CreateServerPage.qml", P({{"dedicated", true}}), Window, false},
            {"create-server-maps", "front", "MapRotationPage.qml", {}, Window, false},
            {"create-server-hosts", "front", "HostPickerPage.qml", P({{"sample", true}}), Window, false},
            {"settings", "front", "SettingsPage.qml", {}, Window, false},
            {"settings-player", "front", "SettingsPage.qml", P({{"section", 3}}), Window, false},
            {"settings-controls", "front", "SettingsPage.qml", P({{"section", 2}}), Window, false},
            {"settings-gamepad", "front", "SettingsPage.qml", P({{"section", 2}, {"subsection", 1}}), Window, false},
            {"end-panel", "end", "", {}, Window, false},
            {"end-panel-hunter", "end", "", P({{"hunterTab", true}}), Window, false},
            {"setup", "front", "SetupPage.qml", {}, Window, false},
            {"confirm", "front", "ConfirmPage.qml", P({{"question", QStringLiteral("Quit ") + bridge.Brand() + QStringLiteral("?")}}), Window, false},
            {"pausemenu", "pause", "", {}, Window, false},
            {"pausemenu-small", "pause", "", {}, QSize(560, 320), false},
            {"pausemenu-phone", "pause", "", {}, PhoneLandscape, true},
            {"serverbrowser", "front", "ServerBrowserSample.qml", {}, Window, false},
            {"lobby", "front", "LobbyPage.qml", {}, Window, false},
        };
        const QString only = qEnvironmentVariable("FP_QT_UISHOT_ONLY");

        GLuint texture = 0;
        QSize current;
        int written = 0;
        int wanted = 0;
        for (const Shot& shot : shots)
        {
            if (!only.isEmpty() && !only.split(QLatin1Char(',')).contains(QString::fromUtf8(shot.Name)))
            {
                continue;
            }
            ++wanted;
            context->makeCurrent(surface.get());
            if (shot.Size != current)
            {
                if (texture != 0)
                {
                    gl->glDeleteTextures(1, &texture);
                }
                gl->glGenTextures(1, &texture);
                gl->glBindTexture(GL_TEXTURE_2D, texture);
                gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, shot.Size.width(), shot.Size.height(), 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, nullptr);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                gl->glBindTexture(GL_TEXTURE_2D, 0);
                window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(texture, shot.Size));
                window->resize(shot.Size);
                window->contentItem()->setSize(shot.Size);
                root->setSize(shot.Size);
                current = shot.Size;
            }
            root->setProperty("phone", shot.Phone);
            bridge.SetPage(QString());
            QCoreApplication::processEvents();
            bridge.SetPage(QString::fromUtf8(shot.Page));
            bridge.RequestScreen(QString::fromUtf8(shot.Url), shot.Props);
            // A few passes so images, fonts and bindings settle.
            for (int i = 0; i < 6; ++i)
            {
                QCoreApplication::processEvents();
                control->polishItems();
                control->beginFrame();
                control->sync();
                control->render();
                control->endFrame();
            }
            // FP_QT_UISHOT_KEYS=Down,Down,Return: keys pressed before the
            // photograph, to check where the focus goes.
            const QString keys = qEnvironmentVariable("FP_QT_UISHOT_KEYS");
            if (!keys.isEmpty())
            {
                for (const QString& name : keys.split(QLatin1Char(',')))
                {
                    // PadA: the pad's A, which some controls answer themselves.
                    if (name == QStringLiteral("PadA") && FocusNav::PadAccept(*window))
                    {
                        bridge.KeyboardDriving();
                        for (int i = 0; i < 2; ++i)
                        {
                            QCoreApplication::processEvents();
                            control->polishItems();
                            control->beginFrame();
                            control->sync();
                            control->render();
                            control->endFrame();
                        }
                        continue;
                    }
                    const QKeySequence sequence(name == QStringLiteral("PadA") ? QStringLiteral("Return") : name);
                    const int key = sequence.isEmpty() ? 0 : sequence[0].key();
                    QKeyEvent press(QEvent::KeyPress, key, ::Qt::NoModifier);
                    QCoreApplication::sendEvent(window.get(), &press);
                    bridge.KeyboardDriving();
                    if (!press.isAccepted())
                    {
                        FocusNav::Unhandled(*window, key);
                    }
                    QKeyEvent release(QEvent::KeyRelease, key, ::Qt::NoModifier);
                    QCoreApplication::sendEvent(window.get(), &release);
                    for (int i = 0; i < 2; ++i)
                    {
                        QCoreApplication::processEvents();
                        control->polishItems();
                        control->beginFrame();
                        control->sync();
                        control->render();
                        control->endFrame();
                    }
                }
            }
            context->makeCurrent(surface.get());
            GLuint fbo = 0;
            gl->glGenFramebuffers(1, &fbo);
            gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            QImage image(shot.Size, QImage::Format_RGBA8888_Premultiplied);
            gl->glReadPixels(0, 0, shot.Size.width(), shot.Size.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
            gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            gl->glDeleteFramebuffers(1, &fbo);
            image = image.flipped(::Qt::Vertical).convertToFormat(QImage::Format_RGB32);
            const QString path = QDir(directory).filePath(QString::fromUtf8(shot.Name) + QStringLiteral(".png"));
            if (image.save(path))
            {
                ++written;
                std::cout << "[qtuishot] " << path.toStdString() << '\n';
            }
        }
        PlayModel::UseSample = false;
        // Qt Quick first, on its context, then the context itself.
        object.reset();
        engine.reset();
        window.reset();
        control.reset();
        context->makeCurrent(surface.get());
        if (texture != 0)
        {
            gl->glDeleteTextures(1, &texture);
        }
        context->doneCurrent();
        std::cout << "[qtuishot] " << written << " screen(s) written to " << directory.toStdString() << '\n';
        return written == wanted ? 0 : 1;
    }
}
