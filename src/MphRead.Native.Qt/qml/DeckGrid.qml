import QtQuick

// DeckGrid: three cards across (two on a narrow frame), half an em apart,
// in a scroller that clips them.
FocusScope {
    id: grid
    property var model: []
    property Component delegate
    property real ratio: 1
    property int fixedColumns: 0
    readonly property int columns: fixedColumns > 0 ? fixedColumns : (Window.width <= 640 ? 2 : 3)
    readonly property real gap: Theme.roundEven(Theme.em * 0.5)
    readonly property real cell: Math.max(1, (width - gap * (columns - 1)) / columns)
    readonly property real high: Theme.roundEven(cell / Math.max(0.1, ratio))
    readonly property int rows: Math.ceil(model.length / columns)
    readonly property real contentHeight: rows * high + Math.max(0, rows - 1) * gap
    property alias contentY: flick.contentY
    implicitHeight: contentHeight

    function itemAt(i) { return repeater.itemAt(i) }
    function ensureVisible(item) {
        if (item.y < flick.contentY)
            flick.contentY = item.y
        else if (item.y + item.height > flick.contentY + flick.height)
            flick.contentY = Math.min(flick.contentHeight - flick.height, item.y + item.height - flick.height)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: grid.contentHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Repeater {
            id: repeater
            model: grid.model
            delegate: Loader {
                id: slot
                required property var modelData
                required property int index
                x: (index % grid.columns) * (grid.cell + grid.gap)
                y: Math.floor(index / grid.columns) * (grid.high + grid.gap)
                width: grid.cell
                height: grid.high
                sourceComponent: grid.delegate
                onLoaded: {
                    item.anchors.fill = slot
                    item.modelData = Qt.binding(() => slot.modelData)
                    item.index = slot.index
                    item.activeFocusChanged.connect(() => { if (item.activeFocus) grid.ensureVisible(slot) })
                }
            }
        }
    }
    ThinScroll { flick: flick }
}
