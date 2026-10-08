#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QRectF>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>

#include <cstdint>
#include <vector>

namespace MphRead::Qt
{
    // ClassicMenu: the DS game's own title and menus (Mods::ClassicMenu).
    // Both screens are drawn at the DS's 256x192 and shown as two textures
    // in the scene graph, side by side (or stacked, as the DS holds them) at
    // the largest whole-pixel scale that fits. A click on the touch screen is
    // a touch, arrows the D-pad, Enter A and Escape B. MULTIPLAYER comes out
    // as multiplayerRequested.
    class ClassicMenuItem : public QQuickItem
    {
        Q_OBJECT
        Q_PROPERTY(bool running READ Running WRITE SetRunning NOTIFY runningChanged)
        Q_PROPERTY(QString error READ Error NOTIFY runningChanged)
        Q_PROPERTY(bool stacked READ Stacked WRITE SetStacked NOTIFY stackedChanged)
        // FRUITY_CLASSIC_SCRIPT is set: a check wants the menus on at once.
        Q_PROPERTY(bool scripted READ Scripted CONSTANT)

    public:
        explicit ClassicMenuItem(QQuickItem* parent = nullptr);
        ~ClassicMenuItem() override;

        [[nodiscard]] bool Running() const noexcept { return _running; }
        void SetRunning(bool value);
        [[nodiscard]] QString Error() const { return _error; }
        [[nodiscard]] bool Stacked() const noexcept { return _stacked; }
        void SetStacked(bool value);
        [[nodiscard]] bool Scripted() const;

    signals:
        void runningChanged();
        void stackedChanged();
        void multiplayerRequested();
        void adventureRequested();
        void quitRequested();

    protected:
        QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
        void mousePressEvent(QMouseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;

    private:
        void Step();
        void RunScript();
        void Layout(QRectF& top, QRectF& bottom) const;

        QTimer _timer;
        QElapsedTimer _clock;
        std::vector<std::uint32_t> _top;
        std::vector<std::uint32_t> _bottom;
        QImage _topImage;
        QImage _bottomImage;
        bool _fresh = false;
        QString _error;
        bool _running = false;
        bool _stacked = false;
        int _steps = 0;
    };
}
