import QtQuick

// UiLayout.Page: the backdrop, the sheet over it and one panel centred on
// that, three rows inside -- the strip or heading, the body, and the foot
// (the marks, then the note).
FocusScope {
    id: page
    property bool overGame: false
    property real widthEms: 44
    property string heading
    // Each is an Item handed over by the screen; the page seats it.
    property Item strip
    property Item body
    property Item no
    property Item yes
    property Item extra
    property Item note
    property bool centreBody: false
    // DeckCard.Fill: the panel takes the whole height it is given.
    property bool fill: false

    readonly property real em: Theme.emFor(width, height)
    readonly property real gap: Theme.roundEven(em * 0.65)
    readonly property real footGap: Theme.roundEven(em * 0.45)
    readonly property bool hasMarks: no !== null || yes !== null || extra !== null
    // The card's inside, for bodies that size themselves to it.
    readonly property real innerWidth: card.width - card.pad * 2
    readonly property real bodyHeight: bodySlot.height

    function seat(item, slot) {
        if (item) {
            item.parent = slot
        }
    }
    onStripChanged: seat(strip, stripSlot)
    onBodyChanged: seat(body, bodySlot)
    onNoChanged: seat(no, marks)
    onYesChanged: seat(yes, marks)
    onExtraChanged: seat(extra, marks)
    onNoteChanged: seat(note, noteSlot)

    Loader {
        anchors.fill: parent
        sourceComponent: page.overGame ? scrim : backdrop
        Component { id: backdrop; Backdrop { } }
        Component { id: scrim; Rectangle { color: Theme.scrim } }
    }

    // DeckSheet: the scrim and the panel on it, fading in together.
    Item {
        id: sheet
        anchors.fill: parent
        property real t: Theme.still ? 1 : 0
        opacity: Theme.bezier(Math.min(1, t / 0.22 * 0.4), 0.3, 0.8, 0.4, 1)
        NumberAnimation on t { running: !Theme.still; from: 0; to: 1; duration: 400 }

        Rectangle { anchors.fill: parent; color: Theme.sheet }

        // SheetPad: .9em at the sides, 1.1em above and below.
        Item {
            id: padBox
            anchors.fill: parent
            anchors.leftMargin: Theme.roundEven(page.em * 0.9)
            anchors.rightMargin: anchors.leftMargin
            anchors.topMargin: Theme.roundEven(page.em * 1.1)
            anchors.bottomMargin: anchors.topMargin

            DeckCard {
                id: card
                em: page.em
                maxWidthEms: page.widthEms
                width: Math.min(padBox.width, page.em * page.widthEms)
                readonly property real natural: head.height + page.gap + (page.body ? page.body.implicitHeight : 0)
                                                + page.gap + foot.height + card.pad * 2
                height: page.fill ? padBox.height : Math.min(padBox.height, natural)
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 14 * (1 - rise)
                readonly property real rise: Theme.bezier(sheet.t, 0.18, 1.55, 0.35, 1)
                scale: 0.9 + 0.1 * rise
                readonly property real contentHeight: height - card.pad * 2

                Item {
                    id: head
                    width: parent.width
                    height: page.strip ? page.strip.height : (title.visible ? title.height : 0)
                    Item {
                        id: stripSlot
                        anchors.fill: parent
                    }
                    // UiLayout.Heading: lower case and dim, still carrying
                    // its column margin.
                    Text {
                        id: title
                        visible: !page.strip && page.heading.length > 0
                        x: 72 + Theme.roundEven((parent.width - 72 - implicitWidth) / 2)
                        topPadding: 26
                        text: page.heading.toLowerCase()
                        font.family: Theme.pixel
                        font.pixelSize: 15
                        color: Theme.textDim
                    }
                }
                Item {
                    id: bodySlot
                    y: head.height + page.gap
                    width: parent.width
                    height: Math.max(0, card.contentHeight - head.height - foot.height - page.gap * 2)
                }
                Item {
                    id: foot
                    y: card.contentHeight - height
                    width: parent.width
                    readonly property real marksHeight: Math.max(page.no && page.no.visible ? page.no.height : 0,
                                                              page.yes && page.yes.visible ? page.yes.height : 0,
                                                              page.extra && page.extra.visible ? page.extra.height : 0)
                    height: page.hasMarks ? marksHeight + page.footGap + noteSlot.height
                                          : noteSlot.height
                    // GapDock: leaving on the left, the rest along the right.
                    Item {
                        id: marks
                        width: parent.width
                        height: foot.marksHeight
                        readonly property real markGap: Theme.roundEven(page.em * 0.6)
                    }
                    Item {
                        id: noteSlot
                        y: page.hasMarks ? foot.marksHeight + page.footGap : 0
                        width: parent.width
                        height: page.note ? page.note.height : 0
                    }
                }
            }
        }
    }

    // The marks' places, once they are seated.
    Binding { when: page.no !== null; target: page.no; property: "x"; value: 0 }
    Binding { when: page.no !== null; target: page.no; property: "y"; value: page.no ? Theme.roundEven((foot.marksHeight - page.no.height) / 2) : 0 }
    Binding { when: page.yes !== null; target: page.yes; property: "x"; value: page.yes ? marks.width - page.yes.width : 0 }
    Binding { when: page.yes !== null; target: page.yes; property: "y"; value: page.yes ? Theme.roundEven((foot.marksHeight - page.yes.height) / 2) : 0 }
    Binding {
        when: page.extra !== null; target: page.extra; property: "x"
        value: page.extra ? marks.width - (page.yes && page.yes.visible ? page.yes.width + marks.markGap : 0) - page.extra.width : 0
    }
    Binding { when: page.extra !== null; target: page.extra; property: "y"; value: page.extra ? Theme.roundEven((foot.marksHeight - page.extra.height) / 2) : 0 }
    Binding { when: page.body !== null; target: page.body; property: "width"; value: bodySlot.width }
    Binding {
        when: page.body !== null; target: page.body; property: "height"
        value: page.centreBody && page.body ? Math.min(page.body.implicitHeight, bodySlot.height) : bodySlot.height
    }
    Binding {
        when: page.body !== null && page.centreBody; target: page.body; property: "y"
        value: page.body ? Theme.roundEven((bodySlot.height - Math.min(page.body.implicitHeight, bodySlot.height)) / 2) : 0
    }
    Binding { when: page.note !== null; target: page.note; property: "width"; value: noteSlot.width }
    Binding { when: page.strip !== null; target: page.strip; property: "x"; value: page.strip ? Theme.roundEven((head.width - page.strip.width) / 2) : 0 }
}
