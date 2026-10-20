import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import Rhine
import "."
import "BootMotion.mjs" as Motion

// Qt Quick 3D counterpart of scene.ts: warm fog, image-based room light, the
// instanced array (shell, edges, fasteners, inlay, diffuser), the extracted
// cassette and its descending copies; pointer input follows bindPointer().
View3D {
    id: view
    property alias field: archive
    property bool interactive: true
    property real quality: 1            // render quality 0.5–1 (SUPER PERFORMANCE uses 0.5)
    property var motion: ({})
    property real cinematicTime: -1     // opening film time while it drives the camera
    property bool detailMode: false
    signal openRequested()
    renderMode: View3D.Offscreen

    ArchiveField { id: archive; viewAspect: view.width / Math.max(1, view.height); motion: view.motion }
    FrameAnimation {
        running: view.visible
        onTriggered: {
            archive.advance(frameTime); camera.lookAt(archive.cameraAim); Theme.amount = archive.themeAmount
            view.stepDecryption(Math.min(frameTime, 0.05))
            view.tick++
        }
    }

    readonly property color paper: Theme.paper
    function t(key, fallback) { return tune[key] !== undefined ? tune[key] : fallback }
    environment: ExtendedSceneEnvironment {
        backgroundMode: SceneEnvironment.Color
        clearColor: view.paper
        antialiasingMode: view.quality < 0.75 ? SceneEnvironment.NoAA : SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.High
        tonemapMode: SceneEnvironment.TonemapModeAces
        exposure: view.t("exposure", 0.86) - 0.05 * archive.themeAmount
        // RoomEnvironment stand-in: a soft procedural sky drives reflections and
        // the diffuse fill the web scene gets from its PMREM environment.
        lightProbe: Texture {
            textureData: ProceduralSkyTextureData {
                skyTopColor: "#f6f1ea"; skyHorizonColor: "#e9e2d9"; skyCurve: 0.6; skyEnergy: 1.0
                groundBottomColor: "#b4a18c"; groundHorizonColor: "#ddd4c8"; groundCurve: 0.4; groundEnergy: 0.9
                sunColor: "#fff7ed"; sunLatitude: 55; sunLongitude: 220; sunAngleMin: 1; sunAngleMax: 60; sunCurve: 0.2; sunEnergy: 1.0
                textureQuality: ProceduralSkyTextureData.SkyTextureQualityMedium
            }
        }
        probeExposure: view.t("probe", 0.9) * (1 - 0.33 * archive.themeAmount)
        aoEnabled: view.quality >= 0.75
        aoStrength: view.t("ao", 35); aoDistance: view.t("aoDistance", 0.22); aoSoftness: 50; aoSampleRate: 3; aoDither: true
        depthOfFieldEnabled: view.quality >= 0.75
        depthOfFieldFocusDistance: archive.focusDistance
        depthOfFieldFocusRange: view.t("dofRange", 6) + 36 * archive.detailAmount
        depthOfFieldBlurAmount: view.t("dofBlur", 4.0) - 2.2 * archive.detailAmount
        fog: Fog { enabled: true; color: view.paper; depthEnabled: true; depthNear: archive.fogNear; depthFar: archive.fogFar; depthCurve: 1 }
    }
    PerspectiveCamera {
        id: camera
        position: archive.cameraPosition
        fieldOfView: archive.fieldOfView
        clipNear: 5; clipFar: 300
    }
    camera: camera
    // archive-lighting.ts baseline: key #fff7ed 1.4 from (-6, 14, -5), fill 0.6 from (7, 8, -10).
    DirectionalLight {
        eulerRotation: Qt.vector3d(-62, -130, 0)
        color: "#fff4e8"; brightness: view.t("key", 1.15) * (1 - 0.35 * archive.themeAmount)
        ambientColor: Theme.mix(view.t("ambient", "#b5a999"), "#1c2427")
        castsShadow: true; shadowMapQuality: view.quality < 0.75 ? Light.ShadowMapQualityMedium : Light.ShadowMapQualityHigh
        shadowFactor: view.t("shadow", 28); shadowMapFar: 80; pcfFactor: 4
        csmNumSplits: 2
    }
    DirectionalLight { eulerRotation: Qt.vector3d(-36, 145, 0); color: "#ffffff"; brightness: view.t("fill", 0.6) * (1 - 0.35 * archive.themeAmount) }
    // Hemisphere ground bounce (#b4a18c).
    DirectionalLight { eulerRotation: Qt.vector3d(90, 0, 0); color: "#d8c9b4"; brightness: view.t("ground", 0.45) * (1 - 0.35 * archive.themeAmount) }

    Model {
        source: "#Rectangle"; y: -4.63; eulerRotation.x: -90; scale: Qt.vector3d(2, 2, 1)
        receivesShadows: true
        materials: PrincipledMaterial { baseColor: Theme.mix("#d8c9b9", "#192125"); roughness: 0.95 }
    }

    // Array surfaces keep only the shell, edges, fasteners, inlay and diffuser.
    ShellMaterial { id: arrayShell; instanced: true; quality: 0; tint: view.t("tint", 0.2); transmission: view.t("transmission", 0.78) }
    ArrayMaterial { id: arrayEdge; light: "#fff5e9"; dark: "#687277"; roughness: 0.38 }
    ArrayMaterial { id: arrayDiffuser; light: "#806447"; dark: "#192226"; roughness: 0.7 }
    ArrayMaterial { id: arrayInlay; light: "#e4d6c5"; dark: "#c6a36b"; roughness: 0.52; metalness: 0.05 }
    ArrayMaterial { id: arrayTitanium; light: "#c8cbcd"; dark: "#b1b9bb"; roughness: 0.19; metalness: 0.82 }
    Node {
        id: array
        Model { id: shell; source: "meshes/cube_5328_mesh.mesh"; instancing: archive; materials: [arrayShell]; pickable: true; receivesShadows: true }
        Model { source: "meshes/cube_5329_mesh.mesh"; instancing: archive; materials: [arrayEdge]; receivesShadows: true }
        Model { source: "meshes/cube_5330_mesh.mesh"; instancing: archive; materials: [arrayDiffuser]; castsShadows: true }
        Model { source: "meshes/cube_5538_mesh.mesh"; instancing: archive; materials: [arrayInlay] }
        Model { source: "meshes/cylinder_101_mesh.mesh"; instancing: archive; materials: [arrayTitanium] }
    }
    Cassette {
        id: selectedCassette
        position: archive.modelPosition
        eulerRotation: archive.modelRotation
        label: archive.selectedIndex
        quality: archive.modelQuality
        clarity: decryption.clarity
        theme: archive.selectedTheme
    }
    Repeater3D {
        id: copies
        model: 4
        delegate: Cassette {
            required property int index
            property var slot: (void archive.cameraPosition, archive.outgoingAt(index))
            visible: slot.visible
            position: slot.visible ? slot.position : Qt.vector3d(0, -100, 0)
            eulerRotation: slot.visible ? slot.rotation : Qt.vector3d(0, 0, 0)
            label: slot.visible ? slot.label : 0
            quality: slot.visible ? slot.quality : 0
            theme: (void archive.cameraPosition, archive.outgoingTheme(index))
        }
    }

    property real tick: 0
    // Decryption timeline (decryption.ts DecryptionController): the scan line,
    // then the cover glass clears from the top; interactive runs at 1.5×.
    property alias decryption: decryption
    QtObject {
        id: decryption
        property var frame: Motion.decryptionFrame(-1)
        property real clarity: 0
        property real elapsed: -1
        property bool active: false
    }
    readonly property real decryptionSpan: (Motion.DECRYPTION_END - Motion.DECRYPTION_START) / 1.5
    Connections {
        target: archive
        function onSelectionChanged() { decryption.active = false; decryption.elapsed = -1; decryption.clarity = 0; decryption.frame = Motion.decryptionFrame(-1) }
    }
    function stepDecryption(dt) {
        const d = decryption
        if (cinematicTime >= 0) { d.frame = Motion.decryptionFrame(cinematicTime + 5); d.clarity = d.frame.clarity; d.active = false; return }
        const enabled = motion.modelDecryption !== false
        if (!detailMode) {
            d.active = false; d.elapsed = -1
            d.clarity = enabled ? d.clarity * Math.exp(-dt * 9) : 0
            if (d.clarity < 0.0001) d.clarity = 0
            d.frame = Motion.decryptionFrame(-1)
            return
        }
        if (!d.active) { d.active = true; d.elapsed = d.clarity > 0.999 ? decryptionSpan : -1 }
        if (!enabled) { d.elapsed = decryptionSpan; d.clarity = 1; d.frame = Motion.decryptionFrame(Motion.DECRYPTION_END); return }
        if (d.elapsed < 0 && archive.documentReady) d.elapsed = 0
        else if (d.elapsed >= 0) d.elapsed = Math.min(d.elapsed + dt, decryptionSpan)
        if (d.elapsed >= 0) {
            d.frame = Motion.decryptionFrame(Motion.DECRYPTION_START + d.elapsed * 1.5)
            d.clarity = d.frame.clarity > d.clarity ? d.frame.clarity : d.frame.phase === "clear" ? 1 : d.clarity * Math.exp(-dt * 9)
        }
    }

    // Project a point on the cassette face (model space x, y at z 0.255) to view pixels.
    function projectCard(x, y) {
        const p = selectedCassette.mapPositionToScene(Qt.vector3d(x, y, 0.255))
        return view.mapFrom3DScene(p)
    }
    function projectVector(from, to) {
        const a = view.mapFrom3DScene(from), b = view.mapFrom3DScene(to)
        return Qt.point(b.x - a.x, b.y - a.y)
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        enabled: view.interactive
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        property real lastX: 0
        property bool browsing: false
        property real wheelTotal: 0
        property real wheelTime: 0
        property int hoverTarget: -1
        cursorShape: archive.canInspect ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
            : archive.dragging ? Qt.ClosedHandCursor : hoverTarget >= 0 ? Qt.PointingHandCursor : archive.canBrowse ? Qt.OpenHandCursor : Qt.ArrowCursor
        // Hit test: array instance, selected cassette or a descending copy.
        function pick(x, y) {
            const hit = view.pick(x, y)
            if (!hit.objectHit) return { kind: "none" }
            if (hit.objectHit === shell) return { kind: "instance", index: hit.instanceIndex }
            for (let i = 0; i < copies.count; i++) {
                const copy = copies.objectAt(i)
                if (copy && copy.visible && hit.objectHit === copy.shell) return { kind: "copy", index: i }
            }
            return { kind: "selected" }
        }
        function hover(x, y) {
            if (!archive.canBrowse) { hoverTarget = -1; archive.hoverInstance(-1); return }
            archive.setPointer(x / width - 0.5, y / height - 0.5)
            const target = pick(x, y)
            hoverTarget = target.kind === "none" ? -1 : 1
            if (target.kind === "instance") archive.hoverInstance(target.index)
            else if (target.kind === "copy") archive.hoverOutgoing(target.index)
            else archive.hoverInstance(-1)
        }
        onPressed: mouse => {
            lastX = mouse.x
            browsing = archive.canBrowse
            if (browsing) {
                // One cell step along each track, projected at the file's centre.
                const c = archive.modelPosition.plus(Qt.vector3d(0, 1.85, 0))
                const lane = view.projectVector(c.plus(Qt.vector3d(2.6, 0, 0)), c.plus(Qt.vector3d(-2.6, 0, 0)))
                const row = view.projectVector(c.plus(Qt.vector3d(0, 0, 0.31)), c.plus(Qt.vector3d(0, 0, -0.31)))
                archive.dragStart(mouse.x, mouse.y, lane, row, Date.now())
            }
        }
        onPositionChanged: mouse => {
            if (!pressed) { hover(mouse.x, mouse.y); return }
            if (browsing) archive.dragMove(mouse.x, mouse.y, Date.now())
            else if (archive.canInspect) archive.dragRotate(mouse.x - lastX)
            lastX = mouse.x
        }
        onReleased: mouse => {
            if (!browsing) return
            browsing = false
            if (archive.dragEnd(mouse.x, mouse.y, Date.now())) return
            const target = pick(mouse.x, mouse.y)
            if (target.kind === "instance") archive.selectInstance(target.index)
            else if (target.kind === "copy") archive.selectOutgoing(target.index)
            else if (target.kind === "selected") view.openRequested()
        }
        onCanceled: { browsing = false; archive.cancelPointer() }
        onExited: { hoverTarget = -1; archive.hoverInstance(-1); archive.setPointer(0, 0) }
        onWheel: wheel => {
            if (!archive.canBrowse || pressed || (wheel.modifiers & Qt.ControlModifier) || Math.abs(wheel.angleDelta.x) > Math.abs(wheel.angleDelta.y)) return
            // Accumulate like the web: reset after 180 ms or on direction change.
            const now = Date.now()
            const delta = Math.max(-300, Math.min(300, -wheel.angleDelta.y))
            if (now - wheelTime > 180 || Math.sign(delta) !== Math.sign(wheelTotal)) wheelTotal = 0
            wheelTime = now
            wheelTotal += delta
            const steps = Math.min(3, Math.floor(Math.abs(wheelTotal) / 100))
            if (!steps) return
            const direction = Math.sign(wheelTotal)
            wheelTotal -= direction * steps * 100
            for (let i = 0; i < steps; i++) archive.stepRow(direction)
        }
    }
}
