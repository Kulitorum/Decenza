import QtQuick
import Decenza

// Animated cup-fill visualization for espresso extraction.
// Realistic ceramic cup with portafilter, crema, steam, and reflections.
// Cup outline color tracks extraction goal accuracy (green/yellow/red).
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
            canvas.requestPaint()
        }
    }

    onCurrentWeightChanged: canvas.requestPaint()
    onTargetWeightChanged: canvas.requestPaint()
    onCurrentFlowChanged: if (!animTimer.running) canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Threaded

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

        // Helper: draw back half of an ellipse arc (0 to PI)
        function backArc(ctx, cx, cy, rx, ry, steps, asMove) {
            for (var i = 0; i <= steps; i++) {
                var a = Math.PI * i / steps
                var x = cx + rx * Math.cos(a)
                var y = cy + ry * Math.sin(a)
                if (i === 0 && asMove) ctx.moveTo(x, y)
                else ctx.lineTo(x, y)
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            var w = width
            var h = height
            ctx.reset()
            ctx.clearRect(0, 0, w, h)

            var s = Math.min(w, h)
            var cx = w / 2
            var N = 36 // arc resolution

            // ============================================================
            // Geometry
            // ============================================================
            var cupW = s * 0.42
            var cupBotW = cupW * 0.82
            var rimOvalH = cupW * 0.22
            var botOvalH = cupBotW * 0.18
            var cupH = s * 0.44
            var rimCy = h * 0.35
            var botCy = rimCy + cupH
            var rimThick = Theme.scaled(6)

            var handleW = cupW * 0.35
            var handleH = cupH * 0.55
            var handleX = cx + cupW - Theme.scaled(2)
            var handleY = rimCy + cupH * 0.12

            var fillRatio = root.targetWeight > 0
                ? Math.min(root.currentWeight / root.targetWeight, 1.0) : 0
            var interiorH = cupH - Theme.scaled(4)
            var fillH = fillRatio * interiorH
            var fillTopY = botCy - fillH
            var isPouring = root.phase === MachineStateType.Phase.Preinfusion ||
                            root.phase === MachineStateType.Phase.Pouring

            // ============================================================
            // Drop shadow (layered soft ellipses)
            // ============================================================
            var shadowCy = botCy + botOvalH + Theme.scaled(18)
            var shadowRx = cupW * 1.2
            var shadowRy = rimOvalH * 0.5
            for (var dsi = 4; dsi >= 0; dsi--) {
                var dsAlpha = 0.03 + dsi * 0.025
                var dsSpread = Theme.scaled(3 + dsi * 6)
                ctx.beginPath()
                ctx.ellipse(cx - shadowRx - dsSpread, shadowCy - shadowRy - dsSpread * 0.3,
                            (shadowRx + dsSpread) * 2, (shadowRy + dsSpread * 0.3) * 2)
                ctx.fillStyle = Qt.rgba(0, 0, 0, dsAlpha)
                ctx.fill()
            }

            // ============================================================
            // Cup reflection on saucer (subtle ghost)
            // ============================================================
            var reflCy = botCy + botOvalH + Theme.scaled(6)
            var reflH = cupH * 0.18
            var reflAlpha = 0.04
            ctx.save()
            // Clip to saucer area
            ctx.beginPath()
            ctx.ellipse(cx - cupW * 1.1, reflCy - rimOvalH * 0.55,
                        cupW * 2.2, rimOvalH * 1.1 + reflH)
            ctx.clip()
            // Mirror of cup body (flipped, faded)
            var reflGrad = ctx.createLinearGradient(0, reflCy, 0, reflCy + reflH)
            reflGrad.addColorStop(0, Qt.rgba(0.3, 0.3, 0.35, reflAlpha))
            reflGrad.addColorStop(0.5, Qt.rgba(0.25, 0.25, 0.3, reflAlpha * 0.5))
            reflGrad.addColorStop(1, Qt.rgba(0.2, 0.2, 0.25, 0))
            ctx.beginPath()
            ctx.moveTo(cx - cupBotW * 0.9, reflCy)
            ctx.lineTo(cx - cupW * 0.85, reflCy + reflH)
            ctx.lineTo(cx + cupW * 0.85, reflCy + reflH)
            ctx.lineTo(cx + cupBotW * 0.9, reflCy)
            ctx.closePath()
            ctx.fillStyle = reflGrad
            ctx.fill()
            ctx.restore()

            // ============================================================
            // Saucer
            // ============================================================
            var saucerCy = botCy + botOvalH + Theme.scaled(4)
            var saucerRx = cupW * 1.1
            var saucerRy = rimOvalH * 0.55
            var saucerThick = Theme.scaled(8)

            // Saucer side band
            var saucerSideGrad = ctx.createLinearGradient(cx - saucerRx, 0, cx + saucerRx, 0)
            saucerSideGrad.addColorStop(0, Qt.rgba(0.12, 0.12, 0.14, 1))
            saucerSideGrad.addColorStop(0.25, Qt.rgba(0.24, 0.24, 0.28, 1))
            saucerSideGrad.addColorStop(0.45, Qt.rgba(0.34, 0.34, 0.39, 1))
            saucerSideGrad.addColorStop(0.55, Qt.rgba(0.32, 0.32, 0.37, 1))
            saucerSideGrad.addColorStop(0.75, Qt.rgba(0.22, 0.22, 0.26, 1))
            saucerSideGrad.addColorStop(1, Qt.rgba(0.10, 0.10, 0.12, 1))

            ctx.beginPath()
            frontArc(ctx, cx, saucerCy, saucerRx, saucerRy, N, true)
            // Bottom edge (reverse, offset down)
            for (var sb = N; sb >= 0; sb--) {
                var sba = Math.PI + (Math.PI * sb / N)
                ctx.lineTo(cx + saucerRx * Math.cos(sba),
                           saucerCy + saucerThick + saucerRy * Math.sin(sba))
            }
            ctx.closePath()
            ctx.fillStyle = saucerSideGrad
            ctx.fill()

            // Saucer top — radial gradient for specular
            var saucerTopGrad = ctx.createRadialGradient(
                cx - saucerRx * 0.15, saucerCy - saucerRy * 0.3, saucerRx * 0.1,
                cx, saucerCy, saucerRx)
            saucerTopGrad.addColorStop(0, Qt.rgba(0.42, 0.42, 0.47, 1))
            saucerTopGrad.addColorStop(0.3, Qt.rgba(0.35, 0.35, 0.40, 1))
            saucerTopGrad.addColorStop(0.7, Qt.rgba(0.25, 0.25, 0.30, 1))
            saucerTopGrad.addColorStop(1, Qt.rgba(0.15, 0.15, 0.18, 1))
            ctx.beginPath()
            ctx.ellipse(cx - saucerRx, saucerCy - saucerRy, saucerRx * 2, saucerRy * 2)
            ctx.fillStyle = saucerTopGrad
            ctx.fill()
            ctx.strokeStyle = Qt.rgba(0.55, 0.55, 0.6, 0.3)
            ctx.lineWidth = Theme.scaled(1)
            ctx.stroke()

            // Saucer raised rim (subtle inner ring)
            var saucerInnerRx = saucerRx * 0.88
            var saucerInnerRy = saucerRy * 0.85
            ctx.strokeStyle = Qt.rgba(0.45, 0.45, 0.5, 0.25)
            ctx.lineWidth = Theme.scaled(1)
            ctx.beginPath()
            frontArc(ctx, cx, saucerCy, saucerInnerRx, saucerInnerRy, N, true)
            ctx.stroke()

            // Saucer specular highlight
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.13)
            ctx.lineWidth = Theme.scaled(2)
            ctx.beginPath()
            for (var sk = 7; sk <= 15; sk++) {
                var ska = Math.PI + (Math.PI * sk / 22)
                var skx = cx + (saucerRx - Theme.scaled(6)) * Math.cos(ska)
                var sky = saucerCy + (saucerRy - Theme.scaled(3)) * Math.sin(ska)
                if (sk === 7) ctx.moveTo(skx, sky)
                else ctx.lineTo(skx, sky)
            }
            ctx.stroke()

            // ============================================================
            // Contact shadow (cup sitting on saucer)
            // ============================================================
            var contactGrad = ctx.createRadialGradient(cx, botCy + botOvalH, cupBotW * 0.3,
                                                        cx, botCy + botOvalH, cupBotW * 1.1)
            contactGrad.addColorStop(0, Qt.rgba(0, 0, 0, 0.2))
            contactGrad.addColorStop(0.6, Qt.rgba(0, 0, 0, 0.08))
            contactGrad.addColorStop(1, Qt.rgba(0, 0, 0, 0))
            ctx.beginPath()
            ctx.ellipse(cx - cupBotW * 1.1, botCy + botOvalH - Theme.scaled(4),
                        cupBotW * 2.2, Theme.scaled(10))
            ctx.fillStyle = contactGrad
            ctx.fill()

            // ============================================================
            // Handle (behind cup body)
            // ============================================================
            var handleThick = Theme.scaled(11)
            var hx0 = handleX, hy0 = handleY
            var hx3 = handleX, hy3 = handleY + handleH
            var hcx1 = handleX + handleW * 1.4, hcy1 = handleY - handleH * 0.05
            var hcx2 = handleX + handleW * 1.4, hcy2 = handleY + handleH * 1.05
            var ihcx1 = handleX + handleW * 0.75, ihcy1 = handleY + handleH * 0.1
            var ihcx2 = handleX + handleW * 0.75, ihcy2 = handleY + handleH * 0.9

            ctx.beginPath()
            ctx.moveTo(hx0, hy0 - handleThick * 0.35)
            ctx.bezierCurveTo(hcx1, hcy1 - handleThick * 0.35,
                              hcx2, hcy2 + handleThick * 0.35,
                              hx3, hy3 + handleThick * 0.35)
            ctx.lineTo(hx3, hy3 - handleThick * 0.3)
            ctx.bezierCurveTo(ihcx2, ihcy2, ihcx1, ihcy1,
                              hx0, hy0 + handleThick * 0.3)
            ctx.closePath()

            var handleGrad = ctx.createLinearGradient(handleX, hy0, handleX + handleW * 1.4, hy0)
            handleGrad.addColorStop(0, Qt.rgba(0.20, 0.20, 0.24, 1))
            handleGrad.addColorStop(0.25, Qt.rgba(0.34, 0.34, 0.39, 1))
            handleGrad.addColorStop(0.5, Qt.rgba(0.38, 0.38, 0.43, 1))
            handleGrad.addColorStop(0.75, Qt.rgba(0.28, 0.28, 0.33, 1))
            handleGrad.addColorStop(1, Qt.rgba(0.14, 0.14, 0.18, 1))
            ctx.fillStyle = handleGrad
            ctx.fill()
            ctx.strokeStyle = Qt.rgba(0.5, 0.5, 0.55, 0.4)
            ctx.lineWidth = Theme.scaled(1.5)
            ctx.stroke()

            // Handle highlight
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.12)
            ctx.lineWidth = Theme.scaled(1.5)
            ctx.beginPath()
            ctx.moveTo(hx0 + Theme.scaled(4), hy0 - handleThick * 0.15)
            ctx.bezierCurveTo(hcx1 * 0.97 + handleX * 0.03, hcy1 - handleThick * 0.25,
                              hcx2 * 0.5 + hcx1 * 0.5, (hcy1 + hcy2) * 0.5 - handleThick * 0.15,
                              hcx2 * 0.97 + handleX * 0.03, hcy2 * 0.6 + hcy1 * 0.4)
            ctx.stroke()

            // Handle attachment shadows (where handle meets cup)
            ctx.beginPath()
            ctx.arc(handleX - Theme.scaled(1), handleY + Theme.scaled(2),
                    Theme.scaled(6), 0, Math.PI * 2)
            ctx.fillStyle = Qt.rgba(0, 0, 0, 0.15)
            ctx.fill()
            ctx.beginPath()
            ctx.arc(handleX - Theme.scaled(1), handleY + handleH - Theme.scaled(2),
                    Theme.scaled(6), 0, Math.PI * 2)
            ctx.fill()

            // ============================================================
            // Cup body — ceramic cylinder
            // ============================================================
            var bodyGrad = ctx.createLinearGradient(cx - cupW, 0, cx + cupW, 0)
            bodyGrad.addColorStop(0, Qt.rgba(0.10, 0.10, 0.12, 1))
            bodyGrad.addColorStop(0.08, Qt.rgba(0.16, 0.16, 0.19, 1))
            bodyGrad.addColorStop(0.20, Qt.rgba(0.26, 0.26, 0.31, 1))
            bodyGrad.addColorStop(0.35, Qt.rgba(0.33, 0.33, 0.38, 1))
            bodyGrad.addColorStop(0.45, Qt.rgba(0.36, 0.36, 0.41, 1))
            bodyGrad.addColorStop(0.55, Qt.rgba(0.34, 0.34, 0.39, 1))
            bodyGrad.addColorStop(0.70, Qt.rgba(0.26, 0.26, 0.30, 1))
            bodyGrad.addColorStop(0.85, Qt.rgba(0.17, 0.17, 0.20, 1))
            bodyGrad.addColorStop(1, Qt.rgba(0.08, 0.08, 0.10, 1))

            ctx.beginPath()
            ctx.moveTo(cx - cupW, rimCy)
            ctx.lineTo(cx - cupBotW, botCy)
            frontArc(ctx, cx, botCy, cupBotW, botOvalH, N, false)
            ctx.lineTo(cx + cupW, rimCy)
            ctx.closePath()
            ctx.fillStyle = bodyGrad
            ctx.fill()

            // Vertical shading overlay
            ctx.save()
            ctx.beginPath()
            ctx.moveTo(cx - cupW, rimCy)
            ctx.lineTo(cx - cupBotW, botCy)
            frontArc(ctx, cx, botCy, cupBotW, botOvalH, N, false)
            ctx.lineTo(cx + cupW, rimCy)
            ctx.closePath()
            ctx.clip()

            var vertBodyGrad = ctx.createLinearGradient(0, rimCy, 0, botCy + botOvalH)
            vertBodyGrad.addColorStop(0, Qt.rgba(1, 1, 1, 0.06))
            vertBodyGrad.addColorStop(0.4, Qt.rgba(1, 1, 1, 0.0))
            vertBodyGrad.addColorStop(0.8, Qt.rgba(0, 0, 0, 0.06))
            vertBodyGrad.addColorStop(1, Qt.rgba(0, 0, 0, 0.14))
            ctx.fillStyle = vertBodyGrad
            ctx.fillRect(cx - cupW, rimCy, cupW * 2, cupH + botOvalH)
            ctx.restore()

            // ============================================================
            // Cup interior
            // ============================================================
            var interiorInset = Theme.scaled(4)
            var topIRx = cupW - interiorInset
            var topIRy = rimOvalH - Theme.scaled(2)
            var botIRx = cupBotW - interiorInset
            var botIRy = botOvalH - Theme.scaled(3)

            ctx.save()
            ctx.beginPath()
            backArc(ctx, cx, rimCy, topIRx, topIRy, N, true)
            ctx.lineTo(cx - botIRx, botCy)
            frontArc(ctx, cx, botCy, botIRx, botIRy, N, false)
            ctx.lineTo(cx + topIRx, rimCy)
            ctx.closePath()
            ctx.clip()

            var interiorGrad = ctx.createLinearGradient(cx - topIRx, 0, cx + topIRx, 0)
            interiorGrad.addColorStop(0, Qt.rgba(0.07, 0.07, 0.09, 1))
            interiorGrad.addColorStop(0.3, Qt.rgba(0.13, 0.13, 0.16, 1))
            interiorGrad.addColorStop(0.5, Qt.rgba(0.15, 0.15, 0.18, 1))
            interiorGrad.addColorStop(0.7, Qt.rgba(0.12, 0.12, 0.15, 1))
            interiorGrad.addColorStop(1, Qt.rgba(0.05, 0.05, 0.07, 1))

            var intTopY = rimCy - topIRy
            var intBotY = fillH > 1 ? fillTopY : botCy + botIRy
            ctx.fillStyle = interiorGrad
            ctx.fillRect(cx - topIRx, intTopY, topIRx * 2, intBotY - intTopY)

            // ---- Visible cup bottom (when empty or low fill) ----
            if (fillRatio < 0.25) {
                var botVisAlpha = (1.0 - fillRatio / 0.25) * 0.6
                var cupFloorCy = botCy - Theme.scaled(2)
                var cupFloorRx = botIRx * 0.95
                var cupFloorRy = botIRy * 0.9

                // Floor ellipse with radial gradient
                var floorGrad = ctx.createRadialGradient(
                    cx - cupFloorRx * 0.1, cupFloorCy, cupFloorRx * 0.15,
                    cx, cupFloorCy, cupFloorRx)
                floorGrad.addColorStop(0, Qt.rgba(0.18, 0.18, 0.22, botVisAlpha))
                floorGrad.addColorStop(0.5, Qt.rgba(0.13, 0.13, 0.16, botVisAlpha))
                floorGrad.addColorStop(1, Qt.rgba(0.08, 0.08, 0.10, botVisAlpha * 0.6))
                ctx.beginPath()
                ctx.ellipse(cx - cupFloorRx, cupFloorCy - cupFloorRy,
                            cupFloorRx * 2, cupFloorRy * 2)
                ctx.fillStyle = floorGrad
                ctx.fill()

                // Floor rim ring
                ctx.strokeStyle = Qt.rgba(0.25, 0.25, 0.3, botVisAlpha * 0.4)
                ctx.lineWidth = Theme.scaled(1)
                ctx.stroke()
            }

            // Inner shadow at rim (ambient occlusion)
            var innerShadow = ctx.createLinearGradient(0, rimCy - topIRy, 0, rimCy + cupH * 0.18)
            innerShadow.addColorStop(0, Qt.rgba(0, 0, 0, 0.3))
            innerShadow.addColorStop(0.4, Qt.rgba(0, 0, 0, 0.1))
            innerShadow.addColorStop(1, Qt.rgba(0, 0, 0, 0))
            ctx.fillStyle = innerShadow
            ctx.fillRect(cx - topIRx, intTopY, topIRx * 2, cupH * 0.22)

            ctx.restore()

            // ============================================================
            // Liquid fill
            // ============================================================
            if (fillH > 1) {
                ctx.save()

                var clipInset = Theme.scaled(5)
                var clipBotRx = cupBotW - clipInset
                var clipBotRy = botOvalH - Theme.scaled(4)
                ctx.beginPath()
                ctx.moveTo(cx - cupW + clipInset, rimCy)
                ctx.lineTo(cx - clipBotRx, botCy)
                frontArc(ctx, cx, botCy, clipBotRx, clipBotRy, N, false)
                ctx.lineTo(cx + cupW - clipInset, rimCy)
                ctx.closePath()
                ctx.clip()

                var t = fillH / interiorH
                var fillRx = cupBotW + (cupW - cupBotW) * t
                var fillOvalH = botOvalH + (rimOvalH - botOvalH) * t

                var waveAmp = root.currentFlow > 0.1 ? Theme.scaled(2.5) : 0
                var wSteps = 36

                // Liquid body — vertical gradient
                var liqVertGrad = ctx.createLinearGradient(0, botCy + botOvalH, 0, fillTopY)
                liqVertGrad.addColorStop(0, "#140600")
                liqVertGrad.addColorStop(0.15, "#1E0A00")
                liqVertGrad.addColorStop(0.35, "#3A1808")
                liqVertGrad.addColorStop(0.55, "#5A2E14")
                liqVertGrad.addColorStop(0.75, "#7A4422")
                liqVertGrad.addColorStop(0.9, "#a86e3e")
                liqVertGrad.addColorStop(1.0, "#c08050")

                // Build liquid path with wave surface
                ctx.beginPath()
                ctx.moveTo(cx - clipBotRx, botCy)
                frontArc(ctx, cx, botCy, clipBotRx, clipBotRy, N, false)
                ctx.lineTo(cx + fillRx, fillTopY)
                for (var wi = wSteps; wi >= 0; wi--) {
                    var wFrac = wi / wSteps
                    var wx = cx - fillRx + fillRx * 2 * wFrac
                    var wy = fillTopY + Math.sin(root.wavePhase + wi * 0.5) * waveAmp
                    ctx.lineTo(wx, wy)
                }
                ctx.closePath()
                ctx.fillStyle = liqVertGrad
                ctx.fill()

                // Horizontal curvature shading
                var liqHGrad = ctx.createLinearGradient(cx - cupW, 0, cx + cupW, 0)
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

                // Surface specular highlight
                if (fillRatio > 0.05) {
                    var surfGrad = ctx.createLinearGradient(cx - fillRx, fillTopY, cx + fillRx, fillTopY)
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
                        var whx = cx - fillRx * 0.92 + fillRx * 1.84 * (wh / wSteps)
                        var why = fillTopY + Math.sin(root.wavePhase + wh * 0.5) * waveAmp - Theme.scaled(1)
                        if (wh === 0) ctx.moveTo(whx, why)
                        else ctx.lineTo(whx, why)
                    }
                    for (var wh2 = wSteps; wh2 >= 0; wh2--) {
                        var wh2x = cx - fillRx * 0.92 + fillRx * 1.84 * (wh2 / wSteps)
                        var wh2y = fillTopY + Math.sin(root.wavePhase + wh2 * 0.5) * waveAmp + Theme.scaled(6)
                        ctx.lineTo(wh2x, wh2y)
                    }
                    ctx.closePath()
                    ctx.fill()
                }

                // ---- Crema with tiger-stripe pattern ----
                if (fillRatio > 0.12) {
                    var cremaFade = Math.min((fillRatio - 0.12) / 0.2, 1.0)
                    var cremaRx = fillRx * 0.88
                    var cremaRy = fillOvalH * 0.75

                    // Base crema — radial gradient
                    var cremaGrad = ctx.createRadialGradient(
                        cx - cremaRx * 0.1, fillTopY, cremaRx * 0.1,
                        cx, fillTopY, cremaRx)
                    cremaGrad.addColorStop(0, Qt.rgba(0.88, 0.72, 0.48, 0.6 * cremaFade))
                    cremaGrad.addColorStop(0.25, Qt.rgba(0.85, 0.65, 0.38, 0.5 * cremaFade))
                    cremaGrad.addColorStop(0.55, Qt.rgba(0.78, 0.55, 0.30, 0.38 * cremaFade))
                    cremaGrad.addColorStop(0.8, Qt.rgba(0.68, 0.45, 0.22, 0.2 * cremaFade))
                    cremaGrad.addColorStop(1, Qt.rgba(0.55, 0.35, 0.15, 0))
                    ctx.beginPath()
                    ctx.ellipse(cx - cremaRx, fillTopY - cremaRy, cremaRx * 2, cremaRy * 2)
                    ctx.fillStyle = cremaGrad
                    ctx.fill()

                    // Tiger stripes — concentric spiraling arcs
                    var stripeAlpha = 0.25 * cremaFade
                    for (var ts = 0; ts < 4; ts++) {
                        var tsRadius = cremaRx * (0.3 + ts * 0.18)
                        var tsRy = cremaRy * (0.25 + ts * 0.15)
                        var tsPhase = root.steamPhase * (0.15 + ts * 0.05) + ts * 1.5
                        var tsStartAngle = tsPhase % (Math.PI * 2)
                        var tsArcLen = Math.PI * (0.5 + ts * 0.15)

                        ctx.strokeStyle = Qt.rgba(0.45, 0.25, 0.10, stripeAlpha * (1 - ts * 0.18))
                        ctx.lineWidth = Theme.scaled(1.5 + ts * 0.3)
                        ctx.beginPath()
                        var tsSteps = 16
                        for (var tsi = 0; tsi <= tsSteps; tsi++) {
                            var tsAngle = tsStartAngle + tsArcLen * tsi / tsSteps
                            var tsx = cx + tsRadius * Math.cos(tsAngle)
                            var tsy = fillTopY + tsRy * Math.sin(tsAngle) +
                                      Math.sin(root.wavePhase + tsi * 0.3) * waveAmp * 0.3
                            if (tsi === 0) ctx.moveTo(tsx, tsy)
                            else ctx.lineTo(tsx, tsy)
                        }
                        ctx.stroke()
                    }

                    // Crema outer ring
                    ctx.strokeStyle = Qt.rgba(0.9, 0.78, 0.55, 0.3 * cremaFade)
                    ctx.lineWidth = Theme.scaled(2)
                    ctx.beginPath()
                    for (var cj = 0; cj <= wSteps; cj++) {
                        var crAng = Math.PI * cj / wSteps
                        var crx = cx - cremaRx * Math.cos(crAng)
                        var cry = fillTopY - cremaRy * 0.3 * Math.sin(crAng) +
                                  Math.sin(root.wavePhase + cj * 0.5) * waveAmp * 0.4
                        if (cj === 0) ctx.moveTo(crx, cry)
                        else ctx.lineTo(crx, cry)
                    }
                    ctx.stroke()

                    // Bright center spot (light reflecting off crema)
                    var centerSpot = ctx.createRadialGradient(
                        cx - cremaRx * 0.15, fillTopY - cremaRy * 0.1, Theme.scaled(2),
                        cx - cremaRx * 0.15, fillTopY - cremaRy * 0.1, cremaRx * 0.35)
                    centerSpot.addColorStop(0, Qt.rgba(1, 0.95, 0.8, 0.2 * cremaFade))
                    centerSpot.addColorStop(0.5, Qt.rgba(1, 0.9, 0.7, 0.08 * cremaFade))
                    centerSpot.addColorStop(1, Qt.rgba(1, 0.85, 0.6, 0))
                    ctx.beginPath()
                    ctx.ellipse(cx - cremaRx * 0.5, fillTopY - cremaRy * 0.45,
                                cremaRx * 0.7, cremaRy * 0.7)
                    ctx.fillStyle = centerSpot
                    ctx.fill()
                }

                // Meniscus darkening (wall edges)
                var menW = Theme.scaled(14)
                var menH = Theme.scaled(12)
                var menGradL = ctx.createLinearGradient(cx - fillRx, 0, cx - fillRx + menW, 0)
                menGradL.addColorStop(0, Qt.rgba(0, 0, 0, 0.35))
                menGradL.addColorStop(1, Qt.rgba(0, 0, 0, 0))
                ctx.fillStyle = menGradL
                ctx.fillRect(cx - fillRx, fillTopY - Theme.scaled(4), menW, menH)

                var menGradR = ctx.createLinearGradient(cx + fillRx, 0, cx + fillRx - menW, 0)
                menGradR.addColorStop(0, Qt.rgba(0, 0, 0, 0.35))
                menGradR.addColorStop(1, Qt.rgba(0, 0, 0, 0))
                ctx.fillStyle = menGradR
                ctx.fillRect(cx + fillRx - menW, fillTopY - Theme.scaled(4), menW, menH)

                // ---- Ripple rings (where stream impacts liquid) ----
                if (isPouring && root.currentFlow > 0.3 && fillRatio < 0.95) {
                    for (var rp = 0; rp < 3; rp++) {
                        var rpAge = (root.ripplePhase + rp * 2.0) % 6.0
                        if (rpAge > 3.0) continue
                        var rpProgress = rpAge / 3.0
                        var rpAlpha = (1.0 - rpProgress) * 0.2
                        var rpRadiusX = Theme.scaled(4) + rpProgress * fillRx * 0.35
                        var rpRadiusY = rpRadiusX * (fillOvalH / fillRx)

                        ctx.strokeStyle = Qt.rgba(0.9, 0.8, 0.6, rpAlpha)
                        ctx.lineWidth = Theme.scaled(1)
                        ctx.beginPath()
                        // Only draw front portion of ripple
                        for (var rpi = 0; rpi <= 20; rpi++) {
                            var rpAngle = Math.PI + (Math.PI * rpi / 20)
                            var rpx = cx + rpRadiusX * Math.cos(rpAngle)
                            var rpy = fillTopY + rpRadiusY * Math.sin(rpAngle) +
                                      Math.sin(root.wavePhase) * waveAmp * 0.5
                            if (rpi === 0) ctx.moveTo(rpx, rpy)
                            else ctx.lineTo(rpx, rpy)
                        }
                        ctx.stroke()
                    }
                }

                ctx.restore()
            }

            // ============================================================
            // Bottomless portafilter + single stream (during extraction)
            // ============================================================
            if (isPouring && root.currentFlow > 0.3 && fillRatio < 1.0) {
                var pfCy = rimCy - rimOvalH - Theme.scaled(36)  // center of portafilter
                var pfRx = cupW * 0.48    // ellipse half-width (seen from below at angle)
                var pfRy = pfRx * 0.28    // perspective squish
                var pfRimW = Theme.scaled(7) // metallic rim thickness
                var pfDepth = Theme.scaled(10) // visible side depth

                // ---- Portafilter side band (visible rim depth) ----
                var pfSideGrad = ctx.createLinearGradient(cx - pfRx, 0, cx + pfRx, 0)
                pfSideGrad.addColorStop(0, Qt.rgba(0.18, 0.18, 0.20, 0.9))
                pfSideGrad.addColorStop(0.2, Qt.rgba(0.40, 0.40, 0.44, 0.9))
                pfSideGrad.addColorStop(0.45, Qt.rgba(0.55, 0.55, 0.60, 0.95))
                pfSideGrad.addColorStop(0.55, Qt.rgba(0.50, 0.50, 0.55, 0.95))
                pfSideGrad.addColorStop(0.8, Qt.rgba(0.35, 0.35, 0.38, 0.9))
                pfSideGrad.addColorStop(1, Qt.rgba(0.15, 0.15, 0.18, 0.9))

                ctx.beginPath()
                frontArc(ctx, cx, pfCy, pfRx, pfRy, N, true)
                // Bottom edge offset down
                for (var pfb = N; pfb >= 0; pfb--) {
                    var pfba = Math.PI + (Math.PI * pfb / N)
                    ctx.lineTo(cx + pfRx * Math.cos(pfba),
                               pfCy + pfDepth + pfRy * Math.sin(pfba))
                }
                ctx.closePath()
                ctx.fillStyle = pfSideGrad
                ctx.fill()

                // ---- Basket bottom (visible through bottomless PF) ----
                // Dark coffee puck with warm tones
                var basketGrad = ctx.createRadialGradient(
                    cx - pfRx * 0.1, pfCy, pfRx * 0.15,
                    cx, pfCy, pfRx * 0.85)
                basketGrad.addColorStop(0, Qt.rgba(0.35, 0.22, 0.12, 0.85))  // warm center
                basketGrad.addColorStop(0.4, Qt.rgba(0.28, 0.16, 0.08, 0.8))
                basketGrad.addColorStop(0.7, Qt.rgba(0.20, 0.12, 0.06, 0.75))
                basketGrad.addColorStop(1, Qt.rgba(0.12, 0.08, 0.04, 0.7))   // dark edge

                var innerPfRx = pfRx - pfRimW
                var innerPfRy = pfRy - pfRimW * 0.28
                ctx.beginPath()
                ctx.ellipse(cx - innerPfRx, pfCy - innerPfRy, innerPfRx * 2, innerPfRy * 2)
                ctx.fillStyle = basketGrad
                ctx.fill()

                // Puck extraction pattern — golden streaks radiating from center
                var puckAlpha = 0.3 + root.currentFlow * 0.08
                for (var pk = 0; pk < 8; pk++) {
                    var pkAngle = (Math.PI * 2 * pk / 8) + root.steamPhase * 0.2
                    var pkX1 = cx + innerPfRx * 0.15 * Math.cos(pkAngle)
                    var pkY1 = pfCy + innerPfRy * 0.15 * Math.sin(pkAngle)
                    var pkX2 = cx + innerPfRx * 0.7 * Math.cos(pkAngle)
                    var pkY2 = pfCy + innerPfRy * 0.7 * Math.sin(pkAngle)

                    ctx.strokeStyle = Qt.rgba(0.6, 0.4, 0.2, puckAlpha)
                    ctx.lineWidth = Theme.scaled(1.5)
                    ctx.beginPath()
                    ctx.moveTo(pkX1, pkY1)
                    ctx.lineTo(pkX2, pkY2)
                    ctx.stroke()
                }

                // ---- Metallic rim (top ellipse ring) ----
                var rimGrad = ctx.createLinearGradient(cx - pfRx, pfCy, cx + pfRx, pfCy)
                rimGrad.addColorStop(0, Qt.rgba(0.20, 0.20, 0.23, 1))
                rimGrad.addColorStop(0.15, Qt.rgba(0.45, 0.45, 0.50, 1))
                rimGrad.addColorStop(0.35, Qt.rgba(0.62, 0.62, 0.67, 1))
                rimGrad.addColorStop(0.5, Qt.rgba(0.70, 0.70, 0.75, 1))   // bright specular
                rimGrad.addColorStop(0.65, Qt.rgba(0.58, 0.58, 0.63, 1))
                rimGrad.addColorStop(0.85, Qt.rgba(0.38, 0.38, 0.42, 1))
                rimGrad.addColorStop(1, Qt.rgba(0.16, 0.16, 0.19, 1))

                // Outer ellipse
                ctx.beginPath()
                ctx.ellipse(cx - pfRx, pfCy - pfRy, pfRx * 2, pfRy * 2)
                // Cut out inner ellipse (ring shape)
                ctx.ellipse(cx - innerPfRx, pfCy - innerPfRy, innerPfRx * 2, innerPfRy * 2)
                ctx.fillStyle = rimGrad
                // Use even-odd to create ring
                ctx.fill("evenodd")

                // Rim outline
                ctx.beginPath()
                ctx.ellipse(cx - pfRx, pfCy - pfRy, pfRx * 2, pfRy * 2)
                ctx.strokeStyle = Qt.rgba(0.6, 0.6, 0.65, 0.5)
                ctx.lineWidth = Theme.scaled(1)
                ctx.stroke()

                // Specular highlight arc on rim
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.25)
                ctx.lineWidth = Theme.scaled(2)
                ctx.beginPath()
                for (var prh = 8; prh <= 20; prh++) {
                    var prha = Math.PI + (Math.PI * prh / N)
                    var prhx = cx + (pfRx - pfRimW * 0.5) * Math.cos(prha)
                    var prhy = pfCy + (pfRy - pfRimW * 0.14) * Math.sin(prha) - Theme.scaled(1)
                    if (prh === 8) ctx.moveTo(prhx, prhy)
                    else ctx.lineTo(prhx, prhy)
                }
                ctx.stroke()

                // ---- Handle stub (right side) ----
                var pfHandleX = cx + pfRx + Theme.scaled(2)
                var pfHandleY = pfCy
                var pfHandleLen = pfRx * 0.45
                var pfHandleThick = Theme.scaled(6)

                var phGrad = ctx.createLinearGradient(pfHandleX, pfHandleY - pfHandleThick,
                                                      pfHandleX, pfHandleY + pfHandleThick)
                phGrad.addColorStop(0, Qt.rgba(0.25, 0.25, 0.28, 0.85))
                phGrad.addColorStop(0.4, Qt.rgba(0.48, 0.48, 0.52, 0.9))
                phGrad.addColorStop(0.6, Qt.rgba(0.42, 0.42, 0.46, 0.9))
                phGrad.addColorStop(1, Qt.rgba(0.22, 0.22, 0.25, 0.85))

                ctx.beginPath()
                ctx.moveTo(pfHandleX - Theme.scaled(3), pfHandleY - pfHandleThick)
                ctx.lineTo(pfHandleX + pfHandleLen, pfHandleY - pfHandleThick * 0.7)
                ctx.quadraticCurveTo(pfHandleX + pfHandleLen + Theme.scaled(4), pfHandleY,
                                     pfHandleX + pfHandleLen, pfHandleY + pfHandleThick * 0.7)
                ctx.lineTo(pfHandleX - Theme.scaled(3), pfHandleY + pfHandleThick)
                ctx.closePath()
                ctx.fillStyle = phGrad
                ctx.fill()

                // ---- Single stream from basket center ----
                var streamTopY = pfCy + pfRy + Theme.scaled(3)
                // Cup interior floor — well above the bottom outline
                var cupFloorY = botCy - botOvalH - Theme.scaled(8)
                var streamBot = fillH > 1 ? Math.min(fillTopY, cupFloorY) : cupFloorY
                var streamLen = streamBot - streamTopY
                if (streamLen < Theme.scaled(5)) streamLen = Theme.scaled(5)

                // Stream width — wider for better visibility
                var streamTopW = Theme.scaled(14) + root.currentFlow * Theme.scaled(3)
                streamTopW = Math.min(streamTopW, Theme.scaled(26))
                var streamBotW = streamTopW * 0.5  // narrows as it falls (cohesive stream)
                var streamSegs = 20

                // Build stream as a wobbling filled shape
                ctx.beginPath()
                // Left edge top to bottom
                for (var ssL = 0; ssL <= streamSegs; ssL++) {
                    var ssLt = ssL / streamSegs
                    var wobL = Math.sin(root.wavePhase * 2.2 + ssLt * 5.0) * Theme.scaled(2.5) * ssLt
                    var ssLw = streamTopW + (streamBotW - streamTopW) * ssLt
                    var ssLx = cx + wobL - ssLw * 0.5
                    var ssLy = streamTopY + streamLen * ssLt
                    if (ssL === 0) ctx.moveTo(ssLx, ssLy)
                    else ctx.lineTo(ssLx, ssLy)
                }
                // Right edge bottom to top
                for (var ssR = streamSegs; ssR >= 0; ssR--) {
                    var ssRt = ssR / streamSegs
                    var wobR = Math.sin(root.wavePhase * 2.2 + ssRt * 5.0) * Theme.scaled(2.5) * ssRt
                    var ssRw = streamTopW + (streamBotW - streamTopW) * ssRt
                    var ssRx = cx + wobR + ssRw * 0.5
                    var ssRy = streamTopY + streamLen * ssRt
                    ctx.lineTo(ssRx, ssRy)
                }
                ctx.closePath()

                // Stream gradient — rich espresso tones
                var stGrad = ctx.createLinearGradient(0, streamTopY, 0, streamBot)
                stGrad.addColorStop(0, Qt.rgba(0.55, 0.35, 0.18, 0.5))
                stGrad.addColorStop(0.15, Qt.rgba(0.52, 0.32, 0.16, 0.65))
                stGrad.addColorStop(0.4, Qt.rgba(0.48, 0.28, 0.14, 0.75))
                stGrad.addColorStop(0.7, Qt.rgba(0.42, 0.24, 0.12, 0.82))
                stGrad.addColorStop(1, Qt.rgba(0.38, 0.20, 0.10, 0.88))
                ctx.fillStyle = stGrad
                ctx.fill()

                // Center highlight (gives the stream volume/roundness)
                ctx.beginPath()
                for (var ssC = 0; ssC <= streamSegs; ssC++) {
                    var ssCt = ssC / streamSegs
                    var wobC = Math.sin(root.wavePhase * 2.2 + ssCt * 5.0) * Theme.scaled(2.5) * ssCt
                    var ssCx = cx + wobC
                    var ssCy = streamTopY + streamLen * ssCt
                    if (ssC === 0) ctx.moveTo(ssCx, ssCy)
                    else ctx.lineTo(ssCx, ssCy)
                }
                ctx.strokeStyle = Qt.rgba(0.75, 0.55, 0.32, 0.25)
                ctx.lineWidth = streamTopW * 0.25
                ctx.lineCap = "round"
                ctx.stroke()

                // Droplets traveling down the stream
                for (var dr = 0; dr < 5; dr++) {
                    var drPhase = (root.wavePhase * 1.5 + dr * 1.4) % 3.5
                    var drT = drPhase / 3.5
                    if (drT > 0.95) continue
                    var drY = streamTopY + streamLen * drT
                    if (fillH > 1 && drY > fillTopY + Theme.scaled(5)) continue

                    var drWob = Math.sin(root.wavePhase * 2.2 + drT * 5.0) * Theme.scaled(2.5) * drT
                    var drSize = Theme.scaled(2.5 + root.currentFlow * 0.4) * (1 - drT * 0.3)
                    ctx.beginPath()
                    ctx.arc(cx + drWob, drY, drSize, 0, Math.PI * 2)
                    ctx.fillStyle = Qt.rgba(0.65, 0.42, 0.22, 0.4 + drT * 0.35)
                    ctx.fill()
                }

                // Splash glow at impact point
                var splashY = fillH > 1 ? Math.min(fillTopY, cupFloorY) : cupFloorY
                var splashGlow = ctx.createRadialGradient(
                    cx, splashY, Theme.scaled(2),
                    cx, splashY, Theme.scaled(18))
                splashGlow.addColorStop(0, Qt.rgba(0.7, 0.5, 0.3, 0.5))
                splashGlow.addColorStop(0.4, Qt.rgba(0.6, 0.4, 0.2, 0.2))
                splashGlow.addColorStop(1, Qt.rgba(0.5, 0.3, 0.15, 0))
                ctx.beginPath()
                ctx.arc(cx, splashY, Theme.scaled(18), 0, Math.PI * 2)
                ctx.fillStyle = splashGlow
                ctx.fill()
            }

            // ============================================================
            // Cup outline — colored by tracking status
            // ============================================================
            var outlineColor = root.hasGoal ? root.trackColor : Theme.textSecondaryColor
            ctx.strokeStyle = outlineColor
            ctx.lineWidth = Theme.scaled(7)
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            // Draw walls + bottom as one continuous path for clean joins
            ctx.beginPath()
            ctx.moveTo(cx - cupW, rimCy)
            ctx.lineTo(cx - cupBotW, botCy)
            frontArc(ctx, cx, botCy, cupBotW, botOvalH, N, false)
            ctx.lineTo(cx + cupW, rimCy)
            ctx.stroke()

            // ============================================================
            // Top rim — ceramic lip with depth
            // ============================================================
            // Back rim
            ctx.strokeStyle = Qt.rgba(0.4, 0.4, 0.45, 0.35)
            ctx.lineWidth = Theme.scaled(2)
            ctx.beginPath()
            backArc(ctx, cx, rimCy, cupW, rimOvalH, N, true)
            ctx.stroke()

            // Rim band
            var outerRimCy = rimCy
            var innerRimCy = rimCy + rimThick
            var innerRimRx = cupW - Theme.scaled(4)
            var innerRimRy = rimOvalH - Theme.scaled(2)

            var rimBandGrad = ctx.createLinearGradient(cx - cupW, 0, cx + cupW, 0)
            rimBandGrad.addColorStop(0, Qt.rgba(0.14, 0.14, 0.17, 1))
            rimBandGrad.addColorStop(0.2, Qt.rgba(0.28, 0.28, 0.33, 1))
            rimBandGrad.addColorStop(0.4, Qt.rgba(0.40, 0.40, 0.45, 1))
            rimBandGrad.addColorStop(0.6, Qt.rgba(0.38, 0.38, 0.43, 1))
            rimBandGrad.addColorStop(0.8, Qt.rgba(0.24, 0.24, 0.28, 1))
            rimBandGrad.addColorStop(1, Qt.rgba(0.10, 0.10, 0.13, 1))

            ctx.beginPath()
            frontArc(ctx, cx, outerRimCy, cupW, rimOvalH, N, true)
            for (var rr = N; rr >= 0; rr--) {
                var rra = Math.PI + (Math.PI * rr / N)
                ctx.lineTo(cx + innerRimRx * Math.cos(rra),
                           innerRimCy + innerRimRy * Math.sin(rra))
            }
            ctx.closePath()
            ctx.fillStyle = rimBandGrad
            ctx.fill()

            // Front rim outer stroke
            ctx.strokeStyle = outlineColor
            ctx.lineWidth = Theme.scaled(7)
            ctx.beginPath()
            frontArc(ctx, cx, outerRimCy, cupW, rimOvalH, N, true)
            ctx.stroke()

            // Rim specular highlight
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.2)
            ctx.lineWidth = Theme.scaled(2)
            ctx.beginPath()
            var rimMidRx = (cupW + innerRimRx) * 0.5
            var rimMidRy = (rimOvalH + innerRimRy) * 0.5
            var rimMidCy = (outerRimCy + innerRimCy) * 0.5
            for (var rh = 6; rh <= 24; rh++) {
                var rha = Math.PI + (Math.PI * rh / N)
                var rhx = cx + rimMidRx * Math.cos(rha)
                var rhy = rimMidCy + rimMidRy * Math.sin(rha) - Theme.scaled(1)
                if (rh === 6) ctx.moveTo(rhx, rhy)
                else ctx.lineTo(rhx, rhy)
            }
            ctx.stroke()

            // ============================================================
            // Body specular highlights
            // ============================================================
            // Primary wide highlight
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.10)
            ctx.lineWidth = Theme.scaled(9)
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.moveTo(cx - cupW * 0.62, rimCy + cupH * 0.1)
            ctx.lineTo(cx - cupBotW * 0.62, botCy - cupH * 0.08)
            ctx.stroke()

            // Sharp thin highlight
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.07)
            ctx.lineWidth = Theme.scaled(2.5)
            ctx.beginPath()
            ctx.moveTo(cx - cupW * 0.52, rimCy + cupH * 0.06)
            ctx.lineTo(cx - cupBotW * 0.52, botCy - cupH * 0.04)
            ctx.stroke()

            // Faint right rim light
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.035)
            ctx.lineWidth = Theme.scaled(5)
            ctx.beginPath()
            ctx.moveTo(cx + cupW * 0.72, rimCy + cupH * 0.15)
            ctx.lineTo(cx + cupBotW * 0.72, botCy - cupH * 0.1)
            ctx.stroke()

            // ============================================================
            // Steam wisps (S-curve with varying thickness)
            // ============================================================
            if (root.currentWeight > 0 && root.currentFlow < 2.0) {
                var steamAlphaBase = Math.min(root.currentWeight / 5.0, 1.0) * 0.14

                for (var sp = 0; sp < 4; sp++) {
                    var steamOffX = (sp - 1.5) * cupW * 0.25
                    var steamBaseY2 = fillH > 1 ? fillTopY : rimCy
                    var steamStartY = Math.min(steamBaseY2, rimCy) - Theme.scaled(3)
                    var steamH = Theme.scaled(30 + sp * 10)
                    var steamEndY = steamStartY - steamH

                    var phOff = root.steamPhase * (0.8 + sp * 0.2) + sp * 1.7
                    // S-curve: two opposing bends
                    var d1x = Math.sin(phOff) * Theme.scaled(10 + sp * 2)
                    var d1y = steamH * 0.33
                    var d2x = Math.sin(phOff * 1.2 + 2) * Theme.scaled(8 + sp * 2)
                    var d2y = steamH * 0.66

                    // Bezier helpers for steam S-curve (hoisted out of loop)
                    function steamX(tt, offX, cd1x, cd2x) {
                        var sx0 = cx + offX
                        var sx1 = cx + offX + cd1x
                        var sx2 = cx + offX + cd2x
                        var sx3 = cx + offX + cd1x * 0.5
                        var u = 1 - tt
                        return u*u*u*sx0 + 3*u*u*tt*sx1 + 3*u*tt*tt*sx2 + tt*tt*tt*sx3
                    }
                    function steamY(tt, sY, cdy1, cdy2, eY) {
                        var sy0 = sY
                        var sy1 = sY - cdy1
                        var sy2 = sY - cdy2
                        var sy3 = eY
                        var u = 1 - tt
                        return u*u*u*sy0 + 3*u*u*tt*sy1 + 3*u*tt*tt*sy2 + tt*tt*tt*sy3
                    }

                    // Fade out at top, thicken slightly in middle
                    var segments = 12
                    for (var seg = 0; seg < segments; seg++) {
                        var segT0 = seg / segments
                        var segT1 = (seg + 1) / segments

                        var segAlpha = steamAlphaBase * (1 - segT0 * 0.85) * (0.5 + sp * 0.12)
                        var segWidth = Theme.scaled(1 + Math.sin(segT0 * Math.PI) * 1.5)

                        ctx.strokeStyle = Qt.rgba(0.85, 0.85, 0.9, segAlpha)
                        ctx.lineWidth = segWidth
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        ctx.moveTo(steamX(segT0, steamOffX, d1x, d2x), steamY(segT0, steamStartY, d1y, d2y, steamEndY))
                        ctx.lineTo(steamX(segT1, steamOffX, d1x, d2x), steamY(segT1, steamStartY, d1y, d2y, steamEndY))
                        ctx.stroke()
                    }
                }
            }

            // ============================================================
            // Completion glow
            // ============================================================
            if (root.currentWeight >= root.targetWeight && root.targetWeight > 0) {
                ctx.shadowColor = Theme.successColor
                ctx.shadowBlur = Theme.scaled(25)
                ctx.strokeStyle = Theme.successColor
                ctx.lineWidth = Theme.scaled(2)
                ctx.beginPath()
                frontArc(ctx, cx, rimCy, cupW, rimOvalH, N, true)
                ctx.stroke()
                ctx.shadowBlur = 0
            }
        }
    }

    // Weight text overlay
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: parent.height * 0.12
        spacing: Theme.scaled(2)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.currentWeight.toFixed(1) + TranslationManager.translate("common.unit.grams", "g")
            color: Theme.textColor
            font.pixelSize: Theme.scaled(38)
            font.weight: Font.Bold
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.targetWeight > 0
                ? TranslationManager.translate("espresso.cupFill.target", "target") + " " + root.targetWeight.toFixed(0) + TranslationManager.translate("common.unit.grams", "g")
                : ""
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(16)
        }
    }
}
