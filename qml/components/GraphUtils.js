.pragma library

// Pure geometry helpers shared by the live, history and comparison shot graphs.
//
// Each of these existed as an identical private function in two of the three graphs. They
// are pure functions of their arguments, so a shared library is enough — no QML singleton,
// no per-graph state.

// Tick spacing that keeps the time axis readable across the whole range of shot lengths,
// from a 5 s flush to a several-minute filter brew.
function niceTimeAxisStep(span) {
    if (span <= 5)  return 1
    if (span <= 10) return 2
    if (span <= 30) return 5
    return 10
}

// Map a pixel x within the plot area back to a time on the axis. Returns -1 when the plot
// has no width yet, which happens on the first layout pass before the view is measured.
function timeAtPixel(pixelX, plot, axisMin, axisMax) {
    if (!plot || plot.width <= 0) return -1
    return axisMin + (pixelX - plot.x) / plot.width * (axisMax - axisMin)
}

// Grow an axis end by a pixel padding, so the last sample does not sit flush against the
// right edge of the plot.
function paddedAxisEnd(axisEnd, plotWidth, paddingPx) {
    var w = Math.max(1, plotWidth)
    return axisEnd * w / Math.max(1, w - paddingPx)
}
