pragma Singleton
import QtQuick

// theme-ui.ts palette; `amount` follows the scene's theme wave (0 light, 1 dark).
QtObject {
    property real amount: 0
    function mix(a, b) {
        const x = Qt.color(a), y = Qt.color(b), t = amount
        return Qt.rgba(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t)
    }
    readonly property color ink: mix("#080a08", "#e0e3dc")
    readonly property color muted: mix("#77756d", "#a6b0b1")
    readonly property color text: mix("#5a584e", "#b9c1c0")
    readonly property color faint: mix("#9a9487", "#6f7c80")
    readonly property color line: mix("#aaa59a", "#536166")
    readonly property color rule: mix("#504b41", "#536166")
    readonly property color paper: mix("#eae5e1", "#11181b")
    readonly property color panel: mix("#eeebe4", "#202a2f")
    readonly property color field: mix("#e7e3d9", "#2a363b")
    readonly property color accent: mix("#9b7247", "#c5a16b")
    readonly property color hover: mix("#e4d5c1", "#2e3b40")
    // .solid-button: dark slab with light ink, inverted in the dark theme.
    readonly property color solid: mix("#252820", "#e0e3dc")
    readonly property color solidHover: mix("#3a3d33", "#c9cec6")
    readonly property color solidInk: mix("#f0eee5", "#11181b")
    readonly property color selection: mix("#d9c5ad", "#44565c")
    readonly property color boot: mix("#e3e1dc", "#161e21")
    readonly property color bootLight: mix("#e7e7e1", "#202a2f")
    readonly property color whiteInk: mix("#ffffff", "#536166")
}
