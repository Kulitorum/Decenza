import QtQuick
import QtQuick.Templates as T
import Decenza

// Both shells derive glass chrome from the displayed page's actual background.
Binding {
    id: root
    required property T.Page page
    target: Theme
    property: "shotChartOnCurrentPage"
    value: {
        const surface = root.page ? root.page.background as BackgroundSurface : null
        return !!(surface && surface.paintsShotChart)
    }
}
