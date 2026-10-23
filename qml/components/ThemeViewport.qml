import QtQml
import Decenza

// The native window and browser use the same sizing rule and publish their own
// dimensions. Page/global preferences remain inputs to the shared Theme.
QtObject {
    id: root
    required property real viewportWidth
    required property real viewportHeight

    property Binding scaleBinding: Binding {
        target: Theme
        property: "scale"
        value: Math.min(root.viewportWidth / Theme.refWidth,
                        root.viewportHeight / Theme.refHeight)
               * Theme.scaleMultiplier * Theme.pageScaleMultiplier
    }
    property Binding widthBinding: Binding {
        target: Theme
        property: "windowWidth"
        value: root.viewportWidth
    }
    property Binding heightBinding: Binding {
        target: Theme
        property: "windowHeight"
        value: root.viewportHeight
    }
}
