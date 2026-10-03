import QtQuick
import FruityPrime.Launcher

// SetupScreen: the one screen a fresh install needs before anything else.
Page {
    id: page
    property var nav
    widthEms: 44
    heading: "game files"

    SetupModel {
        id: setup
        onFinished: if (page.nav) page.nav.pop()
    }

    body: Item {
        implicitHeight: column.height
        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: width
            contentHeight: column.height
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Column {
                id: column
                width: flick.width
                spacing: 8
                Note { width: parent.width; lines: 0; text: setup.intro }
                Note { width: parent.width; lines: 0; text: setup.where; visible: text.length > 0 }
                WordLink {
                    visible: setup.previewsShown
                    enabled: setup.previewsEnabled
                    text: setup.previewsText
                    colour: Theme.textDim
                    onClicked: setup.renderPreviews()
                }
                ProgressRow {
                    visible: setup.showProgress
                    width: parent.width
                    fraction: setup.fraction
                    stage: setup.stage
                }
                Flickable {
                    id: logFlick
                    width: parent.width
                    height: 160
                    contentHeight: log.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Note {
                        id: log
                        width: logFlick.width
                        lines: 0
                        text: setup.log
                        onHeightChanged: logFlick.contentY = Math.max(0, height - logFlick.height)
                    }
                }
            }
        }
        ThinScroll { flick: flick }
    }

    no: Mark {
        shape: "cancel"; label: "back"
        visible: setup.ready
        onClicked: if (page.nav) page.nav.pop()
    }
    yes: Mark {
        id: choose
        shape: "accept"
        label: setup.working ? "working..." : "choose your .nds file"
        enabled: !setup.working
        onClicked: setup.choose()
    }

    Keys.onEscapePressed: if (setup.ready && nav) nav.pop()
    Component.onCompleted: choose.forceActiveFocus()
}
