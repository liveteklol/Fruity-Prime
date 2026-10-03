#include "FocusNav.hpp"

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <limits>
#include <vector>

namespace MphRead::Qt
{
    namespace
    {
        [[nodiscard]] bool IsFlickable(const QQuickItem& item)
        {
            return item.inherits("QQuickFlickable");
        }

        // Takes the keyboard: visible all the way up, enabled, with a size.
        [[nodiscard]] bool Focusable(const QQuickItem& item)
        {
            return item.activeFocusOnTab() && item.isVisible() && item.isEnabled() && item.width() > 0
                && item.height() > 0;
        }

        void Collect(QQuickItem& item, std::vector<QQuickItem*>& into)
        {
            if (!item.isVisible() || !item.isEnabled())
            {
                return;
            }
            if (Focusable(item))
            {
                into.push_back(&item);
            }
            for (QQuickItem* child : item.childItems())
            {
                Collect(*child, into);
            }
        }

        // The part of an item that can be seen, clipped by the lists around it.
        [[nodiscard]] QRectF Seen(const QQuickItem& item)
        {
            QRectF rect = item.mapRectToScene(QRectF(0, 0, item.width(), item.height()));
            for (const QQuickItem* up = item.parentItem(); up != nullptr; up = up->parentItem())
            {
                if (IsFlickable(*up))
                {
                    rect = rect.intersected(up->mapRectToScene(QRectF(0, 0, up->width(), up->height())));
                }
            }
            return rect;
        }

        // A modal layer (the on-screen keyboard) keeps the focus inside it.
        [[nodiscard]] QQuickItem* ModalRoot(QQuickItem& item)
        {
            if (!item.isVisible())
            {
                return nullptr;
            }
            if (item.property("navModal").toBool())
            {
                return &item;
            }
            for (QQuickItem* child : item.childItems())
            {
                if (QQuickItem* found = ModalRoot(*child))
                {
                    return found;
                }
            }
            return nullptr;
        }

        [[nodiscard]] QQuickItem* Current(QQuickWindow& window)
        {
            for (QQuickItem* item = window.activeFocusItem(); item != nullptr; item = item->parentItem())
            {
                if (item->activeFocusOnTab())
                {
                    return item;
                }
            }
            return nullptr;
        }
    }

    bool FocusNav::Move(QQuickWindow& window, Direction direction)
    {
        std::vector<QQuickItem*> candidates;
        QQuickItem* const modal = ModalRoot(*window.contentItem());
        Collect(modal != nullptr ? *modal : *window.contentItem(), candidates);
        QQuickItem* const current = Current(window);
        if (current == nullptr)
        {
            // Nothing has the keyboard yet: the first control, top-left.
            QQuickItem* first = nullptr;
            QRectF best;
            for (QQuickItem* item : candidates)
            {
                const QRectF rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                if (first == nullptr || rect.top() < best.top() - 1
                    || (std::abs(rect.top() - best.top()) <= 1 && rect.left() < best.left()))
                {
                    first = item;
                    best = rect;
                }
            }
            if (first == nullptr)
            {
                return false;
            }
            first->forceActiveFocus(::Qt::TabFocusReason);
            Reveal(*first);
            return true;
        }

        const QRectF from = current->mapRectToScene(QRectF(0, 0, current->width(), current->height()));
        const bool vertical = direction == Direction::Up || direction == Direction::Down;
        QQuickItem* chosen = nullptr;
        double score = std::numeric_limits<double>::max();
        for (QQuickItem* item : candidates)
        {
            if (item == current || current->isAncestorOf(item) || item->isAncestorOf(current))
            {
                continue;
            }
            // Where it would be if its list scrolled it into view: the full rect.
            const QRectF to = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            double along = 0;
            double across = 0;
            switch (direction)
            {
            case Direction::Down:
                along = to.top() - from.bottom();
                break;
            case Direction::Up:
                along = from.top() - to.bottom();
                break;
            case Direction::Right:
                along = to.left() - from.right();
                break;
            case Direction::Left:
                along = from.left() - to.right();
                break;
            }
            // Overlapping by a point still counts as beside.
            if (along < -1)
            {
                continue;
            }
            if (vertical)
            {
                across = std::max(0.0, std::max(to.left() - from.right(), from.left() - to.right()));
                // Rows straight below beat rows off to the side.
                across += std::abs(to.center().x() - from.center().x()) * 0.1;
            }
            else
            {
                across = std::max(0.0, std::max(to.top() - from.bottom(), from.top() - to.bottom()));
                across += std::abs(to.center().y() - from.center().y()) * 0.1;
            }
            const double candidateScore = std::max(0.0, along) + across * 3;
            if (candidateScore < score)
            {
                score = candidateScore;
                chosen = item;
            }
        }
        if (chosen == nullptr)
        {
            return false;
        }
        chosen->forceActiveFocus(::Qt::TabFocusReason);
        Reveal(*chosen);
        return true;
    }

    bool FocusNav::PadAccept(QQuickWindow& window)
    {
        for (QQuickItem* item = window.activeFocusItem(); item != nullptr; item = item->parentItem())
        {
            if (item->metaObject()->indexOfMethod("padAccept()") < 0)
            {
                continue;
            }
            QVariant handled;
            QMetaObject::invokeMethod(item, "padAccept", Q_RETURN_ARG(QVariant, handled));
            return handled.toBool();
        }
        return false;
    }

    void FocusNav::Unhandled(QQuickWindow& window, int key)
    {
        switch (key)
        {
        case ::Qt::Key_Up: (void)Move(window, Direction::Up); break;
        case ::Qt::Key_Down: (void)Move(window, Direction::Down); break;
        case ::Qt::Key_Left: (void)Move(window, Direction::Left); break;
        case ::Qt::Key_Right: (void)Move(window, Direction::Right); break;
        case ::Qt::Key_PageUp: Page(window, false); break;
        case ::Qt::Key_PageDown: Page(window, true); break;
        default: break;
        }
    }

    void FocusNav::Reveal(QQuickItem& item)
    {
        for (QQuickItem* up = item.parentItem(); up != nullptr; up = up->parentItem())
        {
            if (!IsFlickable(*up))
            {
                continue;
            }
            QQuickItem* const content = up->property("contentItem").value<QQuickItem*>();
            if (content == nullptr)
            {
                continue;
            }
            const QRectF rect = item.mapRectToItem(content, QRectF(0, 0, item.width(), item.height()));
            const double y = up->property("contentY").toDouble();
            const double height = up->height();
            const double maxY = std::max(0.0, up->property("contentHeight").toDouble() - height);
            double target = y;
            if (rect.top() < y)
            {
                target = rect.top();
            }
            else if (rect.bottom() > y + height)
            {
                target = rect.bottom() - height;
            }
            up->setProperty("contentY", std::clamp(target, 0.0, maxY));
        }
    }

    void FocusNav::Page(QQuickWindow& window, bool down)
    {
        for (QQuickItem* up = window.activeFocusItem(); up != nullptr; up = up->parentItem())
        {
            if (!IsFlickable(*up))
            {
                continue;
            }
            const double maxY = std::max(0.0, up->property("contentHeight").toDouble() - up->height());
            const double y = up->property("contentY").toDouble() + (down ? 1 : -1) * up->height() * 0.8;
            up->setProperty("contentY", std::clamp(y, 0.0, maxY));
            return;
        }
    }
}
