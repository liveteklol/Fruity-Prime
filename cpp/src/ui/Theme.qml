pragma Singleton
import QtQuick

// GuiTheme: the palette and the type the C#'s launcher draws with.
QtObject {
    readonly property color ink: "#0a0c10"
    readonly property color panel: "#121519"
    readonly property color panelLight: "#1a1f29"
    readonly property color edge: "#262e3c"
    readonly property color text: "#e6eaf2"
    readonly property color dim: "#8a93a6"
    readonly property color accent: "#ffb347"
    readonly property color good: "#5f9e72"
    readonly property color bad: "#a85454"

    readonly property int gap: 14
    readonly property int radius: 6
    readonly property int titleSize: 34
    readonly property int headingSize: 20
    readonly property int bodySize: 15
    readonly property int smallSize: 13
    // The well every screen lays itself out in, centred (UiLayout.Page).
    readonly property int wellWide: 860
    readonly property int wellNarrow: 640
}
