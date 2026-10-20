import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import "."

ListView {
    id: queue
    property var store
    property string peer: ""
    property bool currentOnly: false
    clip: true; spacing: 6; boundsBehavior: Flickable.StopAtBounds
    model: currentOnly ? store.filesFor(peer) : (void store.revision, store.files)
    Controls.ScrollBar.vertical: Controls.ScrollBar {}
    delegate: Item {
        required property var modelData
        width: queue.width - 10; height: body.implicitHeight + 24
        Column {
            id: body; x: 8; y: 10; width: parent.width - 16; spacing: 8
            Text { width: parent.width; text: modelData.name; elide: Text.ElideMiddle; color: Theme.ink; font { family: uiFont; pixelSize: 14 } }
            Text { width: parent.width; text: (modelData.outgoing ? "发送给 " : "来自 ") + modelData.peer + " · " + (modelData.cancelled ? "已取消" : modelData.complete ? "验证完成" : modelData.cacheUploaded ? "已缓存，等待接收" : "传输中"); elide: Text.ElideRight; color: Theme.muted; font { family: uiFont; pixelSize: 12 } }
            Rectangle { width: parent.width; height: 3; color: Theme.line; Rectangle { height: 3; width: parent.width * Math.min(1, modelData.authenticatedBytes / Math.max(1, modelData.totalBytes)); color: Theme.accent } }
            Text { text: store.bytes(modelData.authenticatedBytes) + " / " + store.bytes(modelData.totalBytes); color: Theme.muted; font { family: uiFont; pixelSize: 11 } }
            Flow {
                width: parent.width; spacing: 8
                RButton { visible: modelData.complete && !modelData.outgoing; outlined: true; text: "另存为"; onClicked: { saver.source = modelData.path; saver.selectedFile = "file:///" + modelData.name; saver.open(); } }
                RButton { visible: modelData.complete && !modelData.outgoing; outlined: true; text: "打开文件夹"; onClicked: chat.reveal(modelData.path) }
                RButton { visible: !modelData.cancelled && (!modelData.complete || (!modelData.outgoing && modelData.transportPath === 4)); outlined: true; text: modelData.complete ? "重新下载" : "恢复"; onClicked: store.resumeFile(modelData.fileId) }
                RButton { visible: !modelData.complete && !modelData.cancelled; outlined: true; text: "取消"; onClicked: store.cancelFile(modelData.fileId) }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
    }
    Text { visible: queue.count === 0; anchors.centerIn: parent; text: "暂无传输记录"; color: Theme.muted; font { family: uiFont; pixelSize: 13 } }
    FileDialog { id: saver; property string source: ""; fileMode: FileDialog.SaveFile; title: "保存已验证文件"; onAccepted: { const error = chat.saveCopy(source, selectedFile); store.notify(error || "文件已保存"); } }
}
