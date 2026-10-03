import QtQuick

// UiTabs: faces, not words with dots between them. The one that is up
// wears rust and the wedge; Left and Right step through them.
Row {
    id: tabs
    property var names: []
    property int index: 0
    property real em: Theme.em
    signal changed()
    // Whether the pad's shoulder buttons step these (the page's own tabs,
    // not a sub-page's).
    property bool padTabs: true
    Connections {
        target: shell
        enabled: tabs.padTabs && tabs.visible
        function onTabStep(direction) { tabs.step(direction) }
    }
    spacing: Theme.roundEven(em * 0.4)

    function step(direction) {
        if (names.length === 0)
            return
        index = (index + direction + names.length) % names.length
        changed()
    }
    function pick(i) {
        if (i === index)
            return
        index = i
        changed()
    }

    Repeater {
        model: tabs.names
        DeckButton {
            required property int index
            required property string modelData
            text: modelData
            em: tabs.em
            sizeEms: 1.05; padXEms: 0.8; padYEms: 0.38; lip: 6
            face: index === tabs.index ? Theme.rust : Theme.slate
            selected: index === tabs.index
            activeFocusOnTab: false
            onClicked: tabs.pick(index)
        }
    }
}
