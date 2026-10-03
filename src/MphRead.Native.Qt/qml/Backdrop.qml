import QtQuick
import QtQuick.Shapes

// UiLayout.Backdrop: the photograph, the two slow radial washes of
// MovingBackdrop, then #ground's two gradients over them.
Item {
    id: root
    property bool moving: true

    Image {
        anchors.fill: parent
        source: "launcher-bg.jpg"
        fillMode: Image.PreserveAspectCrop
        // GuiTheme.PixelPerfect: nearest-neighbour bitmaps.
        smooth: false
        mipmap: false
    }

    // MovingBackdrop: phase steps 0.011 every 33 ms.
    property real phase: 0
    NumberAnimation on phase {
        running: root.moving && root.visible
        from: 0; to: 1; duration: 3000; loops: Animation.Infinite
    }
    readonly property real angle: phase * 2 * Math.PI

    component Wash: Shape {
        id: wash
        property real cx
        property real cy
        property real rx
        property real ry
        property color colour
        anchors.fill: parent
        // An ellipse is a circle scaled on one axis.
        transform: Scale {
            origin.x: wash.cx * wash.width; origin.y: wash.cy * wash.height
            yScale: (wash.ry * wash.height) / Math.max(1, wash.rx * wash.width)
        }
        ShapePath {
            strokeWidth: -1
            fillGradient: RadialGradient {
                centerX: wash.cx * wash.width; centerY: wash.cy * wash.height
                focalX: centerX; focalY: centerY
                centerRadius: wash.rx * wash.width; focalRadius: 0
                GradientStop { position: 0; color: wash.colour }
                GradientStop { position: 1; color: Qt.rgba(wash.colour.r, wash.colour.g, wash.colour.b, 0) }
            }
            startX: -wash.width; startY: -wash.height * 4
            PathLine { x: wash.width * 2; y: -wash.height * 4 }
            PathLine { x: wash.width * 2; y: wash.height * 5 }
            PathLine { x: -wash.width; y: wash.height * 5 }
        }
    }
    Wash {
        cx: 0.34 + Math.sin(root.angle) * 0.12
        cy: 0.42 + Math.cos(root.angle * 0.83) * 0.10
        rx: 0.58; ry: 0.68
        colour: Qt.rgba(0xc4 / 255, 0x60 / 255, 0x58 / 255, 50 / 255)
    }
    Wash {
        cx: 0.70 + Math.cos(root.angle * 0.71) * 0.13
        cy: 0.62 + Math.sin(root.angle * 0.91) * 0.11
        rx: 0.62; ry: 0.72
        colour: Qt.rgba(0x30 / 255, 0x70 / 255, 0xba / 255, 58 / 255)
    }

    // #ground: left to right, then top to bottom.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.voidAt(0.55) }
            GradientStop { position: 0.38; color: Theme.voidAt(0) }
            GradientStop { position: 1; color: Theme.voidAt(0) }
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.voidAt(0.72) }
            GradientStop { position: 0.30; color: Theme.voidAt(0.10) }
            GradientStop { position: 0.62; color: Theme.voidAt(0.35) }
            GradientStop { position: 1; color: Theme.voidAt(0.90) }
        }
    }
}
