import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Effects
import "."
import "Records.js" as Data

Item {
    id: modal
    property string kind: ""
    property string shownKind: ""
    property var pinned: []
    property var prefs
    property var store
    property Item backdropSource
    property string filter: "全部档案"
    property string query: ""
    signal chosen(int index)
    signal closed()
    signal replay()
    signal notify(string text)
    function openCodex() {
        if (!chat.hotSwitch && store) {
            if (Object.values(store.drafts).some(text => String(text).length > 0) || Object.keys(store.pendingSends).length) { notify("请先处理草稿或等待消息提交完成，再切换界面。"); return; }
            if (store.files.some(file => !file.complete)) { notify("请等待文件传输完成，再切换界面。"); return; }
        }
        const error = chat.switchFrontend("codex");
        if (error) notify(error);
    }
    function results() {
        const q = query.trim().toLowerCase();
        return Data.records.map((r, i) => i).filter(i => {
            const r = Data.records[i];
            return (shownKind !== "saved" || pinned.includes(r.id)) && (filter === "全部档案" || r.category === filter)
                && (!q || [r.id, r.title, r.en, r.category].some(v => v.toLowerCase().includes(q)));
        });
    }
    anchors.fill: parent
    visible: opacity > 0
    enabled: opacity > 0
    opacity: kind !== "" ? 1 : 0
    Behavior on opacity {
        enabled: modal.prefs ? modal.prefs.on("surfaceTransitions") : true
        NumberAnimation { duration: modal.kind !== "" ? 300 : 200; easing.type: Easing.BezierSpline; easing.bezierCurve: modal.kind !== "" ? [0.22, 1, 0.36, 1, 1, 1] : [0.4, 0, 1, 1, 1, 1] }
    }
    onKindChanged: if (kind) {
        shownKind = kind; filter = "全部档案"; search.text = ""; query = "";
        Qt.callLater(() => { if (kind === "settings") closeButton.forceActiveFocus(); else search.input.forceActiveFocus(); });
    }
    ShaderEffectSource { id: capture; sourceItem: modal.backdropSource; live: modal.visible; visible: false; textureSize: Qt.size(Math.min(1280, modal.width), Math.min(800, modal.height)) }
    MultiEffect { anchors.fill: parent; source: capture; visible: !!modal.backdropSource; blurEnabled: true; blur: 1; blurMax: 48; autoPaddingEnabled: false }
    Rectangle { anchors.fill: parent; color: Theme.paper; opacity: 0.55 }
    MouseArea { anchors.fill: parent; onClicked: modal.closed(); onWheel: e => e.accepted = true }
    Rectangle {
        id: sheet
        enabled: modal.kind !== ""
        anchors.centerIn: parent
        width: Math.min(1056, parent.width - 48); height: Math.min(940, parent.height - 48)
        transform: Translate { y: (1 - modal.opacity) * (modal.kind !== "" ? 12 : 8) }
        color: Theme.panel; border.color: Theme.line
        MouseArea { anchors.fill: parent; onWheel: e => e.accepted = true }
        Row {
            x: 24; y: 22; width: parent.width - 48; spacing: 14
            Text { width: parent.width - closeButton.width - 14; text: modal.shownKind === "settings" ? "终端设置" : modal.shownKind === "saved" ? "收藏入口" : "通讯模块检索"; color: Theme.ink; font { family: boldFont; pixelSize: 26 } }
            RButton { id: closeButton; objectName: "closeTerminalModal"; outlined: true; text: "关闭 ×"; onClicked: modal.closed() }
        }
        Rectangle { x: 24; y: 74; width: parent.width - 48; height: 1; color: Theme.rule }
        Item {
            visible: modal.shownKind !== "settings"
            x: 24; y: 94; width: parent.width - 48; height: parent.height - y - 24
            RField { id: search; objectName: "moduleSearch"; width: parent.width; caption: "输入入口编号、名称或分类"; onTextChanged: modal.query = text; onAccepted: { const matches = modal.results(); if (matches.length) modal.chosen(matches[0]); } }
            Flow {
                id: filters; y: search.height + 16; width: parent.width; spacing: 8
                Repeater { model: Data.categories; delegate: RButton { required property string modelData; outlined: true; text: modelData; active: modal.filter === modelData; onClicked: modal.filter = modelData } }
            }
            ListView {
                id: list; objectName: "moduleSearchResults"
                anchors { top: filters.bottom; topMargin: 16; left: parent.left; right: parent.right; bottom: parent.bottom }
                clip: true; boundsBehavior: Flickable.StopAtBounds
                model: (void modal.query, void modal.filter, void modal.pinned, void modal.shownKind, modal.results())
                Controls.ScrollBar.vertical: Controls.ScrollBar {}
                delegate: Item {
                    required property int modelData
                    readonly property var record: Data.records[modelData]
                    width: list.width - 12; height: 68
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button; Accessible.name: record.title
                    Keys.onReturnPressed: modal.chosen(modelData)
                    Rectangle { anchors.fill: parent; color: area.containsMouse ? Theme.hover : "transparent"; border.width: parent.activeFocus ? 1 : 0; border.color: Theme.accent }
                    Column { x: 12; anchors.verticalCenter: parent.verticalCenter; spacing: 6
                        Text { text: record.id + "　" + record.title; color: Theme.ink; font { family: uiFont; pixelSize: 15 } }
                        Text { text: record.en; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
                    }
                    Text { anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter } text: record.category + "　打开 →"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: modal.chosen(modelData) }
                }
                Text { visible: list.count === 0; anchors.centerIn: parent; text: modal.shownKind === "saved" ? "尚无匹配的收藏入口" : "没有匹配的入口"; color: Theme.muted; font { family: uiFont; pixelSize: 14 } }
            }
        }
        Controls.ScrollView {
            id: settingsScroll
            visible: modal.shownKind === "settings"
            x: 24; y: 94; width: parent.width - 48; height: parent.height - y - 24
            contentWidth: availableWidth
            clip: true
            Column {
                width: settingsScroll.availableWidth - 12; spacing: 16
                Flow {
                    width: parent.width; spacing: 8
                    RButton { outlined: true; text: "浅色"; active: !modal.prefs.dark; onClicked: modal.prefs.dark = false }
                    RButton { outlined: true; text: "深色"; active: modal.prefs.dark; onClicked: modal.prefs.dark = true }
                    RButton { objectName: "enterSendPreference"; outlined: true; text: modal.prefs.enterSends ? "发送方式：Enter" : "发送方式：Ctrl+Enter"; onClicked: modal.prefs.enterSends = !modal.prefs.enterSends }
                    RButton { outlined: true; text: "全屏 ↗"; onClicked: modal.prefs.toggleFullscreen() }
                }
                Text { width: parent.width; wrapMode: Text.Wrap; text: "Enter 发送时，Shift+Enter 换行；Ctrl+Enter 始终可发送。中文输入法选词不会发送。"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
                Setting { title: "高性能渲染"; detail: "降低三维画质和分辨率，保留动画；关闭后恢复原画质"; checked: modal.prefs.superPerformance; onToggled: modal.prefs.superPerformance = !modal.prefs.superPerformance }
                Setting { title: "操作与开场音效"; checked: modal.prefs.sound; onToggled: modal.prefs.sound = !modal.prefs.sound }
                Volume { caption: "音效音量"; value: modal.prefs.soundVolume; onMoved: v => modal.prefs.soundVolume = v }
                Setting { title: "背景音乐"; checked: modal.prefs.music; onToggled: modal.prefs.music = !modal.prefs.music }
                Volume { caption: "音乐音量"; value: modal.prefs.musicVolume; onMoved: v => modal.prefs.musicVolume = v }
                Text { text: "动画与动态效果"; color: Theme.ink; font { family: boldFont; pixelSize: 20 } }
                Flow {
                    width: parent.width; spacing: 8
                    RButton { outlined: true; text: "完整动画"; active: modal.prefs.motionPreset === "full"; onClicked: modal.prefs.applyPreset("full") }
                    RButton { outlined: true; text: "减少动画"; active: modal.prefs.motionPreset === "reduced"; onClicked: modal.prefs.applyPreset("reduced") }
                    RButton { outlined: true; text: "重播完整开场 ↻"; onClicked: modal.replay() }
                }
                Repeater {
                    model: modal.prefs.motionKeys
                    delegate: Setting { required property var modelData; title: modelData[2]; detail: modelData[1]; checked: modal.prefs.on(modelData[0]); onToggled: modal.prefs.setMotion(modelData[0], !checked) }
                }
                Text { text: "三维画质"; color: Theme.ink; font { family: boldFont; pixelSize: 20 } }
                Flow { width: parent.width; spacing: 8
                    Repeater { model: [["performance", "性能"], ["original", "原始"], ["high", "高"], ["ultra", "极高"]]
                        delegate: RButton { required property var modelData; outlined: true; text: modelData[1]; active: modal.prefs.quality === modelData[0]; onClicked: modal.prefs.quality = modelData[0] }
                    }
                }
                Text { width: parent.width; wrapMode: Text.Wrap; text: "实际渲染 " + modal.prefs.renderSize + " · 即时生效并保存在本机。"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
                Text { text: "界面与更新"; color: Theme.ink; font { family: boldFont; pixelSize: 20 } }
                Flow { width: parent.width; spacing: 8
                    RButton { outlined: true; text: "Codex 界面"; enabled: chat.nativeAvailable; onClicked: modal.openCodex() }
                    RButton { outlined: true; active: true; text: "Qt 复刻版" }
                    RButton { outlined: true; text: "软件更新"; visible: chat.hotSwitch; onClicked: chat.openUpdates() }
                }
                Text { width: parent.width; wrapMode: Text.Wrap; text: chat.hotSwitch ? "热切换保留登录、聊天记录、草稿和文件传输。" : "切换前请处理草稿和文件传输，切换后需重新登录。"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
                Text { width: parent.width; wrapMode: Text.Wrap; text: "Ctrl+1～5：模块直达　Ctrl+K：会话检索\n阵列中方向键选择，Enter 打开，/ 检索入口。Esc 关闭最上层浮层。"; color: Theme.text; font { family: uiFont; pixelSize: 13 } }
                Text { topPadding: 12; bottomPadding: 16; text: "界面来源 RhineLabUI（MIT） · POWERED BY RHINE LAB"; color: Theme.faint; font { family: uiFont; pixelSize: 11 } }
            }
        }
    }
    component Setting: Item {
        property string title: ""
        property string detail: ""
        property bool checked: false
        signal toggled()
        width: parent.width; height: Math.max(56, labels.implicitHeight + 16)
        Column { id: labels; width: parent.width - toggle.width - 16; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Text { width: parent.width; wrapMode: Text.Wrap; text: title; color: Theme.ink; font { family: uiFont; pixelSize: 14 } }
            Text { width: parent.width; wrapMode: Text.Wrap; visible: detail !== ""; text: detail; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
        }
        RButton { id: toggle; anchors { right: parent.right; verticalCenter: parent.verticalCenter } outlined: true; active: checked; text: checked ? "已开启" : "已关闭"; onClicked: toggled() }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
    }
    component Volume: Row {
        property string caption: ""
        property real value: 0.5
        signal moved(real value)
        width: parent.width; spacing: 16; height: 32
        Text { anchors.verticalCenter: parent.verticalCenter; width: 72; text: caption; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
        Controls.Slider { width: parent.width - 158; height: 32; value: parent.value; onMoved: parent.moved(value); Accessible.name: parent.caption }
        Text { anchors.verticalCenter: parent.verticalCenter; width: 54; text: Math.round(parent.value * 100) + "%"; color: Theme.ink; font { family: uiFont; pixelSize: 12 } }
    }
}
