import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// DeckButton: a face on a solid lip with a soft drop under both, a hairline
// bevel, a spring pop on hover and a letter hop. Sized in the stage's em.
FocusScope {
    id: root
    property string text
    property var face: Theme.slate
    property real em: 10.81
    property real sizeEms: 1.55
    property real padXEms: 1.5
    property real padYEms: 0.85
    property real lip: 6
    property bool idle: false
    property bool selected: false
    // The support mark: a pixel heart instead of a label.
    property bool heart: false
    property real glyphWidthEms: 1.9
    property real glyphHeightEms: 1.27
    property color glyphColour: "#e8a0a0"
    property string tip
    // The key that does the same thing, in a chip on the face.
    property string keyCap
    signal clicked()

    readonly property real size: em * sizeEms
    readonly property real radius: size * 0.55
    readonly property real tracking: size * 0.04
    readonly property bool hot: enabled && (area.containsMouse || (activeFocus && Theme.keyboardDriving))
    readonly property bool ring: activeFocus && Theme.keyboardDriving
    readonly property bool down: area.pressed
    readonly property var letters: text.split("")

    activeFocusOnTab: true
    implicitWidth: heart ? Theme.roundEven(size * (glyphWidthEms + padXEms * 2))
                         : Theme.roundEven(labelWidth + size * padXEms * 2 + (keyWidth > 0 ? keyGap + keyWidth : 0))
    implicitHeight: heart ? Theme.roundEven(size * (glyphHeightEms + padYEms * 2))
                          : Theme.roundEven(metrics.height + size * padYEms * 2)

    FontMetrics {
        id: metrics
        font.family: Theme.pixel
        font.weight: Font.DemiBold
        font.pointSize: Theme.pt(root.size)
    }
    // DeckText.MeasureTracked: each glyph's advance plus the tracking, and a
    // fixed 0.42 em for a space.
    readonly property real labelWidth: {
        let pen = 0
        for (const c of letters)
            pen += (c === " " ? size * 0.42 : metrics.advanceWidth(c)) + tracking
        return pen
    }

    // DeckChip.DrawKey: the body face at half the label's size.
    readonly property real keyGap: size * 0.5
    readonly property real capSize: size * 0.5
    FontMetrics { id: capMetrics; font.family: Theme.mono; font.bold: true; font.pointSize: Theme.pt(root.capSize) }
    readonly property real keyTextWidth: keyCap.length > 0 ? capMetrics.advanceWidth(keyCap) : 0
    readonly property real keyWidth: keyCap.length > 0 ? Math.max(capSize * 1.5, keyTextWidth + capSize * 0.7) : 0
    readonly property real keyHeight: Math.max(capSize * 1.5, capMetrics.height)

    function shade(c) {
        if (!enabled)
            return Theme.brightness(Theme.saturate(c, 0.3), 0.55)
        return hot ? Theme.saturate(Theme.brightness(c, 1.22), 1.15) : c
    }

    // The idle bob: a raised cosine over 3.4 s, two and a half points deep.
    property real bobPhase: 0
    readonly property real bob: idle && !hot && !Theme.still ? -1.25 * (1 - Math.cos(bobPhase * Math.PI * 2)) : 0
    NumberAnimation on bobPhase {
        running: root.idle && root.visible && !root.hot && !Theme.still
        from: 0; to: 1; duration: 3400; loops: Animation.Infinite
    }

    readonly property real lipNow: down ? 2 : lip
    property real pop: hot ? 1.055 : 1
    Behavior on pop {
        NumberAnimation { duration: 320; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.spring }
    }

    // The letter hop starts on the way in; each glyph runs its own stagger.
    signal hopStarted()
    onHotChanged: if (hot) hopStarted()

    Item {
        id: body
        width: root.width
        height: root.height
        y: root.lip - root.lipNow + root.bob
        scale: root.down ? 1 : root.pop

        // The wedge over the tab that is up.
        Shape {
            id: wedge
            visible: root.selected
            preferredRendererType: Shape.CurveRenderer
            readonly property real wedgeHalf: root.size * 0.4
            readonly property real wedgeTop: -root.size * 0.72
            readonly property real wedgeMid: Theme.roundEven(root.width / 2)
            ShapePath {
                strokeWidth: -1
                fillColor: root.shade(root.face.fill)
                startX: wedge.wedgeMid - wedge.wedgeHalf; startY: wedge.wedgeTop
                PathLine { x: wedge.wedgeMid + wedge.wedgeHalf; y: wedge.wedgeTop }
                PathLine { x: wedge.wedgeMid; y: wedge.wedgeTop + wedge.wedgeHalf }
            }
        }
        // 0 lip 0 the lip colour
        Rectangle {
            width: root.width; height: root.height
            y: root.lipNow
            radius: root.radius
            color: root.shade(root.face.lip)
        }
        // 0 lip+4px 12px rgba(0,0,0,.55)
        RectangularShadow {
            anchors.fill: faceRect
            offset.y: root.lipNow + 4
            blur: 12
            radius: root.radius
            color: Qt.rgba(0, 0, 0, 0.55)
        }
        // The focus ring: 0 0 0 2px accent.
        Rectangle {
            visible: root.ring
            x: -2; y: -2; width: root.width + 4; height: root.height + 4
            radius: root.radius + 2
            color: Theme.accent
        }
        Rectangle {
            id: faceRect
            width: root.width; height: root.height
            radius: root.radius
            color: root.shade(root.face.fill)
        }
        // One hairline of light along the top.
        Rectangle {
            x: 1; y: 1; width: root.width - 2; height: root.height * 0.4
            radius: root.radius - 1
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0x17 / 255) }
                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
        // The key chip, after the label.
        Rectangle {
            visible: root.keyWidth > 0 && root.enabled && !root.heart
            x: Theme.roundEven(Theme.roundEven((root.width - root.labelWidth - (root.keyWidth > 0 ? root.keyGap + root.keyWidth : 0)) / 2)
                               + root.labelWidth + root.keyGap)
            y: Theme.roundEven((root.height - root.keyHeight) / 2)
            width: root.keyWidth; height: root.keyHeight
            radius: root.capSize * 0.3
            color: Qt.rgba(0, 0, 0, 107 / 255)
            Text {
                x: Theme.roundEven((parent.width - root.keyTextWidth) / 2)
                y: Theme.roundEven((parent.height - capMetrics.height) / 2)
                text: root.keyCap
                font: capMetrics.font
                color: Theme.accent
            }
        }
        // The label, one glyph at a time so each can hop.
        Item {
            visible: !root.heart
            x: Theme.roundEven((root.width - root.labelWidth - (root.keyWidth > 0 ? root.keyGap + root.keyWidth : 0)) / 2)
            width: root.labelWidth
            height: root.height
            Repeater {
                id: glyphs
                model: root.letters.length
                Text {
                    id: glyph
                    required property int index
                    readonly property string ch: root.letters[index]
                    readonly property int hopIndex: {
                        let n = 0
                        for (let i = 0; i < index; ++i)
                            if (root.letters[i] !== " ") n++
                        return n
                    }
                    property real amount: 0
                    visible: ch !== " "
                    x: {
                        let pen = 0
                        for (let i = 0; i < index; ++i)
                            pen += (root.letters[i] === " " ? root.size * 0.42
                                    : metrics.advanceWidth(root.letters[i])) + root.tracking
                        return Theme.roundEven(pen)
                    }
                    y: Theme.roundEven((root.height - implicitHeight) / 2) - root.size * 0.18 * amount
                    rotation: (hopIndex % 2 === 0 ? -3 : 3) * amount
                    text: ch
                    color: root.enabled ? Theme.text : Theme.textDim
                    font: metrics.font
                    renderType: Text.NativeRendering
                    SequentialAnimation {
                        id: letterHop
                        PauseAnimation { duration: 22 * hopIndex }
                        NumberAnimation {
                            target: glyph; property: "amount"; from: 0; to: 1; duration: 160
                            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.spring
                        }
                        NumberAnimation {
                            target: glyph; property: "amount"; to: 0; duration: 260
                            easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.spring
                        }
                    }
                    Connections {
                        target: root
                        function onHopStarted() { letterHop.restart() }
                    }
                }
            }
        }
        // DeckHeart.DrawHeart: twelve by eight whole pixels.
        Item {
            visible: root.heart
            readonly property real gw: root.size * root.glyphWidthEms
            readonly property real gh: root.size * root.glyphHeightEms
            readonly property real cell: Math.max(1, Math.floor(Math.min(gw / 12, gh / 8)))
            x: Theme.roundEven((root.width - gw) / 2 + (gw - 12 * cell) / 2)
            y: Theme.roundEven((root.height - gh) / 2 + (gh - 8 * cell) / 2)
            Repeater {
                model: [[0, 2, 3], [0, 7, 3], [1, 1, 10], [2, 1, 10], [3, 1, 10],
                        [4, 2, 8], [5, 3, 6], [6, 4, 4], [7, 5, 2]]
                Rectangle {
                    required property var modelData
                    x: modelData[1] * parent.cell
                    y: modelData[0] * parent.cell
                    width: modelData[2] * parent.cell
                    height: parent.cell
                    color: root.shade(root.glyphColour)
                }
            }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: { Theme.keyboardDriving = false; root.forceActiveFocus() }
        onClicked: root.clicked()
    }
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
