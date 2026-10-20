import QtQuick
import "."
import QtQuick.Dialogs

// .tab-panel content: chat.ts chatPanel(record, tab) as native components.
Flickable {
    id: panel
    property var store
    property string module: "account"
    property string tab: "overview"
    readonly property int panelWidth: width - 12
    width: 636; height: 189 + 22
    clip: true
    contentWidth: width; contentHeight: body.height + 22
    boundsBehavior: Flickable.StopAtBounds
    onModuleChanged: contentY = 0
    onTabChanged: contentY = 0
    // Follow new messages only when the reader was already near the bottom (chat.ts refresh()).
    property bool pinned: true
    onContentYChanged: if (!moving || dragging) pinned = contentHeight - contentY - height < 70
    function scrollToEnd() { if (pinned) contentY = Math.max(0, contentHeight - height) }

    Rectangle { // thin scrollbar (scrollbar-color #bbb4a6)
        parent: panel; visible: panel.contentHeight > panel.height
        x: panel.width - 3; width: 2; color: Theme.faint
        height: Math.max(20, panel.height * panel.height / panel.contentHeight)
        y: (panel.height - height) * panel.contentY / Math.max(1, panel.contentHeight - panel.height)
    }
    Column {
        id: body
        y: 22; width: panel.panelWidth; spacing: 14
        Text { text: store.status; color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1 } }
        Loader {
            width: parent.width
            sourceComponent: ({
                "account/overview": signIn, "account/notes": connection, "account/history": ownIdentity,
                "chat/overview": conversation, "chat/notes": archive, "chat/history": attachments,
                "contacts/overview": directory, "contacts/notes": discovery, "contacts/history": requests,
                "groups/overview": groupConversation, "groups/notes": createGroup, "groups/history": members,
                "files/overview": queue, "files/notes": upload, "files/history": received
            })[panel.module + "/" + panel.tab]
        }
    }

    component Label: Text { color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1 } }
    component Note: Text { width: parent ? parent.width : 300; wrapMode: Text.Wrap; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
    component Empty: Text { width: parent ? parent.width : 300; wrapMode: Text.Wrap; color: Theme.text; topPadding: 6; font { family: uiFont; pixelSize: 13 } }
    component Rule: Rectangle { width: parent ? parent.width : 0; height: 1; color: Theme.line }
    component PeerPicker: Row {
        spacing: 10
        RField { id: peerField; width: panel.panelWidth - go.width - 10; caption: "RECIPIENT / 接收人"; text: store.selectedPeer; onAccepted: store.choosePeer(text) }
        RButton { id: go; outlined: true; text: "连接会话 →"; anchors.bottom: parent.bottom; onClicked: store.choosePeer(peerField.text) }
    }
    component MessageList: Column {
        property bool group: false
        property bool all: false
        width: panel.panelWidth
        Repeater {
            model: store.messages(group, all)
            delegate: Item {
                required property var modelData
                readonly property bool own: modelData.from === store.username
                objectName: "messageRow"
                width: panel.panelWidth; height: messageBody.implicitHeight + 24
                Rectangle { visible: own; x: 0; y: 0; width: 2; height: parent.height; color: Theme.accent }
                Column {
                    id: messageBody
                    x: own ? 14 : 0; y: 12; width: parent.width - x; spacing: 7
                Item {
                    width: parent.width; height: Math.max(from.implicitHeight, 16)
                    Text { id: from; width: parent.width - 170; elide: Text.ElideRight; text: modelData.from; color: Theme.ink; font { family: boldFont; pixelSize: 12 } }
                    Text { anchors.right: parent.right; text: store.time(modelData.timestamp); color: Theme.muted; font { family: uiFont; pixelSize: 10 } }
                }
                Text { width: parent.width; text: modelData.text; wrapMode: Text.WrapAnywhere; color: Theme.ink; lineHeight: 1.4; textFormat: Text.PlainText; font { family: uiFont; pixelSize: 16 } }
                Text { text: store.receiptText(modelData.state) + (all ? " · " + modelData.to : ""); color: Theme.muted; font { family: uiFont; pixelSize: 10 } }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            }
        }
        Empty {
            visible: store.messages(group, all).length === 0
            text: group ? "选择一个群组，读取频道消息。" : store.selectedPeer || all ? "尚无消息记录。" : "输入联系人用户名，打开私密会话。"
        }
    }
    component Composer: Column {
        property bool group: false
        width: panel.panelWidth; spacing: 10
        Item {
            width: parent.width; height: Math.min(160, Math.max(66, editor.contentHeight + 20))
            TextEdit {
                id: editor
                anchors { fill: parent; topMargin: 10; bottomMargin: 10 }
                wrapMode: TextEdit.Wrap; color: Theme.ink; selectByMouse: true; selectionColor: Theme.selection
                font { family: uiFont; pixelSize: 14 }
                // Restoring a draft is not typing; only user edits notify the peer.
                property bool restoring: false
                function restore() { restoring = true; text = store.draft(group); restoring = false }
                Component.onCompleted: restore()
                Connections { target: store; function onSelectedPeerChanged() { editor.restore() } function onSelectedGroupChanged() { editor.restore() } }
                Accessible.name: "消息正文"
                Text { visible: !editor.text && !editor.activeFocus; text: "输入消息…"; color: Theme.faint; font: editor.font }
                onTextChanged: { if (restoring) return; store.setDraft(group, text); if (!group && text) store.typing() }
                Keys.onReturnPressed: event => { if (event.modifiers & Qt.ControlModifier) { send(); event.accepted = true } else event.accepted = false }
                function send() { store.sendMessage(group, text); if (!store.draft(group)) { restoring = true; text = ""; restoring = false; panel.pinned = true } }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: editor.activeFocus ? Theme.accent : Theme.line }
        }
        Item {
            width: parent.width; height: 46
            Text { anchors.verticalCenter: parent.verticalCenter; text: !group && store.typingPeer ? "对方正在输入…" : "Ctrl + Enter 发送"; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
            RButton { anchors.right: parent.right; solid: true; width: 230; text: "SEND MESSAGE"; caption: "发送 ↗"; onClicked: editor.send() }
        }
    }
    component IdentityCard: Column {
        property var identity: null
        property bool own: false
        width: panel.panelWidth; spacing: 10; topPadding: 6
        Empty { visible: !identity; text: "尚未读取安全身份。" }
        Label { visible: !!identity; text: (own ? "LOCAL IDENTITY" : "PEER IDENTITY") + " / " + (identity ? identity.username : "") }
        TextEdit { visible: !!identity; width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; text: identity ? identity.fingerprint : ""; color: Theme.ink; font { family: "Consolas"; pixelSize: 14; letterSpacing: 1 } }
        Note { visible: !!identity; text: own ? "将此指纹通过可信渠道提供给联系人核对。" : identity && identity.verified ? "已人工核对。" : "请通过可信渠道与对方核对上述指纹。" }
        RButton { visible: !!identity && !own && !identity.verified; outlined: true; text: "已核对指纹，信任此身份 →"; onClicked: store.confirmIdentity(identity.username) }
    }
    component UploadForm: Column {
        width: panel.panelWidth; spacing: 16
        property string path: ""
        RField { id: recipient; width: parent.width; caption: "RECIPIENT / 接收人"; text: store.selectedPeer }
        Column {
            spacing: 8; width: parent.width
            Text { text: "ATTACHMENT / 本地文件"; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
            Row {
                spacing: 12
                RButton { outlined: true; text: "选择文件…"; onClicked: picker.open() }
                Text { anchors.verticalCenter: parent.verticalCenter; width: panel.panelWidth - 120; elide: Text.ElideMiddle; text: path || "未选择文件"; color: Theme.text; font { family: uiFont; pixelSize: 12 } }
            }
        }
        Note { text: "首次会话需要核对身份；文件通过加密传输服务发送。" }
        RButton { solid: true; width: parent.width; text: "TRANSFER"; caption: "发送文件 ↗"; onClicked: store.sendFile(recipient.text, path) }
        FileDialog { id: picker; title: "选择要发送的文件"; onAccepted: path = chat.localPath(selectedFile) }
    }
    component FileList: Column {
        property bool received: false
        width: panel.panelWidth
        Repeater {
            model: store.fileRows(received)
            delegate: Column {
                required property var modelData
                width: panel.panelWidth; topPadding: 13; bottomPadding: 13; spacing: 6
                Text { text: modelData.name; width: parent.width; elide: Text.ElideMiddle; color: Theme.ink; font { family: boldFont; pixelSize: 13 } }
                Text { text: (modelData.outgoing ? "TO " : "FROM ") + modelData.peer; color: Theme.muted; font { family: uiFont; pixelSize: 10 } }
                Rectangle {
                    width: parent.width; height: 3; color: Theme.line
                    Rectangle { height: 3; color: Theme.accent; width: parent.width * Math.min(1, modelData.authenticatedBytes / Math.max(1, modelData.totalBytes)) }
                }
                Text { text: store.bytes(modelData.authenticatedBytes) + " / " + store.bytes(modelData.totalBytes) + " · " + (modelData.cancelled ? "已取消" : modelData.complete ? "验证完成" : "传输中"); color: Theme.text; font { family: uiFont; pixelSize: 11 } }
                Row {
                    spacing: 10
                    RButton { visible: modelData.complete && !modelData.outgoing; outlined: true; text: "另存为 ↓"; onClicked: { saver.source = modelData.path; saver.selectedFile = "file:///" + modelData.name; saver.open() } }
                    RButton { visible: modelData.complete && !modelData.outgoing; outlined: true; text: "在文件夹中显示"; onClicked: chat.reveal(modelData.path) }
                    RButton { visible: !modelData.complete && !modelData.cancelled; outlined: true; text: "恢复"; onClicked: store.resumeFile(modelData.fileId) }
                    RButton { visible: !modelData.complete && !modelData.cancelled; outlined: true; text: "取消"; onClicked: store.cancelFile(modelData.fileId) }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
            }
        }
        Empty { visible: store.fileRows(received).length === 0; text: "没有符合条件的传输记录。" }
        FileDialog {
            id: saver; property string source: ""; fileMode: FileDialog.SaveFile; title: "保存已验证文件"
            onAccepted: { const error = chat.saveCopy(source, selectedFile); store.notify(error || "文件已保存") }
        }
    }
    component ContactRow: Item {
        property string name: ""
        property string detail: ""
        default property alias actions: actionRow.data
        width: panel.panelWidth; height: 58
        Column { anchors.verticalCenter: parent.verticalCenter; spacing: 5
            Text { text: name; color: Theme.ink; font { family: boldFont; pixelSize: 13 } }
            Text { visible: detail !== ""; text: detail; color: Theme.muted; font { family: uiFont; pixelSize: 10 } } }
        Row { id: actionRow; anchors { right: parent.right; verticalCenter: parent.verticalCenter } spacing: 8 }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
    }
    component GroupPicker: Column {
        width: panel.panelWidth; spacing: 8
        Text { text: "CHANNEL / 群聊频道"; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
        Flow {
            width: parent.width; spacing: 8
            RButton { visible: store.groups.length > 0; outlined: true; active: store.selectedGroup === 0; text: "选择群组"; onClicked: { store.selectedGroup = 0; store.revision++ } }
            Repeater {
                model: (void store.revision, store.groups)
                delegate: RButton { required property var modelData; outlined: true; active: modelData.id === store.selectedGroup; text: modelData.name; onClicked: { store.selectedGroup = modelData.id; store.revision++ } }
            }
            Empty { visible: store.groups.length === 0; text: "尚无群组，可在「创建群聊」中建立。" }
        }
    }

    Component { id: signIn; Column {
        spacing: 16
        RField { id: user; width: panel.panelWidth; caption: "USER / 用户名"; text: store.config.username }
        RField { id: secret; width: panel.panelWidth; caption: "PASSWORD / 密码"; password: true; onAccepted: login.clicked() }
        Row { spacing: 10
            RButton { id: login; solid: true; width: 260; text: "SIGN IN"; caption: "登录 →"; onClicked: { store.authenticate(user.text, secret.text, false); secret.text = "" } }
            RButton { outlined: true; height: 46; text: "注册并登录"; onClicked: { store.authenticate(user.text, secret.text, true); secret.text = "" } }
            RButton { outlined: true; height: 46; text: "断开连接"; onClicked: store.disconnect() } }
        Note { text: store.config.host + " : " + store.config.port }
    } }
    Component { id: connection; Column {
        spacing: 16
        RField { id: host; width: panel.panelWidth; caption: "SERVER / 服务器"; text: store.config.host }
        RField { id: port; width: panel.panelWidth; caption: "PORT / 端口"; numeric: true; text: String(store.config.port) }
        RField { id: tlsName; width: panel.panelWidth; caption: "TLS NAME / 证书主机名"; text: store.config.serverName }
        RField { id: ca; width: panel.panelWidth; caption: "TRUST / 证书文件"; text: store.config.caFile }
        RField { id: data; width: panel.panelWidth; caption: "LOCAL STORAGE / 数据目录"; text: store.config.dataDirectory }
        RButton { id: direct; property bool checked: store.config.directEnabled; text: (checked ? "■" : "□") + "  允许直连文件传输"; onClicked: checked = !checked }
        RButton { solid: true; width: panel.panelWidth; text: "SAVE PROFILE"; caption: "保存连接配置 →"
            onClicked: store.saveConnection({ host: host.text.trim(), port: Number(port.text), serverName: tlsName.text.trim(), caFile: ca.text.trim(), dataDirectory: data.text.trim(), directEnabled: direct.checked }) }
    } }
    Component { id: ownIdentity; IdentityCard { own: true; identity: (void store.revision, store.identity) } }
    component Thread: Item {
        property bool group: false
        width: panel.panelWidth; height: Math.max(480, panel.height - 78)
        Loader { id: pick; width: parent.width; anchors.top: parent.top; sourceComponent: group ? groupChoice : peerChoice }
        Component { id: peerChoice; PeerPicker {} }
        Component { id: groupChoice; GroupPicker {} }
        Composer { id: compose; group: parent.group; anchors.bottom: parent.bottom }
        Flickable {
            id: timeline
            objectName: "messageTimeline"
            anchors { top: pick.bottom; topMargin: 16; bottom: compose.top; bottomMargin: 16; left: parent.left; right: parent.right }
            clip: true; contentWidth: width; contentHeight: messages.height
            boundsBehavior: Flickable.StopAtBounds
            property bool follow: true
            onContentYChanged: if (dragging) follow = contentHeight - contentY - height < 70
            function tail() { if (follow) contentY = Math.max(0, contentHeight - height) }
            MessageList { id: messages; group: timeline.parent.group; onHeightChanged: Qt.callLater(timeline.tail) }
        }
    }
    Component { id: conversation; Thread {} }
    Component { id: archive; MessageList { all: true } }
    Component { id: attachments; Column {
        spacing: 12
        PeerPicker {}
        RButton { outlined: true; text: "读取联系人安全身份 →"; onClicked: store.inspectIdentity() }
        IdentityCard { identity: (void store.revision, store.identities[store.selectedPeer] ?? null) }
        UploadForm {}
    } }
    Component { id: directory; Column {
        Repeater { model: (void store.revision, store.contacts)
            delegate: ContactRow { required property var modelData; name: modelData[0]; detail: modelData[1] ? "ONLINE / 在线" : "OFFLINE / 离线"
                RButton { outlined: true; text: "OPEN →"; onClicked: store.openPeer(modelData[0]) } } }
        Empty { visible: store.contacts.length === 0; text: "好友目录为空，可在「用户检索」中发送申请。" }
    } }
    Component { id: discovery; Column {
        spacing: 10
        Row { spacing: 10
            RField { id: query; width: panel.panelWidth - find.width - 10; caption: "DISCOVERY / 用户名检索"; text: store.searchQuery; onAccepted: store.search(text) }
            RButton { id: find; outlined: true; anchors.bottom: parent.bottom; text: "检索 →"; onClicked: store.search(query.text) } }
        Repeater { model: (void store.revision, store.strangers)
            delegate: ContactRow { required property var modelData; name: modelData[0]; detail: modelData[1] ? "在线" : "离线"
                RButton { outlined: true; text: "申请好友 ＋"; onClicked: store.addFriend(modelData[0]) }
                RButton { outlined: true; text: "临时会话 →"; onClicked: store.openPeer(modelData[0]) } } }
        Empty { visible: store.strangers.length === 0; text: "登录后输入用户名检索。" }
        RButton { outlined: true; enabled: store.nextSearch !== ""; text: "下一页 →"; onClicked: store.nextUsers() }
    } }
    Component { id: requests; Column {
        spacing: 6
        Label { text: "INCOMING / 收到的申请" }
        Repeater { model: (void store.revision, store.incoming)
            delegate: ContactRow { required property var modelData; name: modelData
                RButton { outlined: true; text: "同意"; onClicked: store.respondFriend(modelData, true) }
                RButton { outlined: true; text: "拒绝"; onClicked: store.respondFriend(modelData, false) } } }
        Empty { visible: store.incoming.length === 0; text: "没有待处理申请。" }
        Item { width: 1; height: 10 }
        Label { text: "OUTGOING / 已发出的申请" }
        Repeater { model: (void store.revision, store.outgoing)
            delegate: Text { required property var modelData; text: modelData + " · 等待确认"; color: Theme.text; font { family: uiFont; pixelSize: 12 } } }
    } }
    Component { id: groupConversation; Thread { group: true } }
    Component { id: createGroup; Column {
        spacing: 16
        RField { id: groupName; width: panel.panelWidth; caption: "NAME / 群组名称" }
        RField { id: groupMembers; width: panel.panelWidth; caption: "MEMBERS / 成员用户名（逗号分隔）" }
        RButton { solid: true; width: panel.panelWidth; text: "CREATE CHANNEL"; caption: "创建群组 →"; onClicked: store.createGroup(groupName.text, groupMembers.text) }
    } }
    Component { id: members; Column {
        spacing: 10
        GroupPicker {}
        Label { visible: !!store.group(); text: (store.group() ? store.group().name : "") + " / 成员" }
        Repeater { model: store.group() ? store.group().members : []
            delegate: ContactRow { required property var modelData; name: modelData
                RButton { outlined: true; text: "核对 / 私聊 →"; onClicked: store.openPeer(modelData) } } }
        Empty { visible: !store.group(); text: "请选择一个群组。" }
        Repeater { model: (void store.revision, store.reportRows)
            delegate: Text { required property var modelData; text: modelData[0] + "　" + modelData[1]; color: Theme.text; font { family: uiFont; pixelSize: 12 } } }
    } }
    Component { id: queue; FileList {} }
    Component { id: upload; UploadForm {} }
    Component { id: received; FileList { received: true } }
}
