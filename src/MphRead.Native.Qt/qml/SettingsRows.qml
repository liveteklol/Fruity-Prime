import QtQuick
import FruityPrime.Launcher

// One settings page: its rows, two points apart, each drawn by its kind.
Column {
    id: rows
    property var model
    property var settings
    // The key rows answer to the settings' capture.
    property bool keys: false
    spacing: 2

    // Focus the row at an index (a pad row the key rows hand over to).
    function focusRow(index) {
        const item = repeater.itemAt(index)
        if (item && item.content)
            item.content.forceActiveFocus()
        return item ? item.content : null
    }
    Repeater {
        id: repeater
        model: rows.model
        Item {
            id: slot
            required property var row
            required property int index
            width: rows.width
            visible: row.shown
            // Room above and below: Heading's margins, a button's, a panel's gap.
            height: visible && loader.item ? row.top + loader.item.height + row.bottom : 0
            readonly property Item content: loader.item
          Loader {
            id: loader
            y: slot.row.top
            width: parent.width
            sourceComponent: {
                switch (row.type) {
                case "caption": return caption
                case "note": return note
                case "text": return plain
                case "choice": return choice
                case "slider": return slider
                case "toggle": return toggle
                case "field": return field
                case "word": return word
                case "button": return button
                case "key": return key
                case "pad": return pad
                case "stand": return stand
                case "monitor": return monitor
                }
                return null
            }

            Component {
                id: caption
                Item {
                    readonly property bool bare: slot.row.face === "bare"
                    height: bare ? 26 : 8 + 30 + 4
                    Caption {
                        y: parent.bare ? 0 : 8
                        width: parent.width
                        height: parent.bare ? 26 : 30
                        text: slot.row.label
                    }
                }
            }
            Component {
                id: note
                Note {
                    text: slot.row.live
                    lines: slot.row.lines
                    color: slot.row.colour !== undefined ? slot.row.colour : Theme.textDim
                }
            }
            Component {
                id: plain
                Text {
                    text: slot.row.text
                    wrapMode: Text.Wrap
                    font.family: Theme.pixel; font.pixelSize: 11
                    color: Theme.textDim
                }
            }
            Component {
                id: choice
                ChoiceRow {
                    label: slot.row.label
                    options: slot.row.options
                    Binding on index { value: slot.row.index }
                    onChanged: rows.model.setIndex(slot.index, index)
                    preview: slot.row.preview === "crosshair" ? crosshair : null
                    Component {
                        id: crosshair
                        CrosshairPreview { shape: rows.settings.crosshair(slot.row.index, slot.row.extra.size) }
                    }
                }
            }
            Component {
                id: slider
                SliderRow {
                    label: slot.row.label
                    min: slot.row.min; max: slot.row.max; step: slot.row.step
                    labelWidth: slot.row.labelWidth
                    Binding on value { value: slot.row.value }
                    valueText: slot.row.valueText
                    onMoved: v => rows.model.setValue(slot.index, v)
                }
            }
            Component {
                id: toggle
                ToggleRow {
                    label: slot.row.label
                    Binding on on { value: slot.row.on }
                    onChanged: rows.model.setOn(slot.index, on)
                }
            }
            Component {
                id: field
                FieldRow {
                    label: slot.row.label
                    boxWidth: slot.row.boxWidth
                    text: slot.row.text
                    onEdited: t => rows.model.setText(slot.index, t)
                }
            }
            Component {
                id: word
                Item {
                    height: link.height
                    WordLink {
                        id: link
                        size: slot.row.textSize
                        enabled: slot.row.enabled
                        text: slot.row.live
                        colour: slot.row.colour !== undefined ? slot.row.colour : Theme.text
                        onClicked: rows.model.click(slot.index)
                    }
                }
            }
            Component {
                id: button
                Item {
                    height: deck.height
                    DeckButton {
                        id: deck
                        text: slot.row.text
                        face: Theme[slot.row.face] || Theme.slate
                        em: Theme.em
                        sizeEms: 0.9; padXEms: 0.8; padYEms: 0.38; lip: 3
                        width: implicitWidth; height: implicitHeight
                        onClicked: rows.model.click(slot.index)
                    }
                }
            }
            Component {
                id: key
                KeyRow {
                    label: slot.row.label
                    binding: slot.row.live
                    keysOnly: slot.row.binding < 0
                    listening: rows.settings.listening && rows.settings.keyRow() === slot.index
                    onListen: rows.settings.listenKey(slot.index)
                    onMouse: b => rows.settings.pressMouse(b)
                    onWheel: up => rows.settings.wheel(up)
                    onPadAccepted: rows.settings.keyToPad(slot.index)
                    onActiveFocusChanged: if (!activeFocus && listening) rows.settings.stopKey()
                }
            }
            Component {
                id: pad
                PadRow {
                    label: slot.row.label
                    pad: slot.row.extra
                    live: slot.row.live
                    chosenSlot: slot.row.index
                    onPickSlot: s => rows.settings.padSlot(slot.index, s)
                    onListen: rows.settings.padListen(slot.index)
                    onKey: k => rows.settings.padKey(slot.index, k)
                    onChoose: c => rows.settings.padChoose(slot.index, c)
                    onAbandoned: rows.settings.padLeave(slot.index)
                }
            }
            Component {
                id: stand
                Item {
                    height: 150
                    HunterStand {
                        width: parent.width; height: 150
                        hunter: slot.row.extra.hunter
                    }
                }
            }
            Component {
                id: monitor
                GamepadMonitor { height: 182 }
            }
        }
          }
    }
}
