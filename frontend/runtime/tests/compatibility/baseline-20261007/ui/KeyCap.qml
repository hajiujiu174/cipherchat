import QtQuick
import "."

// .key / .back-button small: a key label in a thin box.
Rectangle {
    property alias text: label.text
    property int pixelSize: 11
    implicitWidth: Math.max(19, label.implicitWidth + 8); implicitHeight: 21
    color: "transparent"; border.color: Theme.line; border.width: 1
    Text { id: label; anchors.centerIn: parent; color: Theme.muted; font { family: uiFont; pixelSize: parent.pixelSize } }
}
