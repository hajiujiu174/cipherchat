import QtQuick
import QtQuick.Shapes
import "."
import "BootMotion.mjs" as Motion

// inspection-overlay.ts: the decryption scan drawn on the cassette face. Line
// segments, corner markers and the centre point follow the projected card; the
// "CONFIDENTIALITY" caption appears only during the opening film.
Item {
    id: overlay
    property var scene                   // Scene.qml (projectCard, decryption)
    property var frame: Motion.decryptionFrame(-1)
    property bool active: true
    property bool showLabel: false
    property real stageUnit: 1
    property real tick: 0                // re-evaluate projections every frame
    visible: active && (frame.intervals.length > 0 || frame.markers > 0 || frame.point > 0 || (showLabel && frame.label > 0))

    function project(x, y) { void tick; return scene ? scene.projectCard(x, y) : Qt.point(0, 0) }
    function along(t) {
        return [Motion.SCAN_FROM[0] + (Motion.SCAN_TO[0] - Motion.SCAN_FROM[0]) * t,
                Motion.SCAN_FROM[1] + (Motion.SCAN_TO[1] - Motion.SCAN_FROM[1]) * t]
    }
    readonly property string linePath: {
        void tick
        let d = ""
        for (const [a, b] of frame.intervals) {
            const p = along(a), q = along(b)
            const pa = project(p[0], p[1]), pb = project(q[0], q[1])
            d += "M" + pa.x + " " + pa.y + "L" + pb.x + " " + pb.y
        }
        return d
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath { strokeColor: Theme.mix("#24221f", "#e0e3dc"); strokeWidth: 2; fillColor: "transparent"; PathSvg { path: overlay.linePath } }
    }
    Repeater {
        model: Motion.SCAN_CORNERS.length
        delegate: Rectangle {
            required property int index
            readonly property point p: overlay.project(Motion.SCAN_CORNERS[index][0], Motion.SCAN_CORNERS[index][1])
            x: p.x - 4; y: p.y - 4; width: 8; height: 8
            color: Theme.mix("#24221f", "#e0e3dc"); opacity: overlay.frame.markers
            visible: overlay.frame.markers > 0
        }
    }
    Rectangle {
        readonly property var c: overlay.along(0.5)
        readonly property point p: overlay.project(c[0], c[1])
        x: p.x - 1.8; y: p.y - 1.8; width: 3.6; height: 3.6; radius: 1.8
        color: Theme.mix("#24221f", "#e0e3dc"); opacity: overlay.frame.point
    }
    // #inspection-text (left 1202, top 499 on the reference stage).
    Column {
        x: 1202 * overlay.stageUnit; y: 499 * overlay.stageUnit
        scale: overlay.stageUnit; transformOrigin: Item.TopLeft
        spacing: 16
        opacity: overlay.showLabel ? overlay.frame.label : 0
        Text { text: "CONFIDENTIALITY:"; color: Theme.ink; font { family: boldFont; pixelSize: 23 } }
        Text { text: "GENERAL BUSINESS USE"; color: Theme.ink; opacity: overlay.frame.labelValue; font { family: boldFont; pixelSize: 30 } }
    }
}
