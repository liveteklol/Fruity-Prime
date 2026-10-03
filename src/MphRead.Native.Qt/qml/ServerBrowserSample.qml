import QtQuick
import FruityPrime.Launcher

// -uishot's serverbrowser: the sample rows at two widths, centred, to check
// the columns as a row narrows.
Item {
    id: sampleRoot
    property var nav
    PlayModel { id: sample; face: 0 }
    Column {
        y: 12
        width: sampleRoot.width
        spacing: 18
        Repeater {
            model: [600, 400]
            Column {
                required property int modelData
                x: Math.round((sampleRoot.width - width) / 2)
                width: modelData
                spacing: 2
                Repeater {
                    model: sample.servers.length
                    ServerRow {
                        required property int index
                        width: parent.width
                        // Outside a deck stage the row keeps Avalonia's whole-point height.
                        height: 30
                        modelData: sample.servers[index]
                    }
                }
            }
        }
    }
}
