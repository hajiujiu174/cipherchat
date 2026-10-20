import QtQuick
import "."

// .chat-field: small caption above an underlined borderless input.
Column {
    id: field
    property string caption: ""
    property alias text: input.text
    property bool password: false
    property bool numeric: false
    property alias input: input
    signal accepted()
    spacing: 8
    width: 300
    Text { text: field.caption; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
    Item {
        width: field.width; height: 36
        MouseArea { anchors.fill: parent; onClicked: input.forceActiveFocus() }
        TextInput {
            id: input
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            color: Theme.ink; selectionColor: Theme.selection; selectedTextColor: Theme.ink
            font { family: uiFont; pixelSize: 14 }
            echoMode: field.password ? TextInput.Password : TextInput.Normal
            inputMethodHints: field.numeric ? Qt.ImhDigitsOnly : Qt.ImhNone
            validator: field.numeric ? portValidator : null
            clip: true; selectByMouse: true
            activeFocusOnTab: true
            Accessible.name: field.caption
            onAccepted: field.accepted()
            IntValidator { id: portValidator; bottom: 1; top: 65535 }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: input.activeFocus ? Theme.accent : Theme.line }
    }
}
