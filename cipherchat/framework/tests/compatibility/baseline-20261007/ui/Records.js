// Same 15 entries as frontends/rhine/src/data.ts: the upstream five-lane
// archive navigation, with each slot opening a chat module and tab.
.pragma library

var columns = ["联系人", "私密会话", "账号终端", "群聊频道", "文件传输"];
var categories = ["全部档案"].concat(columns);
var entries = [
    ["account", 2, "身份接入", "IDENTITY ACCESS", "overview"],
    ["account", 2, "连接配置", "CONNECTION PROFILE", "notes"],
    ["account", 2, "安全身份", "IDENTITY VERIFICATION", "history"],
    ["chat", 1, "私密会话", "PRIVATE CHANNEL", "overview"],
    ["chat", 1, "消息档案", "MESSAGE ARCHIVE", "notes"],
    ["chat", 1, "会话附件", "CHANNEL ATTACHMENTS", "history"],
    ["contacts", 0, "通讯目录", "CONTACT DIRECTORY", "overview"],
    ["contacts", 0, "用户检索", "CONTACT DISCOVERY", "notes"],
    ["contacts", 0, "好友申请", "CONTACT REQUESTS", "history"],
    ["groups", 3, "群聊频道", "GROUP CHANNELS", "overview"],
    ["groups", 3, "建立群组", "CREATE CHANNEL", "notes"],
    ["groups", 3, "成员与签收", "MEMBERS & RECEIPTS", "history"],
    ["files", 4, "传输队列", "TRANSFER QUEUE", "overview"],
    ["files", 4, "发送文件", "OUTGOING TRANSFER", "notes"],
    ["files", 4, "接收档案", "RECEIVED FILES", "history"]
];
var records = entries.map(function (e, i) {
    return { id: "X-" + String(i + 1).padStart(3, "0"), module: e[0], lane: e[1], title: e[2], en: e[3], tab: e[4],
             category: columns[e[1]], clearance: "PRIVATE COMMUNICATION", department: "CIPHERCHAT" };
});
var tabs = ["overview", "notes", "history"];
function tabLabels(module) {
    return { account: ["身份接入", "连接配置", "安全身份"], chat: ["即时通讯", "消息档案", "附件与身份"],
             contacts: ["通讯目录", "用户检索", "好友申请"], groups: ["群聊频道", "创建群聊", "成员与签收"],
             files: ["传输队列", "发送文件", "接收档案"] }[module];
}
function columnFiles(lane) {
    var list = [];
    for (var i = 0; i < records.length; ++i) if (records[i].lane === lane) list.push(i);
    return list;
}
function moduleIndex(module) {
    for (var i = 0; i < records.length; ++i) if (records[i].module === module && records[i].tab === "overview") return i;
    return 0;
}
