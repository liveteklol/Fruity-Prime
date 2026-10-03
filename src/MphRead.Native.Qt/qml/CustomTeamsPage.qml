import QtQuick

// CustomTeamPicker: two to four teams and each one's size, held to the
// server's player slots.
Page {
    id: page
    property var layout: [2, 2, 2, 0, 0]
    property int maxPlayers: 8
    signal done(int count, var sizes)
    signal cancelled()

    widthEms: 44
    heading: "custom teams"

    readonly property int count: countRow.index + 2
    readonly property var sizes: [sizeA.index + 1, sizeB.index + 1, sizeC.index + 1, sizeD.index + 1]
    readonly property int total: sizes.slice(0, count).reduce((a, b) => a + b, 0)
    readonly property bool valid: total <= Math.max(2, Math.min(8, maxPlayers))
    readonly property var numbers: ["1", "2", "3", "4", "5", "6", "7", "8"]
    function sizeIndex(team) {
        return layout[0] >= 2 && team < layout[0] ? Math.max(1, layout[team + 1]) - 1 : 1
    }
    function describe() {
        const letters = sizes.slice(0, count).join("v")
        return letters
    }

    body: Column {
        spacing: 2
        ChoiceRow { id: countRow; width: parent.width; label: "Teams"; options: ["2", "3", "4"]; index: Math.max(0, page.layout[0] - 2); focus: true }
        ChoiceRow { id: sizeA; width: parent.width; label: "Team A size"; options: page.numbers; index: page.sizeIndex(0) }
        ChoiceRow { id: sizeB; width: parent.width; label: "Team B size"; options: page.numbers; index: page.sizeIndex(1) }
        ChoiceRow { id: sizeC; width: parent.width; label: "Team C size"; options: page.numbers; index: page.sizeIndex(2); visible: page.count > 2 }
        ChoiceRow { id: sizeD; width: parent.width; label: "Team D size"; options: page.numbers; index: page.sizeIndex(3); visible: page.count > 3 }
        Note {
            width: parent.width
            text: page.valid ? page.describe() + " · " + page.total + " player slots"
                             : "Custom teams must use no more than " + Math.max(2, Math.min(8, page.maxPlayers)) + " player slots."
            color: page.valid ? Theme.textDim : Theme.warm
        }
    }

    no: Mark { shape: "cancel"; label: "back"; onClicked: page.cancelled() }
    yes: Mark {
        shape: "accept"; label: "use teams"
        enabled: page.valid
        onClicked: page.done(page.count, page.sizes)
    }
    Keys.onEscapePressed: cancelled()
}
