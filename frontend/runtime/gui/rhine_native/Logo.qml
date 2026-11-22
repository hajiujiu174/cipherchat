import QtQuick
import "."
import QtQuick.Shapes

// RhineLabUI/src/brand.ts mark: viewBox 310 × 145, or 310 × 185 with lettering.
Item {
    id: logo
    property color ink: Theme.ink
    property bool showText: true
    property string letters: "RHINE·LAB"
    property real draw: 1
    readonly property real unit: Math.min(width / 310, height / (showText ? 185 : 145))
    Item {
        width: 310; height: logo.showText ? 185 : 145
        scale: logo.unit; transformOrigin: Item.TopLeft
        x: (logo.width - width * logo.unit) / 2; y: (logo.height - height * logo.unit) / 2
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: logo.ink; strokeWidth: 26; fillColor: "transparent"; capStyle: ShapePath.FlatCap
                PathSvg { path: "M156 75C127 48 103 15 70 15C37 15 15 39 15 70S38 128 70 128C103 128 127 96 176 52M155 75C182 99 208 128 240 128C273 128 295 105 295 73S273 15 240 15C221 15 207 23 192 38" }
            }
            ShapePath {
                strokeColor: logo.ink; strokeWidth: 15; fillColor: "transparent"; capStyle: ShapePath.FlatCap
                PathSvg { path: "M44 70h50M69 45v50M219 70h44" }
            }
        }
        Text {
            visible: logo.showText
            x: 165 - width / 2; y: 158
            text: logo.letters; color: logo.ink
            font { family: boldFont; pixelSize: 16; letterSpacing: 22 }
        }
    }
}
