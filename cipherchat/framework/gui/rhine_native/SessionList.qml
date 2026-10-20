import QtQuick
import QtQuick.Controls as Controls
import "."

Item {
    id: sessions
    property var store
    property bool group: false
    signal chosen()
    signal createRequested()
    function focusSearch() { search.input.forceActiveFocus(); }
    Column {
        anchors.fill: parent
        spacing: 12
        Row {
            width: parent.width; spacing: 8
            Text { width: parent.width - create.width - 8; text: sessions.group ? "群聊" : "会话"; color: Theme.ink; font { family: boldFont; pixelSize: 16 } }
            RButton { id: create; objectName: "newConversation"; text: sessions.group ? "＋ 建群" : "＋ 新建"; onClicked: sessions.createRequested() }
        }
        RField { id: search; objectName: "sessionSearch"; width: parent.width; caption: sessions.group ? "搜索群组" : "搜索联系人 / 会话" }
        ListView {
            id: list
            objectName: "conversationList"
            width: parent.width; height: Math.max(0, sessions.height - y)
            clip: true; boundsBehavior: Flickable.StopAtBounds
            model: store.conversations(sessions.group, search.text)
            Controls.ScrollBar.vertical: Controls.ScrollBar {}
            delegate: Item {
                id: row
                required property var modelData
                width: list.width; height: 76
                readonly property bool current: sessions.group ? store.selectedGroup === modelData.id : store.selectedPeer === modelData.id
                objectName: "session-" + modelData.id
                activeFocusOnTab: true
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name + (modelData.draft ? " 有草稿" : "")
                function choose() { if (sessions.group) store.chooseGroup(modelData.id); else store.choosePeer(modelData.id); sessions.chosen(); }
                Keys.onReturnPressed: choose()
                Keys.onSpacePressed: choose()
                Rectangle { anchors.fill: parent; color: row.current ? Theme.selection : hover.containsMouse ? Theme.hover : "transparent"; border.width: row.activeFocus ? 1 : 0; border.color: Theme.accent }
                Rectangle { visible: row.current; width: 2; height: parent.height; color: Theme.accent }
                Column {
                    x: 12; y: 12; width: parent.width - 24; spacing: 8
                    Text { width: parent.width; elide: Text.ElideRight; text: row.modelData.name; color: Theme.ink; font { family: uiFont; pixelSize: 14; bold: row.current } }
                    Text { width: parent.width; elide: Text.ElideRight; text: row.modelData.draft ? "[草稿] " + row.modelData.draft.replace(/\n/g, " ") : row.modelData.preview || (row.modelData.online ? "在线" : "点击打开会话"); color: row.modelData.draft ? Theme.accent : Theme.muted; font { family: uiFont; pixelSize: 12 } }
                }
                MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: row.choose() }
            }
            Text { anchors { top: parent.top; topMargin: 28; left: parent.left; right: parent.right } visible: list.count === 0; text: search.text ? "没有匹配的会话" : sessions.group ? "尚无群组，点击「建群」开始" : "从「新建」或联系人开始聊天"; wrapMode: Text.Wrap; color: Theme.muted; font { family: uiFont; pixelSize: 13 } }
        }
    }
}
