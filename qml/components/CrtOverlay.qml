import QtQuick

// CRT screen-surface overlay: thin scanlines drawn on top of the ENTIRE screen
// (including status bar, dialogs, etc.) for the authentic CRT look.
// The heavy effects (bloom, chromatic aberration, noise, vignette, jitter) are
// handled by the GPU shader via layer.effect on pageStack (CrtShaderEffect).
// This overlay just adds the "glass surface" feel.

Item {
    id: crtRoot
    anchors.fill: parent
    visible: Settings.theme.activeShader === "crt"
    enabled: false  // Pass through all input events

    // ── Scanlines: repeating semi-transparent horizontal bars ──
    // Drawn on top of everything for consistent CRT look across all UI
    Canvas {
        id: scanlines
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = "rgba(0, 0, 0, 0.08)"
            var gap = 3
            for (var y = 0; y < height; y += gap) {
                ctx.fillRect(0, y, width, 1)
            }
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
