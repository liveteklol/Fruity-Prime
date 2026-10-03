import QtQuick

// Note: the one string on a screen that is a sentence -- the body face at
// .76em, held to a fixed number of lines whatever it says.
Text {
    id: note
    property real em: Theme.em
    property int lines: 2
    readonly property real size: em * 0.76
    font.family: Theme.mono
    font.pointSize: Theme.pt(size)
    color: Theme.textDim
    wrapMode: Text.Wrap
    lineHeightMode: Text.FixedHeight
    lineHeight: Theme.roundEven(size * 1.15)
    maximumLineCount: lines > 0 ? lines : 100000
    elide: lines > 0 ? Text.ElideRight : Text.ElideNone
    height: lines > 0 ? Theme.roundEven(size * 1.15 * lines) : implicitHeight
}
