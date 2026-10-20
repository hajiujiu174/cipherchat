import QtQuick
import QtQuick3D

// Frosted_Polymer shell shared by the array instances and the extracted
// cassette (appearance.ts + glass-reveal.ts): the array's warm bottom-to-top
// tint fades as the file rises (quality), a feathered clearing front travels
// down the cover as it decrypts (clarity), and the dark theme arrives per card.
CustomMaterial {
    id: material
    property real quality: 0
    property real clarity: 0
    property real theme: 0
    property bool instanced: false
    property color darkSurface: "#626b70"
    property real tint: 0.2             // strength of the array bottom-to-top warm tint
    property real transmission: 0.78
    shadingMode: CustomMaterial.Shaded
    cullMode: Material.NoCulling
    vertexShader: instanced ? "shell.vert" : "shell_single.vert"
    fragmentShader: "shell.frag"
    property real uQuality: quality
    property real uClarity: clarity
    property real uTheme: theme
    property color uDark: darkSurface
    property real uArray: instanced ? 1 : 0
    property real uTint: tint
    property real uTransmission: transmission
}
