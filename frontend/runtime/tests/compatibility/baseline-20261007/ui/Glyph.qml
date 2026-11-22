import QtQuick
import QtQuick.Shapes
import "."

// Line glyphs for the system navigation: search lens and settings clock.
Shape {
    id: glyph
    property string kind: "search"
    property color ink: Theme.ink
    width: 23; height: 23
    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        strokeColor: glyph.ink; strokeWidth: 1.8; fillColor: "transparent"; capStyle: ShapePath.RoundCap
        PathSvg { path: glyph.kind === "search"
            ? "M9.5 2.5a7 7 0 1 1 0 14a7 7 0 1 1 0-14M14.6 14.6L21 21"
            : "M11.5 2a9.5 9.5 0 1 1 0 19a9.5 9.5 0 1 1 0-19M11.5 6.5v5h4" }
    }
}
