import Foundation

// Mirrors src/widget/widgetsharedkeys.h. Schema: docs/CLAUDE_MD/WIDGET_SNAPSHOT.md
enum WidgetKeys {
    static let appGroupId = "group.io.github.kulitorum.decenza"
    static let snapshotKey = "machineStatusSnapshot"
    // Snapshot older than this (while still "connected") is shown as stale.
    static let freshWindowMinutes = 3
}

// Authoritative short widget labels for the raw MachineState phase strings.
// Mirrors the table in docs/CLAUDE_MD/WIDGET_SNAPSHOT.md and the Android
// MachineStatusWidgetProvider.PHASE_LABELS — keep all three in sync. Short
// so they fit the smallest widget size without truncation.
enum WidgetPhase {
    static let labels: [String: String] = [
        "Disconnected": "Disconnected",
        "Sleep": "Sleep",
        "Idle": "Idle",
        "Heating": "Heating",
        "Ready": "Ready",
        "EspressoPreheating": "Preheating",
        "Preinfusion": "Preinfusion",
        "Pouring": "Pouring",
        "Ending": "Finishing",
        "Steaming": "Steaming",
        "HotWater": "Hot Water",
        "Flushing": "Flushing",
        "Refill": "Refill",
        "Descaling": "Descaling",
        "Cleaning": "Cleaning",
    ]
    static func shortLabel(_ phase: String) -> String {
        if phase.isEmpty { return "Decenza" }
        return labels[phase] ?? phase
    }
}

struct LastShot {
    let yieldG: Double
    let durationSec: Double
    let qualityBadge: String?
}

struct MachineStatusSnapshot {
    let connected: Bool
    let phase: String
    let temperatureC: Double?
    let targetTemperatureC: Double?
    let steamTemperatureC: Double?
    let lastShot: LastShot?
    let capturedAt: Date?

    /// Reads + parses the App Group snapshot. Returns nil when absent or
    /// unparsable — callers must render the disconnected state in that case.
    static func load() -> MachineStatusSnapshot? {
        guard
            let defaults = UserDefaults(suiteName: WidgetKeys.appGroupId),
            let json = defaults.string(forKey: WidgetKeys.snapshotKey),
            let data = json.data(using: .utf8),
            let obj = try? JSONSerialization.jsonObject(with: data)
                as? [String: Any]
        else { return nil }

        func num(_ k: String) -> Double? { (obj[k] as? NSNumber)?.doubleValue }

        var shot: LastShot? = nil
        if let s = obj["lastShot"] as? [String: Any],
           let y = (s["yieldG"] as? NSNumber)?.doubleValue,
           let d = (s["durationSec"] as? NSNumber)?.doubleValue {
            let badge = s["qualityBadge"] as? String
            shot = LastShot(yieldG: y, durationSec: d,
                            qualityBadge: (badge?.isEmpty == false) ? badge : nil)
        }

        var captured: Date? = nil
        if let ts = obj["capturedAt"] as? String {
            let f = ISO8601DateFormatter()
            f.formatOptions = [.withInternetDateTime]
            captured = f.date(from: ts)
        }

        return MachineStatusSnapshot(
            connected: (obj["connected"] as? Bool) ?? false,
            phase: (obj["phase"] as? String) ?? "",
            temperatureC: num("temperatureC"),
            targetTemperatureC: num("targetTemperatureC"),
            steamTemperatureC: num("steamTemperatureC"),
            lastShot: shot,
            capturedAt: captured)
    }
}

// MARK: - Display derivation (mirrors the Android provider rules)

struct WidgetDisplay {
    let phaseLabel: String
    let tempLine: String
    let lastShotLine: String
    let statusLine: String
    let disconnected: Bool

    static func disconnectedState() -> WidgetDisplay {
        WidgetDisplay(phaseLabel: "Disconnected", tempLine: "",
                      lastShotLine: "", statusLine: "Tap to open",
                      disconnected: true)
    }

    static func from(_ snap: MachineStatusSnapshot?) -> WidgetDisplay {
        guard let s = snap, s.connected else {
            return disconnectedState()
        }

        let phase = s.phase
        var status = ""
        if let at = s.capturedAt {
            let min = Int(Date().timeIntervalSince(at) / 60.0)
            if min >= WidgetKeys.freshWindowMinutes {
                status = "updated \(min) min ago"
            }
        } else {
            status = "updated a while ago"
        }

        return WidgetDisplay(
            phaseLabel: WidgetPhase.shortLabel(phase),
            tempLine: tempLine(phase: phase,
                               t: s.temperatureC,
                               target: s.targetTemperatureC,
                               steam: s.steamTemperatureC),
            lastShotLine: lastShotLine(s.lastShot),
            statusLine: status,
            disconnected: false)
    }

    private static func tempLine(phase: String, t: Double?,
                                 target: Double?, steam: Double?) -> String {
        if phase == "Steaming", let st = steam {
            return "\(Int(st.rounded())) °C steam"
        }
        if phase == "Heating", let c = t, let g = target {
            return "Heating \(Int(c.rounded())) → \(Int(g.rounded())) °C"
        }
        guard let c = t else { return "" }
        if phase == "Ready" { return "Ready · \(Int(c.rounded())) °C" }
        return "\(Int(c.rounded())) °C"
    }

    private static func lastShotLine(_ shot: LastShot?) -> String {
        guard let s = shot else { return "" }
        var line = String(format: "Last: %.1fg · %ds",
                          s.yieldG, Int(s.durationSec.rounded()))
        if let b = s.qualityBadge { line += " · \(b)" }
        return line
    }
}
