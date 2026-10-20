import QtQuick

// State and commands from frontends/rhine/src/chat.ts, driven by ChatBridge.
// Views bind to `revision` so any event re-evaluates the derived lists.
QtObject {
    id: store
    property int revision: 0
    readonly property var config: chat.config
    property real lastTyping: 0
    property string username: ""
    property int phase: 0
    onPhaseChanged: if (phase === 6) Qt.callLater(() => openModule("chat"))
    property var identity: null
    property var history: []
    property var files: []
    property var groups: []
    property var contacts: []
    property var incoming: []
    property var outgoing: []
    property var identities: ({})
    property var strangers: []
    property string nextSearch: ""
    property string searchQuery: ""
    property string selectedPeer: ""
    property int selectedGroup: 0
    property string report: ""
    property var reportRows: []
    property string typingPeer: ""
    property var drafts: ({})
    signal notify(string text)
    signal openModule(string module)
    readonly property var phases: ["未连接", "TLS 已连接", "协商协议", "服务就绪", "注册中", "验证登录", "已登录"]
    readonly property string status: phases[phase] ?? "未知状态"

    function put(list, key, value) {
        const index = list.findIndex(item => item[key] === value[key]);
        if (index < 0) list.push(value); else list[index] = value;
    }
    function receive(event) {
        switch (event.type) {
        case "restored": {
            const changed = username !== event.username;
            username = event.username; identity = event.identity; history = event.history; files = event.files; groups = event.groups;
            if (changed) { contacts = []; incoming = []; outgoing = []; identities = {}; drafts = {}; selectedPeer = ""; selectedGroup = 0; }
            break;
        }
        case "account": phase = event.phase; break;
        case "message": put(history, "messageId", event.value); break;
        case "file": put(files, "fileId", event.value); break;
        case "group": put(groups, "id", event.value); break;
        case "contacts": contacts = event.users; incoming = event.incoming; outgoing = event.outgoing; break;
        case "strangers": strangers = event.users; nextSearch = event.next; break;
        case "identity":
            identities[event.value.username] = event.value;
            if (!event.value.verified) notify("请在会话「附件与身份」核对 " + event.value.username + " 的指纹");
            break;
        case "friendResult": notify(event.ok ? "联系人请求已处理" : event.error); break;
        case "error": notify(event.message); break;
        case "groupReport": reportRows = event.value.members.map(m => [m.recipient, m.error || "已提交发送"]); break;
        case "presence": { const c = contacts.find(c => c[0] === event.username); if (c) c[1] = event.online; break; }
        case "typing": if (event.username === selectedPeer) typingPeer = event.active ? event.username : ""; break;
        }
    }
    property Connections bridge: Connections {
        target: chat
        function onEvents(list) { for (const e of list) store.receive(e); store.revision++; }
    }

    function send(request) { const error = chat.command(request); if (error) throw Error(error); }
    function run(action) { try { action(); } catch (error) { notify(error.message); } }
    function messages(group, all) {
        void revision;
        return history.filter(m => group ? m.groupId === selectedGroup && selectedGroup !== 0
            : !m.groupId && (all || m.from === selectedPeer || m.to === selectedPeer)).slice(-100);
    }
    function draftKey(group) { return group ? "group:" + selectedGroup : "peer:" + selectedPeer; }
    function draft(group) { void revision; return drafts[draftKey(group)] ?? ""; }
    function setDraft(group, text) { drafts[draftKey(group)] = text; }
    function receiptText(state) { return ["等待发送", "服务器已接收", "对方已签收", "发送失败"][state] ?? ""; }
    function time(stamp) { return stamp ? new Date(stamp).toLocaleString(Qt.locale("zh_CN"), "yyyy/M/d HH:mm:ss") : ""; }
    function bytes(size) { return size >= 1048576 ? (size / 1048576).toFixed(1) + " MiB" : (size / 1024).toFixed(1) + " KiB"; }
    function fileRows(received) { void revision; return files.filter(f => !received || !f.outgoing && f.complete); }
    function group() { void revision; return groups.find(g => g.id === selectedGroup) ?? null; }

    function authenticate(user, password, register) {
        run(() => {
            chat.setConfig({ username: user.trim() });
            send(Object.assign({}, chat.config, { command: "authenticate", password: password, registerNew: register }));
        });
    }
    function saveConnection(values) {
        if (!(values.port >= 1 && values.port <= 65535)) { notify("端口需在 1–65535 之间"); return; }
        chat.setConfig(values); notify("连接配置已保存，将在下次登录使用");
    }
    function choosePeer(peer) { selectedPeer = peer.trim(); typingPeer = ""; revision++; }
    function sendMessage(group, text) {
        run(() => {
            text = text.trim(); if (!text) return;
            if (!group && !selectedPeer || group && !selectedGroup) throw Error("请先选择接收人或群组");
            send(group ? { command: "sendGroup", groupId: selectedGroup, text: text } : { command: "sendText", peer: selectedPeer, text: text });
            delete drafts[draftKey(group)]; revision++;
        });
    }
    // Typing hints are throttled to one per second, like the other clients.
    function typing() {
        const now = Date.now();
        if (!selectedPeer || phase !== 6 || now - lastTyping < 1000) return;
        lastTyping = now;
        chat.command({ command: "typing", peer: selectedPeer });
    }
    function search(query) { run(() => { searchQuery = query.trim(); send({ command: "searchUsers", query: searchQuery }); }); }
    function nextUsers() { run(() => send({ command: "searchUsers", query: searchQuery, after: nextSearch })); }
    function createGroup(name, members) {
        run(() => { send({ command: "createGroup", name: name.trim(), members: members.split(/[,，\s]+/).filter(Boolean) }); notify("已提交群组创建请求"); });
    }
    function sendFile(peer, path) {
        run(() => { if (!peer.trim() || !path) throw Error("请选择文件并填写接收人"); send({ command: "sendFile", peer: peer.trim(), path: path }); notify("文件已交给加密传输队列"); });
    }
    function disconnect() { run(() => send({ command: "disconnect" })); }
    function openPeer(peer) { selectedPeer = peer; revision++; openModule("chat"); }
    function inspectIdentity() { run(() => { if (!selectedPeer) throw Error("请先填写联系人用户名"); send({ command: "inspectIdentity", peer: selectedPeer }); }); }
    function confirmIdentity(peer) {
        run(() => {
            const id = identities[peer]; if (!id) return;
            send({ command: "confirmIdentity", peer: peer, signing: id.signing, dh: id.dh });
            id.verified = true; revision++; notify("已确认身份，待发消息继续发送");
        });
    }
    function addFriend(peer) { run(() => send({ command: "addFriend", username: peer })); }
    function respondFriend(peer, accept) { run(() => send({ command: "respondFriend", username: peer, accept: accept })); }
    function resumeFile(id) { run(() => send({ command: "resumeFile", fileId: id })); }
    function cancelFile(id) { run(() => send({ command: "cancelFile", fileId: id })); }
}
