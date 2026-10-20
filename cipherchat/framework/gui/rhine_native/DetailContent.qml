import QtQuick
import "."
import "Records.js" as Data

// .detail-content (left 1154, top 289, width 636) from main.ts renderDetail(),
// with the document-decryption redaction lifting once the cassette is raised.
Item {
    id: detail
    property var store
    property int index: 0
    property string tab: "overview"
    property bool active: false          // detail mode is showing this document
    property real decryptionClarity: 0   // cover glass clarity from the scene
    property bool revealEnabled: true    // DOCUMENT REVEAL motion setting
    property bool transitions: true      // SURFACE TRANSITIONS
    property bool externalPanel: false
    property real progress: 1
    property bool started: false
    readonly property var record: Data.records[index]
    width: 636; height: content.height

    // document-decryption.ts: covered through the scan; the text opens with the
    // glass (0.95 s), each line window departing in reading order.
    function resetReveal() {
        reveal.stop(); started = false
        progress = revealEnabled && decryptionClarity < 1 ? 0 : 1
    }
    onIndexChanged: resetReveal()
    onActiveChanged: if (active) resetReveal()
    onDecryptionClarityChanged: if (active && !started && decryptionClarity > 0 && progress < 1) { started = true; reveal.restart() }
    NumberAnimation { id: reveal; target: detail; property: "progress"; to: 1; duration: 950 }
    readonly property int coverCount: 6
    component Redact: Item {
        property Item target
        property int order: 0
        readonly property real t: Math.max(0, Math.min(1, (detail.progress - order / (detail.coverCount - 1) * 0.22) / 0.78))
        // Brief acceleration, decisive departure, long deceleration; no bounce.
        readonly property real eased: t < 0.2 ? 0.4 * Math.pow(t / 0.2, 2) : 1 - 0.6 * Math.pow((1 - t) / 0.8, 16 / 3)
        parent: target; z: 2
        x: -1; y: -1
        width: target ? Math.min(target.width, target.contentWidth ?? target.width) + 2 : 0
        height: target ? target.height + 2 : 0
        clip: true
        visible: detail.progress < 1
        Rectangle { width: parent.width; height: parent.height; x: parent.width * 1.01 * parent.eased; color: Theme.ink }
    }

    Column {
        id: content
        width: parent.width
        Item {
            width: parent.width; height: 14
            Text { text: "FILE " + detail.record.id; color: Theme.ink; font { family: boldFont; pixelSize: 11; letterSpacing: 1.4 } }
            Text { anchors.right: parent.right; text: detail.record.clearance; color: Theme.muted; font { family: uiFont; pixelSize: 9; letterSpacing: 1.4 } }
        }
        Item { width: 1; height: 30 }
        Text { id: heading; width: 620; wrapMode: Text.Wrap; text: detail.record.en; color: Theme.ink; lineHeight: 1.12; font { family: boldFont; pixelSize: 40; letterSpacing: -1.3 } }
        Redact { target: heading; order: 0 }
        Item { width: 1; height: 13 }
        Row {
            spacing: 23
            Text { id: cn; text: detail.record.title; color: Theme.ink; font { family: uiFont; pixelSize: 23 } }
            Text { anchors.verticalCenter: cn.verticalCenter; text: detail.record.category; color: Theme.muted; font { family: uiFont; pixelSize: 11; letterSpacing: 1 } }
        }
        Redact { target: cn; order: 1 }
        Item { width: 1; height: 28 }
        Rectangle { width: parent.width; height: 2; color: Theme.ink }
        Item { width: 1; height: 28 }
        Grid {
            columns: 2; columnSpacing: 42; rowSpacing: 25
            Repeater {
                model: [["MODULE / 模块", detail.record.category], ["IDENTITY / 账号", (void store.revision, store.username || "尚未登录")],
                        ["CHANNEL / 通道", "TLS · END-TO-END"], ["SESSION / 状态", store.status]]
                delegate: Column {
                    required property var modelData
                    required property int index
                    width: (636 - 42) / 2; spacing: 8
                    Text { text: modelData[0]; color: Theme.muted; font { family: uiFont; pixelSize: 9; letterSpacing: 0.7 } }
                    Text { id: value; text: modelData[1]; color: Theme.ink; font { family: uiFont; pixelSize: 14 } }
                    Redact { target: value; order: 2 + index }
                }
            }
        }
        Item { width: 1; height: 34 }
        Item { // .detail-tabs with sliding indicator
            width: parent.width; height: 28
            Row {
                id: tabs; spacing: 34
                Repeater {
                    model: 3
                    delegate: Item {
                        required property int index
                        readonly property bool active: Data.tabs[index] === detail.tab
                        width: label.x + label.width; height: 28
                        activeFocusOnTab: true
                        Accessible.role: Accessible.PageTab; Accessible.name: label.text
                        Keys.onReturnPressed: detail.tab = Data.tabs[index]
                        Text { id: num; text: "0" + (index + 1); color: parent.active ? Theme.ink : Theme.muted; font { family: uiFont; pixelSize: 10 } }
                        Text { id: label; x: num.width + 8; y: -3; text: Data.tabLabels(detail.record.module)[index]; color: parent.active || area.containsMouse ? Theme.ink : Theme.muted; font { family: uiFont; pixelSize: 15 } }
                        MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: detail.tab = Data.tabs[index] }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            Rectangle {
                readonly property Item current: tabs.children[Data.tabs.indexOf(detail.tab)] ?? null
                anchors.bottom: parent.bottom; height: 2; color: Theme.ink
                x: current ? current.x : 0; width: current ? current.width : 0
                Behavior on x { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Behavior on width { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
            }
        }
        ChatPanel {
            visible: !detail.externalPanel
            height: detail.externalPanel ? 0 : 211
            id: panel
            store: detail.store; module: detail.record.module; tab: detail.tab
            // ContentTransition: the new tab content fades in.
            onTabChanged: if (detail.transitions) tabIn.restart()
            // ContentTransition.reveal: 0.35 → 1 over 150 ms.
            NumberAnimation { id: tabIn; target: panel; property: "opacity"; from: 0.35; to: 1; duration: 150; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.22, 1, 0.36, 1, 1, 1] }
        }
        Item { width: 1; height: 19 }
        Item { width: 1; height: 22 }
        Item {
            width: parent.width; height: 10
            Text { text: "界面来源 RhineLabUI ↗"; color: Theme.faint; font { family: uiFont; pixelSize: 8; letterSpacing: 0.6 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally("https://github.com/LBEILC/RhineLabUI") } }
            Text { anchors.right: parent.right; text: String(detail.index + 1).padStart(3, "0") + " / " + String(Data.records.length).padStart(3, "0"); color: Theme.faint; font { family: uiFont; pixelSize: 8; letterSpacing: 0.6 } }
        }
    }
}
