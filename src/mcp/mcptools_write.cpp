#include "core/settings_app.h"
#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "../history/shothistorystorage.h"
#include "../history/shotprojection.h"
#include "../history/coffeebagstorage.h"
#include "../history/equipmentstorage.h"
#include "../core/basketaliases.h"
#include "../history/bagid.h"
#include "../network/visualizeruploader.h"
#include "../controllers/profilemanager.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_dye.h"
#include "../core/settings_network.h"
#include "../core/settings_mqtt.h"
#include "../core/settings_autowake.h"
#include "../core/settings_hardware.h"
#include "../core/settings_ai.h"
#include "../core/settings_theme.h"
#include "../core/settings_visualizer.h"
#include "../core/settings_calibration.h"
#include "../core/settings_mcp.h"
#include "../core/accessibilitymanager.h"
#include "../core/translationmanager.h"
#include "../core/batterymanager.h"
#include "../screensaver/screensavervideomanager.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QMetaObject>
#include <QCoreApplication>

#include "../core/dbutils.h"

void registerWriteTools(McpToolRegistry* registry, ProfileManager* profileManager,
                        ShotHistoryStorage* shotHistory, Settings* settings,
                        VisualizerUploader* visualizerUploader,
                        CoffeeBagStorage* bagStorage,
                        AccessibilityManager* accessibility,
                        ScreensaverVideoManager* screensaver,
                        TranslationManager* translation,
                        BatteryManager* battery)
{
    // shots_update — replaces shots_set_feedback with full metadata editing (same as QML)
    registry->registerAsyncTool(
        "shots_update",
        "Update any metadata field on a shot. Supports all fields the QML shot editor can change: "
        "enjoyment, notes, dose, yield, bean info, grinder info, barista, TDS, EY, and the shot's "
        "Bean Base snapshot (beanBase).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID"}}},
                {"enjoyment", QJsonObject{{"type", "integer"}, {"description", "Enjoyment rating 0-100"}}},
                {"notes", QJsonObject{{"type", "string"}, {"description", "Tasting notes"}}},
                {"doseWeight", QJsonObject{{"type", "number"}, {"description", "Dose weight in grams"}}},
                {"drinkWeight", QJsonObject{{"type", "number"}, {"description", "Yield/drink weight in grams"}}},
                {"beanBrand", QJsonObject{{"type", "string"}, {"description", "Bean brand"}}},
                {"beanType", QJsonObject{{"type", "string"}, {"description", "Bean type/name"}}},
                {"roastLevel", QJsonObject{{"type", "string"}, {"description", "Roast level"}}},
                {"roastDate", QJsonObject{{"type", "string"}, {"description", "Roast date (YYYY-MM-DD)"}}},
                // Grinder identity (brand/model/burrs) is owned by the shot's
                // equipment package, not the shot row, so it is intentionally not
                // editable here — change it via the equipment package. Only the
                // per-shot grind setting remains a shot-level field.
                {"grinderSetting", QJsonObject{{"type", "string"}, {"description", "Grinder setting"}}},
                {"barista", QJsonObject{{"type", "string"}, {"description", "Barista name"}}},
                {"beverageType", QJsonObject{{"type", "string"}, {"description", "Beverage type (e.g. 'espresso', 'lungo'). Saved locally; the Visualizer shot schema carries beverage type via the profile, not the shot, so editing it here does not propagate to visualizer.coffee."}}},
                {"drinkTds", QJsonObject{{"type", "number"}, {"description", "TDS measurement"}}},
                {"drinkEy", QJsonObject{{"type", "number"}, {"description", "Extraction yield percentage"}}},
                {"beanBase", QJsonObject{{"type", "object"}, {"description",
                    "Replace this shot's stored Bean Base snapshot (the canonical bean record the shot "
                    "was pulled with — shown as `beanBase` in shots_get_detail). Pass a full entry object "
                    "(e.g. copied from a correctly-linked shot's beanBase, with fields like id, roasterName, "
                    "roastName, origin, variety, process, tastingTags). Pass an empty object {} to clear "
                    "the link. Use this to fix shots recorded against the wrong bean."}}}
            }},
            {"required", QJsonArray{"shotId"}}
        },
        [shotHistory, settings, visualizerUploader](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            qint64 shotId = args["shotId"].toInteger();
            if (shotId <= 0) {
                respond(QJsonObject{{"error", "Valid shotId is required"}});
                return;
            }

            // Keys must match what updateShotMetadataStatic() reads (camelCase)
            QVariantMap metadata;
            if (args.contains("enjoyment"))
                metadata["enjoyment"] = qBound(0, args["enjoyment"].toInt(), 100);
            if (args.contains("notes"))
                metadata["espressoNotes"] = args["notes"].toString();
            if (args.contains("doseWeight"))
                metadata["doseWeight"] = args["doseWeight"].toDouble();
            if (args.contains("drinkWeight"))
                metadata["finalWeight"] = args["drinkWeight"].toDouble();
            if (args.contains("beanBrand"))
                metadata["beanBrand"] = args["beanBrand"].toString();
            if (args.contains("beanType"))
                metadata["beanType"] = args["beanType"].toString();
            if (args.contains("roastLevel"))
                metadata["roastLevel"] = args["roastLevel"].toString();
            if (args.contains("roastDate"))
                metadata["roastDate"] = args["roastDate"].toString();
            // grinderBrand/Model/Burrs are not accepted: grinder identity lives on
            // the equipment package, not the shot (updateShotMetadataStatic's field
            // map omits them). Only the per-shot grind setting is editable here.
            if (args.contains("grinderSetting"))
                metadata["grinderSetting"] = args["grinderSetting"].toString();
            if (args.contains("barista"))
                metadata["barista"] = args["barista"].toString();
            if (args.contains("beverageType"))
                metadata["beverageType"] = args["beverageType"].toString();
            if (args.contains("drinkTds"))
                metadata["drinkTds"] = args["drinkTds"].toDouble();
            if (args.contains("drinkEy"))
                metadata["drinkEy"] = args["drinkEy"].toDouble();
            if (args.contains("beanBase")) {
                // Snapshot semantics: we store the data, not a reference —
                // an empty object clears the link, anything else is saved
                // verbatim as the shot's compact-JSON snapshot. Guard the
                // coercion footguns: a STRING here (classic LLM mistake)
                // would silently coerce to {} = the clear sentinel, turning
                // "link this bean" into "unlink" with a success response.
                if (!args["beanBase"].isObject()) {
                    respond(QJsonObject{{"error",
                        "beanBase must be a JSON object (pass {} to clear the link)"}});
                    return;
                }
                const QJsonObject bean = args["beanBase"].toObject();
                if (!bean.isEmpty() && bean.value("id").toVariant().toString().isEmpty()) {
                    respond(QJsonObject{{"error",
                        "beanBase object must carry a non-empty id (from bean_search or another shot's beanBase)"}});
                    return;
                }
                metadata["beanBaseJson"] = bean.isEmpty()
                    ? QString()
                    : QString::fromUtf8(QJsonDocument(bean).toJson(QJsonDocument::Compact));
            }

            if (metadata.isEmpty()) {
                respond(QJsonObject{{"error", "Provide at least one field to update"}});
                return;
            }

            // Build ShotProjection-keyed overrides for visualizer PATCH (field names differ from DB keys).
            QVariantMap vizOverrides;
            if (args.contains("enjoyment"))
                vizOverrides["enjoyment0to100"] = qBound(0, args["enjoyment"].toInt(), 100);
            if (args.contains("notes"))
                vizOverrides["espressoNotes"] = args["notes"].toString();
            if (args.contains("doseWeight"))
                vizOverrides["doseWeightG"] = args["doseWeight"].toDouble();
            if (args.contains("drinkWeight"))
                vizOverrides["finalWeightG"] = args["drinkWeight"].toDouble();
            if (args.contains("beanBrand"))
                vizOverrides["beanBrand"] = args["beanBrand"].toString();
            if (args.contains("beanType"))
                vizOverrides["beanType"] = args["beanType"].toString();
            if (args.contains("roastLevel"))
                vizOverrides["roastLevel"] = args["roastLevel"].toString();
            if (args.contains("roastDate"))
                vizOverrides["roastDate"] = args["roastDate"].toString();
            if (args.contains("grinderBrand"))
                vizOverrides["grinderBrand"] = args["grinderBrand"].toString();
            if (args.contains("grinderModel"))
                vizOverrides["grinderModel"] = args["grinderModel"].toString();
            // grinderBurrs intentionally omitted from vizOverrides — Visualizer API
            // has no separate burrs field (only combined grinder_model). The burrs
            // value is still persisted to the local DB via the metadata map above.
            if (args.contains("grinderSetting"))
                vizOverrides["grinderSetting"] = args["grinderSetting"].toString();
            if (args.contains("barista"))
                vizOverrides["barista"] = args["barista"].toString();
            // beverageType intentionally omitted from vizOverrides — Visualizer's
            // shot PATCH schema has no beverage_type field. Still persisted to
            // local DB via the metadata map above.
            if (args.contains("drinkTds"))
                vizOverrides["drinkTdsPct"] = args["drinkTds"].toDouble();
            if (args.contains("drinkEy"))
                vizOverrides["drinkEyPct"] = args["drinkEy"].toDouble();

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, shotId, metadata, vizOverrides,
                                               respond, shotHistory, settings, visualizerUploader]() {
                bool ok = false;
                QString visualizerId;
                ShotProjection vizShot;
                withTempDb(dbPath, "mcp_update", [&](QSqlDatabase& db) {
                    ok = ShotHistoryStorage::updateShotMetadataStatic(db, shotId, metadata);
                    if (ok) {
                        QSqlQuery idQuery(db);
                        idQuery.prepare("SELECT visualizer_id FROM shots WHERE id = :id");
                        idQuery.bindValue(":id", shotId);
                        if (idQuery.exec()) {
                            if (idQuery.next())
                                visualizerId = idQuery.value(0).toString();
                        } else {
                            qWarning() << "MCP shots_update: failed to query visualizer_id for shot"
                                       << shotId << ":" << idQuery.lastError().text();
                        }
                        if (!visualizerId.isEmpty()) {
                            ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId, nullptr);
                            vizShot = ShotHistoryStorage::convertShotRecord(record);
                        }
                    }
                });

                QJsonObject result;
                if (ok) {
                    result["success"] = true;
                    QStringList fields;
                    for (auto it = metadata.begin(); it != metadata.end(); ++it)
                        fields << it.key();
                    result["updated"] = QJsonArray::fromStringList(fields);
                    result["message"] = "Shot " + QString::number(shotId) + " updated";
                } else {
                    result["error"] = "Failed to update shot " + QString::number(shotId);
                }

                QMetaObject::invokeMethod(qApp, [respond, result, shotHistory, shotId, ok,
                                                  visualizerId, vizShot, vizOverrides, settings, visualizerUploader]() mutable {
                    if (ok) {
                        shotHistory->invalidateDistinctCache();
                        emit shotHistory->shotMetadataUpdated(shotId, true);
                    }

                    bool willAutoUpdate = false;
                    QString skipReason;
                    if (ok && visualizerUploader && !visualizerId.isEmpty()
                            && settings && settings->visualizer()->visualizerAutoUpdate()) {
                        if (vizShot.isValid()) {
                            willAutoUpdate = true;
                            qInfo() << "MCP shots_update: auto-updating visualizer shot" << visualizerId
                                    << "for local shot id" << shotId;
                            visualizerUploader->updateShotOnVisualizerWithOverrides(
                                visualizerId, QVariant::fromValue(vizShot), vizOverrides);
                        } else {
                            skipReason = QString("failed to reload shot %1 for visualizer PATCH").arg(shotId);
                            qWarning() << "MCP shots_update:" << skipReason;
                        }
                    }

                    // Only surface visualizer-update status on the success path —
                    // a DB update failure produces an error response, and tacking
                    // a status field onto it is semantically confusing for LLM callers.
                    if (ok) {
                        result["visualizerUpdateTriggered"] = willAutoUpdate;
                        if (!skipReason.isEmpty())
                            result["visualizerUpdateSkippedReason"] = skipReason;
                    }
                    respond(result);
                }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "control");

    // shots_upload_to_visualizer — first-POST a historical shot. Companion to
    // shots_update's PATCH path: shots_update only fires the auto-update PATCH for
    // shots that already have a visualizer_id, so historical shots that were never
    // auto-uploaded (e.g. an older shot recorded before credentials were set up, or
    // an upload that failed at shot completion) need this entry point instead.
    registry->registerAsyncTool(
        "shots_upload_to_visualizer",
        "Upload a historical shot to visualizer.coffee for the first time (POST). "
        "Use this for shots that were never auto-uploaded and therefore have no "
        "visualizer_id yet. For shots that are already uploaded, use shots_update "
        "to PATCH metadata instead — this tool refuses to re-upload an existing shot.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID to upload"}}}
            }},
            {"required", QJsonArray{"shotId"}}
        },
        [shotHistory, settings, visualizerUploader](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            // Validate the input first so the unit-test fixture can cover the
            // shotId guard without wiring full ShotHistoryStorage / VisualizerUploader.
            qint64 shotId = args["shotId"].toInteger();
            if (shotId <= 0) {
                respond(QJsonObject{{"error", "Valid shotId is required"}});
                return;
            }
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }
            if (!visualizerUploader || !settings) {
                respond(QJsonObject{{"error", "Visualizer uploader not available"}});
                return;
            }

            const QString dbPath = shotHistory->databasePath();

            QThread* thread = QThread::create([dbPath, shotId, respond, settings, visualizerUploader]() {
                bool shotFound = false;
                QString existingVisualizerId;
                ShotProjection shot;
                withTempDb(dbPath, "mcp_upload", [&](QSqlDatabase& db) {
                    QSqlQuery idQuery(db);
                    idQuery.prepare("SELECT visualizer_id FROM shots WHERE id = :id");
                    idQuery.bindValue(":id", shotId);
                    bool ran = idQuery.exec();
                    if (ran && idQuery.next()) {
                        shotFound = true;
                        existingVisualizerId = idQuery.value(0).toString();
                        if (existingVisualizerId.isEmpty()) {
                            ShotRecord record = ShotHistoryStorage::loadShotRecordStatic(db, shotId, nullptr);
                            shot = ShotHistoryStorage::convertShotRecord(record);
                        }
                    }
                });

                QMetaObject::invokeMethod(qApp,
                    [respond, shotId, shotFound, existingVisualizerId, shot, settings, visualizerUploader]() mutable {
                        if (!shotFound) {
                            respond(QJsonObject{{"error", QString("Shot %1 not found").arg(shotId)}});
                            return;
                        }
                        if (!existingVisualizerId.isEmpty()) {
                            respond(QJsonObject{
                                {"error", QString("Shot %1 is already uploaded to visualizer (id %2); use shots_update to PATCH instead")
                                    .arg(shotId).arg(existingVisualizerId)}
                            });
                            return;
                        }
                        if (!shot.isValid()) {
                            respond(QJsonObject{{"error", QString("Failed to load shot %1 for upload").arg(shotId)}});
                            return;
                        }

                        // Pre-flight checks mirror validateUpload so we never enter
                        // the call chain that emits uploadFailed/uploadSkipped on a
                        // shared signal — VisualizerUploader's header explicitly
                        // warns that concurrent callers would mis-attribute those
                        // signals on a UI page that is filtering on its own
                        // in-flight flags. Failing fast here also lets the MCP
                        // caller distinguish "rejected by policy" from "dispatched
                        // but might fail over the network" — the latter still
                        // returns success below.
                        if (settings->visualizer()->visualizerUsername().isEmpty()
                                || settings->visualizer()->visualizerPassword().isEmpty()) {
                            respond(QJsonObject{{"error", "Visualizer credentials not configured"}});
                            return;
                        }
                        QString beverageType;
                        if (!shot.profileJson.isEmpty()) {
                            QJsonDocument profileDoc = QJsonDocument::fromJson(shot.profileJson.toUtf8());
                            if (!profileDoc.isNull())
                                beverageType = profileDoc.object()["beverage_type"].toString();
                        }
                        if (beverageType == "cleaning" || beverageType == "calibrate" || beverageType == "descale") {
                            respond(QJsonObject{
                                {"error", QString("Shot %1 uses a maintenance profile (%2); not uploaded").arg(shotId).arg(beverageType)}
                            });
                            return;
                        }
                        const double minDuration = settings->visualizer()->visualizerMinDuration();
                        if (shot.durationSec < minDuration) {
                            respond(QJsonObject{
                                {"error", QString("Shot %1 too short (%2s < %3s); not uploaded")
                                    .arg(shotId).arg(shot.durationSec, 0, 'f', 1).arg(minDuration, 0, 'f', 0)}
                            });
                            return;
                        }

                        // Empty overrides — the projection loaded from DB already
                        // carries the user's current metadata (notes, ratings, bean
                        // info, etc.), so no edit-field overlay is needed.
                        visualizerUploader->uploadShotFromHistoryWithOverrides(QVariant::fromValue(shot), QVariantMap{});
                        respond(QJsonObject{
                            {"success", true},
                            {"uploadTriggered", true},
                            {"message", QString("Upload dispatched for shot %1; the visualizer id will land in the local DB once the response arrives").arg(shotId)}
                        });
                    }, Qt::QueuedConnection);
            });

            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "control");

    // shots_delete
    registry->registerAsyncTool(
        "shots_delete",
        "Delete a shot by ID. This is permanent and cannot be undone.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"shotId", QJsonObject{{"type", "integer"}, {"description", "Shot ID to delete"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }},
            {"required", QJsonArray{"shotId"}}
        },
        [shotHistory](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Shot history not available"}});
                return;
            }

            qint64 shotId = args["shotId"].toInteger();
            if (shotId <= 0) {
                respond(QJsonObject{{"error", "Valid shotId is required"}});
                return;
            }

            // Connect to shotDeleted signal to respond after deletion completes
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = QObject::connect(shotHistory, &ShotHistoryStorage::shotDeleted,
                shotHistory, [respond, shotId, conn](qint64 deletedId) {
                    if (deletedId != shotId) return;
                    QObject::disconnect(*conn);
                    respond(QJsonObject{{"success", true}, {"message", "Shot " + QString::number(shotId) + " deleted"}});
                });

            shotHistory->requestDeleteShot(shotId);
        },
        "settings");

    // profiles_set_active
    registry->registerAsyncTool(
        "profiles_set_active",
        "Load and activate a profile on the machine by filename. "
        "IMPORTANT: Only call this when the user explicitly asks to change the active profile on the machine.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"filename", QJsonObject{{"type", "string"}, {"description", "Profile filename to activate"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }},
            {"required", QJsonArray{"filename"}}
        },
        [profileManager](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!profileManager) {
                respond(QJsonObject{{"error", "Controller not available"}});
                return;
            }

            QString filename = args["filename"].toString();
            if (filename.isEmpty()) {
                respond(QJsonObject{{"error", "filename is required"}});
                return;
            }

            if (!profileManager->profileExists(filename)) {
                respond(QJsonObject{{"error", "Profile not found: " + filename}});
                return;
            }

            QMetaObject::invokeMethod(profileManager, [profileManager, filename, respond]() {
                profileManager->loadProfile(filename);
                respond(QJsonObject{{"success", true}, {"message", "Profile activated: " + filename}});
            }, Qt::QueuedConnection);
        },
        "settings");

    // settings_set
    //
    // Schema is built into a local variable so the property names can be
    // extracted into validSettingsKeys (one pass at registration time).
    // The lambda then rejects any args key that's not in the schema instead
    // of silently ignoring it (#986). Without this, typos like
    // `setings_set(dyeBenBrand: ...)` succeed with `{updated: []}` errors.
    QJsonObject settingsSetSchema = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                // Espresso / profile
                {"espressoTemperature", QJsonObject{{"type", "number"}, {"description", "Brew temperature in Celsius"}}},
                {"targetWeight", QJsonObject{{"type", "number"}, {"description", "Target shot weight in grams"}}},
                // Steam
                {"steamTemperature", QJsonObject{{"type", "number"}, {"description", "Steam temperature in Celsius"}}},
                {"steamTimeout", QJsonObject{{"type", "integer"}, {"description", "Steam timeout in seconds"}}},
                {"steamFlowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Steam flow rate in mL/s"}}},
                {"keepSteamHeaterOn", QJsonObject{{"type", "boolean"}, {"description", "Keep steam heater on between operations"}}},
                {"steamAutoFlushSeconds", QJsonObject{{"type", "integer"}, {"description", "Auto-flush after steam (0 to disable)"}}},
                {"steamTwoTapStop", QJsonObject{{"type", "boolean"}, {"description", "Require two taps to stop steaming"}}},
                // Hot water
                {"waterTemperature", QJsonObject{{"type", "number"}, {"description", "Hot water temperature in Celsius"}}},
                {"waterVolume", QJsonObject{{"type", "integer"}, {"description", "Hot water volume in ml"}}},
                {"waterVolumeMode", QJsonObject{{"type", "string"}, {"description", "Hot water mode: 'weight' or 'volume'"}}},
                {"hotWaterFlowRateMlPerSec", QJsonObject{{"type", "number"}, {"description", "Hot water flow rate in mL/s (0.5-10.0)"}}},
                // Flush
                {"flushFlowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Flush flow rate in mL/s (0-10)"}}},
                {"flushSeconds", QJsonObject{{"type", "number"}, {"description", "Flush duration in seconds"}}},
                // DYE metadata
                {"dyeBeanBrand", QJsonObject{{"type", "string"}, {"description", "Bean brand"}}},
                {"dyeBeanType", QJsonObject{{"type", "string"}, {"description", "Bean type/name"}}},
                {"dyeRoastDate", QJsonObject{{"type", "string"}, {"description", "Roast date"}}},
                {"dyeRoastLevel", QJsonObject{{"type", "string"}, {"description", "Roast level"}}},
                {"dyeGrinderBrand", QJsonObject{{"type", "string"}, {"description", "Grinder brand"}}},
                {"dyeGrinderModel", QJsonObject{{"type", "string"}, {"description", "Grinder model"}}},
                {"dyeGrinderBurrs", QJsonObject{{"type", "string"}, {"description", "Grinder burrs"}}},
                {"dyeGrinderSetting", QJsonObject{{"type", "string"}, {"description", "Grinder setting"}}},
                {"dyeBeanWeight", QJsonObject{{"type", "number"}, {"description", "Dose weight in grams"}}},
                {"dyeDrinkWeight", QJsonObject{{"type", "number"}, {"description", "Drink weight in grams"}}},
                {"dyeDrinkTds", QJsonObject{{"type", "number"}, {"description", "TDS measurement"}}},
                {"dyeDrinkEy", QJsonObject{{"type", "number"}, {"description", "Extraction yield percentage"}}},
                {"dyeEspressoEnjoyment", QJsonObject{{"type", "integer"}, {"description", "Enjoyment rating 0-100"}}},
                {"dyeShotNotes", QJsonObject{{"type", "string"}, {"description", "Shot notes"}}},
                {"dyeBarista", QJsonObject{{"type", "string"}, {"description", "Barista name"}}},
                // Machine
                {"themeMode", QJsonObject{{"type", "string"}, {"description", "Theme mode: 'dark', 'light', or 'system'"}}},
                {"darkThemeName", QJsonObject{{"type", "string"}, {"description", "Dark mode theme name"}}},
                {"lightThemeName", QJsonObject{{"type", "string"}, {"description", "Light mode theme name"}}},
                {"autoSleepMinutes", QJsonObject{{"type", "integer"}, {"description", "Auto-sleep timeout in minutes"}}},
                {"postShotReviewTimeout", QJsonObject{{"type", "integer"}, {"description", "Post-shot review timeout in seconds"}}},
                {"refillKitOverride", QJsonObject{{"type", "integer"}, {"description", "Refill kit override: 0=off, 1=on, 2=auto"}}},
                {"waterRefillPoint", QJsonObject{{"type", "integer"}, {"description", "Water refill warning threshold in mm"}}},
                {"waterLevelDisplayUnit", QJsonObject{{"type", "string"}, {"description", "Water level display unit"}}},
                {"useFlowScale", QJsonObject{{"type", "boolean"}, {"description", "Use virtual flow scale"}}},
                {"screenBrightness", QJsonObject{{"type", "number"}, {"description", "Screen brightness 0.0-1.0"}}},
                {"defaultShotRating", QJsonObject{{"type", "integer"}, {"description", "Default shot enjoyment rating 0-100"}}},
                {"launcherMode", QJsonObject{{"type", "boolean"}, {"description", "Enable kiosk/launcher mode (Android only)"}}},
                {"flowCalibrationMultiplier", QJsonObject{{"type", "number"}, {"description", "Flow calibration multiplier"}}},
                {"autoFlowCalibration", QJsonObject{{"type", "boolean"}, {"description", "Enable automatic flow calibration"}}},
                {"ignoreVolumeWithScale", QJsonObject{{"type", "boolean"}, {"description", "Ignore stop-at-volume when a BLE scale is configured"}}},
                {"autoWakeEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable auto-wake schedule"}}},
                {"autoWakeStayAwakeEnabled", QJsonObject{{"type", "boolean"}, {"description", "Stay awake after auto-wake"}}},
                {"autoWakeStayAwakeMinutes", QJsonObject{{"type", "integer"}, {"description", "Stay awake duration in minutes"}}},
                // Connections
                {"usbSerialEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable USB serial polling for DE1"}}},
                {"showScaleDialogs", QJsonObject{{"type", "boolean"}, {"description", "Show scale connection alert dialogs"}}},
                // Screensaver
                {"screensaverType", QJsonObject{{"type", "string"}, {"description", "Screensaver type"}}},
                {"dimDelayMinutes", QJsonObject{{"type", "integer"}, {"description", "Screen dim delay in minutes"}}},
                {"dimPercent", QJsonObject{{"type", "integer"}, {"description", "Screen dim percentage 0-100"}}},
                {"pipesSpeed", QJsonObject{{"type", "number"}, {"description", "Pipes screensaver speed"}}},
                {"pipesCameraSpeed", QJsonObject{{"type", "number"}, {"description", "Pipes camera speed"}}},
                {"pipesShowClock", QJsonObject{{"type", "boolean"}, {"description", "Show clock in pipes screensaver"}}},
                {"flipClockUse3D", QJsonObject{{"type", "boolean"}, {"description", "Use 3D flip clock"}}},
                {"videosShowClock", QJsonObject{{"type", "boolean"}, {"description", "Show clock in video screensaver"}}},
                {"cacheEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable screensaver video cache"}}},
                {"attractorShowClock", QJsonObject{{"type", "boolean"}, {"description", "Show clock in attractor screensaver"}}},
                {"imageDisplayDuration", QJsonObject{{"type", "integer"}, {"description", "Image display duration in seconds"}}},
                {"showDateOnPersonal", QJsonObject{{"type", "boolean"}, {"description", "Show date on personal media"}}},
                {"shotMapShape", QJsonObject{{"type", "string"}, {"description", "Shot map globe shape"}}},
                {"shotMapTexture", QJsonObject{{"type", "string"}, {"description", "Shot map globe texture"}}},
                {"shotMapShowClock", QJsonObject{{"type", "boolean"}, {"description", "Show clock in shot map"}}},
                {"shotMapShowProfiles", QJsonObject{{"type", "boolean"}, {"description", "Show profiles in shot map"}}},
                {"shotMapShowTerminator", QJsonObject{{"type", "boolean"}, {"description", "Show day/night terminator in shot map"}}},
                // Accessibility
                {"accessibilityEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable accessibility features"}}},
                {"ttsEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable text-to-speech"}}},
                {"tickEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable tick sounds"}}},
                {"tickSoundIndex", QJsonObject{{"type", "integer"}, {"description", "Tick sound index 1-4"}}},
                {"tickVolume", QJsonObject{{"type", "integer"}, {"description", "Tick volume 0-100"}}},
                {"extractionAnnouncementsEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable extraction announcements"}}},
                {"extractionAnnouncementMode", QJsonObject{{"type", "string"}, {"description", "Announcement mode: 'timed', 'milestones_only', 'both'"}}},
                {"extractionAnnouncementInterval", QJsonObject{{"type", "integer"}, {"description", "Announcement interval in seconds"}}},
                // AI
                {"aiProvider", QJsonObject{{"type", "string"}, {"description", "AI provider name"}}},
                {"mcpEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable MCP server"}}},
                {"mcpAccessLevel", QJsonObject{{"type", "integer"}, {"description", "MCP access level: 0=monitor, 1=control, 2=full"}}},
                {"mcpConfirmationLevel", QJsonObject{{"type", "integer"}, {"description", "MCP confirmation: 0=none, 1=dangerous, 2=all"}}},
                {"discussShotApp", QJsonObject{{"type", "integer"}, {"description", "Discuss Shot app: 0=Claude, 1=Claude Web, 2=ChatGPT, 3=Gemini, 4=Grok, 5=Custom, 6=None (hides Discuss button), 7=Claude Desktop (session URL required, see claudeRcSessionUrl)"}}},
                {"discussShotCustomUrl", QJsonObject{{"type", "string"}, {"description", "Custom URL for Discuss Shot"}}},
                {"ollamaEndpoint", QJsonObject{{"type", "string"}, {"description", "Ollama endpoint URL"}}},
                {"ollamaModel", QJsonObject{{"type", "string"}, {"description", "Ollama model name"}}},
                {"openrouterModel", QJsonObject{{"type", "string"}, {"description", "OpenRouter model name"}}},
                // MQTT
                {"mqttEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable MQTT"}}},
                {"mqttBrokerHost", QJsonObject{{"type", "string"}, {"description", "MQTT broker hostname"}}},
                {"mqttBrokerPort", QJsonObject{{"type", "integer"}, {"description", "MQTT broker port"}}},
                {"mqttUsername", QJsonObject{{"type", "string"}, {"description", "MQTT username"}}},
                {"mqttBaseTopic", QJsonObject{{"type", "string"}, {"description", "MQTT base topic"}}},
                {"mqttPublishInterval", QJsonObject{{"type", "integer"}, {"description", "MQTT publish interval in seconds"}}},
                {"mqttRetainMessages", QJsonObject{{"type", "boolean"}, {"description", "Retain MQTT messages"}}},
                {"mqttHomeAssistantDiscovery", QJsonObject{{"type", "boolean"}, {"description", "Enable Home Assistant MQTT discovery"}}},
                {"mqttClientId", QJsonObject{{"type", "string"}, {"description", "MQTT client ID"}}},
                // Themes
                {"activeThemeName", QJsonObject{{"type", "string"}, {"description", "Active theme name"}}},
                {"activeShader", QJsonObject{{"type", "string"}, {"description", "Active screen shader (empty for none, 'crt' for CRT)"}}},
                // Visualizer
                {"visualizerAutoUpload", QJsonObject{{"type", "boolean"}, {"description", "Auto-upload shots to visualizer.coffee"}}},
                {"visualizerAutoUpdate", QJsonObject{{"type", "boolean"}, {"description", "Auto-update shot metadata on visualizer.coffee after editing"}}},
                {"visualizerMinDuration", QJsonObject{{"type", "number"}, {"description", "Minimum shot duration for upload (seconds)"}}},
                {"visualizerExtendedMetadata", QJsonObject{{"type", "boolean"}, {"description", "Upload extended metadata"}}},
                {"visualizerShowAfterShot", QJsonObject{{"type", "boolean"}, {"description", "Show visualizer after shot"}}},
                {"visualizerClearNotesOnStart", QJsonObject{{"type", "boolean"}, {"description", "Clear notes when starting a shot"}}},
                // Update
                {"autoCheckUpdates", QJsonObject{{"type", "boolean"}, {"description", "Auto-check for updates"}}},
                {"betaUpdatesEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable beta update channel"}}},
                // Data
                {"webSecurityEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable web security (TOTP auth)"}}},
                {"dailyBackupHour", QJsonObject{{"type", "integer"}, {"description", "Daily backup hour (0-23)"}}},
                {"shotServerEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable web server"}}},
                {"shotServerPort", QJsonObject{{"type", "integer"}, {"description", "Web server port"}}},
                // History
                {"shotHistorySortField", QJsonObject{{"type", "string"}, {"description", "Shot history sort field"}}},
                {"shotHistorySortDirection", QJsonObject{{"type", "string"}, {"description", "Shot history sort direction"}}},
                // Language
                {"currentLanguage", QJsonObject{{"type", "string"}, {"description", "App language code (e.g., 'en', 'de', 'ja')"}}},
                // Debug
                {"simulationMode", QJsonObject{{"type", "boolean"}, {"description", "Enable DE1 simulator"}}},
                // Battery
                {"chargingMode", QJsonObject{{"type", "integer"}, {"description", "Smart charging mode"}}},
                // Heater calibration (values in display units — same as QML sliders)
                {"heaterIdleTempC", QJsonObject{{"type", "number"}, {"description", "Heater idle temperature in Celsius (0.0-99.0)"}}},
                {"heaterWarmupFlowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Heater warmup flow rate in mL/s (0.5-6.0)"}}},
                {"heaterTestFlowMlPerSec", QJsonObject{{"type", "number"}, {"description", "Heater test flow rate in mL/s (0.5-8.0)"}}},
                {"heaterWarmupTimeoutSec", QJsonObject{{"type", "number"}, {"description", "Heater warmup timeout in seconds (1.0-30.0)"}}},
                // Auto-favorites
                {"autoFavoritesGroupBy", QJsonObject{{"type", "string"}, {"description", "Auto-favorites group by field"}}},
                {"autoFavoritesMaxItems", QJsonObject{{"type", "integer"}, {"description", "Max auto-favorites items"}}},
                {"autoFavoritesOpenBrewSettings", QJsonObject{{"type", "boolean"}, {"description", "Open brew settings on favorite select"}}},
                {"autoFavoritesHideUnrated", QJsonObject{{"type", "boolean"}, {"description", "Hide unrated shots from auto-favorites"}}},
                // Confirmation
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        };
    QSet<QString> validSettingsKeys;
    {
        const QJsonObject props = settingsSetSchema.value("properties").toObject();
        for (auto it = props.constBegin(); it != props.constEnd(); ++it)
            validSettingsKeys.insert(it.key());
    }
    registry->registerAsyncTool(
        "settings_set",
        "Update any app setting on the device. This is the tool to use when the user asks to change "
        "grind size (dyeGrinderSetting), dose weight (dyeBeanWeight), drink/yield weight (targetWeight), "
        "brew temperature (espressoTemperature), or any other setting. "
        "Covers all QML settings tabs: machine, calibration, connections, screensaver, accessibility, AI, "
        "espresso, steam, water, flush, DYE metadata, MQTT, themes, visualizer, update, data, "
        "history, language, debug, battery, heater, auto-favorites. "
        "API keys and passwords are excluded (sensitive). "
        "For temperature and weight changes on the active profile, this tool handles the profile update automatically. "
        "IMPORTANT: Only call this when the user explicitly asks to change settings on the machine. "
        "For discussion and recommendations, respond in chat instead.",
        settingsSetSchema,
        [profileManager, settings, accessibility, screensaver, translation, battery, validSettingsKeys](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!settings) {
                respond(QJsonObject{{"error", "Settings not available"}});
                return;
            }

            // Reject unknown keys before applying any setters. Catches typos
            // and outdated names that would otherwise return {updated: []}
            // and look like a server-side problem to an LLM.
            QStringList unknownKeys;
            for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
                if (!validSettingsKeys.contains(it.key()))
                    unknownKeys.append(it.key());
            }
            if (!unknownKeys.isEmpty()) {
                respond(QJsonObject{
                    {"error", "Unknown settings key(s)"},
                    {"unknownKeys", QJsonArray::fromStringList(unknownKeys)}
                });
                return;
            }

            QStringList updated;
            // Collect setter closures — executed together on the main thread after validation
            QVector<std::function<void()>> setters;
            auto addSetter = [&setters](std::function<void()> fn) { setters.append(std::move(fn)); };

            // === Espresso temperature / target weight (profile-aware) ===
            bool needsProfileUpdate = args.contains("espressoTemperature") || args.contains("targetWeight");
            if (needsProfileUpdate && profileManager) {
                QString editorType = profileManager->currentEditorType();
                if (editorType == "advanced") {
                    QVariantMap profileData = profileManager->getCurrentProfile();
                    if (args.contains("espressoTemperature")) {
                        profileData["espresso_temperature"] = args["espressoTemperature"].toDouble();
                        updated << "espressoTemperature";
                    }
                    if (args.contains("targetWeight")) {
                        profileData["target_weight"] = args["targetWeight"].toDouble();
                        updated << "targetWeight";
                    }
                    profileManager->uploadProfile(profileData);
                } else {
                    QVariantMap currentParams = profileManager->getOrConvertRecipeParams();
                    if (args.contains("espressoTemperature")) {
                        double v = args["espressoTemperature"].toDouble();
                        currentParams["fillTemperature"] = v;
                        currentParams["pourTemperature"] = v;
                        currentParams["tempStart"] = v;
                        currentParams["tempPreinfuse"] = v;
                        currentParams["tempHold"] = v;
                        currentParams["tempDecline"] = v;
                        updated << "espressoTemperature";
                    }
                    if (args.contains("targetWeight")) {
                        currentParams["targetWeight"] = args["targetWeight"].toDouble();
                        updated << "targetWeight";
                    }
                    profileManager->uploadRecipeProfile(currentParams);
                }
                profileManager->uploadCurrentProfile();  // MCP is one-shot, upload immediately

                // Sync QSettings so settings_get reads back the updated values.
                // uploadRecipeProfile/uploadProfile update the profile object but
                // don't write to QSettings (issue #527).
                if (args.contains("espressoTemperature") && settings)
                    settings->brew()->setEspressoTemperature(args["espressoTemperature"].toDouble());
                if (args.contains("targetWeight") && settings)
                    settings->brew()->setTargetWeight(args["targetWeight"].toDouble());
            }

            // === Steam ===
            if (args.contains("steamTemperature")) {
                double v = args["steamTemperature"].toDouble();
                addSetter([settings, v]() { settings->brew()->setSteamTemperature(v); });
                updated << "steamTemperature";
            }
            if (args.contains("steamTimeout")) {
                int v = args["steamTimeout"].toInt();
                addSetter([settings, v]() { settings->brew()->setSteamTimeout(v); });
                updated << "steamTimeout";
            }
            if (args.contains("steamFlowMlPerSec")) {
                int v = static_cast<int>(args["steamFlowMlPerSec"].toDouble() * 100.0);
                addSetter([settings, v]() { settings->brew()->setSteamFlow(v); });
                updated << "steamFlowMlPerSec";
            }
            if (args.contains("keepSteamHeaterOn")) {
                bool v = args["keepSteamHeaterOn"].toBool();
                addSetter([settings, v]() { settings->brew()->setKeepSteamHeaterOn(v); });
                updated << "keepSteamHeaterOn";
            }
            if (args.contains("steamAutoFlushSeconds")) {
                int v = args["steamAutoFlushSeconds"].toInt();
                addSetter([settings, v]() { settings->brew()->setSteamAutoFlushSeconds(v); });
                updated << "steamAutoFlushSeconds";
            }
            if (args.contains("steamTwoTapStop")) {
                bool v = args["steamTwoTapStop"].toBool();
                auto* hw = settings->hardware();
                addSetter([hw, v]() { hw->setSteamTwoTapStop(v); });
                updated << "steamTwoTapStop";
            }

            // === Hot water ===
            if (args.contains("waterTemperature")) {
                double v = args["waterTemperature"].toDouble();
                addSetter([settings, v]() { settings->brew()->setWaterTemperature(v); });
                updated << "waterTemperature";
            }
            if (args.contains("waterVolume")) {
                int v = args["waterVolume"].toInt();
                addSetter([settings, v]() { settings->brew()->setWaterVolume(v); });
                updated << "waterVolume";
            }
            if (args.contains("waterVolumeMode")) {
                QString v = args["waterVolumeMode"].toString();
                addSetter([settings, v]() { settings->brew()->setWaterVolumeMode(v); });
                updated << "waterVolumeMode";
            }
            if (args.contains("hotWaterFlowRateMlPerSec")) {
                int v = static_cast<int>(args["hotWaterFlowRateMlPerSec"].toDouble() * 10.0);
                auto* hw = settings->hardware();
                addSetter([hw, v]() { hw->setHotWaterFlowRate(v); });
                updated << "hotWaterFlowRateMlPerSec";
            }

            // === Flush ===
            if (args.contains("flushFlowMlPerSec")) {
                double v = args["flushFlowMlPerSec"].toDouble();
                addSetter([settings, v]() { settings->brew()->setFlushFlow(v); });
                updated << "flushFlowMlPerSec";
            }
            if (args.contains("flushSeconds")) {
                double v = args["flushSeconds"].toDouble();
                addSetter([settings, v]() { settings->brew()->setFlushSeconds(v); });
                updated << "flushSeconds";
            }

            // === DYE metadata ===
            if (args.contains("dyeBeanBrand")) {
                QString v = args["dyeBeanBrand"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeBeanBrand(v); });
                updated << "dyeBeanBrand";
            }
            if (args.contains("dyeBeanType")) {
                QString v = args["dyeBeanType"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeBeanType(v); });
                updated << "dyeBeanType";
            }
            if (args.contains("dyeRoastDate")) {
                QString v = args["dyeRoastDate"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeRoastDate(v); });
                updated << "dyeRoastDate";
            }
            if (args.contains("dyeRoastLevel")) {
                QString v = args["dyeRoastLevel"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeRoastLevel(v); });
                updated << "dyeRoastLevel";
            }
            if (args.contains("dyeGrinderBrand")) {
                QString v = args["dyeGrinderBrand"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeGrinderBrand(v); });
                updated << "dyeGrinderBrand";
            }
            if (args.contains("dyeGrinderModel")) {
                QString v = args["dyeGrinderModel"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeGrinderModel(v); });
                updated << "dyeGrinderModel";
            }
            if (args.contains("dyeGrinderBurrs")) {
                QString v = args["dyeGrinderBurrs"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeGrinderBurrs(v); });
                updated << "dyeGrinderBurrs";
            }
            if (args.contains("dyeGrinderSetting")) {
                QString v = args["dyeGrinderSetting"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeGrinderSetting(v); });
                updated << "dyeGrinderSetting";
            }
            if (args.contains("dyeBeanWeight")) {
                double v = args["dyeBeanWeight"].toDouble();
                addSetter([settings, v]() { settings->dye()->setDyeBeanWeight(v); });
                updated << "dyeBeanWeight";
            }
            if (args.contains("dyeDrinkWeight")) {
                double v = args["dyeDrinkWeight"].toDouble();
                addSetter([settings, v]() { settings->dye()->setDyeDrinkWeight(v); });
                updated << "dyeDrinkWeight";
            }
            // dyeDrinkTds/dyeDrinkEy are session-scratch fields (not persisted)
            // — writing them via settings_set is a footgun, since the value
            // gets snapshotted into whatever shot completes next. To patch
            // a saved shot, use shots_update with drinkTds/drinkEy instead.
            if (args.contains("dyeEspressoEnjoyment")) {
                int v = qBound(0, args["dyeEspressoEnjoyment"].toInt(), 100);
                addSetter([settings, v]() { settings->dye()->setDyeEspressoEnjoyment(v); });
                updated << "dyeEspressoEnjoyment";
            }
            if (args.contains("dyeShotNotes")) {
                QString v = args["dyeShotNotes"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeShotNotes(v); });
                updated << "dyeShotNotes";
            }
            if (args.contains("dyeBarista")) {
                QString v = args["dyeBarista"].toString();
                addSetter([settings, v]() { settings->dye()->setDyeBarista(v); });
                updated << "dyeBarista";
            }

            // === Machine ===
            if (args.contains("themeMode")) {
                QString v = args["themeMode"].toString();
                addSetter([settings, v]() { settings->theme()->setThemeMode(v); });
                updated << "themeMode";
            }
            if (args.contains("darkThemeName")) {
                QString v = args["darkThemeName"].toString();
                addSetter([settings, v]() { settings->theme()->setDarkThemeName(v); });
                updated << "darkThemeName";
            }
            if (args.contains("lightThemeName")) {
                QString v = args["lightThemeName"].toString();
                addSetter([settings, v]() { settings->theme()->setLightThemeName(v); });
                updated << "lightThemeName";
            }
            if (args.contains("autoSleepMinutes")) {
                int v = args["autoSleepMinutes"].toInt();
                addSetter([settings, v]() { settings->setValue("autoSleepMinutes", v); });
                updated << "autoSleepMinutes";
            }
            if (args.contains("postShotReviewTimeout")) {
                int v = args["postShotReviewTimeout"].toInt();
                addSetter([settings, v]() { settings->setValue("postShotReviewTimeout", v); });
                updated << "postShotReviewTimeout";
            }
            if (args.contains("refillKitOverride")) {
                int v = args["refillKitOverride"].toInt();
                addSetter([settings, v]() { settings->app()->setRefillKitOverride(v); });
                updated << "refillKitOverride";
            }
            if (args.contains("waterRefillPoint")) {
                int v = args["waterRefillPoint"].toInt();
                addSetter([settings, v]() { settings->app()->setWaterRefillPoint(v); });
                updated << "waterRefillPoint";
            }
            if (args.contains("waterLevelDisplayUnit")) {
                QString v = args["waterLevelDisplayUnit"].toString();
                addSetter([settings, v]() { settings->app()->setWaterLevelDisplayUnit(v); });
                updated << "waterLevelDisplayUnit";
            }
            if (args.contains("useFlowScale")) {
                bool v = args["useFlowScale"].toBool();
                addSetter([settings, v]() { settings->setUseFlowScale(v); });
                updated << "useFlowScale";
            }
            if (args.contains("screenBrightness")) {
                double v = args["screenBrightness"].toDouble();
                addSetter([settings, v]() { settings->theme()->setScreenBrightness(v); });
                updated << "screenBrightness";
            }
            if (args.contains("defaultShotRating")) {
                int v = qBound(0, args["defaultShotRating"].toInt(), 100);
                addSetter([settings, v]() { settings->visualizer()->setDefaultShotRating(v); });
                updated << "defaultShotRating";
            }
            if (args.contains("launcherMode")) {
                bool v = args["launcherMode"].toBool();
                addSetter([settings, v]() { settings->app()->setLauncherMode(v); });
                updated << "launcherMode";
            }
            if (args.contains("flowCalibrationMultiplier")) {
                double v = args["flowCalibrationMultiplier"].toDouble();
                addSetter([settings, v]() { settings->calibration()->setFlowCalibrationMultiplier(v); });
                updated << "flowCalibrationMultiplier";
            }
            if (args.contains("autoFlowCalibration")) {
                bool v = args["autoFlowCalibration"].toBool();
                addSetter([settings, v]() { settings->calibration()->setAutoFlowCalibration(v); });
                updated << "autoFlowCalibration";
            }
            if (args.contains("ignoreVolumeWithScale")) {
                bool v = args["ignoreVolumeWithScale"].toBool();
                addSetter([settings, v]() { settings->brew()->setIgnoreVolumeWithScale(v); });
                updated << "ignoreVolumeWithScale";
            }
            {
                auto* aw = settings->autoWake();
                if (args.contains("autoWakeEnabled")) {
                    bool v = args["autoWakeEnabled"].toBool();
                    addSetter([aw, v]() { aw->setAutoWakeEnabled(v); });
                    updated << "autoWakeEnabled";
                }
                if (args.contains("autoWakeStayAwakeEnabled")) {
                    bool v = args["autoWakeStayAwakeEnabled"].toBool();
                    addSetter([aw, v]() { aw->setAutoWakeStayAwakeEnabled(v); });
                    updated << "autoWakeStayAwakeEnabled";
                }
                if (args.contains("autoWakeStayAwakeMinutes")) {
                    int v = args["autoWakeStayAwakeMinutes"].toInt();
                    addSetter([aw, v]() { aw->setAutoWakeStayAwakeMinutes(v); });
                    updated << "autoWakeStayAwakeMinutes";
                }
            }

            // === Connections ===
            if (args.contains("usbSerialEnabled")) {
                bool v = args["usbSerialEnabled"].toBool();
                addSetter([settings, v]() { settings->setUsbSerialEnabled(v); });
                updated << "usbSerialEnabled";
            }
            if (args.contains("showScaleDialogs")) {
                bool v = args["showScaleDialogs"].toBool();
                addSetter([settings, v]() { settings->setShowScaleDialogs(v); });
                updated << "showScaleDialogs";
            }

            // === Screensaver ===
            if (screensaver) {
                if (args.contains("screensaverType")) {
                    QString v = args["screensaverType"].toString();
                    addSetter([screensaver, v]() { screensaver->setScreensaverType(v); });
                    updated << "screensaverType";
                }
                if (args.contains("dimDelayMinutes")) {
                    int v = args["dimDelayMinutes"].toInt();
                    addSetter([screensaver, v]() { screensaver->setDimDelayMinutes(v); });
                    updated << "dimDelayMinutes";
                }
                if (args.contains("dimPercent")) {
                    int v = args["dimPercent"].toInt();
                    addSetter([screensaver, v]() { screensaver->setDimPercent(v); });
                    updated << "dimPercent";
                }
                if (args.contains("pipesSpeed")) {
                    double v = args["pipesSpeed"].toDouble();
                    addSetter([screensaver, v]() { screensaver->setPipesSpeed(v); });
                    updated << "pipesSpeed";
                }
                if (args.contains("pipesCameraSpeed")) {
                    double v = args["pipesCameraSpeed"].toDouble();
                    addSetter([screensaver, v]() { screensaver->setPipesCameraSpeed(v); });
                    updated << "pipesCameraSpeed";
                }
                if (args.contains("pipesShowClock")) {
                    bool v = args["pipesShowClock"].toBool();
                    addSetter([screensaver, v]() { screensaver->setPipesShowClock(v); });
                    updated << "pipesShowClock";
                }
                if (args.contains("flipClockUse3D")) {
                    bool v = args["flipClockUse3D"].toBool();
                    addSetter([screensaver, v]() { screensaver->setFlipClockUse3D(v); });
                    updated << "flipClockUse3D";
                }
                if (args.contains("videosShowClock")) {
                    bool v = args["videosShowClock"].toBool();
                    addSetter([screensaver, v]() { screensaver->setVideosShowClock(v); });
                    updated << "videosShowClock";
                }
                if (args.contains("cacheEnabled")) {
                    bool v = args["cacheEnabled"].toBool();
                    addSetter([screensaver, v]() { screensaver->setCacheEnabled(v); });
                    updated << "cacheEnabled";
                }
                if (args.contains("attractorShowClock")) {
                    bool v = args["attractorShowClock"].toBool();
                    addSetter([screensaver, v]() { screensaver->setAttractorShowClock(v); });
                    updated << "attractorShowClock";
                }
                if (args.contains("imageDisplayDuration")) {
                    int v = args["imageDisplayDuration"].toInt();
                    addSetter([screensaver, v]() { screensaver->setImageDisplayDuration(v); });
                    updated << "imageDisplayDuration";
                }
                if (args.contains("showDateOnPersonal")) {
                    bool v = args["showDateOnPersonal"].toBool();
                    addSetter([screensaver, v]() { screensaver->setShowDateOnPersonal(v); });
                    updated << "showDateOnPersonal";
                }
                if (args.contains("shotMapShape")) {
                    QString v = args["shotMapShape"].toString();
                    addSetter([screensaver, v]() { screensaver->setShotMapShape(v); });
                    updated << "shotMapShape";
                }
                if (args.contains("shotMapTexture")) {
                    QString v = args["shotMapTexture"].toString();
                    addSetter([screensaver, v]() { screensaver->setShotMapTexture(v); });
                    updated << "shotMapTexture";
                }
                if (args.contains("shotMapShowClock")) {
                    bool v = args["shotMapShowClock"].toBool();
                    addSetter([screensaver, v]() { screensaver->setShotMapShowClock(v); });
                    updated << "shotMapShowClock";
                }
                if (args.contains("shotMapShowProfiles")) {
                    bool v = args["shotMapShowProfiles"].toBool();
                    addSetter([screensaver, v]() { screensaver->setShotMapShowProfiles(v); });
                    updated << "shotMapShowProfiles";
                }
                if (args.contains("shotMapShowTerminator")) {
                    bool v = args["shotMapShowTerminator"].toBool();
                    addSetter([screensaver, v]() { screensaver->setShotMapShowTerminator(v); });
                    updated << "shotMapShowTerminator";
                }
            }

            // === Accessibility ===
            if (accessibility) {
                if (args.contains("accessibilityEnabled")) {
                    bool v = args["accessibilityEnabled"].toBool();
                    addSetter([accessibility, v]() { accessibility->setEnabled(v); });
                    updated << "accessibilityEnabled";
                }
                if (args.contains("ttsEnabled")) {
                    bool v = args["ttsEnabled"].toBool();
                    addSetter([accessibility, v]() { accessibility->setTtsEnabled(v); });
                    updated << "ttsEnabled";
                }
                if (args.contains("tickEnabled")) {
                    bool v = args["tickEnabled"].toBool();
                    addSetter([accessibility, v]() { accessibility->setTickEnabled(v); });
                    updated << "tickEnabled";
                }
                if (args.contains("tickSoundIndex")) {
                    int v = args["tickSoundIndex"].toInt();
                    addSetter([accessibility, v]() { accessibility->setTickSoundIndex(v); });
                    updated << "tickSoundIndex";
                }
                if (args.contains("tickVolume")) {
                    int v = qBound(0, args["tickVolume"].toInt(), 100);
                    addSetter([accessibility, v]() { accessibility->setTickVolume(v); });
                    updated << "tickVolume";
                }
                if (args.contains("extractionAnnouncementsEnabled")) {
                    bool v = args["extractionAnnouncementsEnabled"].toBool();
                    addSetter([accessibility, v]() { accessibility->setExtractionAnnouncementsEnabled(v); });
                    updated << "extractionAnnouncementsEnabled";
                }
                if (args.contains("extractionAnnouncementMode")) {
                    QString v = args["extractionAnnouncementMode"].toString();
                    addSetter([accessibility, v]() { accessibility->setExtractionAnnouncementMode(v); });
                    updated << "extractionAnnouncementMode";
                }
                if (args.contains("extractionAnnouncementInterval")) {
                    int v = args["extractionAnnouncementInterval"].toInt();
                    addSetter([accessibility, v]() { accessibility->setExtractionAnnouncementInterval(v); });
                    updated << "extractionAnnouncementInterval";
                }
            }

            // === AI ===
            {
                auto* a = settings->ai();
                if (args.contains("aiProvider")) {
                    QString v = args["aiProvider"].toString();
                    addSetter([a, v]() { a->setAiProvider(v); });
                    updated << "aiProvider";
                }
                if (args.contains("ollamaEndpoint")) {
                    QString v = args["ollamaEndpoint"].toString();
                    addSetter([a, v]() { a->setOllamaEndpoint(v); });
                    updated << "ollamaEndpoint";
                }
                if (args.contains("ollamaModel")) {
                    QString v = args["ollamaModel"].toString();
                    addSetter([a, v]() { a->setOllamaModel(v); });
                    updated << "ollamaModel";
                }
                if (args.contains("openrouterModel")) {
                    QString v = args["openrouterModel"].toString();
                    addSetter([a, v]() { a->setOpenrouterModel(v); });
                    updated << "openrouterModel";
                }
            }
            if (args.contains("mcpEnabled")) {
                bool v = args["mcpEnabled"].toBool();
                addSetter([settings, v]() { settings->mcp()->setMcpEnabled(v); });
                updated << "mcpEnabled";
            }
            if (args.contains("mcpAccessLevel")) {
                int v = qBound(0, args["mcpAccessLevel"].toInt(), 2);
                addSetter([settings, v]() { settings->mcp()->setMcpAccessLevel(v); });
                updated << "mcpAccessLevel";
            }
            if (args.contains("mcpConfirmationLevel")) {
                int v = qBound(0, args["mcpConfirmationLevel"].toInt(), 2);
                addSetter([settings, v]() { settings->mcp()->setMcpConfirmationLevel(v); });
                updated << "mcpConfirmationLevel";
            }
            if (args.contains("discussShotApp")) {
                int v = qBound(0, args["discussShotApp"].toInt(), settings->network()->discussAppClaudeDesktop());
                addSetter([settings, v]() { settings->network()->setDiscussShotApp(v); });
                updated << "discussShotApp";
            }
            if (args.contains("discussShotCustomUrl")) {
                QString v = args["discussShotCustomUrl"].toString();
                addSetter([settings, v]() { settings->network()->setDiscussShotCustomUrl(v); });
                updated << "discussShotCustomUrl";
            }

            // === MQTT ===
            {
                auto* m = settings->mqtt();
                if (args.contains("mqttEnabled")) {
                    bool v = args["mqttEnabled"].toBool();
                    addSetter([m, v]() { m->setMqttEnabled(v); });
                    updated << "mqttEnabled";
                }
                if (args.contains("mqttBrokerHost")) {
                    QString v = args["mqttBrokerHost"].toString();
                    addSetter([m, v]() { m->setMqttBrokerHost(v); });
                    updated << "mqttBrokerHost";
                }
                if (args.contains("mqttBrokerPort")) {
                    int v = args["mqttBrokerPort"].toInt();
                    addSetter([m, v]() { m->setMqttBrokerPort(v); });
                    updated << "mqttBrokerPort";
                }
                if (args.contains("mqttUsername")) {
                    QString v = args["mqttUsername"].toString();
                    addSetter([m, v]() { m->setMqttUsername(v); });
                    updated << "mqttUsername";
                }
                if (args.contains("mqttBaseTopic")) {
                    QString v = args["mqttBaseTopic"].toString();
                    addSetter([m, v]() { m->setMqttBaseTopic(v); });
                    updated << "mqttBaseTopic";
                }
                if (args.contains("mqttPublishInterval")) {
                    int v = args["mqttPublishInterval"].toInt();
                    addSetter([m, v]() { m->setMqttPublishInterval(v); });
                    updated << "mqttPublishInterval";
                }
                if (args.contains("mqttRetainMessages")) {
                    bool v = args["mqttRetainMessages"].toBool();
                    addSetter([m, v]() { m->setMqttRetainMessages(v); });
                    updated << "mqttRetainMessages";
                }
                if (args.contains("mqttHomeAssistantDiscovery")) {
                    bool v = args["mqttHomeAssistantDiscovery"].toBool();
                    addSetter([m, v]() { m->setMqttHomeAssistantDiscovery(v); });
                    updated << "mqttHomeAssistantDiscovery";
                }
                if (args.contains("mqttClientId")) {
                    QString v = args["mqttClientId"].toString();
                    addSetter([m, v]() { m->setMqttClientId(v); });
                    updated << "mqttClientId";
                }
                // mqttPassword excluded — sensitive
            }

            // === Themes ===
            if (args.contains("activeThemeName")) {
                QString v = args["activeThemeName"].toString();
                addSetter([settings, v]() { settings->theme()->setActiveThemeName(v); });
                updated << "activeThemeName";
            }
            if (args.contains("activeShader")) {
                QString v = args["activeShader"].toString();
                addSetter([settings, v]() { settings->theme()->setActiveShader(v); });
                updated << "activeShader";
            }

            // === Visualizer ===
            if (args.contains("visualizerAutoUpload")) {
                bool v = args["visualizerAutoUpload"].toBool();
                addSetter([settings, v]() { settings->visualizer()->setVisualizerAutoUpload(v); });
                updated << "visualizerAutoUpload";
            }
            if (args.contains("visualizerAutoUpdate")) {
                bool v = args["visualizerAutoUpdate"].toBool();
                addSetter([settings, v]() { settings->visualizer()->setVisualizerAutoUpdate(v); });
                updated << "visualizerAutoUpdate";
            }
            if (args.contains("visualizerMinDuration")) {
                double v = args["visualizerMinDuration"].toDouble();
                addSetter([settings, v]() { settings->visualizer()->setVisualizerMinDuration(v); });
                updated << "visualizerMinDuration";
            }
            if (args.contains("visualizerExtendedMetadata")) {
                bool v = args["visualizerExtendedMetadata"].toBool();
                addSetter([settings, v]() { settings->visualizer()->setVisualizerExtendedMetadata(v); });
                updated << "visualizerExtendedMetadata";
            }
            if (args.contains("visualizerShowAfterShot")) {
                bool v = args["visualizerShowAfterShot"].toBool();
                addSetter([settings, v]() { settings->visualizer()->setVisualizerShowAfterShot(v); });
                updated << "visualizerShowAfterShot";
            }
            if (args.contains("visualizerClearNotesOnStart")) {
                bool v = args["visualizerClearNotesOnStart"].toBool();
                addSetter([settings, v]() { settings->visualizer()->setVisualizerClearNotesOnStart(v); });
                updated << "visualizerClearNotesOnStart";
            }
            // visualizerUsername/Password excluded — sensitive

            // === Update ===
            if (args.contains("autoCheckUpdates")) {
                bool v = args["autoCheckUpdates"].toBool();
                addSetter([settings, v]() { settings->app()->setAutoCheckUpdates(v); });
                updated << "autoCheckUpdates";
            }
            if (args.contains("betaUpdatesEnabled")) {
                bool v = args["betaUpdatesEnabled"].toBool();
                addSetter([settings, v]() { settings->app()->setBetaUpdatesEnabled(v); });
                updated << "betaUpdatesEnabled";
            }

            // === Data ===
            if (args.contains("webSecurityEnabled")) {
                bool v = args["webSecurityEnabled"].toBool();
                addSetter([settings, v]() { settings->network()->setWebSecurityEnabled(v); });
                updated << "webSecurityEnabled";
            }
            if (args.contains("dailyBackupHour")) {
                int v = qBound(0, args["dailyBackupHour"].toInt(), 23);
                addSetter([settings, v]() { settings->app()->setDailyBackupHour(v); });
                updated << "dailyBackupHour";
            }
            if (args.contains("shotServerEnabled")) {
                bool v = args["shotServerEnabled"].toBool();
                addSetter([settings, v]() { settings->network()->setShotServerEnabled(v); });
                updated << "shotServerEnabled";
            }
            if (args.contains("shotServerPort")) {
                int v = args["shotServerPort"].toInt();
                addSetter([settings, v]() { settings->network()->setShotServerPort(v); });
                updated << "shotServerPort";
            }

            // === History ===
            if (args.contains("shotHistorySortField")) {
                QString v = args["shotHistorySortField"].toString();
                addSetter([settings, v]() { settings->network()->setShotHistorySortField(v); });
                updated << "shotHistorySortField";
            }
            if (args.contains("shotHistorySortDirection")) {
                QString v = args["shotHistorySortDirection"].toString();
                addSetter([settings, v]() { settings->network()->setShotHistorySortDirection(v); });
                updated << "shotHistorySortDirection";
            }

            // === Language ===
            if (translation && args.contains("currentLanguage")) {
                QString v = args["currentLanguage"].toString();
                addSetter([translation, v]() { translation->setCurrentLanguage(v); });
                updated << "currentLanguage";
            }

            // === Debug ===
            if (args.contains("simulationMode")) {
                bool v = args["simulationMode"].toBool();
                addSetter([settings, v]() { settings->app()->setSimulationMode(v); });
                updated << "simulationMode";
            }

            // === Battery ===
            if (battery && args.contains("chargingMode")) {
                int v = args["chargingMode"].toInt();
                addSetter([battery, v]() { battery->setChargingMode(v); });
                updated << "chargingMode";
            }

            // === Heater calibration (display units × 10 = internal storage) ===
            {
                auto* hw = settings->hardware();
                if (args.contains("heaterIdleTempC")) {
                    int v = static_cast<int>(args["heaterIdleTempC"].toDouble() * 10.0);
                    addSetter([hw, v]() { hw->setHeaterIdleTemp(v); });
                    updated << "heaterIdleTempC";
                }
                if (args.contains("heaterWarmupFlowMlPerSec")) {
                    int v = static_cast<int>(args["heaterWarmupFlowMlPerSec"].toDouble() * 10.0);
                    addSetter([hw, v]() { hw->setHeaterWarmupFlow(v); });
                    updated << "heaterWarmupFlowMlPerSec";
                }
                if (args.contains("heaterTestFlowMlPerSec")) {
                    int v = static_cast<int>(args["heaterTestFlowMlPerSec"].toDouble() * 10.0);
                    addSetter([hw, v]() { hw->setHeaterTestFlow(v); });
                    updated << "heaterTestFlowMlPerSec";
                }
                if (args.contains("heaterWarmupTimeoutSec")) {
                    int v = static_cast<int>(args["heaterWarmupTimeoutSec"].toDouble() * 10.0);
                    addSetter([hw, v]() { hw->setHeaterWarmupTimeout(v); });
                    updated << "heaterWarmupTimeoutSec";
                }
            }

            // === Auto-favorites ===
            if (args.contains("autoFavoritesGroupBy")) {
                QString v = args["autoFavoritesGroupBy"].toString();
                addSetter([settings, v]() { settings->network()->setAutoFavoritesGroupBy(v); });
                updated << "autoFavoritesGroupBy";
            }
            if (args.contains("autoFavoritesMaxItems")) {
                int v = args["autoFavoritesMaxItems"].toInt();
                addSetter([settings, v]() { settings->network()->setAutoFavoritesMaxItems(v); });
                updated << "autoFavoritesMaxItems";
            }
            if (args.contains("autoFavoritesOpenBrewSettings")) {
                bool v = args["autoFavoritesOpenBrewSettings"].toBool();
                addSetter([settings, v]() { settings->network()->setAutoFavoritesOpenBrewSettings(v); });
                updated << "autoFavoritesOpenBrewSettings";
            }
            if (args.contains("autoFavoritesHideUnrated")) {
                bool v = args["autoFavoritesHideUnrated"].toBool();
                addSetter([settings, v]() { settings->network()->setAutoFavoritesHideUnrated(v); });
                updated << "autoFavoritesHideUnrated";
            }

            if (updated.isEmpty()) {
                respond(QJsonObject{{"error", "No valid settings provided"}});
                return;
            }

            QJsonObject result;
            result["success"] = true;
            result["updated"] = QJsonArray::fromStringList(updated);

            if (setters.isEmpty()) {
                // All changes were synchronous (e.g., profile temperature/weight)
                respond(result);
            } else {
                // Execute all setters on the main thread, then respond
                QMetaObject::invokeMethod(qApp, [setters, respond, result]() {
                    for (const auto& setter : setters) setter();
                    respond(result);
                }, Qt::QueuedConnection);
            }
        },
        "settings");

    // profiles_set_auto_load — pin a profile as the auto-load target. Validated
    // synchronously (filename non-empty, profile exists, profile is in the
    // Selected list); the actual settings write hops to the GUI thread.
    registry->registerAsyncTool(
        "profiles_set_auto_load",
        "Pin a profile as the auto-load target. The pinned profile is reloaded "
        "on app start, DE1 wake-from-sleep, and after `revertMinutes` of "
        "inactivity on the Idle page. Replaces any prior auto-load. The "
        "filename must exist and be in the Selected list.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"filename", QJsonObject{{"type", "string"}, {"description", "Profile filename (without .json extension)"}}},
                {"revertMinutes", QJsonObject{{"type", "integer"}, {"description", "Optional. Minutes of idle inactivity on the Idle page before reverting to the auto-load profile. Range 0..60 (clamped); 0 disables the idle trigger but keeps the startup and wake-from-sleep triggers."}}}
            }},
            {"required", QJsonArray{"filename"}}
        },
        [profileManager, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!settings || !profileManager) {
                respond(QJsonObject{{"error", "Settings or ProfileManager not available"}});
                return;
            }
            const QString filename = args["filename"].toString();
            if (filename.isEmpty()) {
                respond(QJsonObject{{"error", "filename is required"}});
                return;
            }
            if (!profileManager->profileExists(filename)) {
                respond(QJsonObject{{"error", "Profile not found: " + filename}});
                return;
            }
            if (!profileManager->isProfileInSelectedList(filename)) {
                respond(QJsonObject{{"error", "Profile is not in the Selected list"}});
                return;
            }

            const bool hasRevert = args.contains("revertMinutes");
            const int revertMinutes = hasRevert ? args["revertMinutes"].toInt() : -1;

            QMetaObject::invokeMethod(qApp, [settings, profileManager, filename, hasRevert, revertMinutes, respond]() {
                settings->app()->setAutoLoadProfileFilename(filename);
                if (hasRevert) {
                    settings->app()->setAutoLoadRevertMinutes(revertMinutes);
                }
                QJsonObject result;
                result["success"] = true;
                result["filename"] = filename;
                result["revertMinutes"] = settings->app()->autoLoadRevertMinutes();
                QVariantMap profile = profileManager->getProfileByFilename(filename);
                if (!profile.isEmpty()) {
                    result["title"] = profile["title"].toString();
                }
                respond(result);
            }, Qt::QueuedConnection);
        },
        "settings");

    // profiles_clear_auto_load — disable auto-load without modifying the
    // revert-timeout setting (so enabling auto-load later preserves the
    // configured value).
    registry->registerAsyncTool(
        "profiles_clear_auto_load",
        "Disable auto-load by clearing the pinned filename. Does not modify "
        "`revertMinutes` — the configured timeout is preserved across "
        "enable/disable cycles.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [settings](const QJsonObject&, std::function<void(QJsonObject)> respond) {
            if (!settings) {
                respond(QJsonObject{{"error", "Settings not available"}});
                return;
            }
            QMetaObject::invokeMethod(qApp, [settings, respond]() {
                settings->app()->setAutoLoadProfileFilename("");
                respond(QJsonObject{{"success", true}});
            }, Qt::QueuedConnection);
        },
        "settings");

    // --- Coffee bag tools (bean-bag-inventory) ---

    // Shared bag -> MCP JSON shape: units in field names, ISO dates as-is,
    // Bean Base snapshot parsed into an object (matching shots_get_detail).
    auto bagToJson = [settings](const CoffeeBag& bag) {
        QJsonObject obj;
        obj["bagId"] = bag.id;
        obj["roasterName"] = bag.roasterName;
        obj["coffeeName"] = bag.coffeeName;
        if (!bag.roastDate.isEmpty()) obj["roastDate"] = bag.roastDate;
        if (!bag.roastLevel.isEmpty()) obj["roastLevel"] = bag.roastLevel;
        if (!bag.frozenDate.isEmpty()) obj["frozenDate"] = bag.frozenDate;
        if (!bag.defrostDate.isEmpty()) obj["defrostDate"] = bag.defrostDate;
        if (!bag.notes.isEmpty()) obj["notes"] = bag.notes;
        obj["inInventory"] = bag.inInventory;
        if (!bag.grinderBrand.isEmpty()) obj["grinderBrand"] = bag.grinderBrand;
        if (!bag.grinderModel.isEmpty()) obj["grinderModel"] = bag.grinderModel;
        if (!bag.grinderBurrs.isEmpty()) obj["grinderBurrs"] = bag.grinderBurrs;
        if (!bag.grinderSetting.isEmpty()) obj["grinderSetting"] = bag.grinderSetting;
        if (bag.doseWeightG > 0) obj["doseWeightG"] = bag.doseWeightG;
        if (bag.yieldOverrideG > 0) obj["yieldOverrideG"] = bag.yieldOverrideG;
        if (!bag.beanBaseData.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(bag.beanBaseData.toUtf8());
            if (doc.isObject())
                obj["beanBase"] = doc.object();
        }
        obj["isActive"] = settings && settings->dye()->activeBagId() == bag.id;
        return obj;
    };

    // bag_list — read the coffee bag inventory.
    registry->registerAsyncTool(
        "bag_list",
        "List the user's coffee bags. By default only bags in inventory (open bags currently in "
        "use); pass includeEmpty=true to also list bags marked empty. The bag with isActive=true "
        "is what the next shot will be pulled with.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"includeEmpty", QJsonObject{{"type", "boolean"},
                    {"description", "Also include bags no longer in inventory (marked empty)"}}}
            }}
        },
        [shotHistory, bagToJson](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const bool includeEmpty = args["includeEmpty"].toBool();
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([dbPath, includeEmpty, bagToJson, respond]() {
                QJsonArray bags;
                const bool opened = withTempDb(dbPath, "mcp_bags", [&](QSqlDatabase& db) {
                    QSqlQuery query(db);
                    const QString sql = includeEmpty
                        ? QStringLiteral("SELECT id FROM coffee_bags ORDER BY in_inventory DESC, last_used DESC, id DESC")
                        : QStringLiteral("SELECT id FROM coffee_bags WHERE in_inventory = 1 ORDER BY last_used DESC, id DESC");
                    if (!query.exec(sql))
                        return;
                    while (query.next()) {
                        const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, query.value(0).toLongLong());
                        if (bag.isValid())
                            bags.append(bagToJson(bag));
                    }
                });
                QMetaObject::invokeMethod(qApp, [opened, bags, respond]() {
                    if (!opened) {
                        respond(QJsonObject{{"error", "Could not open shot database"}});
                        return;
                    }
                    respond(QJsonObject{{"bags", bags}, {"count", bags.size()}});
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");

    // bag_update — edit a bag's metadata / lifecycle.
    registry->registerAsyncTool(
        "bag_update",
        "Update fields on a coffee bag (metadata and freeze lifecycle). Only provided fields "
        "change. Pass an empty string to clear a text/date field. Setting inInventory=false marks "
        "the bag empty (removes it from the inventory view; shots keep their snapshots). "
        "Setting defrostDate records a thaw (the latest portion leaving the freezer).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"bagId", QJsonObject{{"type", "integer"}, {"description", "Bag ID (from bag_list)"}}},
                {"roasterName", QJsonObject{{"type", "string"}}},
                {"coffeeName", QJsonObject{{"type", "string"}}},
                {"roastDate", QJsonObject{{"type", "string"}, {"description", "YYYY-MM-DD, '' to clear"}}},
                {"roastLevel", QJsonObject{{"type", "string"}}},
                {"frozenDate", QJsonObject{{"type", "string"}, {"description", "YYYY-MM-DD, '' to clear"}}},
                {"defrostDate", QJsonObject{{"type", "string"}, {"description", "YYYY-MM-DD, '' to clear"}}},
                {"notes", QJsonObject{{"type", "string"}}},
                {"grinderBrand", QJsonObject{{"type", "string"}}},
                {"grinderModel", QJsonObject{{"type", "string"}}},
                {"grinderBurrs", QJsonObject{{"type", "string"}}},
                {"grinderSetting", QJsonObject{{"type", "string"}}},
                {"doseWeightG", QJsonObject{{"type", "number"}}},
                {"yieldOverrideG", QJsonObject{{"type", "number"}}},
                {"inInventory", QJsonObject{{"type", "boolean"}, {"description", "false = mark the bag empty"}}}
            }},
            {"required", QJsonArray{"bagId"}}
        },
        [shotHistory, bagToJson, bagStorage](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const qint64 bagId = args["bagId"].toInteger();
            if (!bagIdIsSet(bagId)) {
                respond(QJsonObject{{"error", "Valid bagId is required"}});
                return;
            }
            QVariantMap fields;
            static const QStringList kEditable = {
                "roasterName", "coffeeName", "roastDate", "roastLevel",
                "frozenDate", "defrostDate", "notes",
                "grinderBrand", "grinderModel", "grinderBurrs", "grinderSetting",
                "doseWeightG", "yieldOverrideG", "inInventory"};
            for (const QString& key : kEditable) {
                if (args.contains(key))
                    fields.insert(key, args[key].toVariant());
            }
            if (fields.isEmpty()) {
                respond(QJsonObject{{"error", "No fields to update"}});
                return;
            }
            const QString dbPath = shotHistory->databasePath();

            // Load the just-updated bag on a background thread and respond.
            auto respondWithBag = [dbPath, bagId, bagToJson, respond]() {
                QThread* t = QThread::create([dbPath, bagId, bagToJson, respond]() {
                    CoffeeBag updated;
                    withTempDb(dbPath, "mcp_bagupd_read", [&](QSqlDatabase& db) {
                        updated = CoffeeBagStorage::loadBagStatic(db, bagId);
                    });
                    QMetaObject::invokeMethod(qApp, [updated, bagToJson, bagId, respond]() {
                        // The update succeeded, but the read-back can come up empty
                        // if the row was deleted (or the read failed) in between —
                        // surface that rather than reporting a hollow bag as success.
                        if (!updated.isValid()) {
                            respond(QJsonObject{{"error", "Bag " + QString::number(bagId)
                                + " updated but reload failed (it may have been deleted)"}});
                            return;
                        }
                        respond(QJsonObject{{"success", true}, {"bag", bagToJson(updated)}});
                    }, Qt::QueuedConnection);
                });
                QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
                t->start();
            };

            if (bagStorage) {
                // Route through the storage INSTANCE so the write fires
                // bagsChanged (open inventory views refresh), bagVisualizer-
                // FieldsChanged (push the edit to the synced Visualizer bag),
                // and the active-bag dye refresh via SettingsDye's bagUpdated
                // handler. That refresh is a no-op re-apply, so it does NOT
                // reset the user's brew overrides — unlike the old
                // setActiveBagId(-1) toggle, which fired clearBrewOverrides.
                QMetaObject::invokeMethod(qApp, [bagStorage, bagId, fields, respondWithBag, respond]() {
                    auto conn = std::make_shared<QMetaObject::Connection>();
                    *conn = QObject::connect(bagStorage, &CoffeeBagStorage::bagUpdated, bagStorage,
                        [conn, bagId, respondWithBag, respond](qint64 updatedId, bool success) {
                            if (updatedId != bagId)
                                return;  // a concurrent update of a different bag
                            QObject::disconnect(*conn);
                            if (!success) {
                                respond(QJsonObject{{"error", "Bag not found or update failed: "
                                                              + QString::number(bagId)}});
                                return;
                            }
                            respondWithBag();
                        });
                    bagStorage->requestUpdateBag(bagId, fields);
                }, Qt::QueuedConnection);
                return;
            }

            // Fallback (no storage instance — e.g. headless tests): direct
            // static write. Skips the in-app refresh/sync signals.
            QThread* thread = QThread::create([dbPath, bagId, fields, respondWithBag, respond]() {
                bool success = false;
                withTempDb(dbPath, "mcp_bagupd", [&](QSqlDatabase& db) {
                    success = CoffeeBagStorage::updateBagFieldsStatic(db, bagId, fields);
                });
                if (success) {
                    respondWithBag();
                } else {
                    QMetaObject::invokeMethod(qApp, [bagId, respond]() {
                        respond(QJsonObject{{"error", "Bag not found or update failed: "
                                                      + QString::number(bagId)}});
                    }, Qt::QueuedConnection);
                }
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "settings");

    // bag_select — set the active bag (what the next shot is pulled with).
    registry->registerAsyncTool(
        "bag_select",
        "Select the active coffee bag — the bag the next shot will be pulled with. Applies the "
        "bag's bean identity and last-used grinder/dose to the next-shot setup. Pass bagId 0 to "
        "clear the selection (no beans selected).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"bagId", QJsonObject{{"type", "integer"},
                    {"description", "Bag ID from bag_list, or 0 to clear the selection"}}}
            }},
            {"required", QJsonArray{"bagId"}}
        },
        [shotHistory, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!settings) {
                respond(QJsonObject{{"error", "Settings not available"}});
                return;
            }
            const qint64 bagId = args["bagId"].toInteger();
            if (!bagIdIsSet(bagId)) {
                QMetaObject::invokeMethod(qApp, [settings, respond]() {
                    settings->dye()->setActiveBagId(-1);
                    respond(QJsonObject{{"success", true}, {"message", "Bean selection cleared"}});
                }, Qt::QueuedConnection);
                return;
            }
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            // Validate the bag exists before selecting it.
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([dbPath, bagId, settings, respond]() {
                CoffeeBag bag;
                withTempDb(dbPath, "mcp_bagsel", [&](QSqlDatabase& db) {
                    bag = CoffeeBagStorage::loadBagStatic(db, bagId);
                });
                QMetaObject::invokeMethod(qApp, [bag, bagId, settings, respond]() {
                    if (!bag.isValid()) {
                        respond(QJsonObject{{"error", "Bag not found: " + QString::number(bagId)}});
                        return;
                    }
                    settings->dye()->setActiveBagId(static_cast<int>(bagId));
                    respond(QJsonObject{{"success", true},
                                        {"message", QString("Active bag: %1 %2")
                                            .arg(bag.roasterName, bag.coffeeName).trimmed()}});
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "control");

    // ----- Equipment packages (add-equipment-packages) -----

    // Flatten an EquipmentPackageView into an MCP-friendly object (units in
    // field names, no raw timestamps, isActive marks the selected package).
    auto packageToJson = [](const EquipmentPackageView& v, qint64 activeId) {
        QJsonObject o;
        o["id"] = v.package.id;
        if (!v.package.name.isEmpty()) o["name"] = v.package.name;
        o["grinderBrand"] = v.grinder.brand;
        o["grinderModel"] = v.grinder.model;
        if (!v.grinder.burrs.isEmpty()) o["grinderBurrs"] = v.grinder.burrs;
        o["rpmAdjustable"] = v.grinder.rpmCapable;
        // Basket identity + registry-derived specs (add-basket-equipment). Emitted
        // only when the package has a basket; specs only when it matches the
        // registry (a custom basket carries identity alone). Units/strings follow
        // MCP conventions (doseRangeG, human-readable wallProfile/relativeFlow).
        if (!v.basket.brand.isEmpty() || !v.basket.model.isEmpty()) {
            QJsonObject basket;
            basket["brand"] = v.basket.brand;
            basket["model"] = v.basket.model;
            if (const BasketAliases::BasketEntry* e =
                    BasketAliases::findEntry(v.basket.brand, v.basket.model)) {
                basket["wallProfile"] = BasketAliases::wallProfileName(e->wall);
                basket["relativeFlow"] = BasketAliases::flowRateName(e->flow);
                basket["precision"] = e->precision;
                if (e->doseMaxG > 0)
                    basket["doseRangeG"] = QJsonObject{{"min", e->doseMinG}, {"max", e->doseMaxG}};
            }
            o["basket"] = basket;
        }
        o["inInventory"] = v.package.inInventory;
        if (!v.package.lastGrindSetting.isEmpty()) o["lastGrindSetting"] = v.package.lastGrindSetting;
        if (v.package.lastRpm > 0) o["lastRpm"] = v.package.lastRpm;
        o["shotCount"] = v.shotCount;
        o["isActive"] = (v.package.id == activeId);
        return o;
    };

    // equipment_list — read the equipment package inventory.
    registry->registerAsyncTool(
        "equipment_list",
        "List the user's equipment packages (the grinder the next shot is ground on). The package "
        "with isActive=true is the active bag's equipment. rpmAdjustable indicates whether the "
        "grinder exposes an rpm dial-in.",
        QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
        [shotHistory, settings, packageToJson](const QJsonObject&, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const qint64 activeId = settings ? settings->dye()->activeEquipmentId() : -1;
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([dbPath, activeId, packageToJson, respond]() {
                QJsonArray packages;
                const bool opened = withTempDb(dbPath, "mcp_equip", [&](QSqlDatabase& db) {
                    for (const EquipmentPackageView& v : EquipmentStorage::loadInventoryStatic(db))
                        packages.append(packageToJson(v, activeId));
                });
                QMetaObject::invokeMethod(qApp, [opened, packages, respond]() {
                    if (!opened) { respond(QJsonObject{{"error", "Could not open shot database"}}); return; }
                    respond(QJsonObject{{"packages", packages}, {"count", packages.size()}});
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "read");

    // equipment_update — edit a package's grinder identity (or create one).
    registry->registerAsyncTool(
        "equipment_update",
        "Update an equipment package's grinder identity (brand/model/burrs), optional basket identity "
        "(basketBrand/basketModel — pass both empty to remove the basket), and optional name. Only "
        "provided fields change. rpmAdjustable is re-derived from the grinder registry; basket specs "
        "are derived from the basket registry. Identity (grinder AND basket) is copy-on-write: editing "
        "a package that has shots forks a NEW package (the old one is retired and kept for its history) "
        "and an unused package is edited in place — so the returned package.id may differ from the "
        "input packageId. An edit matching an existing package merges into it.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"packageId", QJsonObject{{"type", "integer"}, {"description", "Package ID (from equipment_list)"}}},
                {"name", QJsonObject{{"type", "string"}}},
                {"grinderBrand", QJsonObject{{"type", "string"}}},
                {"grinderModel", QJsonObject{{"type", "string"}}},
                {"grinderBurrs", QJsonObject{{"type", "string"}}},
                {"basketBrand", QJsonObject{{"type", "string"}}},
                {"basketModel", QJsonObject{{"type", "string"}}}
            }},
            {"required", QJsonArray{"packageId"}}
        },
        [shotHistory, settings, packageToJson](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const qint64 packageId = args["packageId"].toInteger();
            if (packageId <= 0) { respond(QJsonObject{{"error", "Valid packageId is required"}}); return; }
            const bool touchesGrinder = args.contains("grinderBrand") || args.contains("grinderModel")
                || args.contains("grinderBurrs");
            const bool touchesBasket = args.contains("basketBrand") || args.contains("basketModel");
            QVariantMap pkgFields;
            if (args.contains("name")) pkgFields.insert("name", args["name"].toString());
            const QString brand = args["grinderBrand"].toString();
            const QString model = args["grinderModel"].toString();
            const QString burrs = args["grinderBurrs"].toString();
            const bool haveBrand = args.contains("grinderBrand");
            const bool haveModel = args.contains("grinderModel");
            const bool haveBurrs = args.contains("grinderBurrs");
            const QString basketBrand = args["basketBrand"].toString();
            const QString basketModel = args["basketModel"].toString();
            const bool haveBasketBrand = args.contains("basketBrand");
            const bool haveBasketModel = args.contains("basketModel");
            const qint64 activeId = settings ? settings->dye()->activeEquipmentId() : -1;
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([=]() {
                bool ok = false;
                qint64 resultId = packageId;
                EquipmentPackageView view;
                withTempDb(dbPath, "mcp_equip_upd", [&](QSqlDatabase& db) {
                    if (touchesGrinder || touchesBasket) {
                        // Copy-on-write/merge over the full (grinder + basket)
                        // identity; an untouched side defaults from the current
                        // items so it is preserved. May yield a new id.
                        const EquipmentItem cur = EquipmentStorage::loadGrinderItemStatic(db, packageId);
                        const EquipmentItem curBasket = EquipmentStorage::loadBasketItemStatic(db, packageId);
                        resultId = EquipmentStorage::supersedeOrEditStatic(db, packageId,
                                haveBrand ? brand : cur.brand,
                                haveModel ? model : cur.model,
                                haveBurrs ? burrs : cur.burrs,
                                haveBasketBrand ? basketBrand : curBasket.brand,
                                haveBasketModel ? basketModel : curBasket.model);
                        ok = (resultId > 0);
                    }
                    if (!pkgFields.isEmpty())
                        ok = EquipmentStorage::updatePackageFieldsStatic(db, resultId, pkgFields) || ok;
                    view.package = EquipmentStorage::loadPackageStatic(db, resultId);
                    view.grinder = EquipmentStorage::loadGrinderItemStatic(db, resultId);
                    view.basket = EquipmentStorage::loadBasketItemStatic(db, resultId);
                });
                QMetaObject::invokeMethod(qApp, [ok, view, activeId, packageId, packageToJson, respond]() {
                    if (!ok || !view.package.isValid()) {
                        respond(QJsonObject{{"error", "Package not found or update failed: "
                                                      + QString::number(packageId)}});
                        return;
                    }
                    respond(QJsonObject{{"success", true}, {"package", packageToJson(view, activeId)}});
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "settings");

    // equipment_select — set the active bag's equipment package.
    registry->registerAsyncTool(
        "equipment_select",
        "Select the active equipment package — the grinder the next shot is ground on. Applies the "
        "package's grinder identity and its last grind setting + rpm to the next-shot setup, and "
        "points the active bag at it.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"packageId", QJsonObject{{"type", "integer"}, {"description", "Package ID from equipment_list"}}}
            }},
            {"required", QJsonArray{"packageId"}}
        },
        [shotHistory, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!settings || !shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const qint64 packageId = args["packageId"].toInteger();
            if (packageId <= 0) { respond(QJsonObject{{"error", "Valid packageId is required"}}); return; }
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([dbPath, packageId, settings, respond]() {
                EquipmentPackageView view;
                withTempDb(dbPath, "mcp_equip_sel", [&](QSqlDatabase& db) {
                    view.package = EquipmentStorage::loadPackageStatic(db, packageId);
                    view.grinder = EquipmentStorage::loadGrinderItemStatic(db, packageId);
                    view.basket = EquipmentStorage::loadBasketItemStatic(db, packageId);
                });
                const bool found = view.package.isValid();
                const QVariantMap pkgMap = view.toVariantMap();
                QMetaObject::invokeMethod(qApp, [found, pkgMap, packageId, settings, respond]() {
                    if (!found) {
                        respond(QJsonObject{{"error", "Package not found: " + QString::number(packageId)}});
                        return;
                    }
                    settings->dye()->switchToEquipment(pkgMap);
                    respond(QJsonObject{{"success", true},
                        {"message", QString("Active equipment: %1 %2")
                            .arg(pkgMap.value("grinderBrand").toString(),
                                 pkgMap.value("grinderModel").toString()).trimmed()}});
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        },
        "control");

}
