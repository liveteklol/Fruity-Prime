#include "ClassicMenuItem.hpp"

#include "../../MphRead.Native/Mods/ClassicMenu/ClassicMenu.hpp"
#include "../../MphRead.Native/Mods/ClassicMenu/MenuData.hpp"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGSimpleRectNode>
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
        constexpr int W = Menu::ScreenWidth;
        constexpr int H = Menu::ScreenHeight;
        // Between the two screens, in DS pixels, like the DS's hinge.
        constexpr int Gap = 8;

        // The scene graph's tree for the item: a black backdrop, then the two
        // screens. Textures belong to the nodes.
        struct Screens final : QSGSimpleRectNode
        {
            QSGSimpleTextureNode* Top = nullptr;
            QSGSimpleTextureNode* Bottom = nullptr;
        };

        QImage Picture(const std::vector<std::uint32_t>& pixels)
        {
            return QImage(reinterpret_cast<const uchar*>(pixels.data()), W, H, W * 4, QImage::Format_RGBA8888).copy();
        }
    }

    ClassicMenuItem::ClassicMenuItem(QQuickItem* parent) : QQuickItem(parent)
    {
        setFlag(QQuickItem::ItemHasContents, true);
        setAcceptedMouseButtons(::Qt::LeftButton);
        setActiveFocusOnTab(true);
        // The menus tick at the DS's 30 Hz; the item polls at the display's
        // rate so a tick is shown the frame it happens.
        _timer.setInterval(8);
        _timer.setTimerType(::Qt::PreciseTimer);
        connect(&_timer, &QTimer::timeout, this, &ClassicMenuItem::Step);
    }

    ClassicMenuItem::~ClassicMenuItem()
    {
        if (_running) Menu::SetActive(false, this);
    }

    void ClassicMenuItem::SetRunning(bool value)
    {
        if (value == _running) return;
        _error.clear();
        if (value && !Menu::SetActive(true, this))
        {
            _error = QString::fromStdString(Menu::LastError());
            emit runningChanged();
            return;
        }
        if (!value) Menu::SetActive(false, this);
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
            _topImage = QImage();
            _bottomImage = QImage();
            _fresh = true;
            update();
        }
        emit runningChanged();
    }

    void ClassicMenuItem::SetStacked(bool value)
    {
        if (value == _stacked) return;
        _stacked = value;
        update();
        emit stackedChanged();
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
        if (!Menu::RenderScreens(seconds, _top, _bottom))
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
        _topImage = Picture(_top);
        _bottomImage = Picture(_bottom);
        _fresh = true;
        update();
        RunScript();
        if (const auto request = Menu::TakeRequest())
        {
            if (*request == Mods::ClassicMenu::Request::Multiplayer) emit multiplayerRequested();
            else emit adventureRequested();
        }
    }

    // The two screens at the largest whole-pixel scale that fits (a smaller
    // one only when the item cannot hold even 1x), centred.
    void ClassicMenuItem::Layout(QRectF& top, QRectF& bottom) const
    {
        const qreal ratio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
        const qreal unitsW = _stacked ? W : 2 * W + Gap;
        const qreal unitsH = _stacked ? 2 * H + Gap : H;
        qreal scale = std::min(width() * ratio / unitsW, height() * ratio / unitsH);
        if (scale >= 1.0) scale = std::floor(scale);
        scale /= ratio;
        const qreal x0 = std::round((width() - unitsW * scale) / 2);
        const qreal y0 = std::round((height() - unitsH * scale) / 2);
        top = QRectF(x0, y0, W * scale, H * scale);
        bottom = _stacked
            ? QRectF(x0, y0 + (H + Gap) * scale, W * scale, H * scale)
            : QRectF(x0 + (W + Gap) * scale, y0, W * scale, H * scale);
    }

    QSGNode* ClassicMenuItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
    {
        auto* root = static_cast<Screens*>(old);
        if (_topImage.isNull() || window() == nullptr)
        {
            delete root;
            return nullptr;
        }
        if (root == nullptr)
        {
            root = new Screens();
            root->setColor(::Qt::black);
            root->Top = new QSGSimpleTextureNode();
            root->Bottom = new QSGSimpleTextureNode();
            for (auto* node : {root->Top, root->Bottom})
            {
                node->setOwnsTexture(true);
                node->setFiltering(QSGTexture::Nearest);
                root->appendChildNode(node);
            }
        }
        root->setRect(boundingRect());
        if (_fresh)
        {
            root->Top->setTexture(window()->createTextureFromImage(_topImage));
            root->Bottom->setTexture(window()->createTextureFromImage(_bottomImage));
            _fresh = false;
        }
        QRectF top;
        QRectF bottom;
        Layout(top, bottom);
        root->Top->setRect(top);
        root->Bottom->setRect(bottom);
        return root;
    }

    bool ClassicMenuItem::Scripted() const
    {
        return !qEnvironmentVariableIsEmpty("FRUITY_CLASSIC_SCRIPT");
    }

    // FRUITY_CLASSIC_SCRIPT=DIR;STEP:ACTION;... -- a check of the menus with
    // no one at the keyboard. Actions at a step count: shot (DIR/step-N.png,
    // both screens as the DS holds them), a, b, up, down, left, right,
    // touch:X:Y (DS pixels on the touch screen), quit.
    void ClassicMenuItem::RunScript()
    {
        static const QStringList script = qEnvironmentVariable("FRUITY_CLASSIC_SCRIPT").split(QLatin1Char(';'));
        if (script.size() < 2) return;
        // Counted per process: the UI can be rebuilt with a new item.
        static int steps = 0;
        _steps = ++steps;
        using namespace Mods::ClassicMenu;
        for (int i = 1; i < script.size(); ++i)
        {
            const QStringList parts = script[i].split(QLatin1Char(':'));
            if (parts.size() < 2 || parts[0].toInt() != _steps) continue;
            const QString action = parts[1];
            if (action == QLatin1String("shot"))
            {
                QImage both(W, 2 * H, QImage::Format_RGBA8888);
                QPainter painter(&both);
                painter.drawImage(0, 0, _topImage);
                painter.drawImage(0, H, _bottomImage);
                painter.end();
                both.save(script[0] + QStringLiteral("/step-%1.png").arg(_steps));
            }
            else if (action == QLatin1String("a")) Menu::Press(static_cast<std::uint16_t>(KeyA | KeyStart));
            else if (action == QLatin1String("b")) Menu::Press(KeyB);
            else if (action == QLatin1String("up")) Menu::Navigate(0, 1);
            else if (action == QLatin1String("down")) Menu::Navigate(0, -1);
            else if (action == QLatin1String("left")) Menu::Navigate(-1, 0);
            else if (action == QLatin1String("right")) Menu::Navigate(1, 0);
            else if (action == QLatin1String("touch") && parts.size() >= 4) Menu::Touch(parts[2].toFloat(), parts[3].toFloat());
            else if (action == QLatin1String("quit")) emit quitRequested();
        }
    }

    void ClassicMenuItem::mousePressEvent(QMouseEvent* event)
    {
        forceActiveFocus();
        QRectF top;
        QRectF bottom;
        Layout(top, bottom);
        const QPointF p = event->position();
        if (bottom.contains(p))
        {
            Menu::Touch(static_cast<float>((p.x() - bottom.x()) / bottom.width() * W),
                static_cast<float>((p.y() - bottom.y()) / bottom.height() * H));
        }
        event->accept();
    }

    void ClassicMenuItem::keyPressEvent(QKeyEvent* event)
    {
        using namespace Mods::ClassicMenu;
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
