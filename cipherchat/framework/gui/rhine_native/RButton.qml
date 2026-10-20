import QtQuick
import "."

// Text button styles from style.css: plain, outlined (.chat-module button) and
// solid (.solid-button, left label with Chinese caption on the right).
Item {
    id: button
    property string text: ""
    property string caption: ""
    property bool solid: false
    property bool outlined: false
    property int pixelSize: 12
    property real letterSpacing: 0
    property color ink: Theme.ink
    property bool active: false
    signal clicked()
    implicitWidth: solid ? 280 : label.implicitWidth + (outlined ? 24 : 0)
    implicitHeight: solid ? 46 : outlined ? 36 : label.implicitHeight + 8
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: text + (caption ? " " + caption : "")
    Accessible.onPressAction: clicked()
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    Rectangle {
        anchors.fill: parent
        color: button.solid ? (area.containsMouse ? Theme.solidHover : Theme.solid) : "transparent"
        border.width: button.outlined || button.activeFocus ? 1 : 0
        border.color: button.activeFocus ? Theme.accent : area.containsMouse ? Theme.accent : Theme.line
    }
    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        x: button.solid ? 17 : button.outlined ? 12 : 0
        text: button.text
        color: button.solid ? Theme.solidInk : area.containsMouse || button.active ? Theme.accent : button.ink
        font { family: uiFont; pixelSize: button.solid ? 11 : button.pixelSize; letterSpacing: button.solid ? 1 : button.letterSpacing; bold: button.solid }
    }
    Text {
        visible: button.solid && button.caption !== ""
        anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
        text: button.caption; color: Theme.solidInk
        font { family: uiFont; pixelSize: 11 }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { button.forceActiveFocus(); button.clicked() }
    }
}
