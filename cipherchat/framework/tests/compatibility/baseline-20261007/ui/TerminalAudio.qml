import QtQuick
import QtMultimedia

// audio.ts TerminalAudio: the original three looping score stems mixed per
// scene, and interface sounds pre-rendered from synthesizeSound() (same
// oscillators, envelopes and limiter) plus the three typing samples.
Item {
    id: audio
    property bool sound: true
    property bool music: true
    property real soundVolume: 0.55
    property real musicVolume: 0.5
    property bool unlocked: false        // after the entry gesture
    property string scene: "boot"
    property real duck: 1
    property var stemGains: [0.48, 0.32, 0.18]
    property real bootPrevious: -1
    readonly property var cues: [
        [9.16, "brand"], [11.84, "confirm"], [19.48, "scan"], [21.84, "confirm"], [22.76, "welcome"],
        [23.52, "text-reveal"], [25.04, "text-reveal"], [26.92, "array"], [30.68, "open"], [34.3, "inspect"]]
    // TYPING_FRAMES from typing-rhythm.ts (25 fps film frames).
    readonly property var typingFrames: [170,172,173,174,175,176,177,178,179,180,182,183,184,185,186,187,282,284,285,287,290,291,293,294,295,320,321,323,325,327,329,331,333,335,337,339,367,369,370,372,373,375,376,379,381,382,384,385,387,388,389,423,425,426,427,428,430,431,433,434,435,436,437,438,439,440,449,453,457]
    readonly property var intervals: ({ key: 0.024, tick: 0.055, column: 0.055 })
    property var last: ({})
    property int keyIndex: 0

    readonly property var names: ["page-open", "page-close", "ui-tick", "brand", "text-reveal", "tick", "column", "open",
        "confirm", "back", "scan", "welcome", "array", "inspect", "key-0", "key-1", "key-2"]
    property var effects: ({})
    Component { id: effectComponent; SoundEffect {} }
    Component.onCompleted: {
        const map = {}
        for (const name of names) {
            // Two voices each so rapid repeats overlap instead of cutting off.
            map[name] = [0, 1].map(() => effectComponent.createObject(audio, { source: "qrc:/rhine_native/audio/" + name + ".wav" }))
            map[name].next = 0
        }
        effects = map
    }

    function play(type) {
        if (!sound || !unlocked) return
        const now = Date.now() / 1000
        const gap = intervals[type] ?? 0.12
        if (now - (last[type] ?? -1e9) < gap) return
        last[type] = now
        const name = type === "key" ? "key-" + (keyIndex++ % 3) : type
        const voices = effects[name]
        if (!voices) return
        const voice = voices[voices.next++ % voices.length]
        voice.volume = soundVolume * 0.8
        voice.play()
        if (["open", "brand", "welcome", "array"].includes(type)) { duckAnimation.restart() }
    }
    SequentialAnimation {
        id: duckAnimation
        NumberAnimation { target: audio; property: "duck"; to: 0.65; duration: 35 }
        NumberAnimation { target: audio; property: "duck"; to: 1; duration: 900 }
    }

    // Scene mixes (mixScene / updateBoot).
    function mix(gains) { stemGains = gains }
    onSceneChanged: {
        bootPrevious = -1
        mix({ boot: [0.48, 0.32, 0.18], archive: [0.9, 0.72, 0.65], detail: [0.72, 0.36, 0.12] }[scene] ?? [0.9, 0.72, 0.65])
    }
    function updateBoot(appTime, frozen) {
        const time = appTime + 5
        const previous = bootPrevious
        bootPrevious = time
        const phase = time < 22.76 ? 0 : time < 26.92 ? 1 : time < 34.3 ? 2 : 3
        mix([[0.48, 0.32, 0.18], [0.68, 0.55, 0.32], [0.9, 0.72, 0.65], [0.72, 0.36, 0.12]][phase])
        if (frozen || previous < 0 || time < previous || time - previous > 0.3) return
        for (const [at, cue] of cues) if (at > previous && at <= time) play(cue)
        if (typingFrames.some(f => f / 25 > previous + 1e-6 && f / 25 <= time + 1e-6)) play("key")
    }

    readonly property bool playing: music && unlocked
    Repeater {
        model: ["atmosphere", "motif", "pulse"]
        delegate: Item {
            required property string modelData
            required property int index
            property real gain: audio.stemGains[index]
            Behavior on gain { NumberAnimation { duration: 1100 } }
            MediaPlayer {
                id: player
                source: "qrc:/rhine_native/audio/" + modelData + ".ogg"
                loops: MediaPlayer.Infinite
                audioOutput: AudioOutput { volume: audio.musicVolume * gain * audio.duck * 0.8 * fade.value }
                Component.onCompleted: if (audio.playing) play()
            }
            QtObject { id: fade; property real value: audio.playing ? 1 : 0; Behavior on value { NumberAnimation { duration: 1200 } } }
            Connections {
                target: audio
                function onPlayingChanged() { if (audio.playing) player.play(); else stopLater.restart() }
            }
            Timer { id: stopLater; interval: 1300; onTriggered: if (!audio.playing) player.pause() }
        }
    }
}
