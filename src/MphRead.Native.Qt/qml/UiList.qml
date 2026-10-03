import QtQuick

// UiList: the one list in the launcher -- the scrolling, the selection and
// the up and down keys every list shares.
FocusScope {
    id: list
    property var model: []
    property Component delegate: defaultRow
    // The gap between rows in frame ems, or zero for the flat one point.
    property real spacingEms: 0
    // Whether the first row to arrive becomes the selection.
    property bool autoSelectFirst: true
    property int selectedIndex: -1
    property color thumb: Theme.edge
    readonly property real contentHeight: column.height
    signal activated(int index)
    signal chosen(int index)

    implicitHeight: column.height

    onModelChanged: {
        if (selectedIndex >= model.length)
            selectedIndex = -1
        if (selectedIndex < 0 && autoSelectFirst && model.length > 0)
            select(0)
    }
    function select(i) {
        if (i === selectedIndex)
            return
        selectedIndex = i
    }
    function focusRow(i) {
        if (i < 0 || i >= repeater.count)
            return
        select(i)
        const item = repeater.itemAt(i)
        item.forceActiveFocus()
        if (item.y < flick.contentY)
            flick.contentY = item.y
        else if (item.y + item.height > flick.contentY + flick.height)
            flick.contentY = item.y + item.height - flick.height
    }
    function focusFirst() {
        focusRow(selectedIndex >= 0 ? selectedIndex : 0)
    }
    function handleKey(key) {
        if (repeater.count === 0)
            return false
        const step = key === Qt.Key_Down ? 1 : key === Qt.Key_Up ? -1 : 0
        if (step === 0)
            return false
        const next = selectedIndex < 0 ? (step > 0 ? 0 : repeater.count - 1)
                                       : Math.max(0, Math.min(repeater.count - 1, selectedIndex + step))
        Theme.keyboardDriving = true
        focusRow(next)
        return true
    }

    Component {
        id: defaultRow
        ListRow {
            property var modelData: ({})
            title: modelData.title || ""
            detail: modelData.detail || ""
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: column
            x: 3
            width: flick.width - 6
            spacing: list.spacingEms > 0 ? Theme.em * list.spacingEms : 1
            Repeater {
                id: repeater
                model: list.model
                delegate: Loader {
                    id: slot
                    required property var modelData
                    required property int index
                    width: column.width
                    height: item ? item.height : 0
                    sourceComponent: list.delegate
                    onLoaded: {
                        item.width = Qt.binding(() => slot.width)
                        if (item.hasOwnProperty("modelData"))
                            item.modelData = Qt.binding(() => slot.modelData)
                        item.selected = Qt.binding(() => list.selectedIndex === slot.index)
                        item.clicked.connect(() => { list.select(slot.index); list.chosen(slot.index) })
                        item.activated.connect(() => { list.select(slot.index); list.activated(slot.index) })
                        item.activeFocusChanged.connect(() => { if (item.activeFocus) list.select(slot.index) })
                    }
                    function forceActiveFocus() { if (item) item.forceActiveFocus() }
                }
            }
        }
    }
    ThinScroll { flick: flick; thumb: list.thumb }
}
