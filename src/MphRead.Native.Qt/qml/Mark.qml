import QtQuick

// UiMark: the pair at the foot of every screen. Leaving is brass on the
// left; what the screen is for is the one blue, on the right, in capitals.
DeckButton {
    id: mark
    // "cancel", "accept", "add" or "fetch".
    property string shape: "accept"
    property string label
    readonly property bool accept: shape === "accept"

    em: Theme.em
    text: accept ? label.toUpperCase() : (label.length > 0 ? label.charAt(0).toUpperCase() + label.slice(1) : "")
    face: shape === "cancel" ? Theme.brass : accept ? Theme.blue : Theme.slate
    keyCap: shape === "cancel" ? "ESC" : accept ? "⏎" : ""
    sizeEms: accept ? 1.4 : 1.05
    padXEms: accept ? 1.4 : 1.0
    padYEms: accept ? 0.4 : 0.5
    lip: accept ? 6 : 4
    width: implicitWidth
    height: implicitHeight
}
