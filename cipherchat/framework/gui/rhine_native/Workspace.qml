import QtQuick
import QtQuick.Effects
import "."
import "Records.js" as Data

Item {
    id: work
    property var store
    property var prefs
    property var terminal
    property Item sceneItem
    property int index: 0
    property string tab: "overview"
    property real clarity: 0
    property bool active: false
    readonly property bool locked: !store.sessionAvailable
    readonly property string module: Data.records[index].module
    readonly property bool conversation: !locked && (module === "chat" || module === "groups") && tab === "overview"
    readonly property bool wide: terminal.width >= 1280
    property bool drawerOpen: false
    property bool newPeerOpen: false
    property real revealProgress: 1
    signal tabRequested(string target)
    function focusEditor() { if (content.item && typeof content.item.focusEditor === "function") content.item.focusEditor(); }
    function closeTransient() {
        if (newPeerOpen) { newPeerOpen = false; return true; }
        if (drawerOpen) { drawerOpen = false; return true; }
        return false;
    }
    function searchSessions() {
        if (!conversation) { terminal.navigateModule("chat"); return; }
        if (wide) sessions.focusSearch();
        else { drawerOpen = true; Qt.callLater(() => drawerSessions.focusSearch()); }
    }
    function createConversation() {
        if (module === "groups") { drawerOpen = false; tabRequested("notes"); }
        else { newPeerOpen = true; Qt.callLater(() => newPeer.input.forceActiveFocus()); }
    }
    function resetReveal() {
        reveal.stop();
        revealProgress = prefs.on("documentReveal") && prefs.on("modelDecryption") ? 0 : 1;
        if (active && clarity > 0) reveal.restart();
    }
    onIndexChanged: { drawerOpen = false; newPeerOpen = false; resetReveal(); }
    onActiveChanged: if (active) resetReveal()
    onClarityChanged: if (active && clarity > 0 && revealProgress === 0 && !reveal.running) reveal.restart()
    onTabChanged: { drawerOpen = false; if (prefs.on("surfaceTransitions")) tabIn.restart(); }
    onLockedChanged: { drawerOpen = false; newPeerOpen = false; }
    NumberAnimation { id: reveal; target: work; property: "revealProgress"; to: 1; duration: 950 }
    clip: true
    ShaderEffectSource {
        id: capture; sourceItem: work.sceneItem
        sourceRect: Qt.rect(work.x, work.y, work.width, work.height)
        textureSize: Qt.size(Math.ceil(work.width * 0.7), Math.ceil(work.height * 0.7))
        live: work.visible; visible: false
    }
    MultiEffect { anchors.fill: parent; source: capture; blurEnabled: true; blur: 1; blurMax: 48; autoPaddingEnabled: false }
    Rectangle { anchors.fill: parent; radius: 14; color: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.83); border.color: Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.18) }
    MouseArea { anchors.fill: parent; onWheel: event => event.accepted = true }
    Item {
        id: title; x: 22; y: 18; width: parent.width - 44; height: 54
        Row {
            id: titleText; spacing: 10
            Text { text: work.locked ? "登录 CipherChat" : Data.records[work.index].title; color: Theme.ink; font { family: boldFont; pixelSize: 24 } }
            Rolling { visible: !work.locked; anchors.verticalCenter: parent.verticalCenter; text: Data.records[work.index].id; numeric: true; animated: work.prefs.on("rollingNumbers"); color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
        }
        Rolling { y: 34; text: work.locked ? "IDENTITY ACCESS" : Data.records[work.index].en; animated: work.prefs.on("rollingText"); color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
        Item {
            width: titleText.width + 2; height: titleText.height; clip: true; visible: work.revealProgress < 1
            readonly property real t: work.revealProgress
            readonly property real eased: t < 0.2 ? 0.4 * Math.pow(t / 0.2, 2) : 1 - 0.6 * Math.pow((1 - t) / 0.8, 16 / 3)
            Rectangle { width: parent.width; height: parent.height; x: parent.width * 1.01 * parent.eased; color: Theme.ink }
        }
        Row {
            visible: !work.locked
            anchors { right: parent.right; top: parent.top } spacing: 8
            RButton { outlined: true; text: terminal.pinned.includes(Data.records[work.index].id) ? "已收藏" : "收藏入口"; onClicked: terminal.togglePin() }
        }
        Text { visible: !work.locked; anchors { right: parent.right; bottom: parent.bottom } width: Math.min(parent.width * 0.4, 240); horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft; text: store.username ? store.username + " · " + store.status : store.status; color: Theme.accent; font { family: uiFont; pixelSize: 12 } }
    }
    Flow {
        id: tabs; x: 22; y: title.y + title.height + 14; width: parent.width - 44; spacing: 8
        visible: !work.locked
        Repeater {
            model: Data.tabLabels(work.module)
            delegate: RButton { required property int index; required property string modelData; objectName: "workspaceTab-" + Data.tabs[index]; outlined: true; text: modelData; active: work.tab === Data.tabs[index]; onClicked: work.tabRequested(Data.tabs[index]) }
        }
    }
    Rectangle {
        visible: !work.locked
        width: tabs.children[Data.tabs.indexOf(work.tab)] ? tabs.children[Data.tabs.indexOf(work.tab)].width : 0
        height: 2; y: tabs.y + tabs.height - 2; color: Theme.accent
        x: tabs.x + (tabs.children[Data.tabs.indexOf(work.tab)] ? tabs.children[Data.tabs.indexOf(work.tab)].x : 0)
        Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
    }
    Item {
        id: body; objectName: "glassChatPanel"
        readonly property string module: work.locked ? "account" : work.module
        x: 22; y: work.locked ? title.y + title.height + 12 : tabs.y + tabs.height + 14; width: parent.width - 44; height: parent.height - y - 20
        NumberAnimation { id: tabIn; target: body; property: "opacity"; from: 0.35; to: 1; duration: 150; easing.type: Easing.OutCubic }
        SessionList {
            id: sessions; store: work.store; group: work.module === "groups"
            visible: work.conversation && work.wide; width: 208; height: parent.height
            onCreateRequested: work.createConversation()
        }
        Rectangle { visible: sessions.visible; x: sessions.width + 10; width: 1; height: parent.height; color: Theme.line }
        Loader {
            id: content
            x: sessions.visible ? 230 : 0; width: parent.width - x; height: parent.height
            sourceComponent: work.conversation ? thread : !work.locked && work.module === "files" && work.tab === "overview" ? files : form
            Component { id: thread; ConversationView { store: work.store; group: work.module === "groups"; enterSends: work.prefs.enterSends; compact: !work.wide; onListRequested: work.drawerOpen = true; onDetailsRequested: work.tabRequested("history") } }
            Component { id: files; FileQueue { store: work.store } }
            Component { id: form; ChatPanel { store: work.store; module: work.locked ? "account" : work.module; tab: work.locked ? "overview" : work.tab } }
        }
    }
    Item {
        anchors.fill: parent; visible: work.drawerOpen && !work.wide; z: 20
        Rectangle { anchors.fill: parent; color: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.5); MouseArea { anchors.fill: parent; onClicked: work.drawerOpen = false; onWheel: e => e.accepted = true } }
        Rectangle {
            width: Math.min(310, parent.width - 40); height: parent.height; color: Theme.panel; border.color: Theme.line
            MouseArea { anchors.fill: parent; onWheel: e => e.accepted = true }
            RButton { anchors { top: parent.top; right: parent.right; margins: 16 } text: "关闭 ×"; onClicked: work.drawerOpen = false }
            SessionList { id: drawerSessions; anchors { fill: parent; margins: 20; topMargin: 58 } store: work.store; group: work.module === "groups"; onChosen: work.drawerOpen = false; onCreateRequested: work.createConversation() }
        }
    }
    Item {
        anchors.fill: parent; visible: work.newPeerOpen; z: 30
        Rectangle { anchors.fill: parent; color: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.65); MouseArea { anchors.fill: parent; onClicked: work.newPeerOpen = false; onWheel: e => e.accepted = true } }
        Rectangle {
            anchors.centerIn: parent; width: Math.min(420, parent.width - 40); height: 206; color: Theme.panel; border.color: Theme.line
            MouseArea { anchors.fill: parent }
            Column {
                anchors { fill: parent; margins: 20 } spacing: 18
                Text { text: "新建私聊"; color: Theme.ink; font { family: boldFont; pixelSize: 20 } }
                RField { id: newPeer; objectName: "newPeerName"; width: parent.width; caption: "联系人用户名"; onAccepted: openPeer.clicked() }
                Row {
                    spacing: 10
                    RButton { id: openPeer; objectName: "openNewPeer"; solid: true; width: 160; text: "打开会话"; onClicked: { if (!newPeer.text.trim()) return; store.choosePeer(newPeer.text); work.newPeerOpen = false; work.drawerOpen = false; work.tabRequested("overview"); } }
                    RButton { outlined: true; text: "取消"; onClicked: work.newPeerOpen = false }
                }
            }
        }
    }
}
