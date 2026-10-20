import QtQuick
import QtQuick3D

// Opaque array surfaces (edges, diffuser, inlay, fasteners) with the per-card
// dark-theme blend from theme-material.ts carried in the instance data.
CustomMaterial {
    property color light: "#ffffff"
    property color dark: "#687277"
    property real roughness: 0.5
    property real metalness: 0
    property real theme: 0
    property bool instanced: true
    shadingMode: CustomMaterial.Shaded
    cullMode: Material.NoCulling
    vertexShader: instanced ? "array.vert" : "array_single.vert"
    fragmentShader: "array.frag"
    property color uLight: light
    property color uDarkColor: dark
    property real uRoughness: roughness
    property real uMetalness: metalness
    property real uTheme: theme
}
