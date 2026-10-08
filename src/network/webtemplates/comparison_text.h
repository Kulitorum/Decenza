#pragma once

// Words and numbers for ShotComparison::compare() output on the web: the compare
// page and the shot page. The rules are the app's ComparisonText.qml, in English.
// The page defines `texts` (ShotComparisonText::toJson), `dialInLabels`
// (ProfileDialInText::labelMap) and `puckFlags` (EquipmentStorage::puckPrepFlags),
// and includes WEB_JS_ESCAPE_HTML.

inline constexpr const char* WEB_CSS_COMPARISON_TEXT = R"CSS(
        .section { font-size: 0.75rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--text-secondary); margin: 1.1rem 0 0.3rem; }
        .section:first-child { margin-top: 0; }
        .pill { font-size: 0.75rem; padding: 0 0.45rem; border-radius: 999px; margin-left: 0.4rem; white-space: nowrap; }
        .pill.up { color: #ffaa00; background: rgba(255,170,0,0.16); }
        .pill.down { color: #4e85f4; background: rgba(78,133,244,0.16); }
        .note { color: var(--text-secondary); font-size: 0.8125rem; line-height: 1.6; }
        .quote { color: var(--text-secondary); font-style: italic; font-size: 0.875rem; margin-top: 0.5rem; }
        .more-btn { display: block; margin: 0.75rem auto 0; padding: 0.4rem 1rem; border-radius: 8px; border: 1px solid var(--border);
                    background: var(--surface-hover); color: var(--text); cursor: pointer; font-family: inherit; }
        .diff { font-size: 0.8125rem; color: var(--text-secondary); margin: 0.3rem 0; }
)CSS";

inline constexpr const char* WEB_JS_COMPARISON_TEXT = R"JS(
        function txt(id, fb) { var e = texts[id]; return e ? e.label : (fb !== undefined ? fb : id); }
        var DASH = "—";
        function unitLabel(u) {
            return { s: "s", g: "g", bar: "bar", mlPerSec: "mL/s", gPerSec: txt("unit.gPerSec"), celsius: "°C",
                     celsiusDelta: "°C", percent: "%" }[u] || "";
        }
        function signed(v, d) { return (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(v).toFixed(d); }
        function metricText(row, v) {
            if (v === null || v === undefined) return DASH;
            var s = v.toFixed(row.decimals);
            return row.key === "ratio" ? "1:" + s : s;
        }
        function puckLabels(canon) {
            var flags = (canon || "").split(",");
            return puckFlags.filter(function(f) { return flags.indexOf(f.key) >= 0; }).map(function(f) { return f.label; });
        }
        function inputText(row, cell) {
            var has = cell.value !== null && cell.value !== undefined;
            if (row.key === "temperatureOverrideC" && !has) return txt("input.profileTemp");
            if (row.key === "puckPrep") return puckLabels(cell.text).join(" · ") || DASH;
            if (!has || row.unit === "") return cell.text || DASH;
            if (row.unit === "g") return cell.value.toFixed(1) + " g";
            if (row.unit === "rpm") return Math.round(cell.value) + " " + txt("unit.rpm");
            if (row.unit === "celsius") return cell.value.toFixed(1) + " °C";
            return cell.text || String(cell.value);
        }
        function inputDelta(row, d) {
            if (d === null || d === undefined) return "";
            var t = signed(row.unit === "rpm" ? Math.round(d) : d, row.unit === "rpm" ? 0 : row.key === "grinderSetting" ? 2 : 1);
            return t.indexOf(".") >= 0 ? t.replace(/0+$/, "").replace(/\.$/, "") : t;
        }
        // A Δ as a direction, never a verdict: up and down only.
        function pill(delta, text) {
            if (!text) return "";
            return "<span class='pill " + (delta > 0 ? "up" : "down") + "'>" + (delta > 0 ? "▲ " : "▼ ") + text.replace(/^[+−]/, "") + "</span>";
        }
        function plain(key, v) {
            if (typeof v === "number") return key === "rpm" ? String(Math.round(v)) : key === "temperatureOverrideC" ? v.toFixed(1) + " °C" : v.toFixed(1);
            return String(v);
        }
        function ratingText(s) {
            var p = [];
            if (s.rating0to100 !== null && s.rating0to100 !== undefined) p.push(s.rating0to100 + "%");
            if (s.tasteBalance) p.push(txt("taste." + s.tasteBalance, s.tasteBalance));
            if (s.tasteBody) p.push(txt("taste." + s.tasteBody, s.tasteBody));
            return p.length ? p.join(" · ") : DASH;
        }
        function summaryFor(c) {
            var out = [];
            (c.summary || []).forEach(function(f) {
                if (f.kind === "sameSetup") out.push(txt("phrase.sameSetup"));
                else if (f.kind === "noNotable") out.push(txt("phrase.noNotable"));
                else if (f.kind === "input") out.push(txt("input." + f.key, f.key) + " " + plain(f.key, f.from) + " → " + plain(f.key, f.to));
                else if (f.kind === "inputChanged") out.push(txt("phrase.changed").replace("%1", txt("input." + f.key, f.key)));
                else if (f.kind === "moreInputs") out.push(txt("phrase.moreInputs").replace("%1", f.count));
                else if (f.kind === "metric") {
                    var amount = Math.abs(f.delta).toFixed(f.decimals) + (unitLabel(f.unit) ? " " + unitLabel(f.unit) : "");
                    out.push(txt(f.phrase).replace("%1", txt("metric." + f.key, f.key)).replace("%2", amount));
                } else if (f.kind === "stopped") out.push(txt("stopped." + f.stoppedBy, ""));
                else if (f.kind === "badgeAppeared") out.push(txt("phrase.badgeAppeared").replace("%1", txt("badge." + f.badge, f.badge)));
                else if (f.kind === "badgeGone") out.push(txt("phrase.badgeGone").replace("%1", txt("badge." + f.badge, f.badge)));
            });
            out = out.filter(function(t) { return t.length > 0; });
            return out.length > 0 ? out.join("  ·  ") : txt("phrase.noNotable");
        }
        // Profile-diff rows: ProfileDialInText's labels and the decimals C++ chose,
        // as ProfileDialInDiffBlock.qml shows them.
        function dialInLabel(id) { var e = dialInLabels[id]; return e ? e.label : id; }
        function diffRowText(r) {
            var name = dialInLabel(r.kind);
            if (r.frameIndex >= 0) name = (r.frameName || dialInLabel("step").replace("%1", r.frameIndex + 1)) + " · " + name;
            function val(v) {
                var u = { celsius: " °C", celsiusTank: " °C", bar: " bar", mlPerSec: " mL/s", g: " g", ml: " mL" }[r.unit];
                return v.toFixed(r.decimals) + (u !== undefined ? u : r.unit ? " " + r.unit : "");
            }
            return name + " " + (r.numeric ? val(r.oldValue) + " → " + val(r.newValue)
                                           : (r.oldText || DASH) + " → " + (r.newText || DASH));
        }
)JS";
