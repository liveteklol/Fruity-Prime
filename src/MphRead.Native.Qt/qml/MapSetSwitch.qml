import QtQuick

// The two sets of maps wherever maps are listed: the cartridge's own
// (Original) and the ones read from .pk3 packs (Custom), with how many each.
Row {
    id: sets
    // 0 Original, 1 Custom.
    property int index: 0
    property var rooms: []
    spacing: Theme.roundEven(Theme.em * 0.35)

    readonly property int originals: rooms.filter(r => !r.custom).length
    readonly property int customs: rooms.length - originals

    // The rooms of the set shown.
    function pick(all) {
        return all.filter(r => !!r.custom === (sets.index === 1))
    }

    Repeater {
        model: [{ name: "Original", count: sets.originals }, { name: "Custom", count: sets.customs }]
        DeckButton {
            required property var modelData
            required property int index
            text: modelData.name + "  " + modelData.count
            face: sets.index === index ? Theme.brass : Theme.slate
            em: Theme.em; sizeEms: 0.9; padXEms: 0.8; padYEms: 0.35; lip: 3
            width: implicitWidth; height: implicitHeight
            onClicked: sets.index = index
        }
    }
}
