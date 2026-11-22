pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils
import "Motion.js" as Motion

Rectangle {
    id: root
    objectName: "rhineSceneRoot"
    color: darkMode ? "#222420" : "#e8e5e1"
    property bool darkMode: false
    property color accent: "#b66832"
    property bool sceneActive: false
    property bool motionEnabled: true
    property bool inspecting: false
    property int selectedRow: 7
    property int renderedTicks: 0
    property real elapsed: 0
    property real waveAge: 4
    property real lift: 100
    property real velocity: 0
    property real yaw: 0
    property real pitch: 0
    readonly property bool modelLoaded: archive.status === RuntimeLoader.Success
    readonly property string modelError: archive.errorString
    onSelectedRowChanged: waveAge = 0
    onInspectingChanged: {
        yaw = 0; pitch = 0
        if (!motionEnabled) { lift = inspecting ? 225 : 100; velocity = 0 }
    }
    onMotionEnabledChanged: {
        if (!motionEnabled) { lift = inspecting ? 225 : 100; velocity = 0; waveAge = 4 }
    }
    Timer {
        interval: 33
        repeat: true
        running: root.sceneActive && root.motionEnabled
        onTriggered: {
            root.elapsed += 0.033
            root.waveAge += 0.033
            var step = Motion.damp(root.lift, root.velocity, root.inspecting ? 225 : 100, 4.2, 0.033)
            root.lift = step[0]; root.velocity = step[1]
            root.renderedTicks++
        }
    }
    View3D {
        id: viewport
        anchors.fill: parent
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: root.color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            tonemapMode: SceneEnvironment.TonemapModeLinear
        }
        Node {
            y: root.inspecting ? 100 : 0
            eulerRotation: root.inspecting ? Qt.vector3d(-12, 12, 0) : Qt.vector3d(-28, 30, 0)
            PerspectiveCamera {
                id: camera
                z: root.inspecting ? 670 : 1180
                fieldOfView: 34
                clipNear: 1
                clipFar: 4000
            }
        }
        camera: camera
        DirectionalLight {
            eulerRotation: Qt.vector3d(-40, -30, 0)
            color: "#fff7ed"
            ambientColor: root.darkMode ? "#5b6257" : "#a9a49d"
            brightness: root.darkMode ? 0.75 : 1.1
        }
        DirectionalLight { eulerRotation: Qt.vector3d(-20, 145, 0); brightness: 0.5; color: "#ffffff" }
        // Simplified background instances keep the original column/row spacing
        // without rendering 17 detailed meshes for every background cassette.
        PrincipledMaterial { id: edge; baseColor: root.darkMode ? "#51574b" : "#e7dfd3"; roughness: 0.62 }
        PrincipledMaterial { id: face; baseColor: root.darkMode ? "#40463c" : "#f0ece4"; roughness: 0.74 }
        Repeater3D {
            model: 75
            delegate: Node {
                required property int index
                property int lane: Math.floor(index / 15) - 2
                property int row: index % 15
                visible: !root.inspecting && !(lane === 0 && row === root.selectedRow)
                x: lane * 312
                z: (row - 7) * 37.2
                y: -155 + 30 * Math.exp(-Math.pow((row - root.selectedRow) / 3, 2))
                    + 50 * Motion.wave(Math.abs(row - root.selectedRow) + Math.abs(lane) * 1.2, root.waveAge)
                    + (root.motionEnabled ? 60 * Motion.idle(row, lane, root.elapsed) : 0)
                Model { source: "#Cube"; scale: Qt.vector3d(2.16, 2.16, 0.186); materials: [edge] }
                Model { source: "#Cube"; z: 10; scale: Qt.vector3d(2.08, 2.06, 0.01); materials: [face] }
            }
        }
        Node {
            y: -240 + root.lift
            z: root.inspecting ? 0 : (root.selectedRow - 7) * 37.2
            eulerRotation: Qt.vector3d(root.pitch, root.yaw, 0)
            RuntimeLoader {
                id: archive
                objectName: "rhineArchiveModel"
                source: "qrc:/rhine/archive-cassette.glb"
                scale: Qt.vector3d(60, 60, 60)
            }
        }
    }
    // Fade the finite array into the same warm neutral as the upstream stage.
    Rectangle {
        visible: !root.inspecting
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: parent.height * 0.22
        gradient: Gradient {
            GradientStop { position: 0; color: root.color }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    Rectangle {
        visible: !root.inspecting
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: parent.height * 0.20
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: root.color }
        }
    }
    Rectangle { x: 0; y: parent.height / 2; width: 4; height: 24; color: root.accent }
    MouseArea {
        anchors.fill: parent
        property real lastX: 0
        property real lastY: 0
        cursorShape: root.inspecting ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor
        onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: function(mouse) {
            if (pressed && root.inspecting) {
                root.yaw = Math.max(-65, Math.min(65, root.yaw + (mouse.x - lastX) * 0.35))
                root.pitch = Math.max(-30, Math.min(30, root.pitch + (mouse.y - lastY) * 0.25))
                lastX = mouse.x; lastY = mouse.y
            }
        }
        onWheel: function(wheel) {
            if (!root.inspecting) root.selectedRow = (root.selectedRow + (wheel.angleDelta.y > 0 ? 14 : 1)) % 15
        }
    }
}
