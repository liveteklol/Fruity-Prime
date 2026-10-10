#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QRectF>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>

#include <cstdint>
#include <vector>

namespace MphRead::Qt
{
    class ClassicHost;

    // ClassicMenu: the DS game's own title and menus (Mods::ClassicMenu), on
    // one screen, standing in for the launcher's own pages while the debug
    // switch is on. Drawn on the CPU into one picture of the item's shape
    // and shown as one texture. The pointer is the stylus (hover is the
    // stylus resting on an item), arrows the D-pad, Enter A and Escape B;
    // typed keys go to the DS keyboard when it is up, and to a key binding
    // when one waits. Everything the menus decide is done by the launcher's
    // own models (ClassicHost).
    class ClassicMenuItem : public QQuickItem
    {
        Q_OBJECT
        Q_PROPERTY(bool running READ Running WRITE SetRunning NOTIFY runningChanged)
        Q_PROPERTY(QString error READ Error NOTIFY runningChanged)
        // FRUITY_CLASSIC_SCRIPT is set: a check wants the menus on at once.
        Q_PROPERTY(bool scripted READ Scripted CONSTANT)

    public:
        explicit ClassicMenuItem(QQuickItem* parent = nullptr);
        ~ClassicMenuItem() override;

        [[nodiscard]] bool Running() const noexcept { return _running; }
        void SetRunning(bool value);
        [[nodiscard]] QString Error() const { return _error; }
        [[nodiscard]] bool Scripted() const;

        // The launcher moved on: "room" (a lobby opened), "pause", "end", "front".
        Q_INVOKABLE void show(const QString& what);

    signals:
        void runningChanged();
        void quitRequested();

    protected:
        QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
        void mousePressEvent(QMouseEvent* event) override;
        void hoverMoveEvent(QHoverEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;

    private:
        void Step();
        void RunScript();
        [[nodiscard]] QSize PixelSize() const;

        QTimer _timer;
        QElapsedTimer _clock;
        std::vector<std::uint32_t> _pixels;
        QImage _image;
        bool _fresh = false;
        QString _error;
        bool _running = false;
        int _steps = 0;
        ClassicHost* _host = nullptr;
    };
}
