#pragma once

#include <QtCore/QTimer>
#include <QtQuick/QQuickPaintedItem>

namespace MphRead::Qt
{
    // GamepadMonitor: the active controller's sticks, triggers and buttons,
    // live, for testing it on the settings page.
    class GamepadMonitorItem : public QQuickPaintedItem
    {
        Q_OBJECT

    public:
        explicit GamepadMonitorItem(QQuickItem* parent = nullptr);
        void paint(QPainter* painter) override;

    private:
        QTimer _timer;
    };
}
