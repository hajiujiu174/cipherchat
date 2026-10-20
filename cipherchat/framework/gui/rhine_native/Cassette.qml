import QtQuick
import QtQuick3D

// The extracted cassette: original archive-cassette.glb meshes (via balsam)
// with the material adjustments of RhineLabUI scene.ts load(). `quality`
// morphs the array palette into the extracted one as the file lifts (the
// web's CardAppearance); interior parts only appear with it. `clarity` is the
// decryption front on the cover, `theme` the per-card dark-theme amount.
Node {
    id: cassette
    property int label: 0
    property real quality: 1
    property real clarity: 0
    property real theme: 0
    property alias shell: shellModel

    function tone(light, dark) {
        const a = Qt.color(light), b = Qt.color(dark), t = theme
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1)
    }
    function morph(low, high) {
        const a = Qt.color(low), b = Qt.color(high), t = quality
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1)
    }

    ShellMaterial { id: frosted; quality: cassette.quality; clarity: cassette.clarity; theme: cassette.theme }
    PrincipledMaterial { id: ivory; baseColor: cassette.tone(cassette.morph("#fff5e9", "#f0e7df"), "#687277"); metalness: 0.02; roughness: 0.38 + (0.31 - 0.38) * cassette.quality
        transmissionFactor: 0.65 * cassette.quality; thicknessFactor: 0.04; indexOfRefraction: 1.46; cullMode: Material.NoCulling }
    PrincipledMaterial { id: diffuser; baseColor: cassette.tone(cassette.morph("#806447", "#e2dad4"), "#192226"); roughness: 0.7; cullMode: Material.NoCulling }
    PrincipledMaterial { id: printed; baseColor: cassette.tone("#eae5dc", "#303a3e"); roughness: 0.65; cullMode: Material.NoCulling }
    PrincipledMaterial { id: ceramic; baseColor: cassette.tone("#c7beb6", "#969f9f"); metalness: 0.06; roughness: 0.6; cullMode: Material.NoCulling }
    PrincipledMaterial { id: opticalEdges; baseColor: cassette.tone("#d4c7be", "#bbc3bc"); metalness: 0.08; roughness: 0.26; cullMode: Material.NoCulling }
    PrincipledMaterial { id: subsurface; baseColor: cassette.tone("#b9aba1", "#939e9f"); metalness: 0.05; roughness: 0.48; cullMode: Material.NoCulling }
    PrincipledMaterial { id: filmEdge; baseColor: cassette.tone("#c7c3be", "#969f9f"); metalness: 0.2; roughness: 0.27; cullMode: Material.NoCulling }
    PrincipledMaterial { id: amber; baseColor: cassette.tone("#b87640", "#969f9f"); metalness: 0.28; roughness: 0.32; cullMode: Material.NoCulling }
    PrincipledMaterial { id: film; baseColor: cassette.tone("#e6e1db", "#969f9f"); metalness: 0.06; roughness: 0.3; cullMode: Material.NoCulling }
    PrincipledMaterial { id: inlay; baseColor: cassette.tone(cassette.morph("#e4d6c5", "#d5c0a9"), "#c6a36b"); metalness: 0.05 + (0.04 - 0.05) * cassette.quality; roughness: 0.52; cullMode: Material.NoCulling }
    PrincipledMaterial { id: engraving; baseColor: cassette.tone("#c7c2bb", "#969f9f"); metalness: 0.12; roughness: 0.39; cullMode: Material.NoCulling }
    PrincipledMaterial { id: engravingHighlight; baseColor: cassette.tone("#f3f0eb", "#969f9f"); metalness: 0.16; roughness: 0.25; cullMode: Material.NoCulling }
    PrincipledMaterial { id: titanium; baseColor: cassette.tone("#c8cbcd", "#b1b9bb"); metalness: 0.82; roughness: 0.19; cullMode: Material.NoCulling }
    PrincipledMaterial { id: lettering; baseColor: cassette.tone("#e8e5e0", "#969f9f"); metalness: 0.13; roughness: 0.24; cullMode: Material.NoCulling }
    PrincipledMaterial { id: champagne; baseColor: cassette.tone("#d1b492", "#969f9f"); metalness: 0.48; roughness: 0.33; cullMode: Material.NoCulling }

    // Surfaces shared with the array instances.
    Model { id: shellModel; source: "meshes/cube_5328_mesh.mesh"; materials: [frosted]; pickable: true; receivesShadows: true }
    Model { source: "meshes/cube_5329_mesh.mesh"; materials: [ivory]; receivesShadows: true }
    Model { source: "meshes/cube_5330_mesh.mesh"; materials: [diffuser]; castsShadows: true; receivesShadows: true }
    Model { source: "meshes/cube_5538_mesh.mesh"; materials: [inlay] }
    Model { source: "meshes/cylinder_101_mesh.mesh"; materials: [titanium] }
    // Interior and typographic detail exists only on the extracted file.
    Node {
        opacity: cassette.quality
        visible: cassette.quality > 0.01
        Model { source: "meshes/cube_5335_mesh.mesh"; materials: [printed] }
        Model { source: "meshes/database_label_025_mesh.mesh"; materials: [ceramic] }
        Model { source: "meshes/cube_5382_mesh.mesh"; materials: [opticalEdges] }
        Model { source: "meshes/embedded_optical_cavity_segment_015_mesh.mesh"; materials: [subsurface] }
        Model { source: "meshes/concentric_optical_machining_lip_009_mesh.mesh"; materials: [filmEdge] }
        Model { source: "meshes/embedded_amber_annulus_inner_arc_003_mesh.mesh"; materials: [amber] }
        Model { source: "meshes/optical_ribbon_inner_003_mesh.mesh"; materials: [film] }
        Model { source: "meshes/case_engraved_outer_upper_left_012_mesh.mesh"; materials: [engraving] }
        Model { source: "meshes/case_pressed_lip_outer_upper_left_012_mesh.mesh"; materials: [engravingHighlight] }
        Model { source: "meshes/moulded_vertical_lettering_021_mesh.mesh"; materials: [lettering] }
        Model { source: "meshes/cube_5543_mesh.mesh"; materials: [champagne] }

        // Printed label (scene.ts drawLabel), rendered from a 2D item at 1024 × 440.
        Model {
            source: "#Rectangle"
            position: Qt.vector3d(-1.36, 3.04, 0.255)
            scale: Qt.vector3d(0.99 / 100, 0.46 / 100, 1)
            materials: DefaultMaterial {
                lighting: DefaultMaterial.NoLighting
                blendMode: DefaultMaterial.SourceOver
                diffuseMap: Texture {
                    sourceItem: Rectangle {
                        id: labelArt
                        readonly property color ink: cassette.tone("#171713", "#c7c7b5")
                        width: 1024; height: 440; color: cassette.tone("#e6e2d9", "#060809")
                        Rectangle { x: 12; y: 12; width: 1000; height: 6; color: labelArt.ink }
                        Rectangle { x: 12; y: 419; width: 1000; height: 3; color: labelArt.ink }
                        Text { x: 22; y: 30; text: "CIPHERCHAT"; font { family: boldFont; pixelSize: 81 } color: labelArt.ink }
                        Text { x: 25; y: 146; text: "PRIVATE COMMUNICATION"; font { family: uiFont; pixelSize: 32 } color: cassette.tone("#878476", "#8a8f86") }
                        Text { x: 22; y: 240; text: "NO." + String(cassette.label + 1).padStart(3, "0"); font { family: boldFont; pixelSize: 130 } color: labelArt.ink }
                        Rectangle { x: 782; y: 32; width: 221; height: 39; color: labelArt.ink
                            Text { x: 27; y: 5; text: "R L / I S"; font { family: uiFont; pixelSize: 24 } color: labelArt.color } }
                        Text { x: 830; y: 88; text: "INFO"; font { family: boldFont; pixelSize: 64 } color: labelArt.ink }
                        Logo { x: 790; y: 242; width: 210; height: 98; ink: labelArt.ink; showText: false }
                    }
                }
            }
        }
    }
}
