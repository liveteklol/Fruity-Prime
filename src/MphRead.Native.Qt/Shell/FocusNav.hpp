#pragma once

class QQuickItem;
class QQuickWindow;

namespace MphRead::Qt
{
    // FocusNavigator for the Qt Quick menus: where an arrow key or the pad's
    // direction goes when the focused control does not use it itself -- the
    // nearest focusable control that way on screen -- and keeping the focused
    // control inside the lists that scroll it.
    class FocusNav final
    {
    public:
        enum class Direction
        {
            Up,
            Down,
            Left,
            Right
        };

        // After a key went to the scene: the arrows and pages nothing took.
        static void Unhandled(QQuickWindow& window, int key);
        // The pad's A on a control with its own answer (padAccept()); false
        // when it has none and A is Enter.
        static bool PadAccept(QQuickWindow& window);
        // Move the focus one step; false when nothing lies that way.
        static bool Move(QQuickWindow& window, Direction direction);
        // Scroll the list holding the focus by most of its height.
        static void Page(QQuickWindow& window, bool down);
        // Scroll every list around the item until the item is in view.
        static void Reveal(QQuickItem& item);
    };
}
