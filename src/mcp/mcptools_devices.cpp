#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../ble/blemanager.h"
#include "../ble/de1device.h"
#include "../ble/scaledevice.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QMetaObject>
#include <QDateTime>

void registerDeviceTools(McpToolRegistry* registry, BLEManager* bleManager, DE1Device* device)
{
    // devices_list
    registry->registerTool(
        "devices_list",
        "List discovered BLE devices (DE1 machines and scales found during scanning)",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            QVariantList devices = bleManager->discoveredDevices();
            QJsonArray devArr;
            for (const QVariant& v : devices) {
                QVariantMap dm = v.toMap();
                QJsonObject dev;
                dev["address"] = dm["address"].toString();
                dev["name"] = dm["name"].toString();
                dev["type"] = dm["type"].toString();
                dev["rssi"] = dm["rssi"].toInt();
                devArr.append(dev);
            }
            result["devices"] = devArr;
            result["count"] = devArr.size();
            return result;
        },
        "read");

    // devices_scan
    registry->registerTool(
        "devices_scan",
        "Start scanning for BLE devices (DE1 machines and scales). Results appear in devices_list after a few seconds.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            QMetaObject::invokeMethod(bleManager, "startScan", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "BLE scan started. Call devices_list after a few seconds to see results.";
            return result;
        },
        "control");

    // devices_connect_scale
    registry->registerTool(
        "devices_connect_scale",
        "Connect to a scale by its BLE address. Use devices_list to find available scales.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"address", QJsonObject{{"type", "string"}, {"description", "BLE address of the scale to connect"}}}
            }},
            {"required", QJsonArray{"address"}}
        },
        [bleManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            QString address = args["address"].toString();
            if (address.isEmpty()) {
                result["error"] = "address is required";
                return result;
            }
            QMetaObject::invokeMethod(bleManager, "connectToScale",
                Qt::QueuedConnection, Q_ARG(QString, address));
            result["success"] = true;
            result["message"] = "Connecting to scale at " + address;
            return result;
        },
        "control");

    // devices_connection_status
    registry->registerTool(
        "devices_connection_status",
        "Get connection status of the DE1 machine and scale, including the "
        "in-memory (app-run) scale connection-priority dual-HIGH backoff state",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [device, bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (device) {
                result["machineConnected"] = device->isConnected();
                result["machineAddress"] = device->isConnected() ? "connected" : "disconnected";
            }
            result["bleAvailable"] = bleManager != nullptr;

            // Scale connection-priority (dual-HIGH backoff) state. Persisted,
            // epoch-scoped: a same-epoch restart rehydrates it; a deliberate
            // epoch bump, or this MCP reset, re-detects from scratch.
            QJsonObject sp;
            const bool latched = bleManager && bleManager->scaleSkipHighPriority();
            sp["scaleLinkPriority"] = latched ? "balanced" : "high";
            sp["latchedToBalanced"] = latched;
            // "Not suppressed by the latch" — not a guarantee detection is
            // armed right now (that additionally needs a scale connected at
            // HIGH; the latch is the dominant signal).
            sp["detectionActive"] = !latched;
            // Epoch-scoped QSettings persistence: the classification persists
            // across all builds sharing detectionEpoch; a same-epoch restart
            // keeps it latched (no detection window). It re-detects only on a
            // deliberate epoch bump (a release that changed BLE handling /
            // re-classifies everyone) or via devices_reset_scale_priority.
            sp["persisted"] = true;
            sp["persistenceScope"] = "epoch";
            // The active detection epoch (always reported, latched or not).
            sp["detectionEpoch"] = BLEManager::kBleDetectionEpoch;
            if (latched) {
                sp["triggerKind"] = bleManager->scaleSkipHighTriggerKind();
                // Diagnostic ONLY — the versionCode that last classified this
                // device. NOT the rehydrate gate (the epoch is); surfaced so
                // the "classified by build N" field-triage trail survives.
                // ALWAYS emitted when latched: a migrated-legacy / pre-build
                // record has buildCode 0 — exactly the population under triage
                // right after this ships, so absence-of-provenance must be an
                // explicit string, never a silently-missing field (the very
                // "diagnostic black hole" this feature exists to avoid).
                const int byBuild = bleManager->scaleSkipHighBuildCode();
                if (byBuild > 0)
                    sp["classifiedByBuildCode"] = byBuild;
                else
                    sp["classifiedByBuildCode"] =
                        QStringLiteral("unknown (legacy / migrated / pre-buildcode)");
                const QDateTime setT = bleManager->scaleSkipHighSetTime();
                const QDateTime appStart = bleManager->appStartTime();
                if (setT.isValid()) {
                    sp["latchedAtIso8601"] =
                        setT.toOffsetFromUtc(setT.offsetFromUtc()).toString(Qt::ISODate);
                }
                if (setT.isValid() && appStart.isValid()) {
                    const qint64 s = appStart.secsTo(setT);
                    sp["elapsedSinceAppStartSec"] = static_cast<double>(s);
                    sp["elapsedSinceAppStartHuman"] =
                        QStringLiteral("%1 min %2 s after app start")
                            .arg(s / 60).arg(s % 60);
                }
            }
            // Backoff policy mode (observe-mode change) + recent observe
            // events. backoffMode is always reported so the active policy is
            // never hidden; the event list is empty unless observe has fired.
            if (bleManager) {
                sp["backoffMode"] = BLEManager::backoffModeToString(
                    bleManager->backoffMode());
                QJsonArray events;
                const auto recent = bleManager->recentObserveEvents();
                for (const auto& e : recent) {
                    QJsonObject ev;
                    ev["kind"] = e.kind;                 // wouldBackoff | recovered
                    ev["triggerKind"] = e.triggerKind;   // human-readable
                    if (e.time.isValid()) {
                        ev["timeIso8601"] = e.time
                            .toOffsetFromUtc(e.time.offsetFromUtc())
                            .toString(Qt::ISODate);
                    }
                    // Unit-suffixed per kind (CLAUDE.md MCP conventions):
                    // a stall that would have backed off vs. a recovered gap.
                    if (e.kind == QLatin1String("recovered"))
                        ev["gapSec"] = e.durationSec;
                    else
                        ev["stallSec"] = e.durationSec;
                    events.append(ev);
                }
                sp["recentObserveEvents"] = events;  // most-recent-first
            }

            result["scaleConnectionPriority"] = sp;
            return result;
        },
        "read");

    // devices_reset_scale_priority — clears the dual-HIGH backoff latch, both
    // in-memory AND the persisted (epoch-scoped) record (the only operator
    // path; there is intentionally no UI). Takes effect on the next scale
    // (re)connect's detection pass: eventually-consistent, no forced teardown
    // of a live connection. Durable: a restart will NOT rehydrate it (the
    // clear wipes the epoch key too — the device re-detects from scratch).
    registry->registerTool(
        "devices_reset_scale_priority",
        "Reset (clear) the scale connection-priority dual-HIGH backoff latch — "
        "both the in-memory latch and the persisted (epoch-scoped) record. The "
        "reset is durable: an app restart will NOT re-apply it. After "
        "reset, the next scale (re)connect requests HIGH and re-enters "
        "detection from scratch (as if the device were seen for the first "
        "time). Eventually-consistent: it does NOT tear down a "
        "currently-connected scale. Use to recover from a false-positive or to "
        "re-test a device.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"},
                {"description", "Set to true after the user confirms this action in chat"}}}
        }}},
        [bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            const bool wasLatched = bleManager->scaleSkipHighPriority();
            // Marshalled to the BLEManager thread; this handler cannot confirm
            // the clear executed, so report it as accepted/queued rather than
            // asserting a verified-complete state change.
            QMetaObject::invokeMethod(bleManager, [bleManager]() {
                bleManager->clearScaleSkipHighPriority();
            }, Qt::QueuedConnection);
            result["accepted"] = true;
            result["wasLatched"] = wasLatched;
            result["appliesOnNextReconnect"] = true;
            result["message"] = wasLatched
                ? "Scale connection-priority latch clear was queued. It applies "
                  "on the next scale (re)connect (eventually-consistent — the "
                  "current connection is NOT torn down; this response does not "
                  "assert the clear has executed yet)."
                : "Scale connection-priority latch was already clear; clear "
                  "still queued as a no-op, nothing will change.";
            return result;
        },
        "control");

    // devices_set_scale_priority_mode — sets the persistent backoff policy
    // mode (observe-mode change). Follows devices_reset_scale_priority's
    // eventually-consistent, queued-to-the-BLEManager-thread contract (this
    // handler reports accepted/queued, NOT verified-applied — the persist
    // runs after it returns). It additionally enforces the `confirmed` gate
    // for real (the reset tool advertises `confirmed` but does not read it);
    // do not assume confirmation parity with that tool.
    registry->registerTool(
        "devices_set_scale_priority_mode",
        "Set the persistent scale connection-priority backoff policy mode. "
        "'enforce' (default) is the normal dual-HIGH backoff: on a detected "
        "stall/fault cluster it latches the scale link to BALANCED and "
        "reconnects. 'observe' is detect-and-log-only: detection still runs "
        "but takes NO action — the link is forced to HIGH (overriding, but "
        "not erasing, any existing BALANCED latch) and would-back-off / "
        "recovery events are logged and surfaced in devices_connection_status, "
        "so the backoff's aggressiveness can be evaluated on a production "
        "build. The mode persists across app restarts AND build upgrades "
        "until explicitly changed (it is not scoped at all, unlike the latch "
        "which is epoch-scoped). "
        "Eventually-consistent: the change is queued onto the BLE-manager "
        "thread (this response does NOT assert the persist has executed yet), "
        "and the HIGH-forcing additionally only applies on the next scale "
        "(re)connect; the current connection is not torn down.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"mode", QJsonObject{{"type", "string"},
                {"enum", QJsonArray{"enforce", "observe"}},
                {"description", "enforce = normal backoff; observe = "
                                "detect-and-log-only, link forced HIGH"}}},
            {"confirmed", QJsonObject{{"type", "boolean"},
                {"description", "Set to true after the user confirms this action in chat"}}}
        }}, {"required", QJsonArray{"mode"}}},
        [bleManager](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            const QString mode = args.value("mode").toString();
            if (mode != QLatin1String("enforce") &&
                mode != QLatin1String("observe")) {
                result["error"] = "Invalid mode: must be \"enforce\" or "
                                  "\"observe\" (mode unchanged)";
                return result;
            }
            // NOTE: do NOT check `confirmed` here. McpServer strips the
            // `confirmed` key before invoking any handler (mcpserver.cpp:
            // "Strip the confirmed key before passing to tool handler"), and
            // the chat-confirmation handshake is owned entirely by the server
            // via needsChatConfirmation(). A handler-side `confirmed` check is
            // unreachable-true and makes the tool permanently uninvokable
            // (this was the shipped #1219 bug). Confirmation for this tool is
            // enforced by listing it in McpServer::needsChatConfirmation().
            const QString prev = BLEManager::backoffModeToString(
                bleManager->backoffMode());
            const auto target = BLEManager::backoffModeFromString(mode);
            // Marshalled to the BLEManager thread (like the reset tool); this
            // handler reports accepted/queued, not verified-applied.
            QMetaObject::invokeMethod(bleManager, [bleManager, target]() {
                bleManager->setBackoffMode(target);
            }, Qt::QueuedConnection);
            result["accepted"] = true;
            result["previousMode"] = prev;
            result["mode"] = mode;
            result["appliesOnNextReconnect"] = true;
            result["message"] = QStringLiteral(
                "Backoff policy mode change to \"%1\" (was \"%2\") was queued. "
                "It is applied + persisted on the BLE-manager thread (this "
                "response does not assert the write has completed); once "
                "written it survives restarts/build upgrades. %3 "
                "Eventually-consistent: this does NOT tear down the current "
                "scale connection — force a scale reconnect (toggle the scale "
                "or restart the app) for it to take effect.")
                .arg(mode, prev,
                     mode == QLatin1String("observe")
                       ? QStringLiteral("On the next scale reconnect the link "
                           "is forced HIGH and detection logs only (no latch, "
                           "no disconnect).")
                       : QStringLiteral("On the next scale reconnect the "
                           "normal dual-HIGH backoff resumes and any persisted "
                           "latch is honoured again."));
            return result;
        },
        "control");

    // devices_connect_de1
    registry->registerTool(
        "devices_connect_de1",
        "Connect to a DE1 machine by its BLE address. Use devices_list to find available machines.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"address", QJsonObject{{"type", "string"}, {"description", "BLE address of the DE1 to connect"}}}
            }},
            {"required", QJsonArray{"address"}}
        },
        [device](const QJsonObject& args) -> QJsonObject {
            QJsonObject result;
            if (!device) {
                result["error"] = "DE1 device not available";
                return result;
            }
            QString address = args["address"].toString();
            if (address.isEmpty()) {
                result["error"] = "address is required";
                return result;
            }
            if (device->isConnected()) {
                result["message"] = "Already connected to a DE1 machine";
                return result;
            }
            QMetaObject::invokeMethod(device, "connectToDevice",
                Qt::QueuedConnection, Q_ARG(QString, address));
            result["success"] = true;
            result["message"] = "Connecting to DE1 at " + address;
            return result;
        },
        "control");

    // devices_disconnect_scale
    registry->registerTool(
        "devices_disconnect_scale",
        "Disconnect and forget the currently connected BLE scale. "
        "The scale will need to be re-selected via devices_connect_scale.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{
            {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
        }}},
        [bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            bleManager->clearSavedScale();
            result["success"] = true;
            result["message"] = "Scale disconnected and forgotten";
            return result;
        },
        "control");

    // devices_request_scale_debug — ask the active scale to emit a full-state
    // debug frame on the wire. Logged verbatim by handleDebugFrame; useful for
    // on-demand triage when the AI/MCP layer wants to see firmware-internal
    // health (SoC temp, ADC stalls, recovery counter) without waiting for an
    // app-suspend or screensaver event. No-op on non-Decent-WiFi scales (the
    // base-class virtual is empty); other drivers don't have a debug snapshot
    // to request.
    registry->registerTool(
        "devices_request_scale_debug",
        "Ask the active scale to emit a debug-state frame. Currently only "
        "the Half Decent Scale over WiFi responds — the response is written "
        "to the scale log (visible in debug_get_log) and carries SoC "
        "temperature, weight-ADC stall counters, and the ADC-recovery "
        "attempt counter. Other scales return success with a no-op note.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [bleManager](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!bleManager) {
                result["error"] = "BLE manager not available";
                return result;
            }
            ScaleDevice* scale = bleManager->scaleDevice();
            if (!scale || !scale->isConnected()) {
                result["error"] = "No scale connected";
                return result;
            }
            QMetaObject::invokeMethod(scale, "requestDebugSnapshot", Qt::QueuedConnection);
            result["success"] = true;
            result["message"] = "Debug snapshot requested — check the scale log for the response frame";
            return result;
        },
        "read");
}
