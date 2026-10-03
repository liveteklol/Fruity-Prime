import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// DeckTile: one map as a card with the map on it -- the tag, the name, and
// the word along the bottom; a ring that turns accent when it is the one.
FocusScope {
    id: tile
    property string roomKey
    property string code
    property string blurb
    property string verb: "Select"
    property string chosenVerb: "Selected"
    property bool chosen: false
    // How many have picked this map; negative means no ballot.
    property int tally: -1
    property bool leader: false
    property real em: Theme.em
    signal clicked()

    readonly property bool hot: area.containsMouse || (activeFocus && Theme.keyboardDriving)
    readonly property bool raised: hot || chosen
    readonly property real radius: em * 0.55
    readonly property color ring: chosen || leader ? Theme.accent : hot ? Theme.hoverRing : Theme.edge
    activeFocusOnTab: true

    property real pop: hot ? 1.03 : 1
    Behavior on pop {
        enabled: !Theme.still
        SpringAnimation { spring: 14.8; damping: 0.6; epsilon: 0.0005 }
    }

    Item {
        id: body
        anchors.fill: parent
        scale: tile.pop

        RectangularShadow {
            anchors.fill: face
            offset.y: tile.raised ? 16 : 10
            blur: tile.raised ? 26 : 18
            radius: tile.radius
            color: Qt.rgba(0, 0, 0, 0.5)
        }
        Rectangle {
            width: parent.width; height: parent.height
            y: tile.raised ? 7 : 5
            radius: tile.radius
            color: Qt.rgba(0, 0, 0, 0.45)
        }
        Rectangle {
            x: -2; y: -2; width: parent.width + 4; height: parent.height + 4
            radius: tile.radius + 2
            color: tile.ring
        }
        Rectangle {
            id: face
            anchors.fill: parent
            radius: tile.radius
            color: Theme.panelDeep
        }
        Item {
            id: art
            anchors.fill: parent
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: mask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }
            Image {
                anchors.fill: parent
                source: shell.mapShot(tile.roomKey)
                fillMode: Image.PreserveAspectCrop
                smooth: false
                asynchronous: !Theme.still
                cache: true
            }
            // Drift: a warm and a cool glow, placed from the room key.
            Item {
                id: drift
                readonly property real phase: shell.roomPhase(tile.roomKey)
                x: (phase - 0.5) * 0.08 * tile.width - tile.width * 0.25
                y: (phase - 0.5) * 0.06 * tile.height - tile.height * 0.25
                width: tile.width * 1.5
                height: tile.height * 1.5
                component Glow: Shape {
                    id: glow
                    property real cx
                    property real cy
                    property real rx
                    property real ry
                    property color colour
                    anchors.fill: parent
                    transform: Scale {
                        origin.x: glow.cx * glow.width; origin.y: glow.cy * glow.height
                        yScale: (glow.ry * glow.height) / Math.max(1, glow.rx * glow.width)
                    }
                    ShapePath {
                        strokeWidth: -1
                        fillGradient: RadialGradient {
                            centerX: glow.cx * glow.width; centerY: glow.cy * glow.height
                            focalX: centerX; focalY: centerY
                            centerRadius: glow.rx * glow.width; focalRadius: 0
                            GradientStop { position: 0; color: glow.colour }
                            GradientStop { position: 1; color: Qt.rgba(glow.colour.r, glow.colour.g, glow.colour.b, 0) }
                        }
                        startX: 0; startY: -glow.height * 2
                        PathLine { x: glow.width; y: -glow.height * 2 }
                        PathLine { x: glow.width; y: glow.height * 3 }
                        PathLine { x: 0; y: glow.height * 3 }
                    }
                }
                Glow { cx: 0.30; cy: 0.40; rx: 0.45; ry: 0.55; colour: Qt.rgba(1, 179 / 255, 71 / 255, 56 / 255) }
                Glow { cx: 0.72; cy: 0.65; rx: 0.50; ry: 0.60; colour: Qt.rgba(0x2b / 255, 0x4e / 255, 0x6b / 255, 80 / 255) }
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 77 / 255) }
                    GradientStop { position: 0.40; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 51 / 255) }
                    GradientStop { position: 0.72; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 199 / 255) }
                    GradientStop { position: 1; color: Qt.rgba(10 / 255, 12 / 255, 16 / 255, 245 / 255) }
                }
            }
        }
        Rectangle {
            id: mask
            anchors.fill: parent
            radius: tile.radius
            visible: false
            layer.enabled: true
        }

        // .tag, its code in the accent.
        readonly property real pad: Theme.roundEven(tile.em * 0.5)
        readonly property real tagSize: Theme.pixelSize(tile.em * 0.72)
        Rectangle {
            id: tag
            x: body.pad; y: body.pad
            readonly property real padX: Theme.roundEven(body.tagSize * 0.45)
            readonly property real padY: Theme.roundEven(body.tagSize * 0.12)
            width: Theme.roundEven(codeText.implicitWidth + padX * 2)
            height: Theme.roundEven(codeText.implicitHeight + padY * 2)
            radius: Theme.roundEven(body.tagSize * 0.25)
            color: Qt.rgba(0, 0, 0, 0.6)
            border.width: 1
            border.color: Qt.rgba(0xe6 / 255, 0xea / 255, 0xf2 / 255, 0.14)
            Text {
                id: codeText
                x: tag.padX; y: tag.padY
                text: tile.code.toUpperCase()
                font.family: Theme.mono; font.bold: true; font.pixelSize: body.tagSize
                color: Theme.accent
            }
        }

        // .picked: the word on a four-point lip the width of the inside.
        readonly property real wordSize: Theme.pixelSize(tile.em * 0.9)
        readonly property real wordH: Theme.roundEven(wordSize * 1.7)
        readonly property string word: tile.chosen ? tile.chosenVerb : tile.verb
        readonly property real wordY: word.length > 0 ? tile.height - pad - wordH - 4
                                                      : tile.height - pad + Theme.roundEven(tile.em * 0.3)
        Rectangle {
            visible: body.word.length > 0
            x: body.pad; y: body.wordY + 4
            width: Math.max(0, tile.width - body.pad * 2); height: body.wordH
            radius: Theme.roundEven(body.wordSize * 0.55)
            color: tile.chosen ? Theme.brass.lip : Theme.moss.lip
            opacity: 1
        }
        Rectangle {
            id: slab
            visible: body.word.length > 0
            x: body.pad; y: body.wordY
            width: Math.max(0, tile.width - body.pad * 2); height: body.wordH
            radius: Theme.roundEven(body.wordSize * 0.55)
            readonly property color base: tile.chosen ? Theme.brass.fill : Theme.moss.fill
            color: area.containsMouse ? Theme.saturate(Theme.brightness(base, 1.22), 1.15) : base
            TrackedText {
                x: Theme.roundEven((slab.width - width) / 2)
                y: Theme.roundEven((slab.height - height) / 2)
                text: body.word
                size: body.wordSize
            }
        }
        Text {
            visible: tile.blurb.length > 0
            readonly property real size: Theme.pixelSize(tile.em * 0.76)
            x: body.pad
            y: Theme.roundEven(body.wordY - Theme.roundEven(tile.em * 0.3) - height)
            width: Math.max(10, tile.width - body.pad * 2)
            text: tile.blurb
            font.family: Theme.mono; font.pixelSize: size
            color: "#c7cfdd"
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        // The ballot's count, top right.
        Rectangle {
            visible: tile.tally >= 0
            readonly property real side: Math.max(tile.em * 1.7, tallyText.implicitWidth + tile.em * 0.7)
            width: Theme.roundEven(side); height: Theme.roundEven(tile.em * 1.7)
            x: Theme.roundEven(tile.width - tile.em * 0.4 - side); y: Theme.roundEven(tile.em * 0.4)
            radius: Theme.roundEven(tile.em * 0.35)
            color: tile.tally > 0 ? Theme.brass.fill : Qt.rgba(10 / 255, 12 / 255, 16 / 255, 0.85)
            border.width: 1
            border.color: tile.tally > 0 ? "#c9a227" : Theme.edge
            Text {
                id: tallyText
                anchors.centerIn: parent
                text: String(tile.tally)
                font.family: Theme.pixel; font.weight: Font.DemiBold; font.pixelSize: Theme.pixelSize(tile.em)
                color: tile.tally > 0 ? "white" : Theme.textDim
            }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: { Theme.keyboardDriving = false; tile.forceActiveFocus() }
        onClicked: tile.clicked()
    }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onSpacePressed: clicked()
}
