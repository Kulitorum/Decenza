#pragma once

// The shot graph on the web compare page and shot page: Chart.js curves, phase
// markers, a crosshair read out in a table under the plot, and a row of chips for
// curves and phases. The page includes WEB_JS_COMPARISON_TEXT and
// WEB_JS_ESCAPE_HTML, defines graphTraces() returning the shots to draw, base first:
//   [{ curves: { key: [[t, v], ...] }, phases: [{ time, label, suffix }], offset, hidden }]
// defines graphExtraChip as null or { label, tip, on(), toggle() }, then calls
// graphInit(canvasId).

inline constexpr const char* WEB_CSS_SHOT_GRAPH = R"CSS(
        .layout { max-width: 1600px; margin: 0 auto; padding: 1rem; display: grid; gap: 1rem;
                  grid-template-columns: minmax(0, 1fr); }
        /* Side by side once both fit: the graph stays in view while the rest scrolls. */
        @media (min-width: 1300px) {
            .layout { grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); align-items: start; }
            .graph-col { position: sticky; top: 4.5rem; }
        }
        .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 1rem; }
        .chart-wrapper { position: relative; height: 360px; cursor: crosshair; }
        @media (min-width: 1300px) { .chart-wrapper { height: 52vh; } }

        .readout { margin-top: 0.5rem; overflow-x: auto; font-size: 0.8125rem; font-variant-numeric: tabular-nums; }
        .readout table { border-collapse: collapse; }
        .readout th, .readout td { padding: 0.15rem 0.6rem; text-align: right; white-space: nowrap; }
        .readout th:first-child, .readout td:first-child { text-align: left; }
        .readout .quiet { color: var(--text-secondary); opacity: 0.5; }

        .chips { display: flex; flex-wrap: wrap; gap: 0.4rem; margin-top: 0.75rem; align-items: center; }
        .chip { display: inline-flex; align-items: center; gap: 0.35rem; padding: 0.2rem 0.7rem; border-radius: 999px;
                border: 1px solid var(--border); background: transparent; color: var(--text-secondary);
                font-size: 0.8125rem; cursor: pointer; opacity: 0.6; font-family: inherit; }
        .chip.on { opacity: 1; color: var(--text); }
        .chip .dot { width: 7px; height: 7px; border-radius: 50%; }
        .chip-sep { width: 1px; height: 1.4rem; background: var(--border); margin: 0 0.2rem; }

        .swatch { display: inline-block; width: 22px; height: 2px; vertical-align: middle; background: var(--text); }
        .swatch.heavy { height: 3px; }
        .swatch.s1 { background: repeating-linear-gradient(90deg, var(--text) 0 4px, transparent 4px 6px); }
        .swatch.s2 { background: repeating-linear-gradient(90deg, var(--text) 0 7px, transparent 7px 9px, var(--text) 9px 11px, transparent 11px 13px); }
        @media (max-width: 600px) {
            .chip-sep { display: none; }
            .layout { padding: 0.5rem; }
        }
)CSS";

inline constexpr const char* WEB_JS_SHOT_GRAPH = R"JS(
        // A goal curve has no chip: it is drawn, dashed, whenever its own curve is.
        var curves = [
            { key: "pressure", label: "P", name: "Pressure", color: "#18c37e", axis: "y", on: true, tip: "Pressure at the group (bar)" },
            { key: "flow", label: "F", name: "Flow", color: "#4e85f4", axis: "y", on: true, tip: "Flow through the puck (mL/s)" },
            { key: "temp", label: "T", name: "Temp", color: "#e73249", axis: "y3", on: true, tip: "Group temperature (°C)" },
            { key: "weight", label: "W", name: "Weight", color: "#a2693d", axis: "y2", on: true, tip: "Weight in the cup (g)" },
            { key: "weightFlow", label: "WF", name: "Weight flow", color: "#d4a574", axis: "y", on: true, tip: "Weight flow into the cup (g/s)" },
            { key: "resistance", label: "R", name: "Resistance", color: "#eae83d", axis: "y", on: false, tip: "Puck resistance (pressure / flow)" },
            { key: "darcyR", label: "dR", name: "Resistance (P/F²)", color: "#f0a500", axis: "y", on: false, tip: "Darcy resistance (pressure / flow²)" },
            { key: "conductance", label: "C", name: "Conductance", color: "#00c8d7", axis: "y", on: false, tip: "Conductance (flow² / pressure)" },
            { key: "dCdt", label: "dC/dt", name: "dC/dt", color: "#e05aa0", axis: "y4", on: false, tip: "Rate of change of conductance; spikes reveal transient channels" },
            { key: "mixTemp", label: "Tmix", name: "Mix temp", color: "#d79be0", axis: "y3", on: false, tip: "Water mix temperature" },
            { key: "mixTempGoal", label: "Tmixg", name: "Mix temp goal", color: "#a983c9", axis: "y3", on: false, tip: "Water mix temperature goal" },
            { key: "pressureGoal", goalOf: "pressure", color: "#69fdb3", axis: "y" },
            { key: "flowGoal", goalOf: "flow", color: "#7aaaff", axis: "y" }
        ];
        var phaseColors = ["#FFD600", "#E91E63", "#00E5FF", "#76FF03", "#FF6D00"];
        var dashes = [[], [6, 5], [10, 4, 2, 4]];
        var crosshair = null;
        var showAllCurves = false;
        var hiddenPhases = {};
        var phaseLabels = [];
        var chart = null;

        function curveByKey(k) { for (var i = 0; i < curves.length; i++) if (curves[i].key === k) return curves[i]; return null; }
        function curveOn(c) { return c.goalOf ? curveByKey(c.goalOf).on : c.on; }
        // Curves some shot recorded, so a chip never toggles nothing.
        function chipCurves() {
            var tr = graphTraces();
            return curves.filter(function(c) {
                return !c.goalOf && tr.some(function(t) { return (t.curves[c.key] || []).length > 0; });
            });
        }
        function graphSwatch(col) { return "<span class='swatch s" + (col % 3) + (col === 0 ? " heavy" : "") + "'></span>"; }
        function multiShot() { return graphTraces().length > 1; }

        var phasePlugin = {
            id: "phases",
            afterDraw: function(c) {
                var ctx = c.ctx, xs = c.scales.x, ys = c.scales.y;
                ctx.save();
                graphTraces().forEach(function(t, col) {
                    if (t.hidden) return;
                    t.phases.forEach(function(p) {
                        if (hiddenPhases[p.label]) return;
                        var x = xs.getPixelForValue(p.time + (t.offset || 0));
                        if (x < xs.left || x > xs.right) return;
                        var color = phaseColors[phaseLabels.indexOf(p.label) % phaseColors.length];
                        ctx.setLineDash(dashes[col % 3]); ctx.strokeStyle = color; ctx.globalAlpha = 0.7; ctx.lineWidth = 1.5;
                        ctx.beginPath(); ctx.moveTo(x, ys.top); ctx.lineTo(x, ys.bottom); ctx.stroke();
                        if (col === 0) {
                            ctx.save();
                            ctx.setLineDash([]); ctx.globalAlpha = 0.9; ctx.fillStyle = color; ctx.font = "11px sans-serif";
                            ctx.translate(x + 4, ys.top + 6); ctx.rotate(-Math.PI / 2); ctx.textAlign = "right";
                            ctx.fillText(p.label + (p.suffix || ""), 0, 0);
                            ctx.restore();
                        }
                    });
                });
                if (crosshair !== null) {
                    var cx = xs.getPixelForValue(crosshair);
                    ctx.setLineDash([3, 3]); ctx.strokeStyle = "rgba(255,255,255,0.7)"; ctx.globalAlpha = 1; ctx.lineWidth = 1;
                    ctx.beginPath(); ctx.moveTo(cx, ys.top); ctx.lineTo(cx, ys.bottom); ctx.stroke();
                }
                ctx.restore();
            }
        };
        function datasets() {
            var out = [];
            graphTraces().forEach(function(t, col) {
                if (t.hidden) return;
                var dx = t.offset || 0;
                curves.forEach(function(c) {
                    if (!curveOn(c)) return;
                    var pts = (t.curves[c.key] || []).map(function(p) { return { x: p[0] + dx, y: p[1] }; });
                    if (pts.length === 0) return;
                    out.push({ data: pts, borderColor: c.color, pointRadius: 0, yAxisID: c.axis, spanGaps: false,
                               borderWidth: c.goalOf ? 1 : !multiShot() ? 2 : col === 0 ? 2.5 : 1.5,
                               tension: c.goalOf ? 0.1 : 0.2, borderDash: c.goalOf ? [5, 5] : dashes[col % 3] });
                });
            });
            return out;
        }
        function redrawChart() { chart.data.datasets = datasets(); chart.update("none"); renderReadout(); }

        function timeAt(clientX) {
            var r = chart.canvas.getBoundingClientRect(), xs = chart.scales.x;
            return Math.max(xs.min, Math.min(xs.max, xs.getValueForPixel(clientX - r.left)));
        }
        function inspect(t) { crosshair = t; chart.update("none"); renderReadout(); }
        function valueAt(t, key, time) {
            var pts = t.curves[key] || [], best = null, bd = 1;
            var tt = time - (t.offset || 0);
            for (var i = 0; i < pts.length; i++) {
                if (pts[i][1] === null) continue;
                var d = Math.abs(pts[i][0] - tt); if (d < bd) { bd = d; best = pts[i][1]; }
            }
            return best;
        }
        // One row per visible shot whether or not anything is inspected, so a tap never moves the page.
        function renderReadout() {
            var on = chipCurves().filter(function(c) { return c.on; }), many = multiShot();
            var h = "<table><tr><th>" + (crosshair !== null ? crosshair.toFixed(1) + " s" : "") + "</th>";
            on.forEach(function(c) { h += "<th style='color:" + c.color + "'>" + c.label + "</th>"; });
            h += "</tr>";
            graphTraces().forEach(function(t, col) {
                if (t.hidden) return;
                h += "<tr><td>" + (many ? graphSwatch(col) : "") + "</td>";
                on.forEach(function(c) {
                    var v = crosshair !== null ? valueAt(t, c.key, crosshair) : null;
                    h += v === null ? "<td class='quiet'>" + (crosshair === null ? "" : "–") + "</td>" : "<td>" + v.toFixed(1) + "</td>";
                });
                h += "</tr>";
            });
            document.getElementById("readout").innerHTML = h + "</table>";
        }

        function renderChips() {
            var h = "", off = 0;
            chipCurves().forEach(function(c) {
                if (!c.on) off++;
                if (!c.on && !showAllCurves) return;
                h += "<button class='chip" + (c.on ? " on" : "") + "' aria-pressed='" + c.on + "' title='" + escapeHtml(c.name + ": " + c.tip)
                   + "' onclick='toggleCurve(\"" + c.key + "\")'" + (c.on ? " style='border-color:" + c.color + "'" : "")
                   + "><span class='dot' style='background:" + c.color + "'></span>" + c.label + "</button>";
            });
            if (off > 0) h += "<button class='chip' title='" + escapeHtml(txt(showAllCurves ? "tip.fewerCurves" : "tip.moreCurves"))
                            + "' onclick='showAllCurves=!showAllCurves;renderChips()'>" + (showAllCurves ? escapeHtml(txt("ui.fewerCurves")) : "+" + off) + "</button>";
            if (phaseLabels.length > 0) h += "<span class='chip-sep'></span>";
            phaseLabels.forEach(function(p, i) {
                var color = phaseColors[i % phaseColors.length], on = !hiddenPhases[p];
                h += "<button class='chip" + (on ? " on" : "") + "' aria-pressed='" + on + "' title='" + escapeHtml(txt("tip.phase").replace("%1", p))
                   + "' onclick='togglePhase(" + i + ")'" + (on ? " style='border-color:" + color + "'" : "")
                   + "><span class='dot' style='background:" + color + "'></span>" + escapeHtml(p) + "</button>";
            });
            if (graphExtraChip) {
                var x = graphExtraChip, xon = x.on();
                h += "<button class='chip" + (xon ? " on" : "") + "' aria-pressed='" + xon + "' title='" + escapeHtml(x.tip)
                   + "' onclick='graphExtraChip.toggle();renderChips();redrawChart()'>" + escapeHtml(x.label) + "</button>";
            }
            document.getElementById("chips").innerHTML = h;
        }
        function toggleCurve(key) { var c = curveByKey(key); c.on = !c.on; renderChips(); redrawChart(); }
        function togglePhase(i) { var p = phaseLabels[i]; if (hiddenPhases[p]) delete hiddenPhases[p]; else hiddenPhases[p] = true; renderChips(); chart.update("none"); }

        function graphInit(canvasId) {
            graphTraces().forEach(function(t) {
                t.phases.forEach(function(p) { if (phaseLabels.indexOf(p.label) < 0) phaseLabels.push(p.label); });
            });
            chart = new Chart(document.getElementById(canvasId).getContext("2d"), {
                type: "line",
                plugins: [phasePlugin],
                data: { datasets: datasets() },
                options: {
                    responsive: true, maintainAspectRatio: false, animation: false, events: [],
                    plugins: { legend: { display: false }, tooltip: { enabled: false } },
                    scales: {
                        x: { type: "linear", min: 0, title: { display: true, text: "Time (s)", color: "#8b949e" },
                             grid: { color: "rgba(48,54,61,0.5)" }, ticks: { color: "#8b949e" } },
                        y: { min: 0, max: 12, title: { display: true, text: "bar / mL/s", color: "#8b949e" },
                             grid: { color: "rgba(48,54,61,0.5)" }, ticks: { color: "#8b949e" } },
                        y2: { position: "right", min: 0, title: { display: true, text: "g", color: "#a2693d" },
                              grid: { display: false }, ticks: { color: "#a2693d" } },
                        y3: { display: false, min: 40, max: 100 },
                        y4: { display: false }
                    }
                }
            });
            var cvs = chart.canvas, dragging = false, touchDir = null, tsx = 0, tsy = 0;
            cvs.addEventListener("mousedown", function(e) { dragging = true; inspect(timeAt(e.clientX)); });
            document.addEventListener("mousemove", function(e) { if (dragging) inspect(timeAt(e.clientX)); });
            document.addEventListener("mouseup", function() { dragging = false; });
            cvs.addEventListener("touchstart", function(e) { touchDir = null; tsx = e.touches[0].clientX; tsy = e.touches[0].clientY; }, { passive: true });
            cvs.addEventListener("touchmove", function(e) {
                var dx = e.touches[0].clientX - tsx, dy = e.touches[0].clientY - tsy;
                if (!touchDir && Math.hypot(dx, dy) > 10) touchDir = Math.abs(dx) > Math.abs(dy) ? "h" : "v";
                if (touchDir === "h") { e.preventDefault(); inspect(timeAt(e.touches[0].clientX)); }
            }, { passive: false });
            cvs.addEventListener("touchend", function(e) { if (!touchDir) inspect(timeAt(e.changedTouches[0].clientX)); touchDir = null; });
            renderChips();
            renderReadout();
        }
)JS";
