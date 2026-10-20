import QtQuick
import QtQuick.Shapes
import QtQuick.Effects
import "."
import "BootMotion.mjs" as Motion

// The 2D opening, driven frame by frame by RhineLabUI's measured tracks
// (boot-motion.ts, bundled unchanged as BootMotion.mjs). Element geometry
// follows boot.ts / style.css on the 1920 × 1080 reference stage.
Item {
    id: boot
    property real t: 0
    readonly property var s: Motion.bootMotion(t)
    readonly property real vt: s.t
    readonly property int f: s.f
    width: 1920; height: 1080

    function arcPath(r, start, sweep, x, y) {
        x = x ?? 960; y = y ?? 540
        const p = a => (x + Math.cos(a) * r) + " " + (y + Math.sin(a) * r)
        if (sweep >= Math.PI * 1.999)
            return "M" + p(start) + "A" + r + " " + r + " 0 1 1 " + p(start + Math.PI) + "A" + r + " " + r + " 0 1 1 " + p(start + Math.PI * 2)
        if (sweep <= 0.0001) return ""
        return "M" + p(start) + "A" + r + " " + r + " 0 " + (sweep > Math.PI ? 1 : 0) + " 1 " + p(start + sweep)
    }

    // #boot-background: warm radial field with the faint drifting swirl.
    Item {
        anchors.fill: parent
        opacity: boot.s.backgroundOpacity
        Rectangle {
            anchors.fill: parent
            gradient: Gradient { GradientStop { position: 0; color: Theme.bootLight } GradientStop { position: 0.76; color: Theme.boot } GradientStop { position: 1; color: Theme.mix("#e4dfdb", "#11181b") } }
        }
        Shape {
            id: swirl
            anchors.fill: parent
            opacity: 0.09
            layer.enabled: true
            layer.effect: MultiEffect { blurEnabled: true; blur: 0.25; blurMax: 12 }
            preferredRendererType: Shape.CurveRenderer
            transform: [
                Scale { origin.x: 960; origin.y: 540; xScale: 1.08; yScale: 1.08 },
                Translate { x: Math.sin(boot.vt * 0.16) * 18; y: -(boot.vt - 6) * 5 }
            ]
            ShapePath { strokeColor: Theme.whiteInk; strokeWidth: 3; fillColor: "transparent"
                PathSvg { path: "M-210 705C-45 705 182 704 247 567C337 377 99 306 4 435S27 680 169 631C309 584 227 314 279 111S568-113 568-113" } }
            ShapePath { strokeColor: Theme.whiteInk; strokeWidth: 3; fillColor: "transparent"
                PathSvg { path: "M1560-80C1374 114 1671 168 1601 323S1371 367 1431 480S1692 666 1559 787S1329 886 1498 1130" } }
            ShapePath { strokeColor: Theme.whiteInk; strokeWidth: 3; fillColor: "transparent"
                PathAngleArc { centerX: 1450; centerY: 648; radiusX: 346; radiusY: 346; sweepAngle: 360 } }
            ShapePath { strokeColor: Theme.whiteInk; strokeWidth: 3; fillColor: "transparent"
                PathAngleArc { centerX: 1450; centerY: 648; radiusX: 348; radiusY: 348; sweepAngle: 360 } }
        }
    }

    // .access-text (top 532, left 800), typed letter by letter.
    Text {
        x: 800; y: 520
        text: boot.s.access
        opacity: boot.s.accessOpacity
        color: Theme.ink; font { family: uiFont; pixelSize: 18; letterSpacing: 1.2 }
    }

    // .boot-logo: contour drawn with a moving dash, symbols spring in, lettering typed.
    Item {
        x: 518 + boot.s.logo.offsetX; y: 463; width: 296; height: 177
        opacity: boot.s.logoOpacity
        readonly property real unit: Math.min(width / 310, height / 185)
        Item {
            width: 310; height: 185; scale: parent.unit; transformOrigin: Item.TopLeft
            y: (parent.height - 185 * parent.unit) / 2
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    id: contour
                    strokeColor: Theme.ink; strokeWidth: boot.s.logo.strokeWidth; fillColor: "transparent"; capStyle: ShapePath.FlatCap
                    // bootMarkContour, one closed Bezier loop; trim reproduces its dash window.
                    trim.start: 0
                    trim.end: boot.s.logo.length
                    trim.offset: ((boot.s.logo.start % 1) + 1) % 1
                    PathSvg { path: Motion.bootMarkContour }
                }
                visible: boot.s.logo.length > 0 || boot.s.logo.symbolScale > 0
            }
            // Plus: translate(plusX 70) rotate(plusAngle) scale(symbolScale) translate(-69 -70)
            Shape {
                width: 310; height: 185
                visible: boot.s.logo.symbolScale > 0
                preferredRendererType: Shape.CurveRenderer
                transform: [
                    Translate { x: -69; y: -70 },
                    Scale { xScale: boot.s.logo.symbolScale; yScale: boot.s.logo.symbolScale },
                    Rotation { angle: boot.s.logo.plusAngle },
                    Translate { x: boot.s.logo.plusX; y: 70 }
                ]
                ShapePath { strokeColor: Theme.ink; strokeWidth: 15; fillColor: "transparent"; capStyle: ShapePath.FlatCap
                    PathSvg { path: "M44 70h50M69 45v50" } }
            }
            // Minus: translate(minusX 70) scale(symbolScale), width from its own track.
            Shape {
                width: 310; height: 185
                visible: boot.s.logo.symbolScale > 0
                preferredRendererType: Shape.CurveRenderer
                transform: [
                    Scale { xScale: boot.s.logo.symbolScale; yScale: boot.s.logo.symbolScale },
                    Translate { x: boot.s.logo.minusX; y: 70 }
                ]
                ShapePath { strokeColor: Theme.ink; strokeWidth: 15; fillColor: "transparent"; capStyle: ShapePath.FlatCap
                    PathSvg { path: "M" + (-boot.s.logo.minusWidth / 2) + " 0h" + boot.s.logo.minusWidth } }
            }
            Text {
                x: 20; y: 158
                text: boot.s.logoLetters; color: Theme.ink
                font { family: boldFont; pixelSize: 16; letterSpacing: 22 }
            }
        }
    }

    // .auth-status (left 931, top 527): ▪ marker + typed status.
    Row {
        x: 931; y: 516; spacing: 10
        opacity: boot.s.authOpacity
        Text { anchors.verticalCenter: parent.verticalCenter; text: "▪"; color: Theme.ink; font { family: uiFont; pixelSize: 14 } }
        Text { text: boot.s.auth; color: Theme.ink; font { family: uiFont; pixelSize: 21; letterSpacing: -0.2 } }
    }

    // .scan: measured outer/white/inner arcs, side arcs, orbit dots, satellites, core.
    Item {
        anchors.fill: parent
        visible: boot.s.scanVisible
        Item {
            anchors.fill: parent
            opacity: boot.s.ringOpacity
            transform: Scale { origin.x: 960; origin.y: 540; xScale: boot.s.ringScale; yScale: boot.s.ringScale }
            layer.enabled: boot.s.ringBlur > 0
            layer.effect: MultiEffect { blurEnabled: true; blur: Math.min(1, boot.s.ringBlur / 8); blurMax: 16 }
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath { strokeColor: Theme.ink; strokeWidth: 2.4; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathSvg { path: boot.arcPath(boot.s.scan.radius, boot.s.scan.outerStart, boot.s.scan.outerSweep) } }
                ShapePath { strokeColor: Theme.whiteInk; strokeWidth: 4; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathSvg { path: boot.arcPath(boot.s.scan.whiteRadius, boot.s.scan.whiteStart, boot.s.scan.whiteSweep) } }
                ShapePath { strokeColor: Theme.ink; strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathSvg { path: boot.arcPath(boot.s.scan.innerRadius, boot.s.scan.innerStart, boot.s.scan.innerSweep) } }
                ShapePath { strokeColor: Theme.ink; strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathSvg { path: boot.arcPath(boot.s.scan.innerRadius, boot.s.scan.innerStart + Math.PI, boot.s.scan.innerSweep) } }
                ShapePath { strokeColor: boot.s.scanOrbit.sideVisible ? Theme.ink : "transparent"; strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathSvg { path: boot.arcPath(boot.s.scanOrbit.sides[0].radius, boot.s.scanOrbit.sides[0].start, boot.s.scanOrbit.sides[0].sweep, boot.s.scanOrbit.sides[0].x, boot.s.scanOrbit.sides[0].y) } }
                ShapePath { strokeColor: boot.s.scanOrbit.sideVisible ? Theme.ink : "transparent"; strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathSvg { path: boot.arcPath(boot.s.scanOrbit.sides[1].radius, boot.s.scanOrbit.sides[1].start, boot.s.scanOrbit.sides[1].sweep, boot.s.scanOrbit.sides[1].x, boot.s.scanOrbit.sides[1].y) } }
            }
            Repeater { // satellites beneath the core
                model: 6
                delegate: Rectangle {
                    required property int index
                    readonly property var p: boot.s.scanOrbit.satellites[index]
                    visible: p.radius > 0
                    width: p.radius * 2; height: width; radius: width / 2; color: Theme.ink
                    x: p.x - p.radius; y: p.y - p.radius
                }
            }
            Rectangle { // scan-core
                visible: boot.s.ornament
                width: boot.s.coreRadius * 2; height: width; radius: width / 2; color: Theme.ink
                x: 959.5 - boot.s.coreRadius; y: 539.5 - boot.s.coreRadius
            }
            Repeater { // orange orbit dots
                model: 2
                delegate: Rectangle {
                    required property int index
                    readonly property real a: boot.s.scan.orbit + index * Math.PI
                    width: boot.s.scan.dotRadius * 2; height: width; radius: width / 2; color: "#ed821b"
                    x: 960 + Math.cos(a) * boot.s.scan.orbitRadius - boot.s.scan.dotRadius
                    y: 540 + Math.sin(a) * boot.s.scan.orbitRadius - boot.s.scan.dotRadius
                }
            }
            Repeater { // arc end caps (black outer, white inner)
                model: 2
                delegate: Rectangle {
                    required property int index
                    readonly property real a: index ? boot.s.scan.whiteStart : boot.s.scan.outerStart + boot.s.scan.outerSweep
                    readonly property real r: index ? boot.s.scan.whiteRadius : boot.s.scan.radius
                    readonly property real cap: index ? boot.s.scan.whiteCap : boot.s.scan.blackCap
                    visible: cap > 0
                    width: cap * 2; height: width; radius: cap; color: index ? Theme.whiteInk : Theme.ink
                    x: 960 + Math.cos(a) * r - cap; y: 540 + Math.sin(a) * r - cap
                }
            }
        }
        Text {
            anchors.centerIn: parent
            text: "TERMINAL INITIALIZED"; color: Theme.ink
            opacity: boot.s.permissionOpacity
            font { family: uiFont; pixelSize: Math.round(boot.s.scanFont); letterSpacing: boot.s.scanTracking }
        }
    }

    // .welcome (left 720, top 403, 480 × 340), exit scales and blurs out.
    Item {
        id: welcome
        x: 720; y: 403; width: 480; height: 340
        visible: boot.s.welcomeVisible
        opacity: boot.s.welcomeOpacity
        transform: Scale { origin.x: 240; origin.y: 150; xScale: boot.s.welcomeScale; yScale: boot.s.welcomeScale }
        // Exit: blur(8px) with the warm sepia/hue shift of boot.ts.
        layer.enabled: boot.s.exit > 0
        layer.effect: MultiEffect {
            blurEnabled: true; blur: Math.min(1, boot.s.exitBlur / 16); blurMax: 16
            colorizationColor: "#c88a3c"; colorization: 0.6 * boot.s.exit
        }
        Rectangle { x: 5; y: -59; width: 470; height: 392; color: Theme.mix("#080a08", "#202a2f"); opacity: boot.s.welcomePanel }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; y: 0
            text: "WELCOME TO"
            color: Theme.amount > 0.0001 ? Theme.ink : Qt.rgba(1 - boot.s.welcomeInk, 1 - boot.s.welcomeInk, 1 - boot.s.welcomeInk, 1)
            font { family: boldFont; pixelSize: 55 }
        }
        Item {
            x: 35; y: 67; width: 410; height: 60
            opacity: boot.s.companyVisible ? (boot.s.companyMask ? 0.65 : 1) : 0
            Text { anchors.centerIn: parent; text: "CIPHERCHAT"; color: Theme.ink; font { family: boldFont; pixelSize: 48; letterSpacing: -0.5 } }
            Item { // .welcome-highlight: black band with light ink, revealed left to right
                width: parent.width * boot.s.highlight; height: parent.height; clip: true
                Rectangle {
                    width: 410; height: 60; color: Theme.ink
                    Text { anchors.centerIn: parent; text: "CIPHERCHAT"; color: Theme.paper; opacity: boot.s.companyMask ? 0.06 : 1; font { family: boldFont; pixelSize: 48; letterSpacing: -0.5 } }
                }
            }
        }
        Text {
            y: 134; width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: "PRIVATE COMMUNICATION"; color: Theme.ink
            opacity: boot.s.databaseOpacity
            font { family: boldFont; pixelSize: 36 }
        }
        Logo { x: 138.5; y: 187; width: 203; height: 121; opacity: boot.s.welcomeLogo ? 1 : 0 }
    }
    Rectangle { anchors.fill: parent; color: Theme.mix("#ffffff", "#11181b"); opacity: boot.s.white }
}
