import QtQuick

// ConfirmScreen: a question in the display face, No on the left and the
// answer on the right. Escape is No; No has the keyboard to start with.
Page {
    id: confirm
    property string question
    property string yesLabel: "yes"
    property string noLabel: "no"
    property var nav
    property var yesAction
    property var noAction
    signal answered(bool yes)
    onAnswered: yes => {
        if (nav)
            nav.pop()
        const act = yes ? yesAction : noAction
        if (act)
            act()
    }

    widthEms: 19   // UiLayout.WellShort
    centreBody: true
    body: Text {
        text: confirm.question
        font.family: Theme.pixel
        font.pixelSize: 26
        color: Theme.text
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }
    no: Mark { id: noMark; shape: "cancel"; label: confirm.noLabel; focus: true
               KeyNavigation.right: yesMark; onClicked: confirm.answered(false) }
    yes: Mark { id: yesMark; shape: "accept"; label: confirm.yesLabel
                KeyNavigation.left: noMark; onClicked: confirm.answered(true) }

    Component.onCompleted: noMark.forceActiveFocus()
    Keys.onEscapePressed: confirm.answered(false)
}
