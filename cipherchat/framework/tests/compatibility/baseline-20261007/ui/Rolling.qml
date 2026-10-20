import QtQuick
import "."

// Port of @kitlangton/rolling-number as RhineLabUI uses it (duration 460 ms).
// Numbers (`numeric`) spin each digit slot on a 0–9 wheel to the next
// occurrence in the roll direction. Text uses the library's "direct"
// transition: a changed character rolls one step from the old glyph straight
// to the new one. Both follow its damped curve  target + s·(1 + 10y)·e^(−10y);
// new slots rise from one line below after a 14 % hold, removed slots fade.
Item {
    id: roll
    property string text: ""
    property font font
    property color color: Theme.ink
    property bool animated: true
    property int direction: 0           // +1 up, -1 down, 0 = by value (numeric) / up (text)
    property bool numeric: false
    property bool perCharacter: false   // kept for callers; every roller is per character
    readonly property int duration: 460
    readonly property string wheelChars: "0123456789"
    property string previous: ""
    property int slots: text.length
    implicitWidth: row.implicitWidth
    implicitHeight: metrics.height
    width: implicitWidth; height: implicitHeight
    FontMetrics { id: metrics; font: roll.font }

    function rollDirection(from, to) {
        if (direction !== 0) return direction
        if (!numeric) return 1
        const a = from.replace(/^0+(?=\d)/, ""), b = to.replace(/^0+(?=\d)/, "")
        if (a.length !== b.length) return b.length > a.length ? 1 : -1
        return b === a ? 0 : b > a ? 1 : -1
    }
    property int activeDirection: 1
    onTextChanged: {
        activeDirection = rollDirection(previous, text)
        slots = Math.max(text.length, previous.length)
        shrink.restart()
        previous = text
    }
    Component.onCompleted: previous = text
    Timer { id: shrink; interval: roll.duration; onTriggered: roll.slots = roll.text.length }

    Row {
        id: row
        Repeater {
            model: roll.slots
            delegate: Item {
                id: slot
                required property int index
                readonly property string target: index < roll.text.length ? roll.text.charAt(index) : ""
                readonly property int targetIndex: roll.numeric ? roll.wheelChars.indexOf(target) : 1
                property real position: targetIndex >= 0 ? targetIndex : 0
                // Direct transition: a two-face wheel [previous, current].
                property var faces: [target, target]
                property real from: position
                property real to: position
                property real progress: 1
                property string swapOld: ""
                property string current: target
                property bool entering: false
                clip: true
                height: metrics.height
                width: target === "" ? 0 : face.implicitWidth
                Behavior on width { enabled: roll.animated; NumberAnimation { duration: roll.duration; easing.type: Easing.OutCubic } }
                opacity: target === "" ? 0 : 1
                Behavior on opacity { enabled: roll.animated; NumberAnimation { duration: slot.target === "" ? roll.duration * 0.65 : 180 } }
                Text { id: face; visible: false; text: slot.target; font: roll.font }

                function wheelAt(i) {
                    if (!roll.numeric) return faces[Math.max(0, Math.min(1, Math.round(i)))]
                    const n = roll.wheelChars.length; return roll.wheelChars.charAt(((Math.round(i) % n) + n) % n)
                }
                // Next occurrence of `targetIndex` from the current position in the roll direction.
                function aim(pos, dir) {
                    const n = roll.wheelChars.length
                    let r = Math.floor(pos / n) * n + targetIndex
                    if (dir > 0 && r < pos - 0.001) r += n
                    else if (dir < 0 && r > pos + 0.001) r -= n
                    else if (dir === 0) r += Math.round((pos - r) / n) * n
                    return r
                }
                onTargetChanged: {
                    if (target === "") return
                    if (!roll.animated || current === target) { current = target; faces = [target, target]; position = targetIndex >= 0 ? targetIndex : 0; return }
                    if (!roll.numeric) {
                        faces = [current, target]; current = target
                        from = 0; to = 1; position = 0
                        spin.restart()
                        return
                    }
                    const wheel = targetIndex >= 0 && roll.wheelChars.indexOf(current) >= 0
                    if (wheel) {
                        from = position; to = aim(position, roll.activeDirection)
                        current = target
                        spin.restart()
                    } else {
                        swapOld = current; current = target; position = targetIndex >= 0 ? targetIndex : 0
                        swap.restart()
                    }
                }
                Component.onCompleted: {
                    if (roll.animated && roll.previous !== "" && index >= roll.previous.length && target !== "") { entering = true; enter.restart() }
                }
                NumberAnimation {
                    id: spin; target: slot; property: "progress"; from: 0; to: 1; duration: roll.duration
                    onStarted: slot.progress = 0
                }
                onProgressChanged: if (spin.running) {
                    const s = from - to, y = progress
                    position = y >= 1 ? to : to + s * (1 + 10 * y) * Math.exp(-10 * y)
                }
                // Wheel faces: the two characters around the current position.
                readonly property bool onWheel: !roll.numeric || roll.wheelChars.indexOf(current) >= 0 && swapOld === "" || spin.running
                Repeater {
                    model: slot.onWheel ? 2 : 0
                    delegate: Text {
                        required property int index
                        readonly property real base: Math.floor(slot.position)
                        text: slot.wheelAt(base + index)
                        y: (base + index - slot.position) * metrics.height * (roll.activeDirection < 0 ? -1 : 1) + (slot.entering ? enterOffset.value : 0)
                        font: roll.font; color: roll.color
                    }
                }
                // Off-wheel characters: crossfade.
                Text {
                    id: oldFace
                    visible: !slot.onWheel && swap.running
                    text: slot.swapOld; font: roll.font; color: roll.color
                }
                Text {
                    id: newFace
                    visible: !slot.onWheel
                    text: slot.current; font: roll.font; color: roll.color
                    y: slot.entering ? enterOffset.value : 0
                }
                ParallelAnimation {
                    id: swap
                    NumberAnimation { target: oldFace; property: "opacity"; from: 1; to: 0; duration: 180 }
                    NumberAnimation { target: newFace; property: "opacity"; from: 0; to: 1; duration: 180 }
                    onFinished: slot.swapOld = ""
                }
                // Entry: drop in from one line below with the same damped curve after a 14 % hold.
                QtObject { id: enterOffset; property real value: 0 }
                NumberAnimation {
                    id: enter; target: enterProgress; property: "value"; from: 0; to: 1; duration: roll.duration
                    onFinished: slot.entering = false
                }
                QtObject {
                    id: enterProgress
                    property real value: 1
                    onValueChanged: {
                        const s = Math.max(0, Math.min(1, (value - 0.14) / 0.86))
                        enterOffset.value = value >= 1 ? 0 : metrics.height * (1 + 10 * s) * Math.exp(-10 * s)
                    }
                }
            }
        }
    }
}
