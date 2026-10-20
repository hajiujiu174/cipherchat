import QtQuick

// Native state and commands driven by the shared Qt ChatBridge.
// Views bind to `revision` so any event re-evaluates the derived lists.
QtObject {
    id: store
    objectName: "chatStore"
    property int revision: 0
    readonly property var config: chat.config
    property real lastTyping: 0
    property string username: ""
    property int phase: 0
    property string authenticatedUsername: ""
    // A disconnected, previously authenticated session stays readable.
    readonly property bool sessionAvailable: phase === 6 || (username !== "" && authenticatedUsername === username)
    property string lastError: ""
    onPhaseChanged: if (phase === 6) { authenticatedUsername = username; Qt.callLater(() => openModule("chat")); }
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
    property var pinnedPeers: []
    property int draftRevision: 0
    property var readingPositions: ({})
    property var conversationErrors: ({})
    property var pendingAttachment: ({peer: "", path: ""})
    property var pendingSends: ({})
    signal draftRecovered(string key)
    property var bundles: []
    property var stages: []
    property var scope: ({})
    property bool tlsValidated: false
    property bool workspaceRestoring: false
    signal workspaceApplied()
    property Timer workspaceTimer: Timer { interval: 220; onTriggered: store.persistWorkspace() }
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
            if (phase === 6) authenticatedUsername = username;
            if (changed) { contacts = []; incoming = []; outgoing = []; identities = {}; drafts = {}; readingPositions = {}; conversationErrors = {}; pendingAttachment = {peer: "", path: ""}; pendingSends = {}; lastError = ""; selectedPeer = ""; selectedGroup = 0; }
            scope = event.scope || {}; bundles = event.bundles || []; stages = event.stages || [];
            for (const peer of event.peerIdentities || []) identities[peer.username] = peer;
            applyWorkspace(event.workspace || {});
            break;
        }
        case "account": phase = event.phase; break;
        case "message": {
            put(history, "messageId", event.value);
            if (event.value.from === username) {
                const key = event.value.groupId ? "group:" + event.value.groupId : "peer:" + event.value.to;
                const pending = pendingSends[key];
                if (pending && pending.text === event.value.text) {
                    if (event.value.state === 3) recoverPending(key, "消息发送失败");
                    else if (event.value.state === 1 || event.value.state === 2) delete pendingSends[key];
                }
            }
            break;
        }
        case "file": put(files, "fileId", event.value); break;
        case "deliveryBundle": put(bundles, "id", event.value); break;
        case "messageStage": {
            stages.push(event.value);
            const id = event.value.messageId;
            while (stages.filter(e => e.messageId === id).length > 32) stages.splice(stages.findIndex(e => e.messageId === id), 1);
            const ids = [...new Set(stages.map(e => e.messageId))];
            if (ids.length > 500) stages = stages.filter(e => e.messageId !== ids[0]);
            break;
        }
        case "connectionSecurity": tlsValidated = event.tlsValidated; break;
        case "workspace": if (!workspaceTimer.running) applyWorkspace(event.value); break;
        case "group": put(groups, "id", event.value); break;
        case "contacts": contacts = event.users; incoming = event.incoming; outgoing = event.outgoing; break;
        case "strangers": strangers = event.users; nextSearch = event.next; break;
        case "identity":
            identities[event.value.username] = event.value;
            if (!event.value.verified) notify("请从会话标题的「核对身份」查看 " + event.value.username + " 的指纹");
            break;
        case "friendResult": notify(event.ok ? "联系人请求已处理" : event.error); break;
        case "error":
            lastError = event.message;
            for (const key of Object.keys(pendingSends)) recoverPending(key, event.message);
            notify(event.message); break;
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
    function allMessages(group, all) {
        void revision;
        return history.filter(m => group ? m.groupId === selectedGroup && selectedGroup !== 0
            : !m.groupId && (all || m.from === selectedPeer || m.to === selectedPeer));
    }
    function messages(group, all, limit) { return allMessages(group, all).slice(-(limit || 100)); }
    function conversations(group, query) {
        void revision; void draftRevision;
        const rows = {};
        function add(id, name, online) {
            const key = String(id);
            if (!rows[key]) rows[key] = {id: id, name: name || key, online: !!online, stamp: 0, preview: "", draft: ""};
            return rows[key];
        }
        if (group) groups.forEach(g => add(g.id, g.name, false));
        else {
            contacts.forEach(c => add(c[0], c[0], c[1]));
            pinnedPeers.forEach(p => add(p));
            if (selectedPeer) add(selectedPeer);
            files.forEach(f => { if (f.peer) add(f.peer); });
        }
        history.forEach(m => {
            if (!!m.groupId !== group) return;
            const id = group ? m.groupId : m.from === username ? m.to : m.from;
            if (!id) return;
            const row = add(id);
            if ((m.timestamp || 0) >= row.stamp) {
                row.stamp = m.timestamp || 0;
                row.preview = (m.from === username ? "我：" : group ? m.from + "：" : "") + (m.text || "附件");
            }
        });
        Object.keys(drafts).forEach(key => {
            const prefix = group ? "group:" : "peer:";
            if (key.startsWith(prefix) && drafts[key] && key.slice(prefix.length) && key !== "group:0")
                add(group ? Number(key.slice(prefix.length)) : key.slice(prefix.length)).draft = drafts[key];
        });
        const q = String(query || "").trim().toLowerCase();
        return Object.values(rows).filter(r => !q || (r.name + " " + r.id).toLowerCase().includes(q))
            .sort((a, b) => b.stamp - a.stamp || a.name.localeCompare(b.name));
    }
    function chooseGroup(id) { selectedGroup = Number(id); workspaceTimer.restart(); revision++; }
    function filesFor(peer) { void revision; return files.filter(f => f.peer === peer); }
    function errorFor(key) { void revision; return conversationErrors[key] || ""; }
    function submitting(group) { void revision; return !!pendingSends[draftKey(group)]; }
    function recoverPending(key, message) {
        const pending = pendingSends[key]; if (!pending) return;
        drafts[key] = pending.text + (drafts[key] ? "\n" + drafts[key] : "");
        delete pendingSends[key]; draftRevision++; workspaceTimer.restart();
        setConversationError(key, message + "；消息已恢复到草稿");
        draftRecovered(key);
    }
    function setConversationError(key, message) {
        const errors = Object.assign({}, conversationErrors); errors[key] = message; conversationErrors = errors; revision++;
    }
    function draftKey(group) { return group ? "group:" + selectedGroup : "peer:" + selectedPeer; }
    function draft(group) { void revision; return drafts[draftKey(group)] ?? ""; }
    function setDraft(group, text) { drafts[draftKey(group)] = text; draftRevision++; workspaceTimer.restart(); }
    function applyWorkspace(value) {
        workspaceRestoring = true;
        drafts = Object.assign({}, value.drafts || {});
        selectedPeer = value.selectedPeer || ""; selectedGroup = value.selectedGroup || 0;
        pinnedPeers = value.pinnedPeers || [];
        workspaceRestoring = false;
        workspaceApplied();
    }
    function persistWorkspace() {
        if (!username || workspaceRestoring) return;
        run(() => send({command: "workspace", value: {selectedPeer: selectedPeer, selectedGroup: selectedGroup,
            pinnedPeers: pinnedPeers, drafts: drafts}}));
    }
    function bundle(id) { void revision; return bundles.find(b => b.id === id); }
    function bundleProgress(b) {
        if (!b) return 0;
        const total = b.items.reduce((n, i) => n + i.size, 0);
        return total ? b.items.reduce((n, i) => n + Math.min(i.authenticatedBytes, i.size), 0) / total
            : b.items.filter(i => i.complete).length / Math.max(1, b.items.length);
    }
    function receiptText(state) { return ["等待发送", "服务器已接收", "对方已签收", "发送失败"][state] ?? ""; }
    function time(stamp) { return stamp ? new Date(stamp).toLocaleString(Qt.locale("zh_CN"), "yyyy/M/d HH:mm:ss") : ""; }
    function bytes(size) { return size >= 1048576 ? (size / 1048576).toFixed(1) + " MiB" : (size / 1024).toFixed(1) + " KiB"; }
    function fileRows(received) { void revision; return files.filter(f => !received || !f.outgoing && f.complete); }
    function group() { void revision; return groups.find(g => g.id === selectedGroup) ?? null; }

    function authenticate(user, password, register) {
        run(() => {
            lastError = "";
            chat.setConfig({ username: user.trim() });
            send(Object.assign({}, chat.config, { command: "authenticate", password: password, registerNew: register }));
        });
    }
    function saveConnection(values) {
        if (!(values.port >= 1 && values.port <= 65535)) { notify("端口需在 1–65535 之间"); return; }
        chat.setConfig(values); notify("连接配置已保存，将在下次登录使用");
    }
    function choosePeer(peer) { selectedPeer = peer.trim(); typingPeer = ""; workspaceTimer.restart(); revision++; }
    function sendMessage(group, text) {
        const key = draftKey(group);
        let createdPending = false;
        try {
            if (!text.trim()) throw Error("请输入消息内容");
            if (!group && !selectedPeer || group && !selectedGroup) throw Error("请先选择接收人或群组");
            if (phase !== 6) throw Error("尚未登录，请从「账号」连接后再发送");
            if (Object.keys(pendingSends).length) throw Error("上一条消息正在提交，请稍候");
            pendingSends[key] = {text: text};
            createdPending = true;
            send(group ? { command: "sendGroup", groupId: selectedGroup, text: text } : { command: "sendText", peer: selectedPeer, text: text });
            delete drafts[key]; draftRevision++;
            setConversationError(key, "");
            workspaceTimer.restart();
            return true;
        } catch (error) { if (createdPending) delete pendingSends[key]; setConversationError(key, error.message); return false; }
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
        try {
            if (!peer.trim() || !path) throw Error("请选择文件和接收人");
            if (phase !== 6) throw Error("尚未登录，请连接后再发送文件");
            send({ command: "sendFile", peer: peer.trim(), path: path });
            setConversationError("peer:" + peer.trim(), ""); notify("文件已交给加密传输队列"); return true;
        } catch (error) { setConversationError("peer:" + peer.trim(), error.message); return false; }
    }
    function disconnect() { run(() => send({ command: "disconnect" })); }
    function openPeer(peer) { choosePeer(peer); openModule("chat"); }
    function inspectIdentity() { run(() => { if (!selectedPeer) throw Error("请先填写联系人用户名"); send({ command: "inspectIdentity", peer: selectedPeer }); }); }
    function confirmIdentity(peer) {
        run(() => {
            const id = identities[peer]; if (!id) return;
            send({ command: "confirmIdentity", peer: peer, signing: id.signing, dh: id.dh });
            notify("已提交身份复核，等待业务线程确认");
        });
    }
    function addFriend(peer) { run(() => send({ command: "addFriend", username: peer })); }
    function respondFriend(peer, accept) { run(() => send({ command: "respondFriend", username: peer, accept: accept })); }
    function resumeFile(id) { run(() => send({ command: "resumeFile", fileId: id })); }
    function cancelFile(id) { run(() => send({ command: "cancelFile", fileId: id })); }
}
