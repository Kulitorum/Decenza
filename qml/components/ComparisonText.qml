pragma Singleton
import QtQuick
import Decenza

// Words and numbers for ShotComparison::compare() output, for every page that shows
// it: the comparison and the shot page. The wording is the ShotComparisonText table,
// the same one the web pages read, translated here. Functions take the rows and
// cells they format, so a page's own comparison map and the comparison model's both
// work.
QtObject {
    readonly property var texts: ComparisonService.texts

    function txt(id, fallback) {
        const e = texts[id]
        return e ? TranslationManager.translate(e.key, e.label) : (fallback !== undefined ? fallback : id)
    }
    function inputLabel(key) { return txt("input." + key, key) }
    function metricLabel(key) { return txt("metric." + key, key) }
    function badgeLabel(key) { return txt("badge." + key, key) }
    function stopText(by) { return by ? txt("stop." + by, "—") : "—" }

    function inputText(row, cell) {
        const hasValue = cell.value !== null && cell.value !== undefined
        // No override means the profile's own temperature, not a missing value.
        if (row.key === "temperatureOverrideC" && !hasValue)
            return txt("input.profileTemp")
        if (row.key === "puckPrep")
            return PuckPrepLabels.labelsFor(cell.text).join(" · ") || "—"
        if (!hasValue || row.unit === "")
            return cell.text || "—"
        switch (row.unit) {
        case "g":       return cell.value.toFixed(1) + " " + TranslationManager.translate("common.unit.grams", "g")
        case "rpm":     return Math.round(cell.value) + " " + txt("unit.rpm")
        case "celsius": return Theme.formatTemperature(cell.value, 1)
        }
        return cell.text || String(cell.value)
    }

    function inputDelta(row, delta) {
        if (delta === null || delta === undefined) return ""
        const d = row.unit === "rpm" ? Math.round(delta) : row.unit === "celsius"
            ? Theme.cDeltaToDisplay(delta) : delta
        const decimals = row.unit === "rpm" ? 0 : row.key === "grinderSetting" ? 2 : 1
        // Trim zeros only after a decimal point: "+0.50" reads "+0.5", "+100" stays.
        const t = signed(d, decimals)
        return t.indexOf(".") >= 0 ? t.replace(/0+$/, "").replace(/\.$/, "") : t
    }

    function unitLabel(unit) {
        switch (unit) {
        case "s":            return TranslationManager.translate("common.unit.seconds", "s")
        case "g":            return TranslationManager.translate("common.unit.grams", "g")
        case "ml":           return TranslationManager.translate("common.unit.milliliters", "mL")
        case "bar":          return TranslationManager.translate("espresso.unit.bar", "bar")
        case "mlPerSec":     return TranslationManager.translate("espresso.unit.flowRate", "mL/s")
        case "gPerSec":      return txt("unit.gPerSec")
        case "celsius":
        case "celsiusDelta": return Theme.tempUnitSuffix()
        case "percent":      return "%"
        }
        return ""
    }

    function metricText(row, v) {
        if (v === null || v === undefined) return "—"
        const shown = row.unit === "celsius" ? Theme.cToDisplay(v)
            : row.unit === "celsiusDelta" ? Theme.cDeltaToDisplay(v) : v
        const s = shown.toFixed(row.decimals)
        return row.key === "ratio" ? "1:" + s : s
    }

    function signed(v, decimals) {
        return (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(v).toFixed(decimals)
    }

    function metricDelta(row, d) {
        if (d === null || d === undefined) return ""
        const shown = row.unit === "celsius" || row.unit === "celsiusDelta" ? Theme.cDeltaToDisplay(d) : d
        return signed(shown, row.decimals)
    }

    function waterForShot(shot) {
        const result = shot.hotWaterResult || ({})
        if (result.status) return Object.assign({ visible: true }, result)
        if (!shot.hotWaterJson) return ({ visible: false })
        try {
            const spec = JSON.parse(shot.hotWaterJson)
            if (!spec.hasWater) return ({ visible: false })
            const water = { visible: true }
            if (typeof spec.temperatureC === "number" && isFinite(spec.temperatureC)
                    && spec.temperatureC >= 0 && spec.temperatureC <= 100)
                water.temperatureC = spec.temperatureC
            return water
        } catch (e) { return ({ visible: false }) }
    }

    function waterAmountText(water) {
        if (water.status === "notDispensed") return txt("water.notDispensed")
        let text = txt("water.notRecorded")
        if (water.weightG !== undefined)
            text = water.weightG.toFixed(1) + " " + unitLabel("g") + " · " + txt("water.scale")
        else if (water.volumeMl !== undefined)
            text = water.volumeMl.toFixed(1) + " " + unitLabel("ml") + " · " + txt("water.flowEstimate")
        if (water.status === "interrupted") text += " · " + txt("water.interrupted")
        return text
    }

    function waterTemperatureText(water) {
        if (water.status === "notDispensed") return ""
        return water.temperatureC === undefined ? txt("water.tempUnavailable")
            : inputLabel("waterTemperatureC") + ": " + Theme.formatTemperature(water.temperatureC, 1)
    }

    function metricCellText(row, cell) {
        if (row.key !== "waterWeightG" && row.key !== "waterVolumeMl")
            return metricText(row, cell.value)
        const water = cell.water || ({})
        if (water.status === "notDispensed") return txt("water.notDispensed")
        let text = metricText(row, cell.value)
        if (cell.value !== null && cell.value !== undefined)
            text += " · " + txt("water." + water.amountSource)
        if (water.status === "interrupted") text += " · " + txt("water.interrupted")
        return text
    }

    function ratingText(s) {
        let parts = []
        if (s.rating0to100 !== null && s.rating0to100 !== undefined) parts.push(s.rating0to100 + "%")
        if (s.tasteBalance) parts.push(txt("taste." + s.tasteBalance, s.tasteBalance))
        if (s.tasteBody) parts.push(txt("taste." + s.tasteBody, s.tasteBody))
        return parts.length > 0 ? parts.join(" · ") : "—"
    }

    // One compared shot's summary, from the facts C++ chose and ordered; this only
    // words them and formats the numbers.
    function summaryFor(c) {
        let out = []
        for (const f of (c.summary || [])) {
            switch (f.kind) {
            case "sameSetup":
                out.push(txt("phrase.sameSetup"))
                break
            case "noNotable":
                out.push(txt("phrase.noNotable"))
                break
            case "input":
                out.push(inputLabel(f.key) + " " + _plain(f.key, f.from) + " → " + _plain(f.key, f.to))
                break
            case "inputChanged":
                out.push(txt("phrase.changed").arg(inputLabel(f.key)))
                break
            case "moreInputs":
                out.push(txt("phrase.moreInputs").arg(f.count))
                break
            case "metric": {
                const amount = metricDelta(f, Math.abs(f.delta)).replace(/^\+/, "")
                    + (unitLabel(f.unit) ? " " + unitLabel(f.unit) : "")
                out.push(txt(f.phrase).arg(metricLabel(f.key)).arg(amount))
                break
            }
            case "stopped":
                out.push(txt("stopped." + f.stoppedBy, ""))
                break
            case "badgeAppeared":
                out.push(txt("phrase.badgeAppeared").arg(badgeLabel(f.badge)))
                break
            case "badgeGone":
                out.push(txt("phrase.badgeGone").arg(badgeLabel(f.badge)))
                break
            }
        }
        out = out.filter(function(t) { return t.length > 0 })
        return out.length > 0 ? out.join("  ·  ") : txt("phrase.noNotable")
    }

    function _plain(key, v) {
        if (typeof v === "number")
            return key === "rpm" ? String(Math.round(v)) : (key === "temperatureOverrideC" || key === "waterTemperatureC")
                ? Theme.formatTemperature(v, 1) : v.toFixed(1)
        return String(v)
    }
}
