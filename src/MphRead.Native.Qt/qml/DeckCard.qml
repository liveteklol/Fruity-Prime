import QtQuick
import QtQuick.Effects

// DeckCard: the panel -- a two-point pale ring, a ten-point PanelDeep lip,
// and a long soft cast under it; one em of padding inside.
Item {
    id: card
    property real em: 10.81
    property real maxWidthEms: 44
    default property alias content: inner.data
    readonly property real pad: em
    readonly property real radius: em * 0.7

    implicitWidth: Math.min(parent ? parent.width : Infinity, em * maxWidthEms)
    implicitHeight: inner.childrenRect.height + pad * 2

    RectangularShadow {
        anchors.fill: face
        offset.y: 26
        blur: 50
        radius: card.radius
        color: Qt.rgba(0, 0, 0, 0.75)
    }
    Rectangle { anchors.fill: face; anchors.topMargin: 10; anchors.bottomMargin: -10; radius: card.radius; color: Theme.panelDeep }
    Rectangle { anchors.fill: face; anchors.margins: -2; radius: card.radius + 2; color: Qt.rgba(0xe6 / 255, 0xea / 255, 0xf2 / 255, 0.18) }
    Rectangle { id: face; anchors.fill: parent; radius: card.radius; color: Theme.panel }
    Item {
        id: inner
        x: card.pad; y: card.pad
        width: card.width - card.pad * 2
        height: card.height - card.pad * 2
    }
}
