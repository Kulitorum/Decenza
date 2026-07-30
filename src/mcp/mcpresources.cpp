#include "mcpserver.h"
#include "mcpresourceregistry.h"
#include "mcptoolregistry.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../controllers/profilemanager.h"
#include "../history/shothistorystorage.h"
#include "../history/bagid.h"
#include "../core/logtags.h"
#include "../core/memorymonitor.h"
#include "../core/settings.h"
#include "../core/settings_dye.h"
#include "../network/webdebuglogger.h"
#include "mcplogfilter.h"

#include <QMap>
#include <QSet>
#include <algorithm>

#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QMetaObject>
#include <QCoreApplication>

#include "../core/dbutils.h"

void registerMcpResources(McpResourceRegistry* registry, DE1Device* device,
                          MachineState* machineState, ProfileManager* profileManager,
                          ShotHistoryStorage* shotHistory, MemoryMonitor* memoryMonitor,
                          Settings* settings)
{
    // decenza://machine/state
    registry->registerResource(
        "decenza://machine/state",
        "Machine State",
        "Current phase, connection status, and water level",
        "application/json",
        [device, machineState]() -> QJsonObject {
            QJsonObject result;
            if (machineState) {
                result["phase"] = machineState->phaseString();
                result["isHeating"] = machineState->isHeating();
                result["isReady"] = machineState->isReady();
                result["isFlowing"] = machineState->isFlowing();
            }
            if (device) {
                result["connected"] = device->isConnected();
                result["waterLevelMl"] = device->waterLevelMl();
                result["waterLevelMm"] = device->waterLevelMm();
            }
            return result;
        });

    // decenza://machine/telemetry
    registry->registerResource(
        "decenza://machine/telemetry",
        "Machine Telemetry",
        "Live pressure, flow, temperature, and weight readings",
        "application/json",
        [device, machineState]() -> QJsonObject {
            QJsonObject result;
            if (device) {
                result["pressureBar"] = device->pressure();
                result["flowMlPerSec"] = device->flow();
                result["temperatureC"] = device->temperature();
                result["goalPressureBar"] = device->goalPressure();
                result["goalFlowMlPerSec"] = device->goalFlow();
                result["goalTemperatureC"] = device->goalTemperature();
            }
            if (machineState) {
                result["scaleWeightG"] = machineState->scaleWeight();
                result["scaleFlowRateMlPerSec"] = machineState->scaleFlowRate();
                result["shotTimeSec"] = machineState->shotTime();
            }
            return result;
        });

    // decenza://profiles/active
    registry->registerResource(
        "decenza://profiles/active",
        "Active Profile",
        "Currently loaded profile name and settings",
        "application/json",
        [profileManager]() -> QJsonObject {
            QJsonObject result;
            if (profileManager) {
                result["filename"] = profileManager->baseProfileName();
                result["title"] = profileManager->currentProfileName();
                result["targetWeightG"] = profileManager->profileTargetWeight();
                result["targetTemperatureC"] = profileManager->profileTargetTemperature();
            }
            return result;
        });

    // decenza://shots/recent
    registry->registerAsyncResource(
        "decenza://shots/recent",
        "Recent Shots",
        "Last 10 shots with summary data",
        "application/json",
        [shotHistory](std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"shots", QJsonArray()}, {"count", 0}});
                return;
            }

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, respond]() {
                QJsonObject result;
                QJsonArray shots;

                withTempDb(dbPath, "mcp_res_recent", [&](QSqlDatabase& db) {
                    QSqlQuery query(db);
                    if (query.exec("SELECT id, timestamp, profile_name, dose_weight, final_weight, "
                                   "duration_seconds, enjoyment, "
                                   "bean_brand, bean_type "
                                   "FROM shots ORDER BY timestamp DESC LIMIT 10")) {
                        while (query.next()) {
                            QJsonObject shot;
                            shot["id"] = query.value("id").toLongLong();
                            auto dt = QDateTime::fromSecsSinceEpoch(query.value("timestamp").toLongLong());
                            shot["timestamp"] = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
                            shot["profileName"] = query.value("profile_name").toString();
                            shot["doseG"] = query.value("dose_weight").toDouble();
                            shot["yieldG"] = query.value("final_weight").toDouble();
                            shot["durationSec"] = query.value("duration_seconds").toDouble();
                            const int enjoyment = query.value("enjoyment").toInt();
                            shot["enjoyment0to100"] = enjoyment > 0 ? QJsonValue(enjoyment) : QJsonValue(QJsonValue::Null);
                            shot["beanBrand"] = query.value("bean_brand").toString();
                            shot["beanType"] = query.value("bean_type").toString();
                            shots.append(shot);
                        }
                    }
                });

                result["shots"] = shots;
                result["count"] = shots.size();

                QMetaObject::invokeMethod(qApp, [respond, result]() {
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        });

    // decenza://dialing/current_context
    // Compact snapshot of the active bean, grinder, last 3 shots, active profile, and machine
    // phase — intended for any MCP client (Claude Desktop, Claude mobile, Claude Code, etc.) to
    // read at session start and re-read when the user pulls a new shot.
    registry->registerAsyncResource(
        "decenza://dialing/current_context",
        "Current Dialing Context",
        "Live bean/grinder/profile/machine snapshot plus the last 3 shots, for AI dialing sessions",
        "application/json",
        [settings, profileManager, machineState, shotHistory](std::function<void(QJsonObject)> respond) {
            QJsonObject bean;
            QJsonObject grinder;
            if (settings) {
                // The dye fields are read-throughs of the active coffee bag
                // (bean-bag-inventory), so this block describes the active
                // bag without needing a separate lookup.
                bean["brand"] = settings->dye()->dyeBeanBrand();
                bean["type"] = settings->dye()->dyeBeanType();
                if (bagIdIsSet(settings->dye()->activeBagId()))
                    bean["bagId"] = settings->dye()->activeBagId();
                // Days out of the freezer, not a raw date: the AI shouldn't have
                // to do date math (and may not reliably know today's date). This
                // is the freshness clock the user already sees (the "Def %1d"
                // line in BeanSummary). Omitted when there's no defrost date, it's
                // unparseable, or it's in the future.
                const QString defrostDate = settings->dye()->activeBagDefrostDate();
                if (!defrostDate.isEmpty()) {
                    const QDate def = QDate::fromString(defrostDate.left(10), Qt::ISODate);
                    if (def.isValid()) {
                        const qint64 daysOut = def.daysTo(QDate::currentDate());
                        if (daysOut >= 0)
                            bean["daysOutOfFreezer"] = daysOut;
                    }
                }
                // Normalize roast date to ISO 8601 if parseable, otherwise pass through as user text
                QString rawDate = settings->dye()->dyeRoastDate();
                QDate parsed = QDate::fromString(rawDate, Qt::ISODate);
                if (!parsed.isValid()) parsed = QDate::fromString(rawDate, "yyyy-MM-dd");
                if (!parsed.isValid()) parsed = QDate::fromString(rawDate, "MM/dd/yyyy");
                if (!parsed.isValid()) parsed = QDate::fromString(rawDate, "dd/MM/yyyy");
                bean["roastDate"] = parsed.isValid() ? parsed.toString(Qt::ISODate) : rawDate;
                bean["doseWeightG"] = settings->dye()->dyeBeanWeight();
                grinder["brand"] = settings->dye()->dyeGrinderBrand();
                grinder["model"] = settings->dye()->dyeGrinderModel();
                grinder["setting"] = settings->dye()->dyeGrinderSetting();
                // Equipment package (add-equipment-packages): the active package
                // and its rpm dial-in. rpm is only meaningful when adjustable.
                grinder["packageId"] = settings->dye()->activeEquipmentId();
                grinder["rpmAdjustable"] = settings->dye()->grinderRpmCapable(
                    settings->dye()->dyeGrinderBrand(), settings->dye()->dyeGrinderModel());
                if (settings->dye()->dyeGrinderRpm() > 0)
                    grinder["rpm"] = settings->dye()->dyeGrinderRpm();
            }

            QJsonObject activeProfile;
            if (profileManager) {
                activeProfile["name"] = profileManager->currentProfileName();
                activeProfile["editorType"] = profileManager->currentEditorType();
            }

            QString machinePhase = machineState ? machineState->phaseString() : QString();

            if (!shotHistory || !shotHistory->isReady()) {
                QJsonObject result;
                result["bean"] = bean;
                result["grinder"] = grinder;
                result["activeProfile"] = activeProfile;
                result["machinePhase"] = machinePhase;
                result["recentShots"] = QJsonArray();
                respond(result);
                return;
            }

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, bean, grinder, activeProfile, machinePhase, respond]() {
                QJsonObject result;
                QJsonArray shots;

                withTempDb(dbPath, "mcp_res_dialing_ctx", [&](QSqlDatabase& db) {
                    QSqlQuery query(db);
                    if (query.exec("SELECT id, timestamp, profile_name, dose_weight, final_weight, "
                                   "duration_seconds, drink_tds, drink_ey "
                                   "FROM shots ORDER BY timestamp DESC LIMIT 3")) {
                        while (query.next()) {
                            QJsonObject shot;
                            shot["id"] = query.value("id").toLongLong();
                            auto dt = QDateTime::fromSecsSinceEpoch(query.value("timestamp").toLongLong());
                            shot["timestamp"] = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
                            shot["profileName"] = query.value("profile_name").toString();
                            shot["doseG"] = query.value("dose_weight").toDouble();
                            shot["yieldG"] = query.value("final_weight").toDouble();
                            shot["durationSec"] = query.value("duration_seconds").toDouble();
                            const double tds = query.value("drink_tds").toDouble();
                            const double ey = query.value("drink_ey").toDouble();
                            shot["tdsPercent"] = tds > 0.0 ? QJsonValue(tds) : QJsonValue(QJsonValue::Null);
                            shot["extractionYieldPercent"] = ey > 0.0 ? QJsonValue(ey) : QJsonValue(QJsonValue::Null);
                            shots.append(shot);
                        }
                    }
                });

                result["bean"] = bean;
                result["grinder"] = grinder;
                result["activeProfile"] = activeProfile;
                result["machinePhase"] = machinePhase;
                result["recentShots"] = shots;

                QMetaObject::invokeMethod(qApp, [respond, result]() {
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        });

    // decenza://profiles/list
    registry->registerResource(
        "decenza://profiles/list",
        "All Profiles",
        "List of all available profiles",
        "application/json",
        [profileManager]() -> QJsonObject {
            QJsonObject result;
            if (!profileManager) return result;

            QJsonArray profiles;
            for (const QVariant& v : profileManager->availableProfiles()) {
                QVariantMap pm = v.toMap();
                QJsonObject p;
                p["filename"] = pm["name"].toString();
                p["title"] = pm["title"].toString();
                profiles.append(p);
            }
            result["profiles"] = profiles;
            result["count"] = profiles.size();
            return result;
        });

    // decenza://debug/log
    registry->registerResource(
        "decenza://debug/log",
        "Debug Log",
        "Full persisted debug log with memory snapshot (survives crashes)",
        "application/json",
        [memoryMonitor]() -> QJsonObject {
            QJsonObject result;
            auto* logger = WebDebugLogger::instance();
            QString log = logger ? logger->getPersistedLog() : QString();
            if (memoryMonitor)
                log += memoryMonitor->toSummaryString();
            result["log"] = log;
            result["path"] = logger ? logger->logFilePath() : QString();
            result["lineCount"] = log.count('\n');
            return result;
        });

    // decenza://debug/memory
    registry->registerResource(
        "decenza://debug/memory",
        "Memory Stats",
        "Current RSS, peak RSS, QObject count, and recent memory samples",
        "application/json",
        [memoryMonitor]() -> QJsonObject {
            if (!memoryMonitor) return QJsonObject();
            return memoryMonitor->toJson();
        });
}

// Builds the {"log", "lines"} response fields shared by every debug_get_log
// mode: `log` stays the newline-joined text (unchanged shape for existing
// callers), `lines` is the additive per-line {"line", "text"} array that lets
// a caller follow up a filtered/tailed hit with a precise offset request.
// When `deduped` is true, each `lines[]` entry also carries `count`/`lastLine`
// (see McpLogFilter::dedupeConsecutive) and `log` annotates a collapsed run
// with "(xN)"; when false, entries and `log` are exactly as before dedupe existed.
static void appendLogFields(QJsonObject& result, const QList<McpLogFilter::LineMatch>& matches,
                            bool deduped = false)
{
    QStringList texts;
    QJsonArray lineArray;
    texts.reserve(matches.size());
    for (const auto& m : matches) {
        QJsonObject entry{{"line", static_cast<qint64>(m.line)}, {"text", m.text}};
        if (deduped) {
            texts.append(m.count > 1 ? m.text + QStringLiteral(" (x%1)").arg(m.count) : m.text);
            entry["count"] = static_cast<qint64>(m.count);
            entry["lastLine"] = static_cast<qint64>(m.lastLine);
        } else {
            texts.append(m.text);
        }
        lineArray.append(entry);
    }
    result["log"] = texts.join('\n');
    result["lines"] = lineArray;
}

void registerDebugTools(McpToolRegistry* registry, MemoryMonitor* memoryMonitor)
{
    // The subsystem-marker paragraph, BUILT FROM THE REGISTRY (core/logtags.h) rather
    // than restated here.
    //
    // This matters more than it looks. The whole point of the markers is that an
    // assistant reading a user's log can retrieve one subsystem's story with one
    // query — and it can only do that if this description names the markers that
    // exist. A hand-written list here would be a second copy free to drift, and the
    // failure mode is invisible: a marker the description omits is a marker the
    // assistant never tries, so the story simply looks absent.
    QString markerHelp =
        QStringLiteral("\n\nSUBSYSTEM MARKERS. Device and radio log lines begin with a "
                       "bracketed subsystem marker, then optionally their own source "
                       "(\"[Scale][BLE AcaiaScale] tare sent\"). Pass one as `filter` to "
                       "retrieve that subsystem's whole narrative. Registered markers:");
    for (const DecenzaLog::Subsystem& sub : DecenzaLog::subsystems()) {
        markerHelp += QStringLiteral("\n  %1 — %2")
                          .arg(DecenzaLog::markerFilter(sub.marker), QLatin1String(sub.description));
    }
    markerHelp += QStringLiteral(
        "\n\nA bracketed marker is a SUBSTRING, not a pattern: pass it with `regex` "
        "false/absent. Under `regex: true`, \"[Scale]\" is a character class matching "
        "any line containing S, c, a, l or e — i.e. almost every line — which looks "
        "like a working query returning everything.\n\n"
        "Severity carries audience, so `minLevel: \"INFO\"` plus a marker is the "
        "user-facing story for that subsystem and nothing else: DEBUG is developer "
        "detail (protocol frames, per-poll state), INFO is the narrative a user may "
        "need (lifecycle, discovery outcomes, connect/disconnect, fallback), WARN and "
        "above are problems. That pairing is the same predicate the app's own "
        "connections page uses, so these lines are the ones a user can read on "
        "screen — but the page shows only the current session and only its last few "
        "hundred lines, while you are addressing the whole file. Expect to see more "
        "than the user does, not less.");

    // debug_get_log — chunked access to the persisted debug log with session awareness
    registry->registerTool(
        "debug_get_log",
        "Read the persisted debug log. Supports three addressing modes: "
        "(0) families=true: census of this log's line prefixes — start here when you do not "
        "already know which subsystem you need, because the markers named below are a "
        "MINORITY of the log and a subsystem missing from them is not missing from the log. "
        "(1) sessions=true: list all sessions with index, start line, timestamp, and line count. "
        "(2) session=N: address session N (-1=most recent, -2=previous, 0=first). "
        "(3) Default: address the whole log. "
        "Sessions are listed in the order they were recorded, so index 0 is the oldest "
        "surviving session and -1 is the run happening now. A session may be reported with "
        "`timestamp` null and `startTimeKnown` false; its lines are intact and only its "
        "start time is unknown, so treat null as \"unknown\", never as \"just now\". There "
        "is more than one cause — the log is capped and trimmed from the front, which can "
        "remove the oldest session's marker, and a marker can also be left unparseable by a "
        "run that ended abruptly — so read `startTimeNote` on that session rather than "
        "assuming which happened. "
        "Within modes 2/3, `filter` (substring, or regex when `regex` is true; case-insensitive) "
        "and `minLevel` (DEBUG/INFO/WARN/ERROR/FATAL, mode-2/3 app log only) narrow which lines "
        "qualify before pagination. `dedupe` collapses consecutive qualifying lines that are "
        "identical apart from each line's own leading timestamp into one entry carrying `count` "
        "and `lastLine` (non-consecutive repeats are not collapsed). `tail` (last N qualifying/"
        "deduped entries) takes precedence over `offset` when both are given. Every returned line "
        "carries its absolute line number in the `lines` array."
        + markerHelp,
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"offset", QJsonObject{
                    {"type", "integer"},
                    {"description", "Line number to start from (0-based, or relative within session). Default: 0. Ignored when tail is set."}
                }},
                {"limit", QJsonObject{
                    {"type", "integer"},
                    {"description", "Maximum lines to return (1-2000). Default: 500"}
                }},
                {"sessions", QJsonObject{
                    {"type", "boolean"},
                    {"description", "If true, return a list of sessions instead of log lines"}
                }},
                {"families", QJsonObject{
                    {"type", "boolean"},
                    {"description",
                     "If true, return a census of this log's line prefixes instead of log lines: "
                     "how many lines carry each REGISTERED marker, each unregistered bracketed "
                     "prefix, and each bare \"ClassName:\" prefix, plus how many carry no prefix "
                     "at all. Start here when you do not already know which subsystem you need. "
                     "The registered markers are the only ones this tool's description names, and "
                     "they are a minority of the log — a subsystem missing from that list is not "
                     "absent from the log, it is just not searchable by marker, and this census "
                     "is how you find out it exists at all."}
                }},
                {"session", QJsonObject{
                    {"type", "integer"},
                    {"description", "Return lines from this session only. Negative indexes count from end (-1=most recent)"}
                }},
                {"filter", QJsonObject{
                    {"type", "string"},
                    {"description", "Only return lines containing this text (case-insensitive substring, or regex when `regex` is true)"}
                }},
                {"regex", QJsonObject{
                    {"type", "boolean"},
                    {"description", "Treat `filter` as a case-insensitive regular expression instead of a literal substring"}
                }},
                {"minLevel", QJsonObject{
                    {"type", "string"},
                    {"enum", QJsonArray{"DEBUG", "INFO", "WARN", "ERROR", "FATAL"}},
                    {"description", "Only return lines at or above this severity"}
                }},
                {"tail", QJsonObject{
                    {"type", "integer"},
                    {"description", "Return only the last N qualifying lines instead of paginating from offset. Takes precedence over offset when both are set."}
                }},
                {"dedupe", QJsonObject{
                    {"type", "boolean"},
                    {"description", "Collapse consecutive qualifying lines that are identical apart from each line's own leading timestamp into one entry carrying count/lastLine. Non-consecutive repeats are not collapsed."}
                }}
            }}
        },
        [memoryMonitor](const QJsonObject& args) -> QJsonObject {
            auto* logger = WebDebugLogger::instance();
            if (!logger) {
                return QJsonObject{{"error", "Debug logger not available"}};
            }

            const QString filter = args["filter"].toString();
            const bool regexMode = args["regex"].toBool(false);
            const QString minLevel = args["minLevel"].toString();
            if (!minLevel.isEmpty() && McpLogFilter::levelRank(minLevel) < 0) {
                return QJsonObject{{"error", "Invalid minLevel: " + minLevel + " (must be DEBUG, INFO, WARN, ERROR, or FATAL)"}};
            }
            // tail:0 (or a negative value, clamped to 0) means "no tail" — must NOT be
            // treated the same as a real tail request below, or hasMore gets forced to
            // false on an ordinary paginated page that may have more lines beyond it.
            const qsizetype tail = args.contains("tail")
                ? qMax(qsizetype(0), static_cast<qsizetype>(args["tail"].toInt(0))) : 0;
            const bool tailActive = tail > 0;
            const bool dedupe = args["dedupe"].toBool(false);
            const qsizetype offset = qMax(qsizetype(0), static_cast<qsizetype>(args["offset"].toInt(0)));
            const qsizetype limit = qBound(qsizetype(1), static_cast<qsizetype>(args["limit"].toInt(500)), qsizetype(2000));
            const bool narrowed = !filter.isEmpty() || !minLevel.isEmpty() || tailActive || dedupe;

            // Mode 0: families=true — a census of this log's prefixes.
            //
            // Computed from the log in hand rather than from a list in the source.
            // A static list would be one more thing to drift, and would describe
            // the build rather than the file the reader is holding — a family that
            // never fired this run would still be advertised, and one added since
            // would be missing. This cannot be wrong about the log it just read.
            if (args["families"].toBool()) {
                QSet<QString> registered;
                for (const auto& s : DecenzaLog::subsystems())
                    registered.insert(QString::fromUtf8(s.marker));

                qsizetype scanned = 0;
                const QStringList all = logger->getPersistedLogChunk(0, 1000000, &scanned);
                QMap<QString, int> reg, unreg, cls;
                int none = 0;
                for (const QString& l : all) {
                    const auto p = McpLogFilter::linePrefix(l, registered);
                    switch (p.kind) {
                    case McpLogFilter::PrefixKind::RegisteredMarker:     reg[p.token]++;   break;
                    case McpLogFilter::PrefixKind::UnregisteredBracket:  unreg[p.token]++; break;
                    case McpLogFilter::PrefixKind::ClassPrefix:          cls[p.token]++;   break;
                    case McpLogFilter::PrefixKind::None:                 ++none;           break;
                    }
                }
                const auto toArray = [](const QMap<QString, int>& m, const QString& how) {
                    // Descending by line count: the families worth knowing about are
                    // the ones with volume, and an alphabetical list buries them.
                    QList<QPair<int, QString>> rows;
                    for (auto it = m.constBegin(); it != m.constEnd(); ++it)
                        rows.append({it.value(), it.key()});
                    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
                        return a.first != b.first ? a.first > b.first : a.second < b.second;
                    });
                    QJsonArray out;
                    for (const auto& r : rows)
                        out.append(QJsonObject{{"prefix", r.second}, {"lines", r.first},
                                               {"searchWith", how.arg(r.second)}});
                    return out;
                };

                return QJsonObject{
                    {"linesScanned", static_cast<double>(all.size())},
                    {"registeredMarkers", toArray(reg, QStringLiteral("filter=\"[%1]\""))},
                    {"unregisteredBracketPrefixes",
                     toArray(unreg, QStringLiteral("filter=\"[%1]\""))},
                    {"classPrefixes", toArray(cls, QStringLiteral("filter=\"%1:\""))},
                    {"linesWithNoPrefix", none},
                    {"note",
                     "Only `registeredMarkers` are named in this tool's description and guaranteed "
                     "to return a subsystem's WHOLE story from one search. The other two lists are "
                     "searchable with the `searchWith` filter shown, but that filter is a plain "
                     "substring over one hand-written prefix, so it may be incomplete where the "
                     "same subsystem logs under more than one spelling. `linesWithNoPrefix` are "
                     "attributable to nothing and can only be found by content. "
                     "IMPORTANT: this census describes THIS FILE, not the current build. The log "
                     "is a ring buffer spanning many runs and possibly several app versions, so an "
                     "unregistered prefix here may be one that has since been converted and no "
                     "longer occurs — it is evidence about the lines in front of you, not about "
                     "what the code still does."},
                };
            }

            // Mode 1/2: sessions=true or session=N — resolve via the cached index.
            if (args.contains("sessions") || args.contains("session")) {
                qsizetype totalLines = 0;
                const QList<WebDebugLogger::SessionBoundary> sessions = logger->sessionIndex(&totalLines);

                // One writer for a session's start time, because it is reported
                // from two places (the list below and the single-session result
                // further down) and they must not describe an unknown start
                // differently.
                //
                // An unknown start is reported as JSON null plus an explicit
                // flag and reason, never as "". An empty string reads to a
                // consumer as a parse failure in the tool, which sends the reader
                // looking for a bug here instead of understanding that the
                // information was destroyed by a log trim before they arrived.
                // The note must not assert a CAUSE it cannot know. An empty
                // timestamp has two sources, and only the first is a trim:
                //
                //   startLine == 0  the leading fragment, whose marker the trim
                //                   took. Oldest in the file, by construction.
                //   startLine  > 0  a marker that parsed as a boundary but whose
                //                   timestamp did not read — a line truncated by
                //                   a killed process, say. Nothing to do with
                //                   trimming, and NOT the oldest session.
                //
                // The note used to state the trim story unconditionally, so a
                // torn mid-file marker was reported with a confident wrong
                // explanation and a false claim about its position. Writing a
                // cause you have not established is the same defect as the forged
                // timestamp this whole change exists to remove; it is just harder
                // to catch, because prose does not fail a test.
                const auto describeStart = [](const WebDebugLogger::SessionBoundary& b,
                                              QJsonObject& into) {
                    if (b.timestamp.isEmpty()) {
                        into["timestamp"] = QJsonValue();  // null
                        into["startTimeKnown"] = false;
                        into["startTimeNote"] = b.startLine == 0
                            ? "This session's start marker was removed when the log file was "
                              "trimmed from the front. Its lines are intact; only its start "
                              "time is unrecoverable. It is the oldest session in the file."
                            : "This session's start marker is present but its timestamp did "
                              "not parse — most likely a line truncated when the previous run "
                              "ended abruptly. Its lines are intact; only its start time is "
                              "unreadable.";
                    } else {
                        into["timestamp"] = b.timestamp;
                        into["startTimeKnown"] = true;
                    }
                };

                if (args["sessions"].toBool()) {
                    QJsonArray sessionList;
                    for (qsizetype i = 0; i < sessions.size(); ++i) {
                        QJsonObject s;
                        s["index"] = static_cast<int>(i);
                        s["negativeIndex"] = static_cast<int>(i - sessions.size());
                        s["startLine"] = static_cast<int>(sessions[i].startLine);
                        s["lineCount"] = static_cast<int>(sessions[i].lineCount);
                        describeStart(sessions[i], s);
                        sessionList.append(s);
                    }
                    return QJsonObject{
                        {"sessions", sessionList},
                        {"sessionCount", static_cast<int>(sessions.size())},
                        {"totalLines", static_cast<int>(totalLines)}
                    };
                }

                qsizetype sessionIdx = static_cast<qsizetype>(args["session"].toInt(0));
                if (sessionIdx < 0)
                    sessionIdx = sessions.size() + sessionIdx;
                if (sessionIdx < 0 || sessionIdx >= sessions.size()) {
                    return QJsonObject{{"error", "Session index out of range"},
                                       {"sessionCount", static_cast<int>(sessions.size())}};
                }

                const qsizetype sessStart = sessions[sessionIdx].startLine;
                const qsizetype sessLines = sessions[sessionIdx].lineCount;
                const QStringList rawLines = logger->getPersistedLogChunk(sessStart, sessLines);

                QString filterError;
                QList<McpLogFilter::LineMatch> qualifying =
                    McpLogFilter::filterLines(rawLines, sessStart, filter, regexMode, minLevel, &filterError);
                if (!filterError.isEmpty())
                    return QJsonObject{{"error", filterError}};
                if (dedupe)
                    qualifying = McpLogFilter::dedupeConsecutive(qualifying);

                const QList<McpLogFilter::LineMatch> page = McpLogFilter::paginate(qualifying, offset, limit, tail);

                QJsonObject result;
                result["session"] = static_cast<int>(sessionIdx);
                {
                    // Same shape as the list mode, via the same writer: the key
                    // is named sessionTimestamp here for backward compatibility,
                    // so rename what describeStart() wrote rather than composing
                    // a second description of an unknown start.
                    QJsonObject start;
                    describeStart(sessions[sessionIdx], start);
                    result["sessionTimestamp"] = start["timestamp"];
                    result["sessionStartTimeKnown"] = start["startTimeKnown"];
                    if (start.contains("startTimeNote"))
                        result["sessionStartTimeNote"] = start["startTimeNote"];
                }
                result["offsetLines"] = static_cast<int>(offset);
                result["limitLines"] = static_cast<int>(limit);
                result["sessionLines"] = static_cast<int>(sessLines);
                result["qualifyingLines"] = static_cast<int>(qualifying.size());
                result["returnedLines"] = static_cast<int>(page.size());
                result["hasMore"] = tailActive ? false : ((offset + page.size()) < qualifying.size());
                appendLogFields(result, page, dedupe);

                if (!result["hasMore"].toBool() && memoryMonitor)
                    result["memorySummary"] = memoryMonitor->toSummaryString();

                return result;
            }

            // Mode 3: whole-log addressing.
            if (!narrowed) {
                // Unfiltered, untailed raw pagination — original narrow read,
                // unchanged cost and response shape for existing callers.
                qsizetype totalLines = 0;
                QStringList lines = logger->getPersistedLogChunk(offset, limit, &totalLines);

                QJsonObject result;
                result["offsetLines"] = static_cast<int>(offset);
                result["limitLines"] = static_cast<int>(limit);
                result["totalLines"] = static_cast<int>(totalLines);
                result["returnedLines"] = static_cast<int>(lines.size());
                result["hasMore"] = (offset + lines.size()) < totalLines;
                result["log"] = lines.join('\n');

                if (!result["hasMore"].toBool() && memoryMonitor)
                    result["memorySummary"] = memoryMonitor->toSummaryString();

                return result;
            }

            // Filtered/tailed whole-log search: same bounded scan the session
            // index already uses, since there's no persistent search index.
            qsizetype totalLines = 0;
            const QStringList rawLines = logger->getPersistedLogChunk(0, 100000, &totalLines);

            QString filterError;
            QList<McpLogFilter::LineMatch> qualifying =
                McpLogFilter::filterLines(rawLines, 0, filter, regexMode, minLevel, &filterError);
            if (!filterError.isEmpty())
                return QJsonObject{{"error", filterError}};
            if (dedupe)
                qualifying = McpLogFilter::dedupeConsecutive(qualifying);

            const QList<McpLogFilter::LineMatch> page = McpLogFilter::paginate(qualifying, offset, limit, tail);

            QJsonObject result;
            result["offsetLines"] = static_cast<int>(offset);
            result["limitLines"] = static_cast<int>(limit);
            result["totalLines"] = static_cast<int>(totalLines);
            result["qualifyingLines"] = static_cast<int>(qualifying.size());
            result["returnedLines"] = static_cast<int>(page.size());
            result["hasMore"] = tailActive ? false : ((offset + page.size()) < qualifying.size());
            appendLogFields(result, page, dedupe);

            if (!result["hasMore"].toBool() && memoryMonitor)
                result["memorySummary"] = memoryMonitor->toSummaryString();

            return result;
        },
        "read");
}
