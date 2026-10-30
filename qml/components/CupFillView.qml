import QtQuick
import QtCanvas2D
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
    // An americano or long black's added water (recipe-drink-sequence). Non-zero
    // only for a drink in the cup view; latched below so the drink keeps its full
    // size after the flag that supplied it clears.
    property real waterTarget: 0
    // The scale's flow: the DE1 reports no group flow while hot water pours, so
    // the water's stream is driven by this.
    property real waterFlow: 0

    // Tracking color: green = on track, yellow = drifting, red = way off
    readonly property bool hasGoal: (goalPressure > 0 || goalFlow > 0) &&
        phase !== MachineState.Phase.EspressoPreheating
    readonly property color trackColor: {
        if (!hasGoal) return Theme.textSecondaryColor
        var isPressure = goalPressure > 0
        var delta = isPressure ? Math.abs(currentPressure - goalPressure)
                               : Math.abs(currentFlow - goalFlow)
        var goal = isPressure ? goalPressure : goalFlow
        return Theme.trackingColor(delta, goal, isPressure)
    }

    // ---- Post-shot hold ----
    // The cup keeps showing what it drew until this view is destroyed by
    // navigation — the espresso page stays mounted for the stop-overlay window
    // after the machine has already left the espresso cycle, and without the
    // latch below the fill collapses to 0 the moment phase reaches Ready.
    // The hold needs no expiry: StackView.replace() destroys the view, so
    // "until the screen changes" is this instance's lifetime. Two things end it
    // early, both deliberate — a *new* cycle on a surviving instance (handled
    // below), and the user toggling to the chart and back, which destroys and
    // recreates the view via EspressoPage's Loader and so starts a fresh
    // instance with nothing latched.
    // Phase predicates as functions, not bindings: onPhaseChanged below needs
    // each one for two different phase values (the new one and the previous).
    // Spelled out rather than tested as an ordinal range — every C++ site that
    // asks the same question enumerates the four values (machinestate.cpp's
    // isInEspresso, profilemanager.cpp, mcptools_control.cpp), and a range here
    // would silently change meaning if a phase were ever inserted between
    // EspressoPreheating and Ending, with nothing failing.
    function isExtractionPhase(p) {
        return p === MachineState.Phase.Preinfusion
            || p === MachineState.Phase.Pouring
            || p === MachineState.Phase.Ending
    }
    function isEspressoCycle(p) {
        return p === MachineState.Phase.EspressoPreheating
            || p === MachineState.Phase.Preinfusion
            || p === MachineState.Phase.Pouring
            || p === MachineState.Phase.Ending
    }
    readonly property bool extractionPhase: isExtractionPhase(phase)
    readonly property bool inEspressoCycle: isEspressoCycle(phase)
    property bool extractionSeen: false
    property real heldWeight: 0
    readonly property bool holding: extractionSeen && !inEspressoCycle
    // Everything drawn reads this, never currentWeight — it closes both blank
    // paths at once (the phase gate in cupGeometry() and the weight guards in
    // the two canvases), so the cup also survives being lifted off the scale.
    // This is the only place that deliberately shows a weight the scale is no
    // longer reporting, and only while holding: the cup is a picture of the
    // shot, not a live instrument.
    readonly property real displayWeight: holding ? heldWeight : currentWeight

    // ---- A drink's water ----
    // The scale is tared when hot water starts, so during HotWater it reads the
    // water alone; its peak is the water in the cup, kept across a long black's
    // espresso (which tares again). Coffee is held at its peak while water pours.
    readonly property bool waterPhase: phase === MachineState.Phase.HotWater
    property real waterWeight: 0
    // The hot-water tare lands ~200 ms into the pour and re-bases the reading to
    // zero; until it does, the reading still carries what was in the cup before
    // (an americano's shot), so the water is the rise above the reading at the
    // water's start. A reading that drops below that start is the tare landing.
    property real waterStartReading: 0
    property bool waterTared: false
    property real latchedWaterTarget: 0
    // A long black: water reached the cup before any coffee. Its crema survives.
    property bool waterFirst: false
    readonly property real coffeeWeight: waterPhase ? (extractionSeen ? heldWeight : 0) : displayWeight
    readonly property real liquidWeight: coffeeWeight + waterWeight
    readonly property real drinkTarget: targetWeight + latchedWaterTarget
    readonly property real waterShare: liquidWeight > 0 ? waterWeight / liquidWeight : 0
    readonly property real streamFlow: waterPhase ? Math.max(currentFlow, waterFlow) : currentFlow
    // How full the cup looks. By weight a shot is ~10% of an americano and would
    // barely move, so a drink is drawn half coffee, half water, each filling its
    // half by its own progress. The weight text still reports the real grams.
    readonly property real visualFill: {
        if (latchedWaterTarget <= 0)
            return drinkTarget > 0 ? Math.min(liquidWeight / drinkTarget, 1.0) : 0
        const coffeePart = targetWeight > 0 ? Math.min(coffeeWeight / targetWeight, 1.0) : 0
        const waterPart = Math.min(waterWeight / latchedWaterTarget, 1.0)
        return 0.5 * coffeePart + 0.5 * waterPart
    }
    onWaterTargetChanged: { if (waterTarget > 0) latchedWaterTarget = waterTarget }

    // heldWeight tracks the peak weight seen since flow began, rather than
    // being sampled once when the cycle ends. A single boundary sample had two
    // defects, and peak-tracking removes both:
    //   - The puck keeps dripping after the stop, and the saved yield includes
    //     it (finalWeight in maincontroller.cpp comes from the post-settling
    //     weight, and settling runs for up to ~10 s). A boundary sample froze
    //     the cup several grams below the number the shot record, the review
    //     page and the page's own live readout all report — visibly so, since
    //     that readout sits beside the cup.
    //   - Lifting the cup drives the scale negative. A boundary sample taken at
    //     that instant froze a negative weight for the rest of the view's life,
    //     turning a transient into a permanently empty cup that putting the cup
    //     back could no longer fix.
    // Tracking only once extraction has been seen keeps a pre-flow residual
    // reading (an untared cup during preheat) out of the peak.
    onCurrentWeightChanged: {
        if (waterPhase) {
            if (!waterTared && currentWeight < waterStartReading - 0.5)
                waterTared = true
            const water = waterTared ? currentWeight : currentWeight - waterStartReading
            if (water > waterWeight)
                waterWeight = water
        } else if (extractionSeen && currentWeight > heldWeight) {
            heldWeight = currentWeight
        }
    }

    property int previousPhase: -1
    onPhaseChanged: {
        if (isEspressoCycle(phase) && !isEspressoCycle(previousPhase)) {
            // Entering a cycle — discard any frame held from the previous shot,
            // unless this espresso joins a long black's water.
            extractionSeen = false
            heldWeight = 0
            if (!waterFirst || waterTarget <= 0) {
                waterFirst = false
                waterWeight = 0
                latchedWaterTarget = waterTarget
            }
        }
        if (phase === MachineState.Phase.HotWater && previousPhase !== MachineState.Phase.HotWater) {
            waterFirst = !extractionSeen
            if (waterFirst)
                waterWeight = 0
            waterStartReading = currentWeight
            waterTared = false
        }
        if (isExtractionPhase(phase))
            extractionSeen = true
        previousPhase = phase
    }

    Component.onCompleted: {
        // Seed the phase tracking, and latch immediately when the view is
        // created mid-shot (switching from the chart view during extraction) —
        // onPhaseChanged would otherwise not run until the next transition.
        previousPhase = phase
        if (isExtractionPhase(phase))
            extractionSeen = true
        // Opened while water pours: a long black's first step (the espresso is
        // still to come), or the user switching to the cup during an americano's
        // water, whose finished shot is drawn at its target. Either way the
        // reading is past its tare by now.
        if (waterPhase) {
            waterTared = true
            if (MachineControlService.pendingDrinkStep === MachineControlService.DrinkStep.Espresso) {
                waterFirst = true
            } else {
                extractionSeen = true
                heldWeight = targetWeight
            }
        }
    }

    property real wavePhase: 0
    property real steamPhase: 0
    property real ripplePhase: 0

    Timer {
        id: animTimer
        interval: 33
        repeat: true
        // Stays false through the post-shot hold — inEspressoCycle is already
        // false there — so nothing animates while the cup is held, rather than
        // repainting for the whole post-shot window (the stop overlay, plus
        // however long SAW settling delays the save behind it). The held cup
        // can still redraw when late drip raises the peak; that is data
        // arriving, not animation.
        running: root.streamFlow > 0.1 ||
                 (root.liquidWeight > 0 && (root.inEspressoCycle || root.waterPhase))
        onTriggered: {
            if (root.streamFlow > 0.1) {
                root.wavePhase += 0.15
                root.ripplePhase += 0.12
            }
            root.steamPhase += 0.04
            liquidCanvas.requestPaint()
            effectsCanvas.requestPaint()
        }
    }

    // Repaint on liquidWeight, not currentWeight: during a hold it changes only
    // when late drip raises the held peak, not on every scale sample. While
    // animTimer runs, its next tick (<= 33 ms) draws the new weight anyway.
    onLiquidWeightChanged: { if (!animTimer.running) { liquidCanvas.requestPaint(); effectsCanvas.requestPaint() } }
    // Redraw once when the hold begins, so the frame comes from the held state
    // rather than whatever the last live paint left behind.
    onHoldingChanged:       { liquidCanvas.requestPaint(); effectsCanvas.requestPaint() }
    onTargetWeightChanged:  { liquidCanvas.requestPaint(); effectsCanvas.requestPaint() }
    // Skipped while holding: the DE1 keeps streaming samples at idle, and
    // nothing the flow value feeds is drawn once the cup is held.
    onCurrentFlowChanged:   { if (!animTimer.running && !holding) { liquidCanvas.requestPaint(); effectsCanvas.requestPaint() } }

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

        // Only show fill during or after extraction. Pre-flow phases like
        // EspressoPreheating render empty regardless of weight — the
        // stale-residual case is solved at the source in MachineState
        // (see m_hotWaterFrozenWeight clearing on cycle entry).
        // extractionSeen carries the "after" half: once coffee has been drawn
        // on this view, the fill survives the phase leaving the cycle. With
        // nothing latched, a resting cup on the scale still renders empty.
        var hasExtraction = root.extractionPhase || root.extractionSeen
            || root.waterPhase || root.waterWeight > 0
        var fillRatio = hasExtraction ? root.visualFill : 0
        var interiorH = cupH * 0.8  // 100% fill reaches 80% of cup height
        var fillH = fillRatio * interiorH
        var fillTopY = botCy - fillH
        var isPouring = root.phase === MachineState.Phase.Preinfusion ||
                        root.phase === MachineState.Phase.Pouring || root.waterPhase

        return {
            cx: cx, rimCy: rimCy, botCy: botCy,
            cupW: cupW, cupBotW: cupBotW,
            rimOvalH: rimOvalH, botOvalH: botOvalH, cupH: cupH,
            fillRatio: fillRatio, interiorH: interiorH,
            fillH: fillH, fillTopY: fillTopY, isPouring: isPouring
        }
    }

    // The liquid's vertical gradient, stop by stop. Pure coffee is the espresso
    // palette; added water moves it toward a diluted brown by the water share, and
    // water with no coffee yet (a long black's first step) is pale and clear.
    readonly property var espressoStops: ["#140600", "#1E0A00", "#3A1808", "#5A2E14",
                                          "#7A4422", "#a86e3e", "#c08050"]
    readonly property var dilutedStops: ["#4A2610", "#5A3016", "#74421F", "#8E5A2E",
                                         "#A8743F", "#C49260", "#D4A878"]
    function liquidColor(i: int): color {
        if (coffeeWeight <= 0)
            return Qt.rgba(0.78, 0.87, 0.95, 0.30 + 0.06 * i)
        const a = Qt.color(espressoStops[i])
        const b = Qt.color(dilutedStops[i])
        const t = Math.min(waterShare * 1.3, 1.0)
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1)
    }

    // Helper: draw front half of an ellipse arc (PI to 2*PI)
    function frontArc(ctx, cx, cy, rx, ry, steps, asMove) {
        for (let i = 0; i <= steps; i++) {
            let a = Math.PI + (Math.PI * i / steps)
            let x = cx + rx * Math.cos(a)
            let y = cy + ry * Math.sin(a)
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

            Canvas2D {
                id: liquidCanvas
                anchors.fill: parent
                contextType: "2d"
                fillColor: "transparent"
                alphaBlending: true

                onPaint: function() {
                    // getContext(), not .context: it recreates the context a scene-graph
                    // invalidation deleted (qcanvas2ditem.cpp:294-302, 410-445). Unavailable
                    // until the scene graph is up (qcanvas2ditem.cpp:242-254).
                    const ctx = liquidCanvas.available ? liquidCanvas.getContext("2d") : null
                    if (!ctx) return
                    var w = width, h = height
                    ctx.reset()
                    ctx.clearRect(0, 0, w, h)
                    if (w < 1 || h < 1) return

                    var g = root.cupGeometry(w, h)
                    var N = 36

                    // Only show coffee once there is weight to show, AND only
                    // once the gate above has allowed a fill. Without the
                    // second test the +0.12 crema boost below turns a zero
                    // fillRatio into a 12%-full cup, so a resting cup's weight
                    // drew coffee in exactly the pre-flow case the gate exists
                    // to prevent. Pre-existing; the gate was load-bearing but
                    // never actually reached this far.
                    if (root.liquidWeight <= 0 || g.fillRatio <= 0) return

                    // Shift fill forward so coffee appears with crema at first weight
                    var effectiveFillRatio = Math.min(g.fillRatio + 0.12, 1.0)
                    var effectiveFillH = effectiveFillRatio * g.interiorH
                    var effectiveFillTopY = g.botCy - effectiveFillH

                    var t = effectiveFillH / g.interiorH
                    var fillRx = g.cupBotW + (g.cupW - g.cupBotW) * t
                    var fillOvalH = g.botOvalH + (g.rimOvalH - g.botOvalH) * t
                    var waveAmp = root.streamFlow > 0.1 ? h * 0.006 : 0
                    var wSteps = 36

                    // Liquid body — uses same effectiveFillTopY as crema
                    var liqVertGrad = ctx.createLinearGradient(0, g.botCy + g.botOvalH, 0, effectiveFillTopY)
                    var stops = [0, 0.15, 0.35, 0.55, 0.75, 0.9, 1.0]
                    for (let si = 0; si < stops.length; si++)
                        liqVertGrad.addColorStop(stops[si], root.liquidColor(si))

                    ctx.beginPath()
                    ctx.moveTo(g.cx - g.cupBotW, g.botCy)
                    root.frontArc(ctx, g.cx, g.botCy, g.cupBotW, g.botOvalH, N, false)
                    ctx.lineTo(g.cx + fillRx, effectiveFillTopY)
                    for (let wi = wSteps; wi >= 0; wi--) {
                        let wx = g.cx - fillRx + fillRx * 2 * (wi / wSteps)
                        let wy = effectiveFillTopY + Math.sin(root.wavePhase + wi * 0.5) * waveAmp
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
                        let surfGrad = ctx.createLinearGradient(g.cx - fillRx, effectiveFillTopY, g.cx + fillRx, effectiveFillTopY)
                        surfGrad.addColorStop(0, Qt.rgba(1, 0.95, 0.85, 0))
                        surfGrad.addColorStop(0.25, Qt.rgba(1, 0.95, 0.85, 0.06))
                        surfGrad.addColorStop(0.4, Qt.rgba(1, 0.95, 0.85, 0.14))
                        surfGrad.addColorStop(0.5, Qt.rgba(1, 0.97, 0.90, 0.18))
                        surfGrad.addColorStop(0.6, Qt.rgba(1, 0.95, 0.85, 0.12))
                        surfGrad.addColorStop(0.75, Qt.rgba(1, 0.95, 0.85, 0.04))
                        surfGrad.addColorStop(1, Qt.rgba(1, 0.95, 0.85, 0))
                        ctx.fillStyle = surfGrad
                        ctx.beginPath()
                        for (let wh = 0; wh <= wSteps; wh++) {
                            let whx = g.cx - fillRx * 0.92 + fillRx * 1.84 * (wh / wSteps)
                            let why = effectiveFillTopY + Math.sin(root.wavePhase + wh * 0.5) * waveAmp - h * 0.002
                            if (wh === 0) ctx.moveTo(whx, why)
                            else ctx.lineTo(whx, why)
                        }
                        for (let wh2 = wSteps; wh2 >= 0; wh2--) {
                            let wh2x = g.cx - fillRx * 0.92 + fillRx * 1.84 * (wh2 / wSteps)
                            let wh2y = effectiveFillTopY + Math.sin(root.wavePhase + wh2 * 0.5) * waveAmp + h * 0.014
                            ctx.lineTo(wh2x, wh2y)
                        }
                        ctx.closePath()
                        ctx.fill()
                    }

                    // Crema
                    if (effectiveFillRatio > 0.10 && root.coffeeWeight > 0) {
                        // Water poured onto an americano's shot breaks its crema;
                        // a long black's espresso lands on the water and keeps it.
                        let cremaFade = Math.min((effectiveFillRatio - 0.10) / 0.15, 1.0)
                            * (root.waterFirst ? 1.0 : 1.0 - root.waterShare)
                        let cremaRx = fillRx * 0.88
                        let cremaRy = fillOvalH * 0.75

                        let cremaGrad = ctx.createRadialGradient(
                            g.cx - cremaRx * 0.1, effectiveFillTopY, cremaRx * 0.1,
                            g.cx, effectiveFillTopY, cremaRx)
                        cremaGrad.addColorStop(0, Qt.rgba(0.88, 0.72, 0.48, 0.6 * cremaFade))
                        cremaGrad.addColorStop(0.25, Qt.rgba(0.85, 0.65, 0.38, 0.5 * cremaFade))
                        cremaGrad.addColorStop(0.55, Qt.rgba(0.78, 0.55, 0.30, 0.38 * cremaFade))
                        cremaGrad.addColorStop(0.8, Qt.rgba(0.68, 0.45, 0.22, 0.2 * cremaFade))
                        cremaGrad.addColorStop(1, Qt.rgba(0.55, 0.35, 0.15, 0))
                        ctx.beginPath()
                        ctx.ellipseRect(g.cx - cremaRx, effectiveFillTopY - cremaRy, cremaRx * 2, cremaRy * 2)
                        ctx.fillStyle = cremaGrad
                        ctx.fill()

                        // Tiger stripes
                        let stripeAlpha = 0.25 * cremaFade
                        for (let ts = 0; ts < 4; ts++) {
                            let tsRadius = cremaRx * (0.3 + ts * 0.18)
                            let tsRy2 = cremaRy * (0.25 + ts * 0.15)
                            let tsPhase = root.steamPhase * (0.15 + ts * 0.05) + ts * 1.5
                            let tsStartAngle = tsPhase % (Math.PI * 2)
                            let tsArcLen = Math.PI * (0.5 + ts * 0.15)

                            ctx.strokeStyle = Qt.rgba(0.45, 0.25, 0.10, stripeAlpha * (1 - ts * 0.18))
                            ctx.lineWidth = Math.max(1, h * 0.004 + ts * h * 0.001)
                            ctx.beginPath()
                            let tsSteps = 16
                            for (let tsi = 0; tsi <= tsSteps; tsi++) {
                                let tsAngle = tsStartAngle + tsArcLen * tsi / tsSteps
                                let tsx = g.cx + tsRadius * Math.cos(tsAngle)
                                let tsy = effectiveFillTopY + tsRy2 * Math.sin(tsAngle) +
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
                        for (let cj = 0; cj <= wSteps; cj++) {
                            let crAng = Math.PI * cj / wSteps
                            let crx = g.cx - cremaRx * Math.cos(crAng)
                            let cry = effectiveFillTopY - cremaRy * 0.3 * Math.sin(crAng) +
                                      Math.sin(root.wavePhase + cj * 0.5) * waveAmp * 0.4
                            if (cj === 0) ctx.moveTo(crx, cry)
                            else ctx.lineTo(crx, cry)
                        }
                        ctx.stroke()

                        // Bright center spot
                        let centerSpot = ctx.createRadialGradient(
                            g.cx - cremaRx * 0.15, effectiveFillTopY - cremaRy * 0.1, h * 0.005,
                            g.cx - cremaRx * 0.15, effectiveFillTopY - cremaRy * 0.1, cremaRx * 0.35)
                        centerSpot.addColorStop(0, Qt.rgba(1, 0.95, 0.8, 0.2 * cremaFade))
                        centerSpot.addColorStop(0.5, Qt.rgba(1, 0.9, 0.7, 0.08 * cremaFade))
                        centerSpot.addColorStop(1, Qt.rgba(1, 0.85, 0.6, 0))
                        ctx.beginPath()
                        ctx.ellipseRect(g.cx - cremaRx * 0.5, effectiveFillTopY - cremaRy * 0.45,
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
                    if (g.isPouring && root.streamFlow > 0.3 && effectiveFillRatio < 0.95) {
                        for (let rp = 0; rp < 3; rp++) {
                            let rpAge = (root.ripplePhase + rp * 2.0) % 6.0
                            if (rpAge > 3.0) continue
                            let rpProgress = rpAge / 3.0
                            let rpAlpha = (1.0 - rpProgress) * 0.2
                            let rpRadiusX = h * 0.01 + rpProgress * fillRx * 0.35
                            let rpRadiusY = rpRadiusX * (fillOvalH / fillRx)
                            ctx.strokeStyle = Qt.rgba(0.9, 0.8, 0.6, rpAlpha)
                            ctx.lineWidth = Math.max(1, h * 0.002)
                            ctx.beginPath()
                            for (let rpi = 0; rpi <= 20; rpi++) {
                                let rpAngle = Math.PI + (Math.PI * rpi / 20)
                                let rpx = g.cx + rpRadiusX * Math.cos(rpAngle)
                                let rpy = effectiveFillTopY + rpRadiusY * Math.sin(rpAngle) +
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
    Canvas2D {
        id: effectsCanvas
        contextType: "2d"
        fillColor: "transparent"
        alphaBlending: true
        x: root.cupX
        y: 0
        width: root.cupDisplayW
        height: root.cupDisplayH + root.effectsExtra

            onPaint: function() {
                const ctx = effectsCanvas.available ? effectsCanvas.getContext("2d") : null  // as liquidCanvas
                if (!ctx) return
                var w = effectsCanvas.width
                var canvasH = effectsCanvas.height
                var cupH = root.cupDisplayH
                var yOff = root.effectsExtra  // offset: cup geometry starts at yOff
                ctx.reset()
                ctx.clearRect(0, 0, w, canvasH)
                if (w < 1 || cupH < 1) return

                var g = root.cupGeometry(w, cupH)
                // Shift all geometry down by yOff to align with cup image
                g.rimCy += yOff
                g.botCy += yOff
                // Match the visual fill level used by the liquid canvas (+0.12 boost)
                var effectiveFillRatio = Math.min(g.fillRatio + 0.12, 1.0)
                var effectiveFillH = effectiveFillRatio * g.interiorH
                var effectiveFillTopY = g.botCy - effectiveFillH
                var N = 36

                // ---- Stream from above (during extraction) ----
                if (g.isPouring && root.streamFlow > 0.3 && (root.liquidWeight > 0 || root.waterPhase)
                        && g.fillRatio < 1.0) {
                    let streamTopY = 0  // enters from top of screen
                    let cupFloorY = g.botCy - g.botOvalH - cupH * 0.02
                    let streamBot = root.liquidWeight > 0 ? Math.min(effectiveFillTopY, cupFloorY) : cupFloorY
                    let streamLen = Math.max(streamBot - streamTopY, cupH * 0.01)
                    // Sizes use the espresso flow range: hot water runs several times
                    // faster, and an unclamped flow turns the droplets into blobs.
                    let visFlow = Math.min(root.streamFlow, 3.0)
                    let streamTopW = Math.min(cupH * 0.032 + visFlow * cupH * 0.007, cupH * 0.06)
                    let streamBotW = streamTopW * 0.5
                    let streamSegs = 20

                    ctx.beginPath()
                    for (let ssL = 0; ssL <= streamSegs; ssL++) {
                        let ssLt = ssL / streamSegs
                        let wobL = Math.sin(root.wavePhase * 2.2 + ssLt * 5.0) * cupH * 0.006 * ssLt
                        let ssLw = streamTopW + (streamBotW - streamTopW) * ssLt
                        if (ssL === 0) ctx.moveTo(g.cx + wobL - ssLw * 0.5, streamTopY + streamLen * ssLt)
                        else ctx.lineTo(g.cx + wobL - ssLw * 0.5, streamTopY + streamLen * ssLt)
                    }
                    for (let ssR = streamSegs; ssR >= 0; ssR--) {
                        let ssRt = ssR / streamSegs
                        let wobR = Math.sin(root.wavePhase * 2.2 + ssRt * 5.0) * cupH * 0.006 * ssRt
                        let ssRw = streamTopW + (streamBotW - streamTopW) * ssRt
                        ctx.lineTo(g.cx + wobR + ssRw * 0.5, streamTopY + streamLen * ssRt)
                    }
                    ctx.closePath()
                    let stGrad = ctx.createLinearGradient(0, streamTopY, 0, streamBot)
                    if (root.waterPhase) {
                        // Water: the espresso stream's column, in a pale glassy tint.
                        stGrad.addColorStop(0, Qt.rgba(0.82, 0.90, 0.97, 0.55))
                        stGrad.addColorStop(0.15, Qt.rgba(0.78, 0.87, 0.96, 0.65))
                        stGrad.addColorStop(0.4, Qt.rgba(0.74, 0.84, 0.94, 0.72))
                        stGrad.addColorStop(0.7, Qt.rgba(0.70, 0.81, 0.92, 0.78))
                        stGrad.addColorStop(1, Qt.rgba(0.66, 0.78, 0.90, 0.82))
                    } else {
                        stGrad.addColorStop(0, Qt.rgba(0.55, 0.35, 0.18, 0.5))
                        stGrad.addColorStop(0.15, Qt.rgba(0.52, 0.32, 0.16, 0.65))
                        stGrad.addColorStop(0.4, Qt.rgba(0.48, 0.28, 0.14, 0.75))
                        stGrad.addColorStop(0.7, Qt.rgba(0.42, 0.24, 0.12, 0.82))
                        stGrad.addColorStop(1, Qt.rgba(0.38, 0.20, 0.10, 0.88))
                    }
                    ctx.fillStyle = stGrad
                    ctx.fill()

                    // Stream center highlight
                    ctx.beginPath()
                    for (let ssC = 0; ssC <= streamSegs; ssC++) {
                        let ssCt = ssC / streamSegs
                        let wobC = Math.sin(root.wavePhase * 2.2 + ssCt * 5.0) * cupH * 0.006 * ssCt
                        if (ssC === 0) ctx.moveTo(g.cx + wobC, streamTopY + streamLen * ssCt)
                        else ctx.lineTo(g.cx + wobC, streamTopY + streamLen * ssCt)
                    }
                    ctx.strokeStyle = root.waterPhase ? Qt.rgba(1, 1, 1, 0.30)
                                                      : Qt.rgba(0.75, 0.55, 0.32, 0.25)
                    ctx.lineWidth = streamTopW * 0.25
                    ctx.lineCap = "round"
                    ctx.stroke()

                    // Droplets
                    for (let dr = 0; dr < 5; dr++) {
                        let drT = ((root.wavePhase * 1.5 + dr * 1.4) % 3.5) / 3.5
                        if (drT > 0.95) continue
                        let drY = streamTopY + streamLen * drT
                        if (root.liquidWeight > 0 && drY > effectiveFillTopY + cupH * 0.01) continue
                        let drWob = Math.sin(root.wavePhase * 2.2 + drT * 5.0) * cupH * 0.006 * drT
                        let drSize = (cupH * 0.006 + visFlow * cupH * 0.001) * (1 - drT * 0.3)
                        ctx.beginPath()
                        ctx.arc(g.cx + drWob, drY, drSize, 0, Math.PI * 2)
                        ctx.fillStyle = root.waterPhase ? Qt.rgba(0.80, 0.89, 0.97, 0.30 + drT * 0.25)
                                                        : Qt.rgba(0.65, 0.42, 0.22, 0.4 + drT * 0.35)
                        ctx.fill()
                    }

                    // Splash glow
                    let splashY = root.liquidWeight > 0 ? Math.min(effectiveFillTopY, cupFloorY) : cupFloorY
                    let splashR = cupH * 0.042
                    let splashGlow = ctx.createRadialGradient(g.cx, splashY, cupH * 0.005, g.cx, splashY, splashR)
                    if (root.waterPhase) {
                        splashGlow.addColorStop(0, Qt.rgba(0.9, 0.95, 1.0, 0.45))
                        splashGlow.addColorStop(0.4, Qt.rgba(0.85, 0.92, 1.0, 0.18))
                        splashGlow.addColorStop(1, Qt.rgba(0.8, 0.9, 1.0, 0))
                    } else {
                        splashGlow.addColorStop(0, Qt.rgba(0.7, 0.5, 0.3, 0.5))
                        splashGlow.addColorStop(0.4, Qt.rgba(0.6, 0.4, 0.2, 0.2))
                        splashGlow.addColorStop(1, Qt.rgba(0.5, 0.3, 0.15, 0))
                    }
                    ctx.beginPath()
                    ctx.arc(g.cx, splashY, splashR, 0, Math.PI * 2)
                    ctx.fillStyle = splashGlow
                    ctx.fill()
                }

                // ---- Steam wisps ----
                if (root.liquidWeight > 0 && root.streamFlow < 2.0) {
                    let steamAlphaBase = Math.min(root.liquidWeight / 5.0, 1.0) * 0.14
                    for (let sp = 0; sp < 4; sp++) {
                        let steamOffX = (sp - 1.5) * g.cupW * 0.25
                        let steamBaseY = root.liquidWeight > 0 ? effectiveFillTopY : g.rimCy
                        let steamStartY = Math.min(steamBaseY, g.rimCy) - cupH * 0.007
                        let steamH2 = cupH * (0.07 + sp * 0.023)
                        let steamEndY = steamStartY - steamH2

                        let phOff = root.steamPhase * (0.8 + sp * 0.2) + sp * 1.7
                        let d1x = Math.sin(phOff) * w * (0.015 + sp * 0.003)
                        let d1y = steamH2 * 0.33
                        let d2x = Math.sin(phOff * 1.2 + 2) * w * (0.012 + sp * 0.003)
                        let d2y = steamH2 * 0.66

                        let segments = 12
                        for (let seg = 0; seg < segments; seg++) {
                            let t0 = seg / segments, t1 = (seg + 1) / segments
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
                // if (root.displayWeight >= root.targetWeight && root.targetWeight > 0) {
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
            text: root.liquidWeight.toFixed(1) + TranslationManager.translate("common.unit.grams", "g")
            color: Theme.tintedOverlayColor(root.trackColor, 0.5)
            font.pixelSize: Theme.scaled(38)
            font.weight: Font.Bold
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.drinkTarget > 0
                ? TranslationManager.translate("espresso.cupFill.target", "target") + " " + root.drinkTarget.toFixed(0) + TranslationManager.translate("common.unit.grams", "g")
                : ""
            color: Theme.tintedOverlayColor(root.trackColor, 0.4)
            font.pixelSize: Theme.scaled(16)
        }
    }
}
