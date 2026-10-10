#include "ClassicMenuItem.hpp"

#include "ClassicHost.hpp"
#include "SettingsModel.hpp"

#include "../../MphRead.Native/Mods/ClassicMenu/ClassicMenu.hpp"
#include "../../MphRead.Native/Mods/ClassicMenu/MenuData.hpp"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGTexture>

#include <algorithm>
#include <cmath>
#include <memory>

namespace MphRead::Qt
{
    namespace
    {
        using Menu = Mods::ClassicMenu::Facade;
    }

    ClassicMenuItem::ClassicMenuItem(QQuickItem* parent) : QQuickItem(parent)
    {
        setFlag(QQuickItem::ItemHasContents, true);
        setAcceptedMouseButtons(::Qt::LeftButton);
        setAcceptHoverEvents(true);
        setActiveFocusOnTab(true);
        _host = new ClassicHost(this);
        connect(_host, &ClassicHost::roomClosed, this, [](const QString&)
        {
            // back to the multiplayer pages, as the launcher's own lobby page does
            Menu::Show("front");
        });
        // The menus tick at the DS's 30 Hz; the item polls at the display's
        // rate so a tick is shown the frame it happens.
        _timer.setInterval(8);
        _timer.setTimerType(::Qt::PreciseTimer);
        connect(&_timer, &QTimer::timeout, this, &ClassicMenuItem::Step);
    }

    ClassicMenuItem::~ClassicMenuItem()
    {
        if (_running)
        {
            Menu::SetHost(nullptr);
            Menu::SetActive(false, this);
        }
    }

    void ClassicMenuItem::SetRunning(bool value)
    {
        if (value == _running) return;
        _error.clear();
        if (value)
        {
            // keep the session a lobby or a match left behind: only start one
            // when none is there for this item
            if (!Menu::Owns(this) && !Menu::SetActive(true, this))
            {
                _error = QString::fromStdString(Menu::LastError());
                emit runningChanged();
                return;
            }
            Menu::SetHost(_host);
        }
        _running = value;
        if (_running)
        {
            _clock.start();
            _timer.start();
            forceActiveFocus();
        }
        else
        {
            _timer.stop();
            _image = QImage();
            _fresh = true;
            update();
        }
        emit runningChanged();
    }

    void ClassicMenuItem::show(const QString& what)
    {
        if (what == QLatin1String("room")) _host->OpenRoom();
        Menu::Show(what.toStdString());
    }

    QSize ClassicMenuItem::PixelSize() const
    {
        const qreal ratio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
        return QSize(std::max(1, static_cast<int>(std::lround(width() * ratio))), std::max(1, static_cast<int>(std::lround(height() * ratio))));
    }

    void ClassicMenuItem::Step()
    {
        if (!_running) return;
        if (Menu::Active() && !Menu::Owns(this))
        {
            // Another item (a rebuilt UI) runs the menus now.
            _timer.stop();
            return;
        }
        const double seconds = static_cast<double>(_clock.restart()) / 1000.0;
        const QSize size = PixelSize();
        bool changed = true;
        if (!Menu::Render(seconds, size.width(), size.height(), _pixels, &changed))
        {
            if (!Menu::Active())
            {
                _error = QString::fromStdString(Menu::LastError());
                _running = false;
                _timer.stop();
                emit runningChanged();
            }
            return;
        }
        // no tick, no input: the picture on screen stands
        if (!changed)
        {
            RunScript();
            return;
        }
        // the picture's own size: the shell scales it to the item
        int w = 0, h = 0;
        Menu::DrawnSize(w, h);
        if (w <= 0 || h <= 0 || static_cast<std::size_t>(w) * static_cast<std::size_t>(h) != _pixels.size()) return;
        _image = QImage(reinterpret_cast<const uchar*>(_pixels.data()), w, h, w * 4, QImage::Format_RGBA8888).copy();
        _fresh = true;
        update();
        RunScript();
    }

    QSGNode* ClassicMenuItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
    {
        auto* node = static_cast<QSGSimpleTextureNode*>(old);
        if (_image.isNull() || window() == nullptr)
        {
            delete node;
            return nullptr;
        }
        if (node == nullptr)
        {
            node = new QSGSimpleTextureNode();
            node->setOwnsTexture(true);
            node->setFiltering(QSGTexture::Linear);
        }
        if (_fresh)
        {
            node->setTexture(window()->createTextureFromImage(_image,
                Menu::OverGame() ? QQuickWindow::TextureHasAlphaChannel : QQuickWindow::CreateTextureOptions{}));
            _fresh = false;
        }
        node->setRect(boundingRect());
        return node;
    }

    bool ClassicMenuItem::Scripted() const
    {
        return !qEnvironmentVariableIsEmpty("FRUITY_CLASSIC_SCRIPT");
    }

    // FRUITY_CLASSIC_SCRIPT=DIR;STEP:ACTION;... -- a check of the menus with
    // no one at the keyboard. Actions at a step count: shot (DIR/step-N.png,
    // the one screen as drawn), a, b, up, down, left, right, touch:X:Y (DS
    // pixels on the touch screen), click:U:V and hover:U:V (fractions of the
    // item), type:WORDS, enter, quit.
    void ClassicMenuItem::RunScript()
    {
        static const QStringList script = qEnvironmentVariable("FRUITY_CLASSIC_SCRIPT").split(QLatin1Char(';'));
        if (script.size() < 2) return;
        // Counted per process: the UI can be rebuilt with a new item.
        static int steps = 0;
        _steps = ++steps;
        using namespace Mods::ClassicMenu;
        const QSize size = PixelSize();
        for (int i = 1; i < script.size(); ++i)
        {
            const QStringList parts = script[i].split(QLatin1Char(':'));
            if (parts.size() < 2 || parts[0].toInt() != _steps) continue;
            const QString action = parts[1];
            if (action == QLatin1String("shot")) _image.save(script[0] + QStringLiteral("/step-%1.png").arg(_steps));
            else if (action == QLatin1String("a")) Menu::Press(static_cast<std::uint16_t>(KeyA | KeyStart));
            else if (action == QLatin1String("b")) Menu::Press(KeyB);
            else if (action == QLatin1String("up")) Menu::Navigate(0, 1);
            else if (action == QLatin1String("down")) Menu::Navigate(0, -1);
            else if (action == QLatin1String("left")) Menu::Navigate(-1, 0);
            else if (action == QLatin1String("right")) Menu::Navigate(1, 0);
            else if (action == QLatin1String("touch") && parts.size() >= 4) Menu::Touch(parts[2].toFloat(), parts[3].toFloat());
            else if ((action == QLatin1String("click") || action == QLatin1String("hover")) && parts.size() >= 4)
            {
                Menu::Pointer(parts[2].toFloat() * static_cast<float>(size.width()), parts[3].toFloat() * static_cast<float>(size.height()),
                    action == QLatin1String("click"));
            }
            else if (action == QLatin1String("type") && parts.size() >= 3) Menu::Type(parts[2].toStdString(), false, false);
            else if (action == QLatin1String("enter")) Menu::Type("", false, true);
            else if (action == QLatin1String("show") && parts.size() >= 3) show(parts[2]);
            else if (action == QLatin1String("page") && parts.size() >= 3) Menu::Visit(parts[2].toStdString());
            else if (action == QLatin1String("quit")) emit quitRequested();
        }
    }

    void ClassicMenuItem::mousePressEvent(QMouseEvent* event)
    {
        forceActiveFocus();
        const qreal ratio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
        const QPointF p = event->position() * ratio;
        Menu::Pointer(static_cast<float>(p.x()), static_cast<float>(p.y()), true);
        event->accept();
    }

    void ClassicMenuItem::hoverMoveEvent(QHoverEvent* event)
    {
        const qreal ratio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
        const QPointF p = event->position() * ratio;
        Menu::Pointer(static_cast<float>(p.x()), static_cast<float>(p.y()), false);
        event->accept();
    }

    void ClassicMenuItem::keyPressEvent(QKeyEvent* event)
    {
        using namespace Mods::ClassicMenu;
        // a key binding waits for its key: the settings have it
        if (Menu::Listening() && _host->Settings() != nullptr)
        {
            _host->Settings()->pressKey(event->key(), event->nativeScanCode(), event->nativeVirtualKey(),
                static_cast<int>(event->modifiers()), event->text());
            event->accept();
            return;
        }
        // the DS keyboard is up: typed keys are words
        const QString text = event->text();
        const bool typing = !text.isEmpty() && text.at(0).isPrint();
        if (typing && Menu::Type(text.toStdString(), false, false)) { event->accept(); return; }
        if ((event->key() == ::Qt::Key_Backspace) && Menu::Type("", true, false)) { event->accept(); return; }
        if ((event->key() == ::Qt::Key_Return || event->key() == ::Qt::Key_Enter) && Menu::Type("", false, true)) { event->accept(); return; }
        switch (event->key())
        {
        case ::Qt::Key_Left: Menu::Navigate(-1, 0); break;
        case ::Qt::Key_Right: Menu::Navigate(1, 0); break;
        case ::Qt::Key_Up: Menu::Navigate(0, 1); break;
        case ::Qt::Key_Down: Menu::Navigate(0, -1); break;
        case ::Qt::Key_Return:
        case ::Qt::Key_Enter:
        case ::Qt::Key_Space:
            Menu::Press(static_cast<std::uint16_t>(KeyA | KeyStart));
            break;
        case ::Qt::Key_Escape:
        case ::Qt::Key_Backspace:
            Menu::Press(KeyB);
            break;
        case ::Qt::Key_Q: Menu::Press(KeyL); break;
        case ::Qt::Key_E: Menu::Press(KeyR); break;
        default:
            QQuickItem::keyPressEvent(event);
            return;
        }
        event->accept();
    }
}
