import QtQuick
import Decenza

// Animated cup-fill visualization for espresso extraction.
// Uses 3D-rendered cup images (background + overlay) with procedural coffee rendering.
// Overlay composited with lighten (MAX) blend via GPU shader.
//
// Layer stack:
//   1. BackGround.png — cup back, interior, handle
//   2. Coffee Canvas (masked by Mask.png) — liquid fill, crema, waves
//   3. Effects Canvas (unmasked) — portafilter, stream, steam, glow
//   4. Overlay.png — rim, front highlights (lighten blend over all above)
//   5. Weight text
Item {
    id: root

    property real currentWeight: 0
    property real targetWeight: 36
    property real currentFlow: 0
    property real shotTime: 0
    property int phase: 0
    property real currentPressure: 0
    property real goalPressure: 0
    property real goalFlow: 0

    // Tracking color: green = on track, yellow = drifting, red = way off
    readonly property bool hasGoal: (goalPressure > 0 || goalFlow > 0) &&
        phase !== MachineStateType.Phase.EspressoPreheating
    readonly property color trackColor: {
        if (!hasGoal) return Theme.textSecondaryColor
        var isPressure = goalPressure > 0
        var delta = isPressure ? Math.abs(currentPressure - goalPressure)
                               : Math.abs(currentFlow - goalFlow)
        var goal = isPressure ? goalPressure : goalFlow
        return Theme.trackingColor(delta, goal, isPressure)
    }

    property real wavePhase: 0
    property real steamPhase: 0
    property real ripplePhase: 0

    Timer {
        id: animTimer
        interval: 33
        repeat: true
        running: root.currentFlow > 0.1 ||
                 (root.currentWeight > 0 && root.phase >= MachineStateType.Phase.EspressoPreheating
                                         && root.phase <= MachineStateType.Phase.Ending)
        onTriggered: {
            if (root.currentFlow > 0.1) {
                root.wavePhase += 0.15
                root.ripplePhase += 0.12
            }
            root.steamPhase += 0.04
            liquidCanvas.requestPaint()
            effectsCanvas.requestPaint()
        }
    }

    onCurrentWeightChanged: { liquidCanvas.requestPaint(); effectsCanvas.requestPaint() }
    onTargetWeightChanged:  { liquidCanvas.requestPaint(); effectsCanvas.requestPaint() }
    onCurrentFlowChanged:   { if (!animTimer.running) { liquidCanvas.requestPaint(); effectsCanvas.requestPaint() } }

    // Cup image dimensions (aspect ratio 701:432, scaled to 80%)
    readonly property real cupScale: 0.8
    readonly property real cupAspect: 701.0 / 432.0
    readonly property real cupDisplayW: Math.min(width, height * cupAspect) * cupScale
    readonly property real cupDisplayH: cupDisplayW / cupAspect
    readonly property real cupX: (width - cupDisplayW) / 2 + Theme.scaled(30)
    readonly property real cupY: height - cupDisplayH  // bottom-anchored

    // Shared geometry (computed from cup image proportions, used by both canvases)
    function cupGeometry(w, h) {
        var cx = w * 0.44
        var rimCy = h * 0.06
        var botCy = h * 0.92
        var cupW = w * 0.46
        var cupBotW = w * 0.40
        var rimOvalH = h * 0.08
        var botOvalH = h * 0.04
        var cupH = botCy - rimCy

        // Only show fill during/after extraction. Pre-flow phases like
        // EspressoPreheating render empty regardless of currentWeight — the
        // stale-residual case is solved at the source in MachineState
        // (see m_hotWaterFrozenWeight clearing on cycle entry).
        var hasExtraction = root.phase === MachineStateType.Phase.Preinfusion
                         || root.phase === MachineStateType.Phase.Pouring
                         || root.phase === MachineStateType.Phase.Ending
        var fillRatio = (root.targetWeight > 0 && hasExtraction)
            ? Math.min(root.currentWeight / root.targetWeight, 1.0) : 0
        var interiorH = cupH * 0.8  // 100% fill reaches 80% of cup height
        var fillH = fillRatio * interiorH
        var fillTopY = botCy - fillH
        var isPouring = root.phase === MachineStateType.Phase.Preinfusion ||
                        root.phase === MachineStateType.Phase.Pouring

        return {
            cx: cx, rimCy: rimCy, botCy: botCy,
            cupW: cupW, cupBotW: cupBotW,
            rimOvalH: rimOvalH, botOvalH: botOvalH, cupH: cupH,
            fillRatio: fillRatio, interiorH: interiorH,
            fillH: fillH, fillTopY: fillTopY, isPouring: isPouring
        }
    }

    // Helper: draw front half of an ellipse arc (PI to 2*PI)
    function frontArc(ctx, cx, cy, rx, ry, steps, asMove) {
        for (var i = 0; i <= steps; i++) {
            var a = Math.PI + (Math.PI * i / steps)
            var x = cx + rx * Math.cos(a)
            var y = cy + ry * Math.sin(a)
            if (i === 0 && asMove) ctx.moveTo(x, y)
            else ctx.lineTo(x, y)
        }
    }

    // ================================================================
    // Composite layer: background + masked coffee + effects
    // Lighten-blended with overlay via GPU shader
    // ================================================================
    Item {
        id: compositeLayer
        x: root.cupX
        y: root.cupY
        width: root.cupDisplayW
        height: root.cupDisplayH
        layer.enabled: true
        layer.smooth: true
        layer.effect: ShaderEffect {
            property var overlayTex: Image {
                source: "qrc:/CoffeeCup/Overlay.png"
                visible: false
            }
            fragmentShader: "qrc:/shaders/cup_lighten.frag.qsb"
        }

        // Sub-layer 1: Cup background image
        Image {
            anchors.fill: parent
            source: "qrc:/CoffeeCup/BackGround.png"
            smooth: true
            fillMode: Image.Stretch
        }

        // Sub-layer 2: Coffee liquid (masked to cup interior)
        Item {
            id: maskedCoffee
            anchors.fill: parent
            layer.enabled: true
            layer.smooth: true
            layer.effect: ShaderEffect {
                property var maskTex: Image {
                    source: "qrc:/CoffeeCup/Mask.png"
                    visible: false
                }
                fragmentShader: "qrc:/shaders/cup_mask.frag.qsb"
            }

            Canvas {
                id: liquidCanvas
                anchors.fill: parent
                renderStrategy: Canvas.Threaded

                onPaint: {
                    var ctx = getContext("2d")
                    var w = width, h = height
                    ctx.reset()
                    ctx.clearRect(0, 0, w, h)
                    if (w < 1 || h < 1) return

                    var g = root.cupGeometry(w, h)
                    var N = 36

                    // Only show coffee once scale reports weight
                    if (root.currentWeight <= 0) return

                    // Shift fill forward so coffee appears with crema at first weight
                    var effectiveFillRatio = Math.min(g.fillRatio + 0.12, 1.0)
                    var effectiveFillH = effectiveFillRatio * g.interiorH
                    var effectiveFillTopY = g.botCy - effectiveFillH

                    var t = effectiveFillH / g.interiorH
                    var fillRx = g.cupBotW + (g.cupW - g.cupBotW) * t
                    var fillOvalH = g.botOvalH + (g.rimOvalH - g.botOvalH) * t
                    var waveAmp = root.currentFlow > 0.1 ? h * 0.006 : 0
                    var wSteps = 36

                    // Liquid body — uses same effectiveFillTopY as crema
                    var liqVertGrad = ctx.createLinearGradient(0, g.botCy + g.botOvalH, 0, effectiveFillTopY)
                    liqVertGrad.addColorStop(0, "#140600")
                    liqVertGrad.addColorStop(0.15, "#1E0A00")
                    liqVertGrad.addColorStop(0.35, "#3A1808")
                    liqVertGrad.addColorStop(0.55, "#5A2E14")
                    liqVertGrad.addColorStop(0.75, "#7A4422")
                    liqVertGrad.addColorStop(0.9, "#a86e3e")
                    liqVertGrad.addColorStop(1.0, "#c08050")

                    ctx.beginPath()
                    ctx.moveTo(g.cx - g.cupBotW, g.botCy)
                    root.frontArc(ctx, g.cx, g.botCy, g.cupBotW, g.botOvalH, N, false)
                    ctx.lineTo(g.cx + fillRx, effectiveFillTopY)
                    for (var wi = wSteps; wi >= 0; wi--) {
                        var wx = g.cx - fillRx + fillRx * 2 * (wi / wSteps)
                        var wy = effectiveFillTopY + Math.sin(root.wavePhase + wi * 0.5) * waveAmp
                        ctx.lineTo(wx, wy)
                    }
                    ctx.closePath()
                    ctx.fillStyle = liqVertGrad
                    ctx.fill()

                    // Horizontal curvature shading
                    var liqHGrad = ctx.createLinearGradient(g.cx - g.cupW, 0, g.cx + g.cupW, 0)
                    liqHGrad.addColorStop(0, Qt.rgba(0, 0, 0, 0.5))
                    liqHGrad.addColorStop(0.12, Qt.rgba(0, 0, 0, 0.18))
                    liqHGrad.addColorStop(0.35, Qt.rgba(0.15, 0.08, 0.03, 0.08))
                    liqHGrad.addColorStop(0.5, Qt.rgba(0.2, 0.1, 0.04, 0.04))
                    liqHGrad.addColorStop(0.65, Qt.rgba(0.1, 0.05, 0.02, 0.08))
                    liqHGrad.addColorStop(0.88, Qt.rgba(0, 0, 0, 0.22))
                    liqHGrad.addColorStop(1, Qt.rgba(0, 0, 0, 0.55))
                    ctx.globalAlpha = 0.6
                    ctx.fillStyle = liqHGrad
                    ctx.fill()
                    ctx.globalAlpha = 1.0

                    // Surface specular
                    if (effectiveFillRatio > 0.05) {
                        var surfGrad = ctx.createLinearGradient(g.cx - fillRx, effectiveFillTopY, g.cx + fillRx, effectiveFillTopY)
                        surfGrad.addColorStop(0, Qt.rgba(1, 0.95, 0.85, 0))
                        surfGrad.addColorStop(0.25, Qt.rgba(1, 0.95, 0.85, 0.06))
                        surfGrad.addColorStop(0.4, Qt.rgba(1, 0.95, 0.85, 0.14))
                        surfGrad.addColorStop(0.5, Qt.rgba(1, 0.97, 0.90, 0.18))
                        surfGrad.addColorStop(0.6, Qt.rgba(1, 0.95, 0.85, 0.12))
                        surfGrad.addColorStop(0.75, Qt.rgba(1, 0.95, 0.85, 0.04))
                        surfGrad.addColorStop(1, Qt.rgba(1, 0.95, 0.85, 0))
                        ctx.fillStyle = surfGrad
                        ctx.beginPath()
                        for (var wh = 0; wh <= wSteps; wh++) {
                            var whx = g.cx - fillRx * 0.92 + fillRx * 1.84 * (wh / wSteps)
                            var why = effectiveFillTopY + Math.sin(root.wavePhase + wh * 0.5) * waveAmp - h * 0.002
                            if (wh === 0) ctx.moveTo(whx, why)
                            else ctx.lineTo(whx, why)
                        }
                        for (var wh2 = wSteps; wh2 >= 0; wh2--) {
                            var wh2x = g.cx - fillRx * 0.92 + fillRx * 1.84 * (wh2 / wSteps)
                            var wh2y = effectiveFillTopY + Math.sin(root.wavePhase + wh2 * 0.5) * waveAmp + h * 0.014
                            ctx.lineTo(wh2x, wh2y)
                        }
                        ctx.closePath()
                        ctx.fill()
                    }

                    // Crema
                    if (effectiveFillRatio > 0.10) {
                        var cremaFade = Math.min((effectiveFillRatio - 0.10) / 0.15, 1.0)
                        var cremaRx = fillRx * 0.88
                        var cremaRy = fillOvalH * 0.75

                        var cremaGrad = ctx.createRadialGradient(
                            g.cx - cremaRx * 0.1, effectiveFillTopY, cremaRx * 0.1,
                            g.cx, effectiveFillTopY, cremaRx)
                        cremaGrad.addColorStop(0, Qt.rgba(0.88, 0.72, 0.48, 0.6 * cremaFade))
                        cremaGrad.addColorStop(0.25, Qt.rgba(0.85, 0.65, 0.38, 0.5 * cremaFade))
                        cremaGrad.addColorStop(0.55, Qt.rgba(0.78, 0.55, 0.30, 0.38 * cremaFade))
                        cremaGrad.addColorStop(0.8, Qt.rgba(0.68, 0.45, 0.22, 0.2 * cremaFade))
                        cremaGrad.addColorStop(1, Qt.rgba(0.55, 0.35, 0.15, 0))
                        ctx.beginPath()
                        ctx.ellipse(g.cx - cremaRx, effectiveFillTopY - cremaRy, cremaRx * 2, cremaRy * 2)
                        ctx.fillStyle = cremaGrad
                        ctx.fill()

                        // Tiger stripes
                        var stripeAlpha = 0.25 * cremaFade
                        for (var ts = 0; ts < 4; ts++) {
                            var tsRadius = cremaRx * (0.3 + ts * 0.18)
                            var tsRy2 = cremaRy * (0.25 + ts * 0.15)
                            var tsPhase = root.steamPhase * (0.15 + ts * 0.05) + ts * 1.5
                            var tsStartAngle = tsPhase % (Math.PI * 2)
                            var tsArcLen = Math.PI * (0.5 + ts * 0.15)

                            ctx.strokeStyle = Qt.rgba(0.45, 0.25, 0.10, stripeAlpha * (1 - ts * 0.18))
                            ctx.lineWidth = Math.max(1, h * 0.004 + ts * h * 0.001)
                            ctx.beginPath()
                            var tsSteps = 16
                            for (var tsi = 0; tsi <= tsSteps; tsi++) {
                                var tsAngle = tsStartAngle + tsArcLen * tsi / tsSteps
                                var tsx = g.cx + tsRadius * Math.cos(tsAngle)
                                var tsy = effectiveFillTopY + tsRy2 * Math.sin(tsAngle) +
                                          Math.sin(root.wavePhase + tsi * 0.3) * waveAmp * 0.3
                                if (tsi === 0) ctx.moveTo(tsx, tsy)
                                else ctx.lineTo(tsx, tsy)
                            }
                            ctx.stroke()
                        }

                        // Crema outer ring
                        ctx.strokeStyle = Qt.rgba(0.9, 0.78, 0.55, 0.3 * cremaFade)
                        ctx.lineWidth = Math.max(1, h * 0.005)
                        ctx.beginPath()
                        for (var cj = 0; cj <= wSteps; cj++) {
                            var crAng = Math.PI * cj / wSteps
                            var crx = g.cx - cremaRx * Math.cos(crAng)
                            var cry = effectiveFillTopY - cremaRy * 0.3 * Math.sin(crAng) +
                                      Math.sin(root.wavePhase + cj * 0.5) * waveAmp * 0.4
                            if (cj === 0) ctx.moveTo(crx, cry)
                            else ctx.lineTo(crx, cry)
                        }
                        ctx.stroke()

                        // Bright center spot
                        var centerSpot = ctx.createRadialGradient(
                            g.cx - cremaRx * 0.15, effectiveFillTopY - cremaRy * 0.1, h * 0.005,
                            g.cx - cremaRx * 0.15, effectiveFillTopY - cremaRy * 0.1, cremaRx * 0.35)
                        centerSpot.addColorStop(0, Qt.rgba(1, 0.95, 0.8, 0.2 * cremaFade))
                        centerSpot.addColorStop(0.5, Qt.rgba(1, 0.9, 0.7, 0.08 * cremaFade))
                        centerSpot.addColorStop(1, Qt.rgba(1, 0.85, 0.6, 0))
                        ctx.beginPath()
                        ctx.ellipse(g.cx - cremaRx * 0.5, effectiveFillTopY - cremaRy * 0.45,
                                    cremaRx * 0.7, cremaRy * 0.7)
                        ctx.fillStyle = centerSpot
                        ctx.fill()
                    }

                    // Meniscus darkening
                    var menW = w * 0.03, menH = h * 0.03
                    var menGradL = ctx.createLinearGradient(g.cx - fillRx, 0, g.cx - fillRx + menW, 0)
                    menGradL.addColorStop(0, Qt.rgba(0, 0, 0, 0.35))
                    menGradL.addColorStop(1, Qt.rgba(0, 0, 0, 0))
                    ctx.fillStyle = menGradL
                    ctx.fillRect(g.cx - fillRx, effectiveFillTopY - h * 0.01, menW, menH)
                    var menGradR = ctx.createLinearGradient(g.cx + fillRx, 0, g.cx + fillRx - menW, 0)
                    menGradR.addColorStop(0, Qt.rgba(0, 0, 0, 0.35))
                    menGradR.addColorStop(1, Qt.rgba(0, 0, 0, 0))
                    ctx.fillStyle = menGradR
                    ctx.fillRect(g.cx + fillRx - menW, effectiveFillTopY - h * 0.01, menW, menH)

                    // Ripple rings
                    if (g.isPouring && root.currentFlow > 0.3 && effectiveFillRatio < 0.95) {
                        for (var rp = 0; rp < 3; rp++) {
                            var rpAge = (root.ripplePhase + rp * 2.0) % 6.0
                            if (rpAge > 3.0) continue
                            var rpProgress = rpAge / 3.0
                            var rpAlpha = (1.0 - rpProgress) * 0.2
                            var rpRadiusX = h * 0.01 + rpProgress * fillRx * 0.35
                            var rpRadiusY = rpRadiusX * (fillOvalH / fillRx)
                            ctx.strokeStyle = Qt.rgba(0.9, 0.8, 0.6, rpAlpha)
                            ctx.lineWidth = Math.max(1, h * 0.002)
                            ctx.beginPath()
                            for (var rpi = 0; rpi <= 20; rpi++) {
                                var rpAngle = Math.PI + (Math.PI * rpi / 20)
                                var rpx = g.cx + rpRadiusX * Math.cos(rpAngle)
                                var rpy = effectiveFillTopY + rpRadiusY * Math.sin(rpAngle) +
                                          Math.sin(root.wavePhase) * waveAmp * 0.5
                                if (rpi === 0) ctx.moveTo(rpx, rpy)
                                else ctx.lineTo(rpx, rpy)
                            }
                            ctx.stroke()
                        }
                    }
                }
            }
        }

    }

    // ================================================================
    // Effects layer (outside lighten blend — portafilter, stream, steam, glow)
    // Extended above the cup so the stream can enter from off-screen
    // ================================================================
    readonly property real effectsExtra: root.cupY  // extra space above cup
    Canvas {
        id: effectsCanvas
        x: root.cupX
        y: 0
        width: root.cupDisplayW
        height: root.cupDisplayH + root.effectsExtra
        renderStrategy: Canvas.Threaded

            onPaint: {
                var ctx = getContext("2d")
                var w = width
                var canvasH = height
                var cupH = root.cupDisplayH
                var yOff = root.effectsExtra  // offset: cup geometry starts at yOff
                ctx.reset()
                ctx.clearRect(0, 0, w, canvasH)
                if (w < 1 || cupH < 1) return

                var g = root.cupGeometry(w, cupH)
                // Shift all geometry down by yOff to align with cup image
                g.rimCy += yOff
                g.botCy += yOff
                g.fillTopY += yOff
                // Match the visual fill level used by the liquid canvas (+0.12 boost)
                var effectiveFillRatio = Math.min(g.fillRatio + 0.12, 1.0)
                var effectiveFillH = effectiveFillRatio * g.interiorH
                var effectiveFillTopY = g.botCy - effectiveFillH
                var N = 36

                // ---- Stream from above (during extraction) ----
                if (g.isPouring && root.currentFlow > 0.3 && root.currentWeight > 0 && g.fillRatio < 1.0) {
                    var streamTopY = 0  // enters from top of screen
                    var cupFloorY = g.botCy - g.botOvalH - cupH * 0.02
                    var streamBot = root.currentWeight > 0 ? Math.min(effectiveFillTopY, cupFloorY) : cupFloorY
                    var streamLen = Math.max(streamBot - streamTopY, cupH * 0.01)
                    var streamTopW = Math.min(cupH * 0.032 + root.currentFlow * cupH * 0.007, cupH * 0.06)
                    var streamBotW = streamTopW * 0.5
                    var streamSegs = 20

                    ctx.beginPath()
                    for (var ssL = 0; ssL <= streamSegs; ssL++) {
                        var ssLt = ssL / streamSegs
                        var wobL = Math.sin(root.wavePhase * 2.2 + ssLt * 5.0) * cupH * 0.006 * ssLt
                        var ssLw = streamTopW + (streamBotW - streamTopW) * ssLt
                        if (ssL === 0) ctx.moveTo(g.cx + wobL - ssLw * 0.5, streamTopY + streamLen * ssLt)
                        else ctx.lineTo(g.cx + wobL - ssLw * 0.5, streamTopY + streamLen * ssLt)
                    }
                    for (var ssR = streamSegs; ssR >= 0; ssR--) {
                        var ssRt = ssR / streamSegs
                        var wobR = Math.sin(root.wavePhase * 2.2 + ssRt * 5.0) * cupH * 0.006 * ssRt
                        var ssRw = streamTopW + (streamBotW - streamTopW) * ssRt
                        ctx.lineTo(g.cx + wobR + ssRw * 0.5, streamTopY + streamLen * ssRt)
                    }
                    ctx.closePath()
                    var stGrad = ctx.createLinearGradient(0, streamTopY, 0, streamBot)
                    stGrad.addColorStop(0, Qt.rgba(0.55, 0.35, 0.18, 0.5))
                    stGrad.addColorStop(0.15, Qt.rgba(0.52, 0.32, 0.16, 0.65))
                    stGrad.addColorStop(0.4, Qt.rgba(0.48, 0.28, 0.14, 0.75))
                    stGrad.addColorStop(0.7, Qt.rgba(0.42, 0.24, 0.12, 0.82))
                    stGrad.addColorStop(1, Qt.rgba(0.38, 0.20, 0.10, 0.88))
                    ctx.fillStyle = stGrad
                    ctx.fill()

                    // Stream center highlight
                    ctx.beginPath()
                    for (var ssC = 0; ssC <= streamSegs; ssC++) {
                        var ssCt = ssC / streamSegs
                        var wobC = Math.sin(root.wavePhase * 2.2 + ssCt * 5.0) * cupH * 0.006 * ssCt
                        if (ssC === 0) ctx.moveTo(g.cx + wobC, streamTopY + streamLen * ssCt)
                        else ctx.lineTo(g.cx + wobC, streamTopY + streamLen * ssCt)
                    }
                    ctx.strokeStyle = Qt.rgba(0.75, 0.55, 0.32, 0.25)
                    ctx.lineWidth = streamTopW * 0.25
                    ctx.lineCap = "round"
                    ctx.stroke()

                    // Droplets
                    for (var dr = 0; dr < 5; dr++) {
                        var drT = ((root.wavePhase * 1.5 + dr * 1.4) % 3.5) / 3.5
                        if (drT > 0.95) continue
                        var drY = streamTopY + streamLen * drT
                        if (root.currentWeight > 0 && drY > effectiveFillTopY + cupH * 0.01) continue
                        var drWob = Math.sin(root.wavePhase * 2.2 + drT * 5.0) * cupH * 0.006 * drT
                        var drSize = (cupH * 0.006 + root.currentFlow * cupH * 0.001) * (1 - drT * 0.3)
                        ctx.beginPath()
                        ctx.arc(g.cx + drWob, drY, drSize, 0, Math.PI * 2)
                        ctx.fillStyle = Qt.rgba(0.65, 0.42, 0.22, 0.4 + drT * 0.35)
                        ctx.fill()
                    }

                    // Splash glow
                    var splashY = root.currentWeight > 0 ? Math.min(effectiveFillTopY, cupFloorY) : cupFloorY
                    var splashR = cupH * 0.042
                    var splashGlow = ctx.createRadialGradient(g.cx, splashY, cupH * 0.005, g.cx, splashY, splashR)
                    splashGlow.addColorStop(0, Qt.rgba(0.7, 0.5, 0.3, 0.5))
                    splashGlow.addColorStop(0.4, Qt.rgba(0.6, 0.4, 0.2, 0.2))
                    splashGlow.addColorStop(1, Qt.rgba(0.5, 0.3, 0.15, 0))
                    ctx.beginPath()
                    ctx.arc(g.cx, splashY, splashR, 0, Math.PI * 2)
                    ctx.fillStyle = splashGlow
                    ctx.fill()
                }

                // ---- Steam wisps ----
                if (root.currentWeight > 0 && root.currentFlow < 2.0) {
                    var steamAlphaBase = Math.min(root.currentWeight / 5.0, 1.0) * 0.14
                    for (var sp = 0; sp < 4; sp++) {
                        var steamOffX = (sp - 1.5) * g.cupW * 0.25
                        var steamBaseY = root.currentWeight > 0 ? effectiveFillTopY : g.rimCy
                        var steamStartY = Math.min(steamBaseY, g.rimCy) - cupH * 0.007
                        var steamH2 = cupH * (0.07 + sp * 0.023)
                        var steamEndY = steamStartY - steamH2

                        var phOff = root.steamPhase * (0.8 + sp * 0.2) + sp * 1.7
                        var d1x = Math.sin(phOff) * w * (0.015 + sp * 0.003)
                        var d1y = steamH2 * 0.33
                        var d2x = Math.sin(phOff * 1.2 + 2) * w * (0.012 + sp * 0.003)
                        var d2y = steamH2 * 0.66

                        var segments = 12
                        for (var seg = 0; seg < segments; seg++) {
                            var t0 = seg / segments, t1 = (seg + 1) / segments
                            // Cubic bezier evaluation for steam S-curve
                            function stmX(tt) {
                                var s0 = g.cx+steamOffX, s1 = g.cx+steamOffX+d1x
                                var s2 = g.cx+steamOffX+d2x, s3 = g.cx+steamOffX+d1x*0.5
                                var u = 1-tt
                                return u*u*u*s0 + 3*u*u*tt*s1 + 3*u*tt*tt*s2 + tt*tt*tt*s3
                            }
                            function stmY(tt) {
                                var s0 = steamStartY, s1 = steamStartY-d1y
                                var s2 = steamStartY-d2y, s3 = steamEndY
                                var u = 1-tt
                                return u*u*u*s0 + 3*u*u*tt*s1 + 3*u*tt*tt*s2 + tt*tt*tt*s3
                            }
                            ctx.strokeStyle = Qt.rgba(0.85, 0.85, 0.9,
                                steamAlphaBase * (1 - t0 * 0.85) * (0.5 + sp * 0.12))
                            ctx.lineWidth = Math.max(1, cupH * (0.002 + Math.sin(t0 * Math.PI) * 0.004))
                            ctx.lineCap = "round"
                            ctx.beginPath()
                            ctx.moveTo(stmX(t0), stmY(t0))
                            ctx.lineTo(stmX(t1), stmY(t1))
                            ctx.stroke()
                        }
                    }
                }

                // ---- Completion glow (disabled — tracking color still computed above) ----
                // if (root.currentWeight >= root.targetWeight && root.targetWeight > 0) {
                //     ctx.shadowColor = Theme.successColor
                //     ctx.shadowBlur = cupH * 0.06
                //     ctx.strokeStyle = Theme.successColor
                //     ctx.lineWidth = Math.max(1, cupH * 0.005)
                //     ctx.beginPath()
                //     ctx.moveTo(g.cx - g.cupW, g.rimCy)
                //     ctx.lineTo(g.cx + g.cupW, g.rimCy)
                //     ctx.stroke()
                //     ctx.shadowBlur = 0
                // }
            }
        }

    // ================================================================
    // Weight text overlay
    // ================================================================
    Column {
        x: root.cupX + root.cupDisplayW * 0.44 - width / 2  // centered on cup's cx (44%)
        y: root.cupY + root.cupDisplayH * 0.55 - height / 2
        spacing: Theme.scaled(2)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.currentWeight.toFixed(1) + TranslationManager.translate("common.unit.grams", "g")
            color: Theme.tintedOverlayColor(root.trackColor, 0.5)
            font.pixelSize: Theme.scaled(38)
            font.weight: Font.Bold
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.targetWeight > 0
                ? TranslationManager.translate("espresso.cupFill.target", "target") + " " + root.targetWeight.toFixed(0) + TranslationManager.translate("common.unit.grams", "g")
                : ""
            color: Theme.tintedOverlayColor(root.trackColor, 0.4)
            font.pixelSize: Theme.scaled(16)
        }
    }
}
