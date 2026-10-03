import QtQuick

// DeckSide: a panel that slides in from the right over the page, 17.5 em
// wide, its card centred on its height; parked a little past the edge.
Item {
    id: side
    property bool open: false
    property real em: Theme.em
    default property alias content: card.content
    readonly property real panelWidth: Theme.roundEven(em * 17.5)

    anchors.fill: parent
    anchors.topMargin: 14; anchors.bottomMargin: 14; anchors.rightMargin: 12
    visible: at > 0.001

    property real at: open ? 1 : 0
    Behavior on at {
        enabled: !Theme.still
        NumberAnimation { duration: 340; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.spring }
    }

    // Clicks outside the panel still reach the page under it.
    DeckCard {
        id: card
        em: side.em
        maxWidthEms: 17.5
        width: side.panelWidth
        height: Math.min(side.height, implicitHeight)
        x: side.width - width + (width + side.em * 1.4) * (1 - side.at)
        anchors.verticalCenter: parent.verticalCenter
        MouseArea { anchors.fill: parent; z: -1 }
    }
}
