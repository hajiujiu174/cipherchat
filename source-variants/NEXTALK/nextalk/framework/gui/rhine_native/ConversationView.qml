import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import "."

Item {
    id: thread
    objectName: "conversationView"
    property var store
    property bool group: false
    property bool enterSends: true
    property bool compact: false
    signal listRequested()
    signal detailsRequested()
    readonly property string sessionKey: store.draftKey(group)
    readonly property string title: group ? (store.group() ? store.group().name : "选择群组") : store.selectedPeer || "选择会话"
    property string loadedKey: ""
    property int messageLimit: 100
    property int totalMessages: 0
    property bool following: true
    property bool adjusting: false
    property var pendingPosition: null
    property int refreshSerial: 0
    property int unseenMessages: 0
    property bool ready: false
    property bool transfersOpen: false
    readonly property var currentFiles: group ? [] : store.filesFor(store.selectedPeer)
    readonly property string errorText: store.errorFor(sessionKey)
    readonly property bool hasSelection: group ? store.selectedGroup !== 0 : store.selectedPeer !== ""
    function focusEditor() { editor.forceActiveFocus(); }

    function reading() {
        // The model can change before ListView has completed its queued layout.
        // Keep the same logical reading position until that layout is restored.
        if (adjusting && pendingPosition) return Object.assign({}, pendingPosition,
            {limit: messageLimit, count: totalMessages, unseen: unseenMessages});
        let index = timeline.indexAt(8, timeline.contentY + 2);
        if (index < 0 && rows.count) index = 0;
        const item = index >= 0 ? timeline.itemAtIndex(index) : null;
        return {limit: messageLimit, following: following, count: totalMessages, unseen: unseenMessages,
            anchor: index >= 0 && index < rows.count ? rows.get(index).messageId : "",
            offset: item ? timeline.contentY - item.y : 0};
    }
    function saveReading() {
        if (loadedKey && ready) store.readingPositions[loadedKey] = adjusting && pendingPosition ? pendingPosition : reading();
    }
    function place(state) {
        timeline.forceLayout();
        if (state.following) timeline.positionViewAtEnd();
        else {
            let index = -1;
            for (let i = 0; i < rows.count; i++) if (rows.get(i).messageId === state.anchor) { index = i; break; }
            if (index >= 0) {
                timeline.positionViewAtIndex(index, ListView.Beginning);
                timeline.forceLayout();
                const item = timeline.itemAtIndex(index);
                if (item) timeline.contentY = Math.max(timeline.originY, Math.min(item.y + (state.offset || 0), timeline.originY + Math.max(0, timeline.contentHeight - timeline.height)));
            }
        }
        following = state.following;
        if (following) unseenMessages = 0;
        adjusting = false;
        pendingPosition = null;
        saveReading();
    }
    function refresh(state) {
        if (!ready) return;
        const saved = state || (adjusting && pendingPosition ? pendingPosition : reading());
        const serial = ++refreshSerial, key = loadedKey;
        pendingPosition = saved;
        adjusting = true;
        const all = store.allMessages(group, false);
        if (!saved.following && all.length > totalMessages) {
            const growth = all.length - totalMessages;
            unseenMessages += growth;
            messageLimit += growth;
        }
        totalMessages = all.length;
        const visible = all.slice(-messageLimit);
        // Keep delegates alive for receipts and appends; rebuild only when the visible window changes.
        let sameStart = rows.count === 0 || visible.length && rows.get(0).messageId === visible[0].messageId;
        if (!sameStart || rows.count > visible.length) rows.clear();
        for (let i = 0; i < visible.length; i++) {
            const value = {messageId: visible[i].messageId, entryData: visible[i]};
            if (i >= rows.count) rows.append(value);
            else if (rows.get(i).messageId !== value.messageId || JSON.stringify(rows.get(i).entryData) !== JSON.stringify(value.entryData)) rows.set(i, value);
        }
        Qt.callLater(() => { if (thread.ready && serial === thread.refreshSerial && key === thread.loadedKey) thread.place(saved); });
    }
    function restoreSession() {
        if (!ready) return;
        saveReading();
        loadedKey = sessionKey;
        pendingPosition = null; adjusting = false;
        const saved = store.readingPositions[loadedKey] || {limit: 100, following: true, anchor: "", offset: 0};
        totalMessages = saved.count || store.allMessages(group, false).length; unseenMessages = saved.unseen || 0;
        messageLimit = saved.limit || 100;
        rows.clear();
        editor.restore();
        transfersOpen = false;
        refresh(saved);
    }
    function loadEarlier() {
        const saved = reading();
        messageLimit += 100;
        saved.limit = messageLimit; saved.following = false;
        refresh(saved);
    }
    function chooseAttachment() {
        if (!store.selectedPeer) { store.setConversationError(sessionKey, "请先选择文件接收人"); return; }
        picker.recipient = store.selectedPeer; picker.open();
    }
    function sendAttachment() {
        const pending = store.pendingAttachment;
        if (store.sendFile(pending.peer, pending.path)) store.pendingAttachment = {peer: "", path: ""};
    }
    onSessionKeyChanged: restoreSession()
    Component.onCompleted: { ready = true; restoreSession(); }
    Component.onDestruction: { saveReading(); ready = false; }
    Connections { target: store
        function onRevisionChanged() { thread.refresh(); }
        function onWorkspaceApplied() { editor.restore(); thread.refresh(); }
        function onDraftRecovered(key) { if (key === thread.sessionKey) editor.restore(); }
    }
    ListModel { id: rows }

    Column {
        id: heading
        anchors { left: parent.left; right: parent.right; top: parent.top }
        spacing: 8
        Row {
            width: parent.width; spacing: 10
            RButton { objectName: "openSessionDrawer"; visible: thread.compact; outlined: true; text: "会话"; onClicked: thread.listRequested() }
            Text { width: parent.width - (thread.compact ? 80 : 0); text: thread.title; elide: Text.ElideRight; color: Theme.ink; font { family: boldFont; pixelSize: 20 } }
        }
        Flow {
            width: parent.width; spacing: 8
            RButton { visible: thread.hasSelection; outlined: true; text: thread.group ? "成员与签收" : "核对身份"; onClicked: { if (thread.group) thread.detailsRequested(); else { store.inspectIdentity(); chat.feature("identity", store.selectedPeer); } } }
            RButton { visible: !thread.group && thread.hasSelection; outlined: true; text: "交付与版本"; onClicked: chat.feature("deliveries", store.selectedPeer) }
            RButton { objectName: "conversationTransfers"; visible: thread.currentFiles.length > 0; outlined: true; text: "传输 " + thread.currentFiles.length + (thread.transfersOpen ? " ▴" : " ▾"); onClicked: thread.transfersOpen = !thread.transfersOpen }
        }
        Text { visible: store.phase !== 6 || thread.errorText !== ""; width: parent.width; text: thread.errorText || "尚未连接，请从顶部「账号」登录。草稿会保留。"; wrapMode: Text.Wrap; color: Theme.accent; font { family: uiFont; pixelSize: 12 } }
        FileQueue { store: thread.store; peer: store.selectedPeer; currentOnly: true; width: parent.width; height: visible ? Math.min(170, thread.height * 0.25) : 0; visible: thread.transfersOpen && thread.currentFiles.length > 0 }
        Rectangle { width: parent.width; height: 1; color: Theme.line }
    }
    Item {
        id: earlierBar
        anchors { left: parent.left; right: parent.right; top: heading.bottom; topMargin: 6 }
        height: thread.totalMessages > 100 ? 36 : 0
        RButton { objectName: "loadEarlier"; visible: thread.totalMessages > rows.count; anchors.centerIn: parent; text: "加载更早消息 · 还有 " + (thread.totalMessages - rows.count) + " 条"; outlined: true; onClicked: thread.loadEarlier() }
        Text { visible: parent.height > 0 && thread.totalMessages <= rows.count; anchors.centerIn: parent; text: "已显示本会话的全部 " + thread.totalMessages + " 条记录"; color: Theme.faint; font { family: uiFont; pixelSize: 11 } }
    }
    ListView {
        id: timeline
        objectName: "messageTimeline"
        anchors { top: earlierBar.bottom; topMargin: 6; bottom: composer.top; bottomMargin: 10; left: parent.left; right: parent.right }
        model: rows; clip: true; spacing: 0; boundsBehavior: Flickable.StopAtBounds
        reuseItems: false
        Controls.ScrollBar.vertical: Controls.ScrollBar {}
        onContentYChanged: if (thread.ready && !thread.adjusting) {
            thread.following = contentY + height >= originY + contentHeight - 35;
            thread.saveReading();
        }
        onHeightChanged: if (thread.ready) { const state = thread.reading(); Qt.callLater(() => thread.place(state)); }
        delegate: Item {
            id: message
            required property var entryData
            required property string messageId
            objectName: "messageRow"
            readonly property bool own: entryData.from === store.username
            width: timeline.width - 14; height: messageBody.implicitHeight + 26
            Rectangle { x: 0; y: 8; width: 2; height: parent.height - 16; color: Theme.accent; visible: message.own }
            Column {
                id: messageBody; x: 12; y: 12; width: parent.width - 20; spacing: 6
                Row {
                    width: parent.width; spacing: 8
                    Text { width: Math.max(60, parent.width - stamp.width - 8); elide: Text.ElideRight; text: message.entryData.from; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
                    Text { id: stamp; text: store.time(message.entryData.timestamp); color: Theme.faint; font { family: uiFont; pixelSize: 10 } }
                }
                TextEdit { objectName: "selectableMessage"; width: parent.width; height: contentHeight; text: message.entryData.text || ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; activeFocusOnTab: false; wrapMode: TextEdit.WrapAnywhere; color: Theme.ink; selectionColor: Theme.selection; font { family: uiFont; pixelSize: 15 } }
                RButton { text: store.receiptText(message.entryData.state) + " · 查看消息详情 ↗"; pixelSize: 11; ink: Theme.muted; onClicked: chat.feature("messageDetails", message.messageId) }
                Column {
                    width: parent.width; spacing: 6
                    readonly property var ext: message.entryData.extensions ? message.entryData.extensions.delivery : null
                    readonly property var bundle: ext && ext.action === "manifest" ? store.bundle(ext.manifest.id) : null
                    visible: !!bundle
                    Text { width: parent.width; wrapMode: Text.Wrap; text: parent.bundle ? "文件交付 · " + parent.bundle.title + " · " + parent.bundle.items.length + " 个文件" : ""; color: Theme.ink; font { family: uiFont; pixelSize: 13 } }
                    Text { width: parent.width; text: !parent.bundle ? "" : parent.bundle.manuallyConfirmed ? "已人工确认收到" : parent.bundle.state === 2 ? "文件已验证 · 等待人工确认" : "交付正在传输"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
                    RButton { outlined: true; text: "交付详情与版本"; onClicked: chat.feature("deliveries", message.entryData.from === store.username ? message.entryData.to : message.entryData.from) }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
        }
        Text { visible: rows.count === 0; anchors.centerIn: parent; width: parent.width - 24; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; text: thread.hasSelection ? "尚无消息，开始这段会话。" : "从会话列表选择联系人或群组"; color: Theme.muted; font { family: uiFont; pixelSize: 14 } }
    }
    RButton { objectName: "jumpToLatest"; anchors { right: timeline.right; bottom: timeline.bottom; bottomMargin: 6 } visible: !thread.following && rows.count > 0; solid: true; width: 156; text: thread.unseenMessages > 0 ? "新消息 " + thread.unseenMessages + " ↓" : "回到最新消息 ↓"; onClicked: { thread.following = true; thread.place({following: true}); } }
    Column {
        id: composer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        spacing: 8
        Rectangle { width: parent.width; height: 1; color: Theme.line }
        Column {
            visible: !thread.group && store.pendingAttachment.path !== ""
            width: parent.width; spacing: 6
            Text { width: parent.width; text: "待发送给 " + store.pendingAttachment.peer + " · " + store.pendingAttachment.path; elide: Text.ElideMiddle; color: Theme.accent; font { family: uiFont; pixelSize: 12 } }
            Row {
                spacing: 8
                RButton { objectName: "sendPendingAttachment"; outlined: true; text: "发送此文件"; onClicked: thread.sendAttachment() }
                RButton { outlined: true; text: "移除"; onClicked: store.pendingAttachment = {peer: "", path: ""} }
            }
        }
        Flickable {
            id: inputScroll
            width: parent.width; height: Math.min(126, Math.max(58, editor.contentHeight + 16))
            clip: true; contentWidth: width; contentHeight: editor.height
            boundsBehavior: Flickable.StopAtBounds
            TextEdit {
                id: editor
                objectName: "messageEditor"
                width: inputScroll.width; height: Math.max(inputScroll.height, contentHeight + 16); topPadding: 8; bottomPadding: 8
                wrapMode: TextEdit.Wrap; selectByMouse: true; color: Theme.ink; selectionColor: Theme.selection
                textFormat: TextEdit.PlainText; font { family: uiFont; pixelSize: 15 }
                Accessible.name: "消息正文"
                property bool restoring: false
                function restore() { restoring = true; text = store.draft(thread.group); cursorPosition = text.length; restoring = false; }
                function send() {
                    if (inputMethodComposing) return;
                    if (store.sendMessage(thread.group, text)) {
                        restoring = true; text = ""; restoring = false;
                        thread.following = true; thread.refresh({following: true});
                    }
                    forceActiveFocus();
                }
                onTextChanged: if (!restoring) { store.setDraft(thread.group, text); if (!thread.group && text) store.typing(); }
                onCursorRectangleChanged: {
                    if (cursorRectangle.y < inputScroll.contentY) inputScroll.contentY = cursorRectangle.y;
                    else if (cursorRectangle.y + cursorRectangle.height > inputScroll.contentY + inputScroll.height) inputScroll.contentY = cursorRectangle.y + cursorRectangle.height - inputScroll.height;
                }
                Keys.onPressed: event => {
                    if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter) return;
                    if (inputMethodComposing) { event.accepted = true; return; }
                    if ((event.modifiers & Qt.ControlModifier) || (thread.enterSends && !(event.modifiers & Qt.ShiftModifier))) { send(); event.accepted = true; }
                    else event.accepted = false;
                }
                Text { visible: !editor.text && !editor.activeFocus; x: 0; y: 8; text: "输入消息…"; color: Theme.faint; font: editor.font }
            }
            Controls.ScrollBar.vertical: Controls.ScrollBar {}
        }
        Row {
            width: parent.width; spacing: 8
            RButton { objectName: "chooseAttachment"; visible: !thread.group; outlined: true; text: "＋ 附件"; onClicked: thread.chooseAttachment() }
            Text { anchors.verticalCenter: parent.verticalCenter; width: parent.width - sendButton.width - (thread.group ? 8 : 94); elide: Text.ElideRight; text: !thread.group && store.typingPeer ? "对方正在输入…" : thread.enterSends ? "Enter 发送 · Shift+Enter 换行" : "Ctrl+Enter 发送 · Enter 换行"; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
            RButton { id: sendButton; objectName: "sendMessage"; enabled: !store.submitting(thread.group); solid: true; width: 100; text: store.submitting(thread.group) ? "提交中…" : "发送 ↗"; onClicked: editor.send() }
        }
    }
    FileDialog { id: picker; objectName: "attachmentFileDialog"; property string recipient: ""; title: "选择发送给 " + recipient + " 的文件"; onAccepted: store.pendingAttachment = {peer: recipient, path: chat.localPath(selectedFile)} }
}
