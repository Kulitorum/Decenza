import QtQuick

// Three opacity pulses to draw the eye to a prompt that just appeared, ending fully opaque.
// Not forever: a running animation redraws the whole window every frame, which cost ~30% of a
// render core on a Galaxy Tab A9+ for as long as an idle prompt sat on screen. Use as
// `PromptPulse on opacity { running: prompt.visible }`; it pulses again whenever `running`
// turns true.
SequentialAnimation {
    loops: 3
    NumberAnimation { to: 0.45; duration: 800 }
    NumberAnimation { to: 1.0; duration: 800 }
}
