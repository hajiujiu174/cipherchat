import QtQuick
import QtQuick.Window
import QtQuick.Shapes
import QtQuick.Effects
import QtQuick.Controls as Controls
import "."
import "Records.js" as Data

// Native CipherChat Rhine terminal. Layout, copy and mode flow follow
// the archived upstream design on the 1920 × 1080 reference stage.
Window {
    id: window
    width: 1600; height: 900
    minimumWidth: 860; minimumHeight: 640
    visible: !embeddedHotSwitch
    title: "CipherChat · Rhine 通讯终端"
    color: Theme.paper

    property string mode: "gate"        // gate | boot | archive | detail
    property string gateState: "loading" // loading | waiting
    property real bootTime: 0
    property int selected: 0
    property int navDirection: 1
    property string activeTab: "overview"
    property string pendingModule: ""
    property var pinned: String(chat.setting("pinned", "")).split(",").filter(Boolean)
    property string modal: ""
    property int hoverIndex: -1
    property real splitRatio: Number(chat.setting("workspace.split", 0.7))
    property string narrowWorkspaceTab: "chat"
    property int queuedIndex: -1
    property string navigationPhase: ""
    property var modalReturnFocus: null
    readonly property var record: Data.records[selected]
    readonly property var files: Data.columnFiles(record.lane)
    readonly property bool booting: mode === "boot"
    readonly property var field: scene.field
    readonly property real modelAreaTop: Math.max(205, height * 0.27)
    readonly property rect modelViewport: Qt.rect(24 / width, modelAreaTop / height, Math.max(1, workspace.x - 48) / width, Math.max(1, height - modelAreaTop - 170) / height)
    // Opening step names (main.ts bootFrame / data-boot).
    readonly property string bootStep: !booting ? "done" : bootTime >= 28.3 ? "inspect" : bootTime >= 25.68 ? "select" : bootTime >= 22 ? "array" : bootLayer.s.step
    readonly property bool archiveFilm: booting && (bootStep === "array" || bootStep === "select")

    function ease(t) { t = Math.max(0, Math.min(1, t)); return t * t * t * (t * (t * 6 - 15) + 10) }
    function pad(n, w) { return String(n).padStart(w, "0") }
    function stored(key, fallback) { const v = chat.setting(key, fallback); return v === "true" ? true : v === "false" ? false : v }

    // Preferences (main.ts prefs + motion-preferences.ts), persisted in the data directory.
    QtObject {
        id: prefs
        objectName: "rhinePreferences"
        property bool dark: window.stored("theme", "light") === "dark"
        property bool superPerformance: window.stored("superPerformance", false) === true
        property bool sound: window.stored("sound", true) === true
        property bool music: window.stored("music", true) === true
        property real soundVolume: Number(window.stored("soundVolume", 0.55))
        property real musicVolume: Number(window.stored("musicVolume", 0.5))
        property string quality: String(window.stored("quality", "original"))
        property bool enterSends: window.stored("enterSends", true) !== false
        onEnterSendsChanged: chat.setSetting("enterSends", enterSends)
        readonly property var motionKeys: [
            ["boot", "BOOT SEQUENCE", "开机标志、扫描与欢迎画面"],
            ["selectionWave", "SELECTION WAVE", "选档时向阵列传播的波浪"],
            ["idleWave", "IDLE MOTION", "停止操作后的阵列起伏"],
            ["pointerParallax", "POINTER PARALLAX", "镜头随指针的轻微偏移"],
            ["dragMomentum", "DRAG MOMENTUM", "松手后按实际速度继续滑行"],
            ["selectionTransition", "SELECTION TRANSITION", "切列、切档时的轨道移动"],
            ["detailTransition", "DETAIL TRANSITION", "抽取、转正、归位与详情镜头"],
            ["modelDecryption", "MODEL DECRYPTION", "模型解密线与磨砂揭示"],
            ["documentReveal", "DOCUMENT REVEAL", "正文的遮罩揭示"],
            ["rollingText", "ROLLING TEXT", "标题、分类与权限标签滚动"],
            ["rollingNumbers", "ROLLING NUMBERS", "序号、列编号与档案编码滚动"],
            ["surfaceTransitions", "SURFACE TRANSITIONS", "详情、检索、收藏与设置窗口过渡"]]
        property var motion: {
            const m = {}
            for (const [key] of motionKeys) m[key] = window.stored("motion." + key, true) !== false
            return m
        }
        readonly property string motionPreset: {
            const values = motionKeys.map(k => motion[k[0]] !== false)
            return values.every(v => v) ? "full" : values.every(v => !v) ? "reduced" : "custom"
        }
        function on(key) { return motion[key] !== false }
        function setMotion(key, value) { const m = Object.assign({}, motion); m[key] = value; motion = m; chat.setSetting("motion." + key, value) }
        function applyPreset(preset) {
            const m = {}
            for (const [key] of motionKeys) { m[key] = preset === "full"; chat.setSetting("motion." + key, m[key]) }
            motion = m
        }
        // Rendering: SUPER PERFORMANCE overrides the quality preset.
        readonly property real effectiveQuality: superPerformance || quality === "performance" ? 0.5 : quality === "original" ? 1 : quality === "high" ? 1.25 : 1.5
        readonly property real renderScale: superPerformance ? 0.7 : { performance: 0.8, original: 1, high: 1.25, ultra: 1.5 }[quality] ?? 1
        readonly property string renderSize: Math.round(scene.width * Screen.devicePixelRatio * renderScale) + " × " + Math.round(scene.height * Screen.devicePixelRatio * renderScale)
        function toggleFullscreen() { window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
        onDarkChanged: { window.field.dark = dark; chat.setSetting("theme", dark ? "dark" : "light") }
        onSuperPerformanceChanged: chat.setSetting("superPerformance", superPerformance)
        onSoundChanged: chat.setSetting("sound", sound)
        onMusicChanged: chat.setSetting("music", music)
        onSoundVolumeChanged: chat.setSetting("soundVolume", soundVolume)
        onMusicVolumeChanged: chat.setSetting("musicVolume", musicVolume)
        onQualityChanged: chat.setSetting("quality", quality)
    }

    ChatStore {
        id: store
        onNotify: text => toast.show(text)
        onOpenModule: module => {
            if (window.mode === "gate" || window.mode === "boot") window.pendingModule = module
            else window.openIndex(Data.moduleIndex(module))
        }
    }
    TerminalAudio {
        id: audio
        sound: prefs.sound && window.visible; music: prefs.music && window.visible
        soundVolume: prefs.soundVolume; musicVolume: prefs.musicVolume
        scene: window.mode === "detail" ? "detail" : window.mode === "archive" ? "archive" : "boot"
    }

    function enter(silent) {
        if (mode !== "gate") return
        audio.unlocked = !silent
        if (!prefs.on("boot") || skipOpening) { finishBoot("archive"); return }
        bootTime = 0; audio.bootPrevious = -1; mode = "boot"; field.setMode("hidden"); field.setCinematic(0)
    }
    function finishBoot(next) {
        field.setCinematic(-1)
        mode = next; field.setMode(next)
        if (next === "detail") activeTab = record.tab
        stageRoot.forceActiveFocus()
        if (pendingModule) {
            const module = pendingModule
            pendingModule = ""
            Qt.callLater(() => window.openIndex(Data.moduleIndex(module)))
        } else if (next === "archive" && !store.sessionAvailable) Qt.callLater(() => {
            if (!store.sessionAvailable && window.mode === "archive" && window.navigationPhase === "") window.navigateModule("account")
        })
    }
    function replay() {
        modal = ""; field.setMode("hidden"); field.selectIndex(0); selected = 0
        if (!prefs.on("boot")) { finishBoot("archive"); return }
        bootTime = 1.76; audio.bootPrevious = -1; mode = "boot"; field.setCinematic(bootTime)
        audio.play("ui-tick")
    }
    function open() {
        if (mode !== "archive") return
        if (!store.sessionAvailable && selected !== 0) { openIndex(0); return }
        activeTab = record.tab
        mode = "detail"; field.setMode("detail")
        audio.play("open")
        navigationPhase = "opening"
    }
    function back() {
        if (modal) { closeModal(); return }
        queuedIndex = -1; navigationPhase = ""
        if (mode === "detail") { mode = "archive"; field.setMode("archive"); stageRoot.forceActiveFocus(); audio.play("back") }
        else if (mode === "boot") finishBoot("archive")
    }
    function showModal(kind) { modalReturnFocus = activeFocusItem; modal = kind; audio.play("page-open") }
    function closeModal() {
        modal = ""; audio.play("page-close")
        if (workspace.visible && workspace.conversation) workspace.focusEditor()
        else if (modalReturnFocus && modalReturnFocus.visible) modalReturnFocus.forceActiveFocus()
        else stageRoot.forceActiveFocus()
    }
    function navigateModule(module) { openIndex(Data.moduleIndex(store.sessionAvailable ? module : "account")) }
    function openIndex(index) {
        if (index < 0 || index >= Data.records.length) return
        modal = ""
        if (mode === "gate" || mode === "boot") { pendingModule = Data.records[index].module; return }
        if (navigationPhase !== "" && navigationPhase !== "closing" && selected === index) { queuedIndex = -1; activeTab = Data.records[index].tab; return }
        if (mode === "detail" && selected === index && navigationPhase === "") { activeTab = Data.records[index].tab; return }
        queuedIndex = index
        if (navigationPhase !== "") return
        if (mode === "detail") { navigationPhase = "closing"; field.setMode("archive") }
        else {
            navigationPhase = "selecting"
            field.selectIndex(queuedIndex); selected = queuedIndex; queuedIndex = -1
        }
    }
    function dismissTopLayer() {
        if (activeFocusItem && activeFocusItem.inputMethodComposing) return
        if (modal) { closeModal(); return }
        if (workspace.closeTransient()) return
        if (mode === "boot") back()
    }
    Timer {
        interval: 16; repeat: true
        running: window.visible && window.navigationPhase !== ""
        onTriggered: {
            if (window.navigationPhase === "opening") {
                if (!window.field.canInspect || scene.decryption.clarity < 0.999) return
                window.navigationPhase = ""
                if (window.queuedIndex >= 0) window.openIndex(window.queuedIndex)
            } else if (window.navigationPhase === "closing") {
                if (window.field.detailAmount > 0.025) return
                window.mode = "archive"; window.navigationPhase = "selecting"
                window.field.selectIndex(window.queuedIndex); window.selected = window.queuedIndex; window.queuedIndex = -1
            } else if (window.navigationPhase === "selecting" && window.field.selectionSettled && window.field.canBrowse) {
                if (window.queuedIndex >= 0) {
                    window.field.selectIndex(window.queuedIndex); window.selected = window.queuedIndex; window.queuedIndex = -1
                } else { window.navigationPhase = ""; window.open() }
            }
        }
    }
    function togglePin() {
        const list = pinned.slice(), at = list.indexOf(record.id)
        if (at >= 0) list.splice(at, 1); else list.push(record.id)
        pinned = list; chat.setSetting("pinned", list.join(","))
        audio.play("confirm")
    }
    function sessionState() {
        return { drafts: store.drafts, selectedPeer: store.selectedPeer, selectedGroup: store.selectedGroup,
                 phase: store.phase, username: store.username, files: store.files, history: store.history,
                 pinnedPeers: store.pinnedPeers }
    }
    function restoreSessionState(state) {
        store.drafts = Object.assign({}, store.drafts, state.drafts || {})
        store.selectedPeer = state.selectedPeer || ""; store.selectedGroup = state.selectedGroup || 0
        store.pinnedPeers = state.pinnedPeers || store.pinnedPeers
        store.revision++
    }
    Connections {
        target: window.field
        function onSelectionChanged(index, axis, direction) {
            window.navDirection = direction === 0 ? window.navDirection : direction
            window.selected = index; window.activeTab = Data.records[index].tab
            if (window.mode === "archive") audio.play(axis === "lane" ? "column" : "tick")
        }
        function onHoverChanged(index) { window.hoverIndex = index }
    }
    Component.onCompleted: {
        field.setDarkImmediate(prefs.dark); field.dark = prefs.dark; Theme.amount = prefs.dark ? 1 : 0
    }

    FrameAnimation {
        running: window.visible && window.mode === "boot"
        onTriggered: {
            window.bootTime = freezeAt >= 0 ? Math.min(window.bootTime + frameTime, freezeAt) : window.bootTime + frameTime
            audio.updateBoot(window.bootTime, freezeAt >= 0)
            if (window.bootTime >= 35) window.finishBoot("detail")
            else window.field.setCinematic(window.bootTime)
        }
    }
    // The gate waits until the scene has compiled its materials, like the web loader.
    Timer { running: window.gateState === "loading"; interval: 900; onTriggered: { window.gateState = "waiting"; if (!prefs.sound && !prefs.music) window.enter(true) } }

    Scene {
        id: scene
        objectName: "rhineScene"
        visible: window.visible
        anchors.fill: parent
        opacity: window.booting ? (window.bootTime < 21.9 ? 0 : window.ease((window.bootTime - 21.9) / 0.13)) : 1
        interactive: window.modal === "" && window.navigationPhase === "" && !workspace.drawerOpen && !workspace.newPeerOpen && (window.mode === "archive" || window.mode === "detail")
        motion: prefs.motion
        quality: prefs.effectiveQuality
        explicitTextureWidth: Math.round(width * Screen.devicePixelRatio * prefs.renderScale)
        explicitTextureHeight: Math.round(height * Screen.devicePixelRatio * prefs.renderScale)
        cinematicTime: window.booting ? window.bootTime : -1
        detailMode: window.mode === "detail"
        onOpenRequested: window.open()
    }
    Binding { target: window.field; property: "detailViewport"; value: window.modelViewport }
    // .archive-atmosphere: soft top wash and vignette; in detail a paper field
    // rises behind the document on the right (--detail-shade).
    Item {
        anchors.fill: parent
        visible: window.mode !== "gate"
        readonly property real shade: window.booting ? 0 : scene.field.detailVisibility
        Rectangle {
            anchors.fill: parent; opacity: 1 - parent.shade
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.35 + 0.47 * Theme.amount) }
                GradientStop { position: 0.38; color: "transparent" }
                GradientStop { position: 0.75; color: "transparent" }
                GradientStop { position: 1; color: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.84 * Theme.amount) }
            }
        }
        Shape {
            anchors.fill: parent; opacity: 1 - parent.shade
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: window.width * 0.42; centerY: window.height * 0.47; focalX: centerX; focalY: centerY
                    centerRadius: Math.hypot(window.width, window.height) * 0.62
                    GradientStop { position: 0.32 / 0.85 * 0.85; color: "transparent" }
                    GradientStop { position: 1; color: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.19 + 0.29 * Theme.amount) }
                }
                PathRectangle { width: window.width; height: window.height }
            }
        }
        Rectangle {
            anchors.fill: parent; opacity: parent.shade
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.40; color: "transparent" }
                GradientStop { position: 0.59; color: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.2) }
                GradientStop { position: 0.68; color: Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, 0.92) }
                GradientStop { position: 1.0; color: Theme.paper }
            }
        }
    }
    InspectionOverlay {
        anchors.fill: parent
        scene: scene
        frame: scene.decryption.frame
        active: prefs.on("modelDecryption") && (window.booting || window.mode === "detail")
        showLabel: window.booting
        stageUnit: stageRoot.unit
        tick: scene.tick
    }

    // Reference stage: desktop layout scales by height and widens to fill.
    Item {
        id: stageRoot
        readonly property real unit: window.height / 1080
        width: window.width / unit; height: 1080
        scale: unit; transformOrigin: Item.TopLeft
        focus: true
        Keys.onPressed: event => {
            if (window.modal || window.navigationPhase !== "" || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) return
            const k = event.key
            if (window.mode === "gate" && window.gateState === "waiting" && (k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Space)) { window.enter(false); event.accepted = true }
            else if (window.mode === "boot" && (k === Qt.Key_Return || k === Qt.Key_Enter)) { window.finishBoot("archive"); event.accepted = true }
            else if (window.mode === "archive") {
                if (k === Qt.Key_Left) window.field.stepColumn(-1)
                else if (k === Qt.Key_Right) window.field.stepColumn(1)
                else if (k === Qt.Key_Up) window.field.stepRow(-1)
                else if (k === Qt.Key_Down) window.field.stepRow(1)
                else if (k === Qt.Key_Return || k === Qt.Key_Enter) window.open()
                else if (k === Qt.Key_Slash && store.sessionAvailable) window.showModal("search")
                else return
                event.accepted = true
            } else if (window.mode === "detail" && k === Qt.Key_Slash && store.sessionAvailable) { window.showModal("search"); event.accepted = true }
        }

        Boot { id: bootLayer; t: window.bootTime; visible: window.booting && vt < 26.92 }

        // Brand block (.brand, left 59 top 114); each line follows brandTrack during the opening.
        Column {
            x: 59; y: 114
            visible: window.mode !== "gate"
            spacing: 0
            Repeater {
                model: 3
                delegate: Item {
                    required property int index
                    readonly property var track: window.booting ? bootLayer.s.brand[index] : ({ x: 0, opacity: 1 })
                    width: index === 2 ? messagesRow.implicitWidth : line.implicitWidth; height: [48, 23, 40][index]
                    opacity: track.opacity
                    Text {
                        id: line
                        x: track.x
                        visible: index < 2
                        text: ["CIPHERCHAT", "PRIVATE COMMUNICATION", ""][index]
                        color: Theme.ink
                        font { family: boldFont; pixelSize: [44, 19, 35][index]; letterSpacing: [2.3, 0.6, 0][index] }
                    }
                    Row {
                        id: messagesRow
                        x: track.x
                        visible: index === 2
                        spacing: 20
                        Text { text: "MESSAGES"; color: Theme.ink; font { family: uiFont; pixelSize: 35; weight: Font.Light } }
                        Text { text: "OS"; color: Theme.ink; font { family: boldFont; pixelSize: 35; letterSpacing: 3 } }
                    }
                }
            }
        }
        RButton {
            visible: window.booting
            anchors { right: parent.right; rightMargin: 60 } y: 121
            text: "ENTER SYSTEM    ↗"; pixelSize: 14; letterSpacing: 1.1; opacity: 0.55
            onClicked: window.finishBoot("archive")
        }
        // Utility navigation shares logical-pixel coordinates with the work area.
        Item {
            parent: window.contentItem
            z: 12; x: 24; y: 20; width: window.width - 48; height: 76
            visible: window.mode === "archive" || window.mode === "detail"
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 800 } }
            Row {
                anchors.right: parent.right; spacing: 6
                Repeater {
                    model: store.sessionAvailable ? [["chat", "会话"], ["contacts", "联系人"], ["groups", "群聊"], ["files", "文件"], ["account", "账号"]] : [["account", "登录"]]
                    delegate: RButton {
                        required property var modelData
                        objectName: "navigate-" + modelData[0]
                        outlined: true; text: modelData[1]; active: window.mode === "detail" && (store.sessionAvailable ? window.record.module === modelData[0] : modelData[0] === "account")
                        onClicked: window.navigateModule(modelData[0])
                    }
                }
                RButton { objectName: "openSettings"; outlined: true; text: "设置"; onClicked: window.showModal("settings") }
            }
            Row {
                anchors { right: parent.right; bottom: parent.bottom } spacing: 16
                RButton { objectName: "returnToArray"; visible: window.mode === "detail"; text: "← 返回阵列"; onClicked: window.back() }
                RButton { objectName: "openIndexSearch"; visible: store.sessionAvailable; text: "检索入口 /"; onClicked: window.showModal("search") }
                RButton { objectName: "openPinned"; visible: store.sessionAvailable; text: "收藏入口 " + window.pinned.length; onClicked: window.showModal("saved") }
            }
        }
        // .powered (right 59, bottom 108); typed in left to right during the opening.
        Item {
            anchors { right: parent.right; rightMargin: 59; bottom: parent.bottom; bottomMargin: 108 }
            width: powered.implicitWidth; height: powered.implicitHeight
            visible: window.mode !== "gate" && (!window.booting || bootLayer.s.poweredLetters > 0)
            Item {
                width: parent.width * (window.booting ? bootLayer.s.poweredLetters / 19 : 1); height: parent.height; clip: true
                Row {
                    id: powered
                    spacing: 5
                    Text { text: "POWERED BY"; color: Theme.ink; font { family: uiFont; pixelSize: 19 } }
                    Text { text: "RHINE LAB"; color: Theme.ink; font { family: boldFont; pixelSize: 19 } }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; anchors.verticalCenterOffset: 1; width: 25; height: 6; color: Theme.ink }
                }
            }
        }

        // Archive overlay (.archive-ui). During the film only the title and rule show.
        Item {
            id: archiveUi
            anchors.fill: parent
            readonly property bool live: window.mode === "archive"
            visible: opacity > 0
            opacity: live || window.archiveFilm ? 1 : 0
            Behavior on opacity { enabled: !window.booting; NumberAnimation { duration: 800 } }
            enabled: live
            Column { // .archive-callout
                x: 970; y: 471; width: 950
                Row {
                    opacity: archiveUi.live ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 700 } }
                    spacing: 16
                    Text { text: "PRIVATE COMMUNICATION"; color: Theme.muted; font { family: uiFont; pixelSize: 12; letterSpacing: 1.3 } }
                    Text { text: "／"; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
                    Rolling { text: window.record.category; direction: window.navDirection; animated: prefs.on("rollingText"); color: Theme.muted; font { family: uiFont; pixelSize: 12; letterSpacing: 1.3 } }
                }
                Item { width: 1; height: 22 }
                Item {
                    width: titleRow.width + 80; height: titleRow.height
                    Row {
                        id: titleRow
                        readonly property int size: window.bootStep === "array" ? 24 : 28
                        Text {
                            color: Theme.ink; font { family: boldFont; pixelSize: titleRow.size; letterSpacing: 0.2 }
                            text: window.bootStep === "array" ? "SELECTING FILES...".slice(0, Math.max(0, Math.floor((window.bootTime - 21.94) * 18))) : "CHANNEL: X-"
                        }
                        Rolling {
                            visible: window.bootStep !== "array"
                            text: window.record.id.slice(2); numeric: true; direction: window.navDirection
                            animated: prefs.on("rollingNumbers") && archiveUi.live
                            color: Theme.ink; font { family: boldFont; pixelSize: titleRow.size; letterSpacing: 0.2 }
                        }
                    }
                    Text { x: titleRow.width + 48; anchors.verticalCenter: titleRow.verticalCenter; text: "↗"; color: Theme.ink; font.pixelSize: 25; opacity: openArea.containsMouse ? 1 : 0; Behavior on opacity { NumberAnimation { duration: 300 } } }
                    MouseArea { id: openArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.open() }
                }
                Item {
                    width: parent.width; height: 55 + 1
                    Rectangle {
                        x: 57; y: 55; width: parent.width - 57; height: 1; color: Theme.rule
                        transform: Scale { origin.x: 893; xScale: window.booting ? window.ease((window.bootTime - 22.08) / 0.9) : 1 }
                        Rectangle { x: -61; y: -3; width: 6; height: 6; color: Theme.ink; opacity: archiveUi.live ? 1 : 0 }
                    }
                }
                Item { width: 1; height: 22 }
                Item {
                    x: 58; width: 652; height: 24
                    opacity: archiveUi.live ? 1 : 0
                    Behavior on opacity { SequentialAnimation { PauseAnimation { duration: 400 } NumberAnimation { duration: 700 } } }
                    Rolling { anchors.verticalCenter: parent.verticalCenter; text: window.record.title; direction: window.navDirection; animated: prefs.on("rollingText"); color: Theme.ink; font { family: uiFont; pixelSize: 16 } }
                    Rolling { anchors { right: parent.right; verticalCenter: parent.verticalCenter } text: window.record.clearance; direction: window.navDirection; animated: prefs.on("rollingText"); color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1 } }
                }
                Item { width: 1; height: 58 }
                RButton {
                    x: 58; text: "OPEN CHANNEL          →"; pixelSize: 14; letterSpacing: 1; onClicked: window.open()
                    opacity: archiveUi.live ? 1 : 0
                    Behavior on opacity { SequentialAnimation { PauseAnimation { duration: 400 } NumberAnimation { duration: 700 } } }
                }
            }
            // #hover-label (left 60, bottom 264)
            Row {
                x: 60; anchors { bottom: parent.bottom; bottomMargin: 264 }
                visible: archiveUi.live && window.hoverIndex >= 0
                readonly property var hovered: Data.records[Math.max(0, window.hoverIndex)]
                Text { text: "X-"; color: Theme.text; font { family: uiFont; pixelSize: 13; letterSpacing: 1 } }
                Rolling { text: parent.hovered.id.slice(2); numeric: true; animated: prefs.on("rollingNumbers"); color: Theme.text; font { family: uiFont; pixelSize: 13; letterSpacing: 1 } }
                Text { text: " / "; color: Theme.text; font { family: uiFont; pixelSize: 13; letterSpacing: 1 } }
                Rolling { text: parent.hovered.title; animated: prefs.on("rollingText"); color: Theme.text; font { family: uiFont; pixelSize: 13; letterSpacing: 1 } }
            }
            Column { // .archive-counter
                x: 60; anchors { bottom: parent.bottom; bottomMargin: 115 }
                visible: archiveUi.live
                spacing: 16
                Text { text: "CHANNEL / SELECT"; color: Theme.ink; font { family: uiFont; pixelSize: 11; letterSpacing: 1.7 } }
                Row {
                    spacing: 21
                    Rolling { id: counterNumber; text: window.pad(window.files.indexOf(window.selected) + 1, 2); numeric: true; direction: window.navDirection; animated: prefs.on("rollingNumbers"); color: Theme.ink; font { family: uiFont; pixelSize: 58; weight: Font.Light } }
                    Text { anchors.bottom: counterNumber.bottom; anchors.bottomMargin: 10; text: "/"; color: Theme.mix("#8f8a80", "#6f7c80"); font { family: uiFont; pixelSize: 32; weight: Font.Light } }
                    Text { anchors.bottom: counterNumber.bottom; anchors.bottomMargin: 12; text: window.pad(window.files.length, 2); color: Theme.mix("#726f67", "#a6b0b1"); font { family: uiFont; pixelSize: 22 } }
                }
            }
            Row { // .archive-navigation
                x: 504; anchors { bottom: parent.bottom; bottomMargin: 131 }
                visible: archiveUi.live
                spacing: 33
                RButton { width: 46; height: 46; text: "  ↑"; pixelSize: 28; Accessible.name: "上一个入口"; onClicked: window.field.stepRow(-1) }
                Row {
                    anchors.verticalCenter: parent.verticalCenter; spacing: 12
                    Repeater {
                        model: window.files
                        delegate: Item {
                            required property int modelData
                            readonly property bool current: modelData === window.selected
                            width: 14; height: 40
                            Rectangle {
                                x: 6; width: 2
                                y: parent.current ? 3 : tickArea.containsMouse ? 8 : 14
                                height: parent.current ? 33 : tickArea.containsMouse ? 24 : 12
                                color: parent.current ? Theme.ink : tickArea.containsMouse ? Theme.accent : Theme.line
                                Behavior on height { NumberAnimation { duration: 400 } } Behavior on y { NumberAnimation { duration: 400 } } Behavior on color { ColorAnimation { duration: 400 } }
                            }
                            MouseArea { id: tickArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.field.selectIndex(modelData) }
                            Accessible.role: Accessible.Button; Accessible.name: "选择入口 " + Data.records[modelData].id + " " + Data.records[modelData].title
                        }
                    }
                }
                RButton { width: 46; height: 46; text: "  ↓"; pixelSize: 28; Accessible.name: "下一个入口"; onClicked: window.field.stepRow(1) }
            }
            Row { // .column-navigation
                x: 1005; anchors { bottom: parent.bottom; bottomMargin: 131 }
                visible: archiveUi.live
                spacing: 24
                RButton { width: 46; height: 46; text: " ←"; pixelSize: 24; Accessible.name: "上一列"; onClicked: window.field.stepColumn(-1) }
                Column {
                    anchors.verticalCenter: parent.verticalCenter; spacing: 6
                    Row {
                        Text { text: "COLUMN "; color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1.3 } }
                        Rolling { text: window.pad(window.record.lane + 1, 2); numeric: true; direction: window.navDirection; animated: prefs.on("rollingNumbers"); color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1.3 } }
                        Text { text: " / 05"; color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1.3 } }
                    }
                    Rolling { text: window.record.category; direction: window.navDirection; animated: prefs.on("rollingText"); color: Theme.ink; font { family: boldFont; pixelSize: 18 } }
                }
                RButton { width: 46; height: 46; text: " →"; pixelSize: 24; Accessible.name: "下一列"; onClicked: window.field.stepColumn(1) }
            }
            // .archive-hint (left 505, bottom 60)
            Row {
                x: 505; anchors { bottom: parent.bottom; bottomMargin: 60 }
                visible: archiveUi.live
                spacing: 6
                component H: Text { color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 0.9 } }
                H { text: "← →  切换列" } H { text: "　／　" } H { text: "↑ ↓  前后入口" } H { text: "　／　" } H { text: "ENTER  打开" } H { text: "　／　" } H { text: "拖动阵列浏览" }
            }
        }

        // Detail overlay (.detail-ui)
        Item {
            anchors.fill: parent
            visible: opacity > 0
            opacity: window.mode === "detail" ? 1 : 0
            // detailTransition = SurfaceTransition(#detail-ui, 180, 180)
            Behavior on opacity { enabled: prefs.on("surfaceTransitions"); NumberAnimation { duration: 180; easing.type: Easing.BezierSpline; easing.bezierCurve: window.mode === "detail" ? [0.22, 1, 0.36, 1, 1, 1] : [0.4, 0, 1, 1, 1, 1] } }
            enabled: window.mode === "detail"
            Item { // .back-button
                id: backButton
                parent: window.contentItem
                z: 9; x: 24; y: window.modelAreaTop - 48; width: backRow.width; height: 36
                visible: window.mode === "detail"
                activeFocusOnTab: true; Keys.onReturnPressed: window.back()
                Accessible.role: Accessible.Button; Accessible.name: "返回入口总览"
                Row {
                    id: backRow; spacing: 20; anchors.verticalCenter: parent.verticalCenter
                    Text { text: "←"; color: backArea.containsMouse ? Theme.accent : Theme.ink; font { family: uiFont; pixelSize: 24 } }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "返回阵列"; color: backArea.containsMouse ? Theme.accent : Theme.ink; font { family: uiFont; pixelSize: 12; letterSpacing: 1 } }
                    KeyCap { anchors.verticalCenter: parent.verticalCenter; text: "↗"; pixelSize: 10 }
                }
                MouseArea { id: backArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.back() }
            }
            Column { // .object-caption
                parent: window.contentItem
                z: 9; x: 24; y: window.height - 155
                visible: window.mode === "detail"
                spacing: 7
                Text { text: "NO." + window.pad(window.selected + 1, 3); color: Theme.ink; font { family: uiFont; pixelSize: 24; letterSpacing: -1 } }
                Text { text: "PRIVATE COMMUNICATION"; color: Theme.muted; font { family: uiFont; pixelSize: 9; letterSpacing: 0.8 } }
                Text { text: "DRAG TO INSPECT   ↔"; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 1.2 } }
                RButton { parent: window.contentItem; objectName: "rotateModel"; z: 9; x: 24; y: window.height - 74; height: 36; visible: window.mode === "detail"; text: "360° 旋转查看档案   ↗"; pixelSize: 12; enabled: window.field.canInspect; onClicked: window.field.spin() }
            }
            Workspace {
                id: workspace
                objectName: "glassWorkspace"
                parent: window.contentItem
                z: 8
                x: window.width - width - 24
                y: locked ? Math.max(108, (window.height - height) / 2) : 108
                width: locked ? Math.min(440, window.width - 48) : Math.max(600, Math.min(1120, window.width * 0.64))
                height: locked ? Math.min(470, window.height - 146) : window.height - y - 38
                visible: window.mode === "detail"
                opacity: window.field.detailVisibility
                enabled: opacity > 0.1 && window.navigationPhase !== "closing"
                store: store; prefs: prefs; terminal: window; sceneItem: scene
                index: window.selected; tab: window.activeTab
                clarity: scene.decryption.clarity; active: window.mode === "detail"
                onTabRequested: target => window.activeTab = target
            }
        }

        // .system-footer (bottom 35)
        Item {
            x: 59; width: parent.width - 118; height: 14
            anchors { bottom: parent.bottom; bottomMargin: 35 }
            visible: opacity > 0
            opacity: window.mode === "archive" || window.mode === "detail" ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 800 } }
            Row {
                spacing: 10
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 5; height: 5; color: store.phase === 6 ? Theme.mix("#777b60", "#c5a16b") : Theme.muted }
                Text { text: store.status; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 0.9 } }
            }
            Row {
                anchors.right: parent.right; spacing: 44
                Row {
                    Text { text: (store.username || "尚未登录") + "   ／   "; color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 0.9 } }
                    Rolling { id: clock; numeric: true; direction: 1; animated: prefs.on("rollingNumbers"); text: Qt.formatTime(new Date(), "HH:mm:ss"); color: Theme.muted; font { family: uiFont; pixelSize: 10; letterSpacing: 0.9 } }
                    Timer { interval: 1000; running: true; repeat: true; onTriggered: clock.text = Qt.formatTime(new Date(), "HH:mm:ss") }
                }
                RButton { y: -4; text: "REINITIALIZE ↗"; pixelSize: 10; letterSpacing: 0.9; ink: Theme.muted; onClicked: window.replay() }
            }
        }

        // Startup gate (startup.ts) and loading mark.
        Rectangle {
            anchors.fill: parent
            visible: window.mode === "gate"
            color: Theme.paper
            Column {
                anchors.centerIn: parent; spacing: 0
                Logo { anchors.horizontalCenter: parent.horizontalCenter; width: 180; height: 107 }
                Item { width: 1; height: 58 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: window.gateState === "loading" ? "CONNECTING TO PRIVATE COMMUNICATION" : "CIPHERCHAT TERMINAL / READY"; color: Theme.muted; font { family: uiFont; pixelSize: 15; letterSpacing: 2 } }
                Item { width: 1; height: 22 }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter; width: 216; height: 1; color: Theme.line
                    Rectangle { height: 1; color: Theme.ink; width: window.gateState === "loading" ? parent.width * loadBar.value : parent.width }
                    QtObject { id: loadBar; property real value: 0; NumberAnimation on value { from: 0; to: 1; duration: 900 } }
                }
                Item { width: 1; height: 38 }
                Item {
                    id: entryButton
                    anchors.horizontalCenter: parent.horizontalCenter; width: 174; height: 58
                    opacity: window.gateState === "waiting" ? 1 : 0.35
                    activeFocusOnTab: true; focus: window.gateState === "waiting"
                    Keys.onReturnPressed: window.enter(false); Keys.onSpacePressed: window.enter(false)
                    Accessible.role: Accessible.Button; Accessible.name: "进入 CipherChat 通讯终端"
                    Rectangle { width: parent.width; height: 1; color: Theme.muted }
                    Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.muted }
                    Text { anchors.centerIn: parent; text: window.gateState === "loading" ? "正在准备终端…" : "点击进入 →"; color: Theme.ink; font { family: uiFont; pixelSize: 20; letterSpacing: 4 } }
                }
                Item { width: 1; height: 22 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: window.gateState === "loading" ? "资源就绪后即可进入" : "轻触屏幕或按 Enter 开始"; color: Theme.muted; font { family: uiFont; pixelSize: 15 } }
                Item { width: 1; height: 14 }
                RButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: window.gateState === "waiting" && (prefs.sound || prefs.music)
                    text: "关闭声音并进入"; pixelSize: 13; ink: Theme.muted
                    onClicked: window.enter(true)
                }
            }
            MouseArea { anchors.fill: parent; z: -1; cursorShape: window.gateState === "waiting" ? Qt.PointingHandCursor : Qt.BusyCursor; onClicked: if (window.gateState === "waiting") window.enter(false) }
        }

        TerminalModal {
            parent: window.contentItem
            z: 50
            objectName: "terminalModal"
            kind: window.modal
            pinned: window.pinned
            prefs: prefs
            store: store
            backdropSource: scene
            onChosen: index => { audio.play("confirm"); window.openIndex(index) }
            onClosed: window.closeModal()
            onReplay: window.replay()
            onNotify: text => toast.show(text)
            focus: window.modal !== ""
        }

        Rectangle { // toast
            id: toast
            parent: window.contentItem
            z: 60
            function show(text) { message.text = text; opacity = 1; hide.restart() }
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 80 }
            width: Math.min(window.width - 48, message.implicitWidth + 48); height: message.implicitHeight + 28; color: Theme.solid; opacity: 0; visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 250 } }
            Text { id: message; width: parent.width - 48; wrapMode: Text.Wrap; anchors.centerIn: parent; color: Theme.solidInk; font { family: uiFont; pixelSize: 13 } }
            Timer { id: hide; interval: 3600; onTriggered: toast.opacity = 0 }
        }
    }
    Shortcut { sequence: "Esc"; onActivated: window.dismissTopLayer() }
    Shortcut { sequence: "Ctrl+K"; enabled: store.sessionAvailable && !window.modal && window.mode === "detail"; onActivated: workspace.searchSessions() }
    Shortcut { sequence: "Ctrl+1"; enabled: store.sessionAvailable && !window.modal && !window.booting; onActivated: window.navigateModule("chat") }
    Shortcut { sequence: "Ctrl+2"; enabled: store.sessionAvailable && !window.modal && !window.booting; onActivated: window.navigateModule("contacts") }
    Shortcut { sequence: "Ctrl+3"; enabled: store.sessionAvailable && !window.modal && !window.booting; onActivated: window.navigateModule("groups") }
    Shortcut { sequence: "Ctrl+4"; enabled: store.sessionAvailable && !window.modal && !window.booting; onActivated: window.navigateModule("files") }
    Shortcut { sequence: "Ctrl+5"; enabled: !window.modal && !window.booting; onActivated: window.navigateModule("account") }

    // Frozen film frame (--frame T --capture DIR): renders the opening at T, like the web ?time=T&freeze=1.
    Timer {
        running: freezeAt >= 0 && captureDir !== ""
        interval: 800
        property int phase: 0
        repeat: true
        onTriggered: {
            if (phase === 0) { window.gateState = "waiting"; window.mode = "boot"; window.field.setMode("hidden"); window.bootTime = Math.max(0, freezeAt - 0.6); phase = 1; return }
            if (phase < 5) { phase++; return }
            running = false
            window.contentItem.grabToImage(r => { r.saveToFile(captureDir.replace("file:///", "") + "/qt-" + freezeAt.toFixed(1) + ".png"); Qt.quit() })
        }
    }
    // Acceptance capture (--capture DIR): drives the real UI and saves frames.
    Timer {
        id: capture
        property int step: 0
        property var quick: [[1.6, "q1-gate", () => window.enter(true)], [0.2, "q1b", () => window.finishBoot("archive")],
            [4, "q2-archive", () => window.open()], [7, "q3-detail", () => { prefs.dark = true }], [3, "q4-dark-detail", () => window.back()],
            [3, "q5-dark-archive", () => { prefs.dark = false; window.showModal("settings") }], [1.5, "q6-settings", () => Qt.quit()]]
        property var script: captureDir.indexOf("quick") >= 0 ? quick : [
            [1.6, "01-gate", () => window.enter(true)],
            [4, "02-boot-access", null], [6, "03-boot-logo", null], [5.6, "04-boot-scan", null], [4.5, "05-boot-welcome", null],
            [2.4, "06-film-array", null], [4, "07-film-select", null], [9, "08-login-detail", () => window.back()],
            [3, "09-archive", () => window.field.stepColumn(-1)], [3, "10-column-chat", () => window.open()],
            [5, "11-chat-detail", () => window.activeTab = "history"], [1.5, "12-chat-attachments", () => window.showModal("search")],
            [1.2, "13-index", () => window.showModal("settings")], [1.2, "14-settings", () => Qt.quit()]
        ]
        running: captureDir !== "" && freezeAt < 0
        interval: 1500
        onTriggered: {
            const [, name, action] = script[step]
            window.contentItem.grabToImage(result => {
                if (!name.endsWith("b")) result.saveToFile(captureDir.replace("file:///", "") + "/" + name + ".png")
                if (action) action()
                step++
                if (step < script.length) { interval = script[step][0] * 1000; start() }
            })
        }
        repeat: false
    }
}
