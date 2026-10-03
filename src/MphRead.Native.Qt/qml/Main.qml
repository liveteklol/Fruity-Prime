import QtQuick

// The menus' root: the front screen or the pause menu under a stack of
// screens, as StartScreen and InGameMenu keep one. Only the top of the stack
// exists; a closed menu leaves nothing in the scene graph.
Item {
    id: root
    // [{ url, props }], the last one showing.
    property var stack: []
    readonly property bool stacked: stack.length > 0

    // The captures hold everything still and switch the phone curve on.
    property bool still: false
    property bool phone: Qt.platform.os === "android" || Qt.platform.os === "ios"
    // What the base screen is asked to show (the end panel's tab).
    property var baseProps: ({})
    Binding { target: Theme; property: "still"; value: root.still }
    Binding { target: Theme; property: "phone"; value: root.phone }
    Binding { target: Theme; property: "em"; value: Theme.emFor(root.width, root.height) }

    function push(url, props) {
        stack = stack.concat([{ url: url, props: props || {} }])
    }
    function pop() {
        stack = stack.slice(0, stack.length - 1)
        if (!stacked && shell.page === "pause" && !base.item)
            shell.resume()
    }
    function reset() {
        stack = []
    }
    // Replace everything with one screen: the captures' way in.
    function only(url, props) {
        stack = [{ url: url, props: props || {} }]
    }

    // StartScreen's ways out of the front screen.
    function openPlay() {
        if (!shell.gameFilesReady) {
            openSetup()
            return
        }
        push("PlayPage.qml", { face: 0 })
    }
    function openSettings(overGame) {
        push("SettingsPage.qml", { overGame: !!overGame })
    }
    function openSetup() {
        push("SetupPage.qml", {})
    }
    function openCreateServer() {
        push("CreateServerPage.qml", {})
    }
    function openLobby() {
        push("LobbyPage.qml", {})
    }
    // The lobby closed: back to the screen under it, told why.
    function lobbyClosed(reason) {
        stack = stack.slice(0, stack.length - 1)
        if (stack.length > 0 && reason.length > 0) {
            const last = stack[stack.length - 1]
            stack = stack.slice(0, stack.length - 1).concat([{ url: last.url, props: Object.assign({}, last.props, { endedReason: reason }) }])
        }
    }
    function openVote() {
        const why = shell.whyNotVoting()
        if (why.length > 0) {
            shell.systemMessage(why)
            shell.resume()
            return
        }
        push("PlayPage.qml", { face: 4, overGame: true })
    }
    function askToQuit() {
        push("ConfirmPage.qml", { question: "Quit " + shell.brand + "?", yesAction: () => shell.quit() })
    }

    Connections {
        target: shell
        function onPageChanged() {
            root.reset()
            if (shell.page === "front" && !shell.gameFilesReady)
                root.openSetup()
        }
        function onLobbyOpened() { root.openLobby() }
        function onKeyboardDriving() { Theme.keyboardDriving = true }
        function onScreenRequested(url, props) {
            if (url.length > 0) {
                root.only(url, props)
            } else {
                root.reset()
                root.baseProps = props
            }
        }
    }

    Loader {
        id: base
        anchors.fill: parent
        visible: !root.stacked
        enabled: visible
        focus: !root.stacked
        sourceComponent: shell.page === "front" ? start
                       : shell.page === "pause" ? pause
                       : shell.page === "end" ? end
                       : null
        onLoaded: if (!root.stacked) item.forceActiveFocus()
    }
    Component {
        id: start
        StartPage {
            focus: true
            onPlay: root.openPlay()
            onSettings: root.openSettings(false)
            onQuit: root.askToQuit()
        }
    }
    Component {
        id: pause
        PausePage {
            focus: true
            onSettings: root.openSettings(true)
            onVoteMap: root.openVote()
        }
    }
    Component { id: end; EndPanel { focus: true; hunterTab: !!root.baseProps.hunterTab } }

    Loader {
        id: top
        anchors.fill: parent
        focus: root.stacked
        readonly property var entry: root.stacked ? root.stack[root.stack.length - 1] : null
        onEntryChanged: {
            if (entry) {
                const props = Object.assign({ nav: root }, entry.props)
                setSource(entry.url, props)
            } else {
                source = ""
                if (base.item)
                    base.item.forceActiveFocus()
            }
        }
        onLoaded: item.forceActiveFocus()
    }

    ControllerKeyboard {
        id: controllerKeyboard
        Component.onCompleted: Theme.keyboard = controllerKeyboard
    }
}
