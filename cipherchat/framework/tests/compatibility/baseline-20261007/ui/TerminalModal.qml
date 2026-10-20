import QtQuick
import QtQuick.Effects
import "."
import "Records.js" as Data

// .terminal-modal: CHANNEL INDEX, PINNED and SYSTEM SETTINGS from main.ts,
// with the SurfaceTransition fade/scale and the blurred stage behind it.
Item {
    id: modal
    property string kind: ""            // "search" | "saved" | "settings"
    property string shownKind: ""
    property var pinned: []
    property var prefs                   // Main.qml preferences object
    property var store
    property Item backdropSource
    property string filter: "全部档案"
    property string query: ""
    signal chosen(int index)
    signal closed()
    signal replay()
    signal notify(string text)
    function openCodex() {
        if (chat.hotSwitch) { const error = chat.switchFrontend("codex"); if (error) notify(error); return }
        if (store && Object.values(store.drafts).some(text => String(text).length > 0)) {
            notify("请先发送或清空草稿，再切换界面。"); return
        }
        if (store && store.files.some(file => !file.complete)) {
            notify("请等待文件传输完成，再切换界面。"); return
        }
        const error = chat.switchFrontend("codex")
        if (error) notify(error)
    }
    anchors.fill: parent
    visible: opacity > 0
    opacity: kind !== "" ? 1 : 0
    // SurfaceTransition: 300 ms in / 200 ms out; the panel rises from 12 px and leaves by 8 px.
    Behavior on opacity {
        enabled: modal.prefs ? modal.prefs.motion.surfaceTransitions !== false : true
        NumberAnimation { duration: modal.kind !== "" ? 300 : 200; easing.type: Easing.BezierSpline; easing.bezierCurve: modal.kind !== "" ? [0.22, 1, 0.36, 1, 1, 1] : [0.4, 0, 1, 1, 1, 1] }
    }
    onKindChanged: if (kind) {
        shownKind = kind; filter = "全部档案"; query = ""; search.text = ""; settingsFlick.contentY = 0
        if (kind !== "settings") Qt.callLater(() => search.input.forceActiveFocus())
    }
    Keys.onEscapePressed: modal.closed()

    component Toggle: Rectangle { // .toggle
        property bool checked: false
        signal toggled()
        width: 52; height: 24; color: checked ? Theme.mix("#5c6647", "#c5a16b") : Theme.field; border.color: Theme.line
        Rectangle { width: 16; height: 16; y: 4; x: parent.checked ? 32 : 4; color: Theme.solidInk; Behavior on x { NumberAnimation { duration: 180 } } }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.toggled() }
        activeFocusOnTab: true; Keys.onSpacePressed: toggled(); Accessible.role: Accessible.CheckBox; Accessible.checked: checked
    }
    component Setting: Item {
        property string title: ""
        property string detail: ""
        property bool checked: false
        property bool rule: true
        signal toggled()
        width: parent ? parent.width : 0; height: 70
        Column { spacing: 7; anchors.verticalCenter: parent.verticalCenter
            Text { text: title; color: Theme.ink; font { family: uiFont; pixelSize: 14; letterSpacing: 0.6 } }
            Text { text: detail; color: Theme.muted; font { family: uiFont; pixelSize: 12 } } }
        Toggle { anchors { right: parent.right; verticalCenter: parent.verticalCenter } checked: parent.checked; onToggled: parent.toggled() }
        Rectangle { visible: parent.rule; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
    }
    component Choice: Rectangle {
        property string text: ""
        property bool selected: false
        property int pixelSize: 13
        signal picked()
        implicitWidth: Math.max(46, label.implicitWidth + 30); implicitHeight: 38
        color: selected ? Theme.solid : "transparent"; border.color: selected ? Theme.solid : Theme.line
        Text { id: label; anchors.centerIn: parent; text: parent.text; color: parent.selected ? Theme.solidInk : Theme.ink; font { family: uiFont; pixelSize: parent.pixelSize } }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.picked() }
        activeFocusOnTab: true; Keys.onSpacePressed: picked(); Accessible.role: Accessible.Button; Accessible.name: text
    }
    component Volume: Row {
        property string caption: ""
        property real value: 0.5
        signal moved(real value)
        spacing: 16; height: 30
        Text { anchors.verticalCenter: parent.verticalCenter; width: 58; text: caption; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
        Item {
            id: track; width: 374; height: 30
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 4; radius: 2; color: Theme.field; border.color: Theme.line }
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width * value; height: 4; radius: 2; color: Theme.mix("#5c6647", "#c5a16b") }
            Rectangle { anchors.verticalCenter: parent.verticalCenter; x: parent.width * value - 8; width: 16; height: 16; radius: 8; color: Theme.mix("#5c6647", "#c5a16b") }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                function set(x) { moved(Math.round(Math.max(0, Math.min(1, x / width)) * 100) / 100) }
                onPressed: mouse => set(mouse.x); onPositionChanged: mouse => { if (pressed) set(mouse.x) }
            }
        }
        Text { anchors.verticalCenter: parent.verticalCenter; text: Math.round(value * 100) + "%"; color: Theme.ink; font { family: uiFont; pixelSize: 11 } }
    }

    function results() {
        const q = query.trim().toLowerCase()
        return Data.records.map((r, i) => i).filter(i => {
            const r = Data.records[i]
            if (shownKind === "saved" && !pinned.includes(r.id)) return false
            if (filter !== "全部档案" && r.category !== filter) return false
            return !q || [r.id, r.title, r.en, r.category].some(v => v.toLowerCase().includes(q))
        })
    }

    // .modal-backdrop: blurred, washed stage.
    MultiEffect {
        anchors.fill: parent
        source: modal.backdropSource
        visible: !!modal.backdropSource
        blurEnabled: true; blur: 1; blurMax: 48
        autoPaddingEnabled: false
    }
    Rectangle { anchors.fill: parent; color: Theme.paper; opacity: 0.55; MouseArea { anchors.fill: parent; onClicked: modal.closed() } }
    Rectangle {
        id: sheet
        anchors.centerIn: parent
        width: 1126; height: 990
        transform: Translate { y: (1 - modal.opacity) * (modal.kind !== "" ? 12 : 8) }
        color: Theme.panel; border.color: Theme.line
        MouseArea { anchors.fill: parent }
        Item {
            x: 56; y: 44; width: parent.width - 112; height: 34
            Text { anchors.verticalCenter: parent.verticalCenter; text: "RHINE LAB / " + (modal.shownKind === "settings" ? "SYSTEM PREFERENCES" : "CHANNEL DIRECTORY"); color: Theme.ink; font { family: uiFont; pixelSize: 13; letterSpacing: 1.2 } }
            Row {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter } spacing: 18
                Text { anchors.verticalCenter: parent.verticalCenter; text: "CLOSE"; color: Theme.ink; font { family: uiFont; pixelSize: 12; letterSpacing: 0.8 } }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "✕"; color: Theme.ink; font.pixelSize: 20 }
            }
            MouseArea { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: 110; cursorShape: Qt.PointingHandCursor; onClicked: modal.closed() }
            Rectangle { anchors.top: parent.bottom; anchors.topMargin: 18; width: parent.width; height: 1; color: Theme.line }
        }

        // Directory (search / saved)
        Column {
            visible: modal.shownKind !== "settings"
            x: 56; y: 130; width: parent.width - 112; spacing: 24
            Row {
                spacing: 20
                Text { id: dirTitle; text: modal.shownKind === "saved" ? "PINNED CHANNELS" : "CHANNEL INDEX"; color: Theme.ink; font { family: boldFont; pixelSize: 40; letterSpacing: -0.5 } }
                Text { anchors.baseline: dirTitle.baseline; text: modal.shownKind === "saved" ? "固定入口" : "通讯模块检索"; color: Theme.muted; font { family: uiFont; pixelSize: 16; letterSpacing: 2 } }
            }
            Item {
                width: parent.width; height: 54
                Rectangle { anchors.fill: parent; color: Theme.field; border.color: Theme.line }
                Glyph { x: 18; anchors.verticalCenter: parent.verticalCenter; kind: "search"; ink: Theme.muted }
                RField { id: search; x: 56; y: 4; width: parent.width - 130; caption: ""; onTextChanged: modal.query = text; onAccepted: { const list = modal.results(); if (list.length) modal.chosen(list[0]) } }
                Text { visible: !search.text; x: 56; anchors.verticalCenter: parent.verticalCenter; text: "输入入口编号、名称或分类"; color: Theme.faint; font { family: uiFont; pixelSize: 14 } }
                KeyCap { anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter } text: "ESC" }
            }
            Flow {
                width: parent.width; spacing: 10
                Repeater {
                    model: Data.categories
                    delegate: Choice { required property string modelData; text: modelData; selected: modal.filter === modelData; onPicked: modal.filter = modelData }
                }
            }
            Item {
                width: parent.width; height: 20
                Text { text: "CHANNEL / 入口"; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
                Text { x: 520; text: "MODULE / 模块"; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
                Text { anchors.right: parent.right; text: "ACCESS"; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            }
            ListView {
                id: list
                width: parent.width; height: 470; clip: true
                model: (void modal.query, void modal.filter, void modal.pinned, void modal.shownKind, modal.results())
                delegate: Item {
                    required property int modelData
                    readonly property var record: Data.records[modelData]
                    width: list.width; height: 60
                    Rectangle { anchors.fill: parent; color: row.containsMouse ? Theme.hover : "transparent" }
                    Column { x: 10; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                        Text { text: record.id + "　" + record.title; color: Theme.ink; font { family: uiFont; pixelSize: 15 } }
                        Text { text: record.en; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } } }
                    Text { x: 520; anchors.verticalCenter: parent.verticalCenter; text: record.category; color: Theme.text; font { family: uiFont; pixelSize: 13 } }
                    Text { anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter } text: "OPEN →"; color: Theme.ink; font { family: uiFont; pixelSize: 12; letterSpacing: 1 } }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                    MouseArea { id: row; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: modal.chosen(modelData) }
                }
                Column {
                    visible: list.count === 0; anchors.centerIn: parent; spacing: 12
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "∅"; color: Theme.faint; font.pixelSize: 36 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: modal.shownKind === "saved" && !modal.query ? "尚无固定入口" : "没有匹配的入口"; color: Theme.ink; font { family: boldFont; pixelSize: 16 } }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: modal.shownKind === "saved" && !modal.query ? "打开入口时，选择 PIN CHANNEL 将其保存在此处。" : "尝试其他名称、编号，或切换分类。"; color: Theme.text; font { family: uiFont; pixelSize: 13 } }
                    RButton { anchors.horizontalCenter: parent.horizontalCenter; text: modal.shownKind === "saved" ? "查看全部入口 →" : "重置检索 →"; onClicked: { if (modal.shownKind === "saved") modal.kind = "search"; search.text = ""; modal.filter = "全部档案" } }
                }
            }
            Item {
                width: parent.width; height: 16
                Text { text: list.count + " 个入口"; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
                Text { anchors.right: parent.right; text: "PRIVATE COMMUNICATION ● CONNECTED"; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
            }
        }

        // Settings
        Flickable {
            id: settingsFlick
            visible: modal.shownKind === "settings"
            x: 56; y: 118; width: parent.width - 112; height: parent.height - 118 - 30
            contentHeight: settings.height + 40; clip: true; boundsBehavior: Flickable.StopAtBounds
            Rectangle { // scrollbar
                parent: settingsFlick; x: settingsFlick.width + 40; width: 4; radius: 2; color: Theme.faint
                visible: settingsFlick.contentHeight > settingsFlick.height
                height: Math.max(40, settingsFlick.height * settingsFlick.height / settingsFlick.contentHeight)
                y: (settingsFlick.height - height) * settingsFlick.contentY / Math.max(1, settingsFlick.contentHeight - settingsFlick.height)
            }
            Column {
                id: settings
                width: parent.width; spacing: 0
                readonly property var p: modal.prefs
                Row {
                    spacing: 22; bottomPadding: 22
                    Text { id: settingsTitle; text: "SYSTEM SETTINGS"; color: Theme.ink; font { family: boldFont; pixelSize: 40; letterSpacing: -0.5 } }
                    Text { anchors.baseline: settingsTitle.baseline; text: "终端偏好设置"; color: Theme.muted; font { family: uiFont; pixelSize: 16; letterSpacing: 2 } }
                }
                Text { bottomPadding: 22; text: "CIPHERCHAT　·　RHINE TERMINAL"; color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1 } }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Item { // 界面配色
                    width: parent.width; height: 78
                    Column { spacing: 7; anchors.verticalCenter: parent.verticalCenter
                        Text { text: "界面配色"; color: Theme.ink; font { family: uiFont; pixelSize: 14 } }
                        Text { text: "玻璃阵列随配色逐张过渡"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } } }
                    Row { anchors { right: parent.right; verticalCenter: parent.verticalCenter } spacing: 6
                        Choice { text: "亮色"; implicitWidth: 80; implicitHeight: 44; selected: !settings.p.dark; onPicked: settings.p.dark = false }
                        Choice { text: "暗色"; implicitWidth: 80; implicitHeight: 44; selected: settings.p.dark; onPicked: settings.p.dark = true } }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                }
                Setting { title: "SUPER PERFORMANCE"; detail: "降低三维画质和渲染分辨率，保留完整动效；关闭后恢复原画质"; checked: settings.p.superPerformance; onToggled: settings.p.superPerformance = !settings.p.superPerformance }
                Row {
                    width: parent.width; spacing: 34
                    Column {
                        width: (parent.width - 34) / 2
                        Setting { title: "INTERFACE SOUND"; detail: "操作与启动音效"; rule: false; checked: settings.p.sound; onToggled: settings.p.sound = !settings.p.sound }
                        Volume { caption: "音效音量"; value: settings.p.soundVolume; onMoved: v => settings.p.soundVolume = v }
                    }
                    Column {
                        width: (parent.width - 34) / 2
                        Setting { title: "BACKGROUND MUSIC"; detail: "观测室 · 背景音乐"; rule: false; checked: settings.p.music; onToggled: settings.p.music = !settings.p.music }
                        Volume { caption: "音乐音量"; value: settings.p.musicVolume; onMoved: v => settings.p.musicVolume = v }
                    }
                }
                Item { width: 1; height: 18 }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Text {
                    topPadding: 20; bottomPadding: 20; lineHeight: 1.5; color: Theme.muted; font { family: uiFont; pixelSize: 12 }
                    text: (settings.p.motionPreset === "full" ? "当前使用完整动画。" : settings.p.motionPreset === "reduced" ? "当前已减少动画。" : "当前使用自定义动画。")
                        + "\n预设：" + ({ full: "完整动画", reduced: "减少动画", custom: "自定义" })[settings.p.motionPreset] + " · 选择会保存在本机"
                }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Item {
                    width: parent.width; height: 78
                    Column { spacing: 7; anchors.verticalCenter: parent.verticalCenter
                        Text { text: "ANIMATION CONTROLS"; color: Theme.ink; font { family: uiFont; pixelSize: 14; letterSpacing: 0.6 } }
                        Text { text: "完整、减少或按分项自定义；关闭后会立即收束当前动画（开场设置下次重播生效）"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } } }
                    Row { anchors { right: parent.right; verticalCenter: parent.verticalCenter } spacing: 10
                        Choice { text: "完整"; pixelSize: 11; selected: settings.p.motionPreset === "full"; onPicked: settings.p.applyPreset("full") }
                        Choice { text: "减少"; pixelSize: 11; selected: settings.p.motionPreset === "reduced"; onPicked: settings.p.applyPreset("reduced") }
                        Choice { text: "自定义"; pixelSize: 11; selected: settings.p.motionPreset === "custom"; onPicked: motionDetails.open = true } }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                }
                Item {
                    id: motionDetails
                    property bool open: false
                    width: parent.width; height: 52
                    Row { anchors.verticalCenter: parent.verticalCenter; spacing: 20
                        Text { text: (motionDetails.open ? "▼" : "▶") + "  精细设置"; color: Theme.ink; font { family: uiFont; pixelSize: 14 } }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "开场 / 阵列 / 详情 / 界面"; color: Theme.muted; font { family: uiFont; pixelSize: 11 } } }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: motionDetails.open = !motionDetails.open }
                }
                Grid {
                    visible: motionDetails.open
                    width: parent.width; columns: 2; columnSpacing: 34
                    Repeater {
                        model: settings.p.motionKeys
                        delegate: Setting {
                            required property var modelData
                            width: (settings.width - 34) / 2; height: 64
                            title: modelData[1]; detail: modelData[2]
                            checked: settings.p.motion[modelData[0]] !== false
                            onToggled: settings.p.setMotion(modelData[0], !checked)
                        }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Item {
                    width: parent.width; height: 78
                    Row { anchors.verticalCenter: parent.verticalCenter; spacing: 20
                        Text { text: "RENDER QUALITY"; color: Theme.ink; font { family: uiFont; pixelSize: 14; letterSpacing: 0.6 } }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "渲染画质"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } } }
                    Row { anchors { right: parent.right; verticalCenter: parent.verticalCenter } spacing: 10
                        Repeater {
                            model: [["performance", "性能"], ["original", "原始"], ["high", "高"], ["ultra", "极高"]]
                            delegate: Choice { required property var modelData; text: modelData[1]; pixelSize: 12; selected: settings.p.quality === modelData[0]; onPicked: settings.p.quality = modelData[0] }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                }
                Text {
                    topPadding: 16; bottomPadding: 16; color: Theme.muted; font { family: uiFont; pixelSize: 12 }
                    text: "实际渲染 " + settings.p.renderSize + " · " + (settings.p.effectiveQuality >= 1 ? "多重采样抗锯齿 · 环境光遮蔽 · 景深" : settings.p.effectiveQuality >= 0.75 ? "环境光遮蔽 · 景深" : "无后期效果") + "　即时生效并自动保存。"
                }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Column {
                    width: parent.width; spacing: 12; topPadding: 22; bottomPadding: 22
                    Text { text: "CIPHERCHAT / FRONTENDS"; color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1 } }
                    Row { spacing: 10
                        Choice { text: "Codex"; enabled: chat.nativeAvailable; opacity: enabled ? 1 : 0.4; onPicked: modal.openCodex() }
                        Choice { text: "Qt 复刻版"; selected: true } }
                    Text { text: chat.hotSwitch ? "热切换保留登录、聊天记录、草稿和文件传输。" : "切换后需要重新登录；请先处理草稿和传输。"; color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
                    RButton { text: "软件更新"; visible: chat.hotSwitch; Accessible.name: "软件更新"; onClicked: chat.openUpdates() }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Column {
                    width: parent.width; spacing: 12; topPadding: 22; bottomPadding: 22
                    Text { text: "KEYBOARD CONTROLS"; color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1 } }
                    Row {
                        spacing: 8
                        component K: KeyCap { anchors.verticalCenter: parent ? parent.verticalCenter : undefined }
                        component T: Text { anchors.verticalCenter: parent ? parent.verticalCenter : undefined; color: Theme.ink; rightPadding: 14; font { family: uiFont; pixelSize: 13 } }
                        K { text: "←" } K { text: "→" } T { text: "切列" }
                        K { text: "↑" } K { text: "↓" } T { text: "选择入口" }
                        K { text: "ENTER" } T { text: "打开" }
                        K { text: "/" } T { text: "检索" }
                        K { text: "ESC" } T { text: "返回" }
                    }
                }
                Row {
                    spacing: 30; topPadding: 6; bottomPadding: 22
                    RButton { outlined: true; text: "FULLSCREEN ↗"; onClicked: settings.p.toggleFullscreen() }
                    RButton { outlined: true; text: "REINITIALIZE SYSTEM ↻"; onClicked: modal.replay() }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                Item {
                    width: parent.width; height: 40
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "ANALYSIS OS / 1.0 · 界面来源 RhineLabUI（MIT）"; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
                    Text { anchors { right: parent.right; verticalCenter: parent.verticalCenter } text: "POWERED BY RHINE LAB"; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1 } }
                }
            }
        }
    }
}
