#include "core/settings_app.h"
#include "mcpserver.h"
#include "mcptoolregistry.h"
#include "mcplogging.h"
#include "../history/shothistorystorage.h"
#include "../history/shotprojection.h"
#include "../history/recipestorage.h"
#include "../history/coffeebagstorage.h"
#include "../history/equipmentstorage.h"
#include "../core/basketaliases.h"
#include "../core/puckprep.h"
#include "../core/yieldspec.h"
#include "../history/bagid.h"
#include "../network/beanbase_blob.h"
#include "../network/beanbaseclient.h"
#include "../network/visualizeruploader.h"
#include "../controllers/profilemanager.h"
#include "../ai/aimanager.h"
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
#include <QPointer>
#include <functional>
#include <limits>
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
                        BatteryManager* battery,
                        AIManager* aiManager,
                        BeanBaseClient* beanbase)
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
                {"rpm", QJsonObject{{"type", "integer"}, {"description", "Grinder motor RPM (variable-RPM grinders); the second half of the dial-in alongside grinderSetting"}}},
                {"barista", QJsonObject{{"type", "string"}, {"description", "Barista name"}}},
                {"beverageType", QJsonObject{{"type", "string"}, {"description", "Beverage type, e.g. 'espresso'. Saved locally only — it does not propagate to visualizer.coffee"}}},
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
            // RPM half of the dial-in; updateShotMetadataStatic already maps {"rpm","rpm"}.
            if (args.contains("rpm"))
                metadata["rpm"] = args["rpm"].toInt();
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
                // Checked, so a database that will not open is not reported as a
                // bad shot id. The old message told the model to "check the id
                // with shots_list" — against a database shots_list cannot reach
                // either, so the advice could only send it in a circle.
                const bool opened = withTempDb(dbPath, "mcp_update", [&](QSqlDatabase& db) {
                    ok = ShotHistoryStorage::updateShotMetadataStatic(db, shotId, metadata);
                    if (ok) {
                        QSqlQuery idQuery(db);
                        idQuery.prepare("SELECT visualizer_id FROM shots WHERE id = :id");
                        idQuery.bindValue(":id", shotId);
                        if (idQuery.exec()) {
                            if (idQuery.next())
                                visualizerId = idQuery.value(0).toString();
                        } else {
                            MCP_WARN_TAGGED("shots_update",
                                            QStringLiteral("failed to query visualizer_id for "
                                                           "shot %1: %2")
                                                .arg(shotId).arg(idQuery.lastError().text()));
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
                } else if (!opened) {
                    result["error"] = "Shot " + QString::number(shotId) + " was not updated — "
                                      "the shot database could not be opened.";
                } else {
                    // The statement ran against an open database and changed
                    // nothing, or failed outright. Only here is "check the id"
                    // useful advice.
                    result["error"] = "Shot " + QString::number(shotId) + " was not updated — "
                                      "no shot with that id, or the write failed. "
                                      "Check the id with shots_list.";
                }

                QMetaObject::invokeMethod(qApp, [respond, result, shotHistory, shotId, ok,
                                                  visualizerId, vizShot, vizOverrides, settings, visualizerUploader]() mutable {
                    if (ok) {
                        // Same notification the in-process edit path emits: this writes
                        // shot metadata directly, so nothing else would tell a
                        // history-derived binding its answer may have moved.
                        emit shotHistory->historyDataChanged();
                        emit shotHistory->shotMetadataUpdated(shotId, true);
                    }

                    bool willAutoUpdate = false;
                    QString skipReason;
                    if (ok && visualizerUploader && !visualizerId.isEmpty()
                            && settings && settings->visualizer()->visualizerAutoUpdate()) {
                        if (vizShot.isValid()) {
                            willAutoUpdate = true;
                            MCP_INFO_TAGGED("shots_update",
                                            QStringLiteral("auto-updating visualizer shot %1 for "
                                                           "local shot id %2")
                                                .arg(visualizerId).arg(shotId));
                            visualizerUploader->updateShotOnVisualizerWithOverrides(
                                visualizerId, QVariant::fromValue(vizShot), vizOverrides);
                        } else {
                            skipReason = QString("failed to reload shot %1 for visualizer PATCH").arg(shotId);
                            MCP_WARN_TAGGED("shots_update", skipReason);
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
        "control", McpTierCore);

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
                        if (Profile::isMaintenanceBeverageType(beverageType)) {
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
        "control", McpTierNiche);

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

            // Respond on the delete's TERMINAL outcome, whichever it is. This used
            // to wait on `shotDeleted`, which fires only on success — a failed or
            // no-such-shot delete left the client waiting forever, with no error
            // and no timeout anywhere in the deferred path.
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = QObject::connect(shotHistory, &ShotHistoryStorage::shotDeleteFinished,
                shotHistory, [respond, shotId, conn](qint64 finishedId, bool success,
                                                     const QString& reason) {
                    if (finishedId != shotId) return;
                    QObject::disconnect(*conn);
                    if (!success) {
                        respond(QJsonObject{{"error", "Shot " + QString::number(shotId)
                                                      + " was not deleted — " + reason}});
                        return;
                    }
                    respond(QJsonObject{{"success", true}, {"message", "Shot " + QString::number(shotId) + " deleted"}});
                });

            shotHistory->requestDeleteShot(shotId);
        },
        "settings", McpTierCore);

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
                // loadProfile refuses a profile it cannot read and KEEPS the
                // previously active one. Reporting success there told the model
                // the machine had switched while it went on brewing the old
                // profile. The `profileExists` check above cannot see this: the
                // file is present, it just does not parse.
                if (!profileManager->loadProfile(filename)) {
                    // Deliberately does NOT say which profile is active now.
                    // loadProfile returns false for two outcomes with opposite
                    // side effects — refused-as-unreadable keeps the previous
                    // profile, not-found loads the DEFAULT — and this tool cannot
                    // tell them apart from a bool. Claiming "the previous profile
                    // is still loaded" would be a confident lie half the time,
                    // which is the failure class this whole change exists to
                    // remove. profiles_get_active answers what is loaded now.
                    respond(QJsonObject{{"error", "Profile not activated: " + filename
                                                  + " — it could not be loaded. Call "
                                                    "profiles_get_active to see which profile "
                                                    "is active now."}});
                    return;
                }
                respond(QJsonObject{{"success", true}, {"message", "Profile activated: " + filename}});
            }, Qt::QueuedConnection);
        },
        "settings", McpTierCore);

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
                {"keepWarmWhenIdle", QJsonObject{{"type", "boolean"}, {"description", "Keep the steam heater warm while the machine is idle"}}},
                {"letRecipeDecide", QJsonObject{{"type", "boolean"}, {"description", "Let the active recipe's pitcher decide the steam heater"}}},
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
                {"dyeGrinderRpm", QJsonObject{{"type", "integer"}, {"description", "Grinder motor RPM (variable-RPM grinders); the second half of the dial-in alongside dyeGrinderSetting"}}},
                {"dyeBeanWeight", QJsonObject{{"type", "number"}, {"description", "Dose weight in grams"}}},
                {"dyeDrinkWeight", QJsonObject{{"type", "number"}, {"description", "Drink weight in grams"}}},
                {"dyeDrinkTds", QJsonObject{{"type", "number"}, {"description", "TDS measurement"}}},
                {"dyeDrinkEy", QJsonObject{{"type", "number"}, {"description", "Extraction yield percentage"}}},
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
                {"aiModel", QJsonObject{{"type", "string"}, {"description", "Model id for the active provider; valid ids in settings_get 'aiAvailableModels'"}}},
                {"mcpEnabled", QJsonObject{{"type", "boolean"}, {"description", "Enable MCP server"}}},
                {"mcpAccessLevel", QJsonObject{{"type", "integer"}, {"description", "MCP access level: 0=monitor, 1=control, 2=full"}}},
                {"mcpConfirmationLevel", QJsonObject{{"type", "integer"}, {"description", "MCP confirmation: 0=none, 1=dangerous, 2=all"}}},
                {"discussShotApp", QJsonObject{{"type", "integer"}, {"description", "Discuss Shot app: 0 Claude, 1 Claude Web, 2 ChatGPT, 3 Gemini, 4 Grok, 5 Custom, 6 None, 7 Claude Desktop"}}},
                {"discussShotCustomUrl", QJsonObject{{"type", "string"}, {"description", "Custom URL for Discuss Shot"}}},
                {"ollamaEndpoint", QJsonObject{{"type", "string"}, {"description", "Ollama endpoint URL"}}},
                {"ollamaModel", QJsonObject{{"type", "string"}, {"description", "Ollama model name"}}},
                {"openaiEndpoint", QJsonObject{{"type", "string"}, {"description", "Custom OpenAI-compatible endpoint URL (empty for default)"}}},
                {"anthropicEndpoint", QJsonObject{{"type", "string"}, {"description", "Custom Anthropic-compatible endpoint URL (empty for default)"}}},
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
                {"simulationMode", QJsonObject{{"type", "boolean"}, {"description",
                    "Enable DE1 simulator. Rejected on builds without a simulator - check "
                    "simulatorAvailable from settings_get first."}}},
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
        "Update any app setting on the device — grind size (dyeGrinderSetting), dose (dyeBeanWeight), "
        "yield (targetWeight), brew temperature (espressoTemperature) and every other setting "
        "across all settings tabs. API keys and passwords are excluded. Temperature and weight "
        "changes to the active profile are applied to the profile automatically. Only call this "
        "when the user explicitly asks to change something; for advice, answer in chat. Coverage "
        "list: get_agent_file topic \"settings_set\".",
        settingsSetSchema,
        [profileManager, settings, accessibility, screensaver, translation, battery, aiManager, validSettingsKeys](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
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

            // Validate aiModel against the target provider's catalog BEFORE any
            // mutation (some setters below apply immediately). The model applies
            // to the provider being set in this same call if present, else the
            // currently active one. Reused by the AI setter block below.
            QString aiModelTargetProvider;
            if (args.contains("aiModel")) {
                const QString modelId = args["aiModel"].toString();
                aiModelTargetProvider = args.contains("aiProvider")
                    ? args["aiProvider"].toString()
                    : settings->ai()->aiProvider();
                if (!aiManager) {
                    respond(QJsonObject{{"error", "AI manager unavailable; cannot set aiModel"}});
                    return;
                }
                const QVariantList catalog = aiManager->availableModels(aiModelTargetProvider);
                if (catalog.isEmpty()) {
                    // Only providers with a model catalog (Gemini today) accept
                    // aiModel. OpenRouter/Ollama have their own free-text model
                    // field; OpenAI/Anthropic are single fixed-model.
                    QString hint;
                    if (aiModelTargetProvider == QStringLiteral("openrouter"))
                        hint = QStringLiteral(" Set openrouterModel instead.");
                    else if (aiModelTargetProvider == QStringLiteral("ollama"))
                        hint = QStringLiteral(" Set ollamaModel instead.");
                    else
                        hint = QStringLiteral(" This provider uses a single fixed model; aiModel cannot be set for it.");
                    respond(QJsonObject{{"error", QString("Provider '%1' has no selectable models via aiModel.%2").arg(aiModelTargetProvider, hint)}});
                    return;
                }
                QStringList validIds;
                for (const QVariant& m : catalog)
                    validIds << m.toMap().value("id").toString();
                if (!validIds.contains(modelId)) {
                    respond(QJsonObject{
                        {"error", QString("Invalid aiModel '%1' for provider '%2'").arg(modelId, aiModelTargetProvider)},
                        {"validModels", QJsonArray::fromStringList(validIds)}
                    });
                    return;
                }
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
            if (args.contains("keepWarmWhenIdle")) {
                bool v = args["keepWarmWhenIdle"].toBool();
                addSetter([settings, v]() { settings->brew()->setKeepWarmWhenIdle(v); });
                updated << "keepWarmWhenIdle";
            }
            if (args.contains("letRecipeDecide")) {
                bool v = args["letRecipeDecide"].toBool();
                addSetter([settings, v]() { settings->brew()->setLetRecipeDecide(v); });
                updated << "letRecipeDecide";
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
            if (args.contains("dyeGrinderRpm")) {
                int v = args["dyeGrinderRpm"].toInt();
                addSetter([settings, v]() { settings->dye()->setDyeGrinderRpm(v); });
                updated << "dyeGrinderRpm";
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
            // There is no enjoyment key for the same reason, made permanent —
            // see settings_dye.h. Rate a shot with shots_update
            // enjoyment0to100.
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
                if (args.contains("aiModel")) {
                    // Validated above; aiModelTargetProvider is resolved there.
                    const QString v = args["aiModel"].toString();
                    const QString provider = aiModelTargetProvider;
                    addSetter([a, provider, v]() { a->setProviderModel(provider, v); });
                    updated << "aiModel";
                }
                if (args.contains("ollamaEndpoint")) {
                    QString v = args["ollamaEndpoint"].toString();
                    addSetter([a, v]() { a->setOllamaEndpoint(v); });
                    updated << "ollamaEndpoint";
                }
                if (args.contains("openaiEndpoint")) {
                    QString v = args["openaiEndpoint"].toString();
                    addSetter([a, v]() { a->setOpenaiEndpoint(v); });
                    updated << "openaiEndpoint";
                }
                if (args.contains("anthropicEndpoint")) {
                    QString v = args["anthropicEndpoint"].toString();
                    addSetter([a, v]() { a->setAnthropicEndpoint(v); });
                    updated << "anthropicEndpoint";
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
                // Refuse rather than report a write that cannot take effect.
                // Tablet release builds have no simulator compiled in, so the
                // setter ignores `true` — listing it in `updated` would tell the
                // client it succeeded while settings_get kept reporting false.
                if (v && !settings->app()->simulatorAvailable()) {
                    respond(QJsonObject{{"error",
                        "simulationMode is not available in this build - the DE1 simulator "
                        "is not included in tablet release builds. No settings were changed."}});
                    return;
                }
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
                // Execute all setters on the main thread, then respond. Steam
                // settings need no push from here: MainController watches the
                // SettingsBrew signals and re-sends the resolved target itself,
                // so every writer — QML, MCP, web, a backup import — reaches the
                // machine without each one remembering to.
                QMetaObject::invokeMethod(qApp, [setters, respond, result]() {
                    for (const auto& setter : setters) setter();
                    respond(result);
                }, Qt::QueuedConnection);
            }
        },
        "settings", McpTierCore);

    // auto_load target=profile, action=set — pin a profile. Validated
    // synchronously (filename non-empty, profile exists, profile is in the
    // The recipe half of auto_load lives in THIS file rather than
    // mcptools_recipes.cpp: that file's recipe_activate/recipe_archive call real
    // MainController methods, so linking it at all — even for tools that do not need
    // MainController — pulls in MainController's whole subsystem closure, which the
    // auto-load tests do not want.

    // auto_load — one tool over both auto-loads, because they are ONE setting with two
    // faces: pinning a profile clears a pinned recipe and vice versa. Six tools each
    // had to restate that exclusivity in prose; here `target` makes it structural.
    //
    // The profile `get` is the only synchronous member (it reads ProfileManager in
    // memory); the rest touch the recipe DB or hop to the GUI thread to write, so the
    // tool is async and the sync one responds inline.
    const McpToolHandler profileGetAutoLoad =
[profileManager, settings](const QJsonObject&) -> QJsonObject {
            QJsonObject result;
            if (!settings) {
                result["error"] = "Settings not available";
                return result;
            }
            const QString filename = settings->app()->autoLoadProfileFilename();
            result["filename"] = filename;
            result["revertMinutes"] = settings->app()->autoLoadRevertMinutes();
            if (!filename.isEmpty() && profileManager) {
                QVariantMap profile = profileManager->getProfileByFilename(filename);
                if (!profile.isEmpty()) {
                    result["title"] = profile["title"].toString();
                }
            }
            return result;
        };
    const McpAsyncToolHandler profileSetAutoLoad =
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
        };
    const McpAsyncToolHandler profileClearAutoLoad =
[settings](const QJsonObject&, std::function<void(QJsonObject)> respond) {
            if (!settings) {
                respond(QJsonObject{{"error", "Settings not available"}});
                return;
            }
            QMetaObject::invokeMethod(qApp, [settings, respond]() {
                settings->app()->setAutoLoadProfileFilename("");
                respond(QJsonObject{{"success", true}});
            }, Qt::QueuedConnection);
        };
    const McpAsyncToolHandler recipeGetAutoLoad =
[shotHistory, settings](const QJsonObject&, std::function<void(QJsonObject)> respond) {
            if (!settings) {
                respond(QJsonObject{{"error", "Settings not available"}});
                return;
            }
            const qint64 recipeId = settings->dye()->autoLoadRecipeId();
            const int revertMinutes = settings->app()->autoLoadRevertMinutes();
            if (recipeId < 0) {
                // Genuinely unconfigured — nothing pinned, no verification needed.
                QJsonObject result;
                result["recipeId"] = QJsonValue(QJsonValue::Null);
                result["revertMinutes"] = revertMinutes;
                respond(result);
                return;
            }
            if (!shotHistory || !shotHistory->isReady()) {
                // A recipe IS configured but its existence/archived state can't be
                // verified right now — report that distinctly rather than as
                // "unconfigured" (recipeId: null), which would misinform a caller
                // into thinking nothing is pinned.
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([dbPath, recipeId, revertMinutes, respond]() {
                QString name;
                bool found = false;
                const bool opened = withTempDb(dbPath, "mcp_recipe_get_auto_load", [&](QSqlDatabase& db) {
                    const Recipe r = RecipeStorage::loadRecipeStatic(db, recipeId);
                    if (r.isValid()) {
                        name = r.name;
                        found = true;
                    }
                });
                QMetaObject::invokeMethod(qApp, [opened, found, name, recipeId, revertMinutes, respond]() {
                    if (!opened) {
                        // A DB-open failure is a transient error, not proof the row
                        // is gone — distinct from the stale-id case below, which
                        // deliberately reports as unconfigured rather than an error
                        // (this read is a snapshot; the next auto-load trigger will
                        // discover and clear a genuinely stale id the same way).
                        respond(QJsonObject{{"error", "Could not open shot database"}});
                        return;
                    }
                    QJsonObject result;
                    result["recipeId"] = found ? QJsonValue(recipeId) : QJsonValue(QJsonValue::Null);
                    if (found)
                        result["name"] = name;
                    result["revertMinutes"] = revertMinutes;
                    respond(result);
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        };
    const McpAsyncToolHandler recipeSetAutoLoad =
[shotHistory, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!settings) {
                respond(QJsonObject{{"error", "Settings not available"}});
                return;
            }
            if (!args.contains("recipeId")) {
                respond(QJsonObject{{"error", "recipeId is required"}});
                return;
            }
            const qint64 recipeId = args["recipeId"].toInteger();
            if (recipeId <= 0) {
                respond(QJsonObject{{"error", "recipeId must be a positive integer"}});
                return;
            }
            if (recipeId > std::numeric_limits<int>::max()) {
                respond(QJsonObject{{"error", "recipeId is out of range"}});
                return;
            }
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const bool hasRevert = args.contains("revertMinutes");
            const int revertMinutes = hasRevert ? args["revertMinutes"].toInt() : -1;
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([dbPath, recipeId, hasRevert, revertMinutes, settings, respond]() {
                QString name;
                bool found = false;
                bool archived = false;
                const bool opened = withTempDb(dbPath, "mcp_recipe_set_auto_load", [&](QSqlDatabase& db) {
                    const Recipe r = RecipeStorage::loadRecipeStatic(db, recipeId);
                    if (r.isValid()) {
                        found = true;
                        name = r.name;
                        archived = r.archived;
                    }
                });
                QMetaObject::invokeMethod(qApp, [opened, found, archived, name, recipeId, hasRevert, revertMinutes, settings, respond]() {
                    if (!opened) {
                        respond(QJsonObject{{"error", "Could not open shot database"}});
                        return;
                    }
                    if (!found) {
                        respond(QJsonObject{{"error", QString("Recipe not found: %1").arg(recipeId)}});
                        return;
                    }
                    if (archived) {
                        respond(QJsonObject{{"error", "Recipe is archived"}});
                        return;
                    }
                    settings->dye()->setAutoLoadRecipeId(static_cast<int>(recipeId));
                    if (hasRevert)
                        settings->app()->setAutoLoadRevertMinutes(revertMinutes);
                    QJsonObject result;
                    result["success"] = true;
                    result["recipeId"] = recipeId;
                    result["name"] = name;
                    result["revertMinutes"] = settings->app()->autoLoadRevertMinutes();
                    respond(result);
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        };
    const McpAsyncToolHandler recipeClearAutoLoad =
[settings](const QJsonObject&, std::function<void(QJsonObject)> respond) {
            if (!settings) {
                respond(QJsonObject{{"error", "Settings not available"}});
                return;
            }
            QMetaObject::invokeMethod(qApp, [settings, respond]() {
                settings->dye()->setAutoLoadRecipeId(-1);
                respond(QJsonObject{{"success", true}});
            }, Qt::QueuedConnection);
        };

    // `target` is required for the same reason `action` is: defaulting it would make
    // "clear the auto-load" clear whichever one the default happened to name, and the
    // other would stay pinned while the caller believed it had cleared everything.
    auto autoLoadTarget = [](const QJsonObject& args, bool& ok) -> QString {
        const QString target = args.value("target").toString();
        ok = (target == QLatin1String("profile") || target == QLatin1String("recipe"));
        return target;
    };
    auto autoLoadTargetError = [](const QString& target) {
        return QJsonObject{{"error", target.isEmpty()
            ? QStringLiteral("auto_load requires a `target`: \"profile\" or \"recipe\"")
            : QStringLiteral("Unknown target \"%1\" — valid targets: profile, recipe").arg(target)}};
    };

    const QVector<McpToolAction> autoLoadActions{
        McpRegistryHelpers::asyncAction("get", "read",
        [profileGetAutoLoad, recipeGetAutoLoad, autoLoadTarget, autoLoadTargetError]
        (const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            bool ok = false;
            const QString target = autoLoadTarget(args, ok);
            if (!ok) { respond(autoLoadTargetError(target)); return; }
            if (target == QLatin1String("profile")) { respond(profileGetAutoLoad(args)); return; }
            recipeGetAutoLoad(args, std::move(respond));
        }),
        McpRegistryHelpers::asyncAction("set", "settings",
        [profileSetAutoLoad, recipeSetAutoLoad, autoLoadTarget, autoLoadTargetError]
        (const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            bool ok = false;
            const QString target = autoLoadTarget(args, ok);
            if (!ok) { respond(autoLoadTargetError(target)); return; }
            if (target == QLatin1String("profile")) { profileSetAutoLoad(args, std::move(respond)); return; }
            recipeSetAutoLoad(args, std::move(respond));
        }),
        McpRegistryHelpers::asyncAction("clear", "settings",
        [profileClearAutoLoad, recipeClearAutoLoad, autoLoadTarget, autoLoadTargetError]
        (const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            bool ok = false;
            const QString target = autoLoadTarget(args, ok);
            if (!ok) { respond(autoLoadTargetError(target)); return; }
            if (target == QLatin1String("profile")) { profileClearAutoLoad(args, std::move(respond)); return; }
            recipeClearAutoLoad(args, std::move(respond));
        }),
    };

    registry->registerActionTool(
        "auto_load",
        "The auto-load pin: what is reloaded on app start, DE1 wake-from-sleep, and after "
        "revertMinutes idle on the Idle page. target=profile or recipe, action=get, set or clear. "
        "A profile and a recipe auto-load are mutually exclusive — setting one clears the other. "
        "clear leaves revertMinutes untouched. Validation rules and what a stale pin reports: "
        "get_agent_file topic \"auto_load\".",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"target", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"profile", "recipe"}},
                    {"description", "Which auto-load to act on. Required"}}},
                {"filename", QJsonObject{{"type", "string"}, {"description", "set + target=profile: profile filename without .json, from the Selected list"}}},
                {"recipeId", QJsonObject{{"type", "integer"}, {"description", "set + target=recipe: recipe id from recipe_list"}}},
                {"revertMinutes", QJsonObject{{"type", "integer"}, {"description", "set: idle minutes before reverting, 0-60. Shared by both targets"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        },
        autoLoadActions);


    auto bagToJson = [settings](const CoffeeBag& bag) {
        QJsonObject obj;
        obj["bagId"] = bag.id;
        obj["roasterName"] = bag.roasterName;
        obj["coffeeName"] = bag.coffeeName;
        // kind is creation-time identity ("coffee" | "tea"; empty rows read
        // as coffee), never editable via action=update.
        obj["kind"] = bag.isTea() ? QStringLiteral("tea") : QStringLiteral("coffee");
        if (bag.isTea()) {
            // Structured brewing data from the blob, per the data conventions
            // (units in names). Absent keys = the vendor stated nothing.
            const TeaBrewingData tea = CoffeeBag::teaBrewingFromBlob(bag.beanBaseData);
            if (!tea.teaType.isEmpty()) obj["teaType"] = tea.teaType;
            if (tea.brewTempC > 0) obj["brewTemperatureC"] = tea.brewTempC;
            if (tea.leafGramsPer100Ml > 0) obj["leafGramsPer100Ml"] = tea.leafGramsPer100Ml;
            if (!tea.steepTime.isEmpty()) obj["steepTime"] = tea.steepTime;
        }
        if (!bag.roastDate.isEmpty()) obj["roastDate"] = bag.roastDate;
        if (!bag.roastLevel.isEmpty()) obj["roastLevel"] = bag.roastLevel;
        if (!bag.frozenDate.isEmpty()) obj["frozenDate"] = bag.frozenDate;
        if (!bag.defrostDate.isEmpty()) obj["defrostDate"] = bag.defrostDate;
        if (!bag.storageHint.isEmpty()) obj["storageHint"] = bag.storageHint;
        if (!bag.openedDate.isEmpty()) obj["openedDate"] = bag.openedDate;
        if (!bag.notes.isEmpty()) obj["notes"] = bag.notes;
        obj["inInventory"] = bag.inInventory;
        if (!bag.grinderBrand.isEmpty()) obj["grinderBrand"] = bag.grinderBrand;
        if (!bag.grinderModel.isEmpty()) obj["grinderModel"] = bag.grinderModel;
        if (!bag.grinderBurrs.isEmpty()) obj["grinderBurrs"] = bag.grinderBurrs;
        if (!bag.grinderSetting.isEmpty()) obj["grinderSetting"] = bag.grinderSetting;
        if (bag.rpm > 0) obj["rpm"] = bag.rpm;  // RPM half of the bean-scoped dial-in (sparse)
        if (bag.doseWeightG > 0) obj["doseWeightG"] = bag.doseWeightG;
        // Yield spec (add-yield-ratio-anchor): sparse, mutually exclusive
        // keys — grams for an absolute anchor, a dose multiplier for a
        // ratio; mode "none" emits neither.
        if (bag.yieldMode == QLatin1String("absolute") && bag.yieldValue > 0)
            obj["yieldG"] = bag.yieldValue;
        else if (bag.yieldMode == QLatin1String("ratio") && bag.yieldValue > 0)
            obj["yieldRatio"] = bag.yieldValue;
        if (!bag.beanBaseData.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(bag.beanBaseData.toUtf8());
            if (doc.isObject())
                obj["beanBase"] = doc.object();
        }
        obj["isActive"] = settings && settings->dye()->activeBagId() == bag.id;
        return obj;
    };

    // bag — the coffee bags CRUD family, one tool with four verbs. `extract_details`
    // stays its own tool: it parses a photographed label, which is a different job
    // that happens to share the noun.
    //
    // action=create stamps `kind` once and never lets it change afterwards (the same
    // rule as the Add Coffee / Add Tea entry points in the UI), and does NOT make the
    // new bag active: a remote client must not silently switch what the user's next
    // shot is recorded against.
    const QVector<McpToolAction> bagActions{
        McpRegistryHelpers::asyncAction("list", "read",
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
        }),
        McpRegistryHelpers::asyncAction("create", "settings",
[bagToJson, bagStorage](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!bagStorage) {
                respond(QJsonObject{{"error", "Bag storage not available"}});
                return;
            }
            const QString kind = args.value("kind").toString().isEmpty()
                ? QStringLiteral("coffee") : args["kind"].toString();
            if (kind != QLatin1String("coffee") && kind != QLatin1String("tea")) {
                respond(QJsonObject{{"error", "kind must be 'coffee' or 'tea'"}});
                return;
            }
            const QString roaster = args["roasterName"].toString().trimmed();
            const QString coffee = args["coffeeName"].toString().trimmed();
            if (roaster.isEmpty() && coffee.isEmpty()) {
                respond(QJsonObject{{"error", "at least one of roasterName / coffeeName is required"}});
                return;
            }

            // Kind-gate both directions, mirroring action=update's rule.
            static const QStringList kTeaOnly = {
                "teaType", "garden", "cultivar", "flush", "brewTempC",
                "leafGramsPer100Ml", "steepTime"};
            static const QStringList kCoffeeOnly = {"roastLevel", "grinderSetting"};
            QStringList offending;
            if (kind == QLatin1String("coffee")) {
                for (const QString& key : kTeaOnly)
                    if (args.contains(key)) offending << key;
                if (!offending.isEmpty()) {
                    respond(QJsonObject{{"error", offending.join(", ")
                        + " only apply to tea bags (this create has kind coffee)"}});
                    return;
                }
            } else {
                for (const QString& key : kCoffeeOnly)
                    if (args.contains(key)) offending << key;
                if (!offending.isEmpty()) {
                    respond(QJsonObject{{"error", offending.join(", ")
                        + " do not apply to tea bags"}});
                    return;
                }
            }

            // Columns.
            QVariantMap bag;
            bag.insert("kind", kind);
            if (!roaster.isEmpty()) bag.insert("roasterName", roaster);
            if (!coffee.isEmpty()) bag.insert("coffeeName", coffee);
            for (const QString& key : {QStringLiteral("roastDate"), QStringLiteral("roastLevel"),
                                       QStringLiteral("grinderSetting"), QStringLiteral("notes")})
                if (args.contains(key)) bag.insert(key, args[key].toString());
            if (args.contains("rpm")) bag.insert("rpm", args["rpm"].toInt());  // RPM half of the dial-in
            if (args.contains("doseWeightG")) bag.insert("doseWeightG", args["doseWeightG"].toDouble());
            bag.insert("inInventory", true);

            // Details land in the blob (same vocabulary as action=update).
            static const QStringList kBlobKeys = {
                "origin", "region", "producer", "variety", "process", "harvest",
                "tastingNotes", "link",
                "teaType", "garden", "cultivar", "flush", "brewTempC",
                "leafGramsPer100Ml", "steepTime"};
            QVariantMap blobEdits;
            for (const QString& key : kBlobKeys)
                if (args.contains(key)) blobEdits.insert(key, args[key].toVariant());
            if (!blobEdits.isEmpty())
                bag.insert("beanBaseData", BeanBaseBlob::mergeBeanDetails(QString(), blobEdits));

            // bagCreated is a broadcast with no request token, so a concurrent
            // create from another surface (in-app Add, web POST) would run this
            // one-shot handler with the OTHER bag. Correlate on the submitted
            // identity: skip an emission whose roaster+coffee+kind don't match
            // ours (a failed create — bagId<=0 — is still ours to report). Two
            // genuinely-identical concurrent creates can't be told apart, but
            // then either bag is a correct answer.
            const QString wantRoaster = roaster;
            const QString wantCoffee = coffee;
            const QString wantKind = kind;
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = QObject::connect(bagStorage, &CoffeeBagStorage::bagCreated, qApp,
                [conn, bagToJson, respond, wantRoaster, wantCoffee, wantKind](qint64 bagId, const QVariantMap& created) {
                    if (bagId > 0
                        && (created.value("roasterName").toString() != wantRoaster
                            || created.value("coffeeName").toString() != wantCoffee
                            || created.value("kind").toString() != wantKind))
                        return;  // someone else's concurrent create
                    QObject::disconnect(*conn);
                    if (bagId <= 0) {
                        respond(QJsonObject{{"error", "Could not create the bag"}});
                        return;
                    }
                    respond(QJsonObject{{"success", true},
                                        {"bag", bagToJson(CoffeeBag::fromVariantMap(created))}});
                });
            bagStorage->requestCreateBag(bag);
        }),
        McpRegistryHelpers::asyncAction("update", "settings",
[shotHistory, bagToJson, bagStorage, beanbase](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const qint64 bagId = args["bagId"].toInteger();
            if (!bagIdIsSet(bagId)) {
                respond(QJsonObject{{"error", "Valid bagId is required"}});
                return;
            }
            // Reject an off-list storageHint loudly rather than writing junk the
            // AI freshness block would then surface verbatim. "" clears it;
            // "frozen" is intentionally invalid (freeze state = frozenDate).
            if (args.contains("storageHint")) {
                const QString hint = args["storageHint"].toString();
                if (!CoffeeBag::isValidStorageHint(hint)) {
                    respond(QJsonObject{{"error",
                        QStringLiteral("storageHint must be one of %1 (or '' to clear); "
                                       "there is no 'frozen' value — set frozenDate instead")
                            .arg(CoffeeBag::storageHintValues().join(QStringLiteral(", ")))}});
                    return;
                }
            }
            // yieldOverrideG was a real bag-update key until this change, so
            // scripts and agent workflows still send it. The field loop below
            // works off a whitelist, which would drop it silently and answer
            // OK — the caller would believe it had set a yield it had not.
            // Reject it loudly instead (the temperatureOverrideC precedent).
            if (args.contains("yieldOverrideG")) {
                respond(QJsonObject{{"error", "yieldOverrideG was replaced by yieldG (an absolute gram target) / yieldRatio (a multiple of the dose) — the bag now holds an explicit yield anchor rather than a deviation from the profile (add-yield-ratio-anchor). Rejected rather than silently dropped: send yieldG for the same behaviour as before."}});
                return;
            }
            // One yield anchor per bag — both keys at once is a contradiction,
            // rejected loudly (mirrors recipe_create/recipe_update).
            if (args.contains("yieldG") && args.contains("yieldRatio")) {
                respond(QJsonObject{{"error", "yieldG and yieldRatio are mutually exclusive — the bag holds ONE yield anchor (an absolute gram target OR a ratio of the dose). Send exactly one; writing it replaces the other automatically."}});
                return;
            }
            QVariantMap fields;
            static const QStringList kEditable = {
                "roasterName", "coffeeName", "roastDate", "roastLevel",
                "frozenDate", "defrostDate", "storageHint", "openedDate", "notes",
                "grinderBrand", "grinderModel", "grinderBurrs", "grinderSetting", "rpm",
                "doseWeightG", "inInventory"};
            for (const QString& key : kEditable) {
                if (args.contains(key))
                    fields.insert(key, args[key].toVariant());
            }
            // Yield spec: writing one wire key IS setting the mode, which
            // implicitly clears the other (add-yield-ratio-anchor).
            if (args.contains("yieldG")) {
                const double g = args["yieldG"].toDouble();
                fields.insert("yieldValue", g > 0 ? YieldSpec::clampAbsolute(g) : 0.0);
                fields.insert("yieldMode", g > 0 ? QStringLiteral("absolute") : QStringLiteral("none"));
            } else if (args.contains("yieldRatio")) {
                const double ratio = args["yieldRatio"].toDouble();
                fields.insert("yieldValue", ratio > 0 ? YieldSpec::clampRatio(ratio) : 0.0);
                fields.insert("yieldMode", ratio > 0 ? QStringLiteral("ratio") : QStringLiteral("none"));
            }
            // Bean-detail edits live in the beanBaseData blob, not columns.
            // Collected here; merged below via the same BeanBaseBlob helper the
            // bag editor uses (empty value removes the key, link keys and the
            // canonical snapshot preserved).
            static const QStringList kBlobKeys = {
                "origin", "region", "farm", "producer", "variety", "elevation",
                "process", "harvest", "qualityScore", "placeOfPurchase",
                "tastingNotes", "link",
                // Tea vocabulary (kind stays immutable; these are blob keys
                // like the coffee details above).
                "teaType", "garden", "cultivar", "flush", "brewTempC",
                "leafGramsPer100Ml", "steepTime"};
            QVariantMap blobEdits;
            for (const QString& key : kBlobKeys) {
                if (args.contains(key))
                    blobEdits.insert(key, args[key].toVariant());
            }
            if (fields.isEmpty() && blobEdits.isEmpty()) {
                respond(QJsonObject{{"error", "No fields to update"}});
                return;
            }
            const QString dbPath = shotHistory->databasePath();

            // Load the just-updated bag on a background thread and respond.
            // Always create the reader thread ON THE MAIN THREAD.
            //
            // This lambda is invoked from THREE places: :1842 (bagUpdated
            // handler, main thread), :1857 (the headless fallback path, from
            // INSIDE a QThread::create worker — the leak), and :1969 (the
            // idempotent-merge early return, main thread via invokeMethod).
            // Only the middle one runs off the main thread.
            //
            // In that second case the QThread built below inherited the
            // WORKER's thread affinity, so deleteLater() on its finished()
            // signal posted a DeferredDelete to the WORKER's event queue. A
            // QThread::create thread never calls exec(), and has usually exited
            // by then, so nothing ever processed that event: the QThread and
            // its internals were never freed.
            //
            // Measured as 6,540 bytes / 70 allocations leaked in
            // tst_mcptools_write. It survived a 250 ms drain of the MAIN
            // thread's event queue — because the pending delete was never on
            // the main thread's queue at all, which is what made two earlier
            // explanations of this leak wrong. Confirmed by probe: on the
            // fallback path this lambda reported
            // currentThread() != qApp->thread() on every call.
            //
            // The hop makes affinity identical on all three paths, so the
            // deferred delete always lands on an event loop that actually runs.
            //
            // Confirmed, not assumed: the nightly Linux ASan job reported
            // 6,540 bytes / 70 allocations here before this change and ZERO
            // after (run 29707910438, 84/84, no LeakSanitizer reports).
            // LeakSanitizer is Linux-only, so no local run could have shown it.
            auto respondWithBag = [dbPath, bagId, bagToJson, respond]() {
              QMetaObject::invokeMethod(qApp, [dbPath, bagId, bagToJson, respond]() {
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
              }, Qt::QueuedConnection);
            };

            // onSuccess runs ONLY after the write is confirmed. Anything with
            // side effects outside this bag — evicting a bag photo whose cache
            // key is shared by every bag on the same canonical bean — belongs
            // there, not before the write, where a failed or nonexistent update
            // would still have changed what other bags display.
            auto proceed = [dbPath, bagId, bagStorage, respondWithBag, respond](
                               const QVariantMap& finalFields,
                               std::function<void()> onSuccess = {}) {
                if (bagStorage) {
                    // Route through the storage INSTANCE so the write fires
                    // bagsChanged (open inventory views refresh), bagVisualizer-
                    // FieldsChanged (push the edit to the synced Visualizer bag),
                    // and the active-bag dye refresh via SettingsDye's bagUpdated
                    // handler. That refresh is a no-op re-apply, so it does NOT
                    // reset the user's brew overrides — unlike the old
                    // setActiveBagId(-1) toggle, which fired clearBrewOverrides.
                    QMetaObject::invokeMethod(qApp, [bagStorage, bagId, finalFields, respondWithBag, respond, onSuccess]() {
                        auto conn = std::make_shared<QMetaObject::Connection>();
                        *conn = QObject::connect(bagStorage, &CoffeeBagStorage::bagUpdated, bagStorage,
                            [conn, bagId, respondWithBag, respond, onSuccess](qint64 updatedId, bool success) {
                                if (updatedId != bagId)
                                    return;  // a concurrent update of a different bag
                                QObject::disconnect(*conn);
                                if (!success) {
                                    respond(QJsonObject{{"error", "Bag not found or update failed: "
                                                                  + QString::number(bagId)}});
                                    return;
                                }
                                if (onSuccess)
                                    onSuccess();
                                respondWithBag();
                            });
                        bagStorage->requestUpdateBag(bagId, finalFields);
                    }, Qt::QueuedConnection);
                    return;
                }

                // Fallback (no storage instance — e.g. headless tests): direct
                // static write. Skips the in-app refresh/sync signals.
                QThread* thread = QThread::create([dbPath, bagId, finalFields, respondWithBag, respond, onSuccess]() {
                    bool success = false;
                    withTempDb(dbPath, "mcp_bagupd", [&](QSqlDatabase& db) {
                        success = CoffeeBagStorage::updateBagFieldsStatic(db, bagId, finalFields);
                    });
                    if (success) {
                        if (onSuccess)
                            QMetaObject::invokeMethod(qApp, onSuccess, Qt::QueuedConnection);
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
            };

            // No blob work: column-only update goes straight through. Identity
            // edits still mirror into the blob's working keys below so display
            // surfaces reading the blob stay consistent — but only when the bag
            // already carries a blob (a plain rename of a detail-less manual
            // bag must not conjure one).
            const bool identityEdit = fields.contains("roasterName")
                || fields.contains("coffeeName") || fields.contains("roastLevel");
            // Coffee-only columns must reach the kind gate in the merge thread
            // (the bag's kind isn't known until it's loaded). roastLevel already
            // routes through via identityEdit; grinderSetting would otherwise
            // short-circuit past the gate onto a tea bag.
            const bool coffeeOnlyEdit = fields.contains("roastLevel")
                || fields.contains("grinderSetting");
            if (blobEdits.isEmpty() && !identityEdit && !coffeeOnlyEdit) {
                proceed(fields);
                return;
            }

            // Read the current blob, merge, then run the normal update.
            QPointer<BeanBaseClient> safeBeanbase(beanbase);
            QThread* mergeThread = QThread::create([dbPath, bagId, fields, blobEdits, safeBeanbase,
                                                   proceed, respondWithBag, respond]() {
                bool found = false;
                bool isTea = false;
                QString currentBlob, curRoaster, curCoffee, curLevel, curBeanBaseId;
                withTempDb(dbPath, "mcp_bagupd_blob", [&](QSqlDatabase& db) {
                    const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, bagId);
                    found = bag.isValid();
                    isTea = bag.isTea();
                    currentBlob = bag.beanBaseData;
                    curRoaster = bag.roasterName;
                    curCoffee = bag.coffeeName;
                    curLevel = bag.roastLevel;
                    curBeanBaseId = bag.beanBaseId;
                });
                QMetaObject::invokeMethod(qApp, [found, isTea, currentBlob, curRoaster, curCoffee, curLevel,
                                                 curBeanBaseId, safeBeanbase,
                                                 bagId, fields, blobEdits, proceed, respondWithBag, respond]() {
                    if (!found) {
                        respond(QJsonObject{{"error", "Bag not found: " + QString::number(bagId)}});
                        return;
                    }
                    // Tea vocabulary is kind-gated: writing teaType/brewTempC/…
                    // onto a coffee bag would plant tea keys the wizard and bag
                    // surfaces then trust (live-caught: a coffee bag accepted
                    // teaType). Kind itself stays immutable, so the error names
                    // the rule instead of silently dropping the keys.
                    if (!isTea) {
                        static const QStringList kTeaOnly = {
                            "teaType", "garden", "cultivar", "flush", "brewTempC",
                            "leafGramsPer100Ml", "steepTime"};
                        QStringList offending;
                        for (const QString& key : kTeaOnly)
                            if (blobEdits.contains(key))
                                offending << key;
                        if (!offending.isEmpty()) {
                            respond(QJsonObject{{"error",
                                QString("%1 only apply to tea bags; bag %2 is a coffee bag "
                                        "(kind is set at creation and immutable)")
                                    .arg(offending.join(", ")).arg(bagId)}});
                            return;
                        }
                    } else {
                        // Reverse gate (symmetry with action=create): roast level and
                        // grinder setting are meaningless on a tea bag — reject
                        // rather than store a value tea surfaces hide anyway.
                        static const QStringList kCoffeeOnly = {"roastLevel", "grinderSetting"};
                        QStringList offending;
                        for (const QString& key : kCoffeeOnly)
                            if (fields.contains(key) && !fields.value(key).toString().trimmed().isEmpty())
                                offending << key;
                        if (!offending.isEmpty()) {
                            respond(QJsonObject{{"error",
                                QString("%1 do not apply to tea bags; bag %2 is a tea bag")
                                    .arg(offending.join(", ")).arg(bagId)}});
                            return;
                        }
                    }
                    // Identity mirrors into the blob's working keys when a
                    // blob exists OR this update introduces one (a non-empty
                    // detail edit) — the same rule as the bag editor's
                    // detailEdits(), so both write paths produce the same
                    // blob. The mirror value is the post-update state: the
                    // arg when given, else the stored column (the editor's
                    // always-populated form fields behave identically).
                    bool anyDetail = false;
                    for (auto it = blobEdits.constBegin(); it != blobEdits.constEnd(); ++it) {
                        if (!it.value().toString().trimmed().isEmpty()) { anyDetail = true; break; }
                    }
                    QVariantMap edits = blobEdits;
                    if (!currentBlob.isEmpty() || anyDetail) {
                        edits.insert("roasterName", fields.value("roasterName", curRoaster));
                        edits.insert("roastName", fields.value("coffeeName", curCoffee));
                        edits.insert("degree", fields.value("roastLevel", curLevel));
                    }
                    QVariantMap finalFields = fields;
                    const QString merged = BeanBaseBlob::mergeBeanDetails(currentBlob, edits);
                    if (merged != currentBlob)
                        finalFields.insert("beanBaseData", merged);

                    // `link` is an editable blob key, so this tool can change a
                    // bag's product URL — and the cached photo describes the OLD
                    // page. The bag editor and the web /beans editor both
                    // re-resolve on this edit; without it MCP was the remaining
                    // way to change the URL and keep the wrong picture.
                    //
                    // Deferred to the write's success handler, NOT run here. The
                    // comparison itself is race-free (the pre-read put the old
                    // blob in hand), but the cache key is shared by every bag on
                    // the same canonical bean, so refreshing for an update that
                    // then fails would change the photo other bags display for a
                    // link change that was never persisted.
                    const auto linkOf = [](const QString& blob) {
                        return QJsonDocument::fromJson(blob.toUtf8())
                            .object().value(QStringLiteral("link")).toString().trimmed();
                    };
                    const QString newLink = linkOf(merged);
                    std::function<void()> refreshPhoto;
                    if (safeBeanbase && !newLink.isEmpty() && newLink != linkOf(currentBlob)) {
                        const QString imageKey = curBeanBaseId.isEmpty()
                            ? QStringLiteral("bag-%1").arg(bagId) : curBeanBaseId;
                        const QString roastName = fields.value("coffeeName", curCoffee).toString();
                        refreshPhoto = [safeBeanbase, imageKey, roastName, newLink]() {
                            if (safeBeanbase)
                                safeBeanbase->refreshBagImage(imageKey, roastName, newLink);
                        };
                    }
                    if (finalFields.isEmpty()) {
                        // Everything merged to its current value (idempotent
                        // re-apply, or clearing an already-absent key): a
                        // semantically successful request — echo the bag
                        // rather than erroring at the caller.
                        respondWithBag();
                        return;
                    }
                    proceed(finalFields, refreshPhoto);
                }, Qt::QueuedConnection);
            });
            QObject::connect(mergeThread, &QThread::finished, mergeThread, &QObject::deleteLater);
            mergeThread->start();
        }),
        McpRegistryHelpers::asyncAction("select", "control",
[shotHistory, settings](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!settings) {
                respond(QJsonObject{{"error", "Settings not available"}});
                return;
            }
            // Absent is NOT zero. Zero means "clear the bean selection" and is
            // documented as such; an omitted argument means the caller forgot, and
            // treating the two the same wipes the bag new shots record against and
            // answers "success". `bag_select` used to require bagId in its schema,
            // and a merged tool's `required` can only name `action`.
            if (!args.contains("bagId")) {
                respond(QJsonObject{{"error", "bagId is required for action=select "
                                              "(0 clears the selection)"}});
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
                // The open result is checked: a database that cannot be opened
                // otherwise reports as "Bag not found", which sends the caller
                // looking for a bag that is right there.
                const bool opened = withTempDb(dbPath, "mcp_bagsel", [&](QSqlDatabase& db) {
                    bag = CoffeeBagStorage::loadBagStatic(db, bagId);
                });
                QMetaObject::invokeMethod(qApp, [bag, bagId, opened, settings, respond]() {
                    if (!opened) {
                        respond(QJsonObject{{"error", "Bag storage could not be opened"}});
                        return;
                    }
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
        }),
    };

    registry->registerActionTool(
        "bag",
        "Coffee bags: list, create, update, select. A bag carries the beans, roast date, the "
        "dose/grind the last shot used, and the equipment package it was dialled on; select makes "
        "one active so new shots record against it. Ids come from action=list. Which fields exist, "
        "what a frozen bag is, and how grind/rpm memory works: get_agent_file topic \"bag\".",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"bagId", QJsonObject{{"type", "integer"}, {"description", "Bag id from action=list. Required by update; select takes it or 0 to clear"}}},
                {"includeEmpty", QJsonObject{{"type", "boolean"}, {"description", "list only: also include bags marked empty"}}},
                {"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"coffee", "tea"}},
                    {"description", "create only: bag kind, set once. Default coffee"}}},
                {"roasterName", QJsonObject{{"type", "string"}, {"description", "Roaster (coffee) / brand (tea)"}}},
                {"coffeeName", QJsonObject{{"type", "string"}, {"description", "Coffee name (coffee) / tea name (tea)"}}},
                {"roastDate", QJsonObject{{"type", "string"}, {"description", "YYYY-MM-DD, '' to clear"}}},
                {"roastLevel", QJsonObject{{"type", "string"}, {"description", "Coffee bags only"}}},
                {"frozenDate", QJsonObject{{"type", "string"}, {"description", "update only: YYYY-MM-DD, '' to clear"}}},
                {"defrostDate", QJsonObject{{"type", "string"}, {"description", "update only: YYYY-MM-DD, '' to clear"}}},
                {"storageHint", QJsonObject{{"type", "string"}, {"description", "counter/airtight/vacuum-sealed/fridge, '' to clear. Valid in any freeze state"}}},
                {"openedDate", QJsonObject{{"type", "string"}, {"description", "YYYY-MM-DD this portion left airtight storage, '' to clear"}}},
                {"grinderBrand", QJsonObject{{"type", "string"}, {"description", "update only"}}},
                {"grinderModel", QJsonObject{{"type", "string"}, {"description", "update only"}}},
                {"grinderBurrs", QJsonObject{{"type", "string"}, {"description", "update only"}}},
                {"grinderSetting", QJsonObject{{"type", "string"}, {"description", "Coffee bags only: bean-scoped dial"}}},
                {"rpm", QJsonObject{{"type", "integer"}, {"description", "Coffee bags only: bean-scoped grinder RPM, paired with grinderSetting"}}},
                {"doseWeightG", QJsonObject{{"type", "number"}, {"description", "Dose in grams"}}},
                {"yieldG", QJsonObject{{"type", "number"}, {"description", "update only: absolute yield target in grams. Excludes yieldRatio; 0 clears"}}},
                {"yieldRatio", QJsonObject{{"type", "number"}, {"description", "update only: yield as a multiple of dose (0.5-6.0). Excludes yieldG; 0 clears"}}},
                {"inInventory", QJsonObject{{"type", "boolean"}, {"description", "update only: false marks the bag empty"}}},
                {"notes", QJsonObject{{"type", "string"}, {"description", "Free-text notes"}}},
                {"origin", QJsonObject{{"type", "string"}, {"description", "Origin country, '' to clear"}}},
                {"region", QJsonObject{{"type", "string"}, {"description", "Growing region"}}},
                {"farm", QJsonObject{{"type", "string"}, {"description", "update only: farm"}}},
                {"producer", QJsonObject{{"type", "string"}, {"description", "Producer / farmer"}}},
                {"variety", QJsonObject{{"type", "string"}, {"description", "Coffee variety; tea cultivar goes in cultivar"}}},
                {"elevation", QJsonObject{{"type", "string"}, {"description", "update only: display string, e.g. '1900-2100 m'"}}},
                {"process", QJsonObject{{"type", "string"}, {"description", "Processing method, e.g. 'Washed'"}}},
                {"harvest", QJsonObject{{"type", "string"}, {"description", "Harvest time, e.g. 'Late 2025'"}}},
                {"qualityScore", QJsonObject{{"type", "string"}, {"description", "update only: cupping score"}}},
                {"placeOfPurchase", QJsonObject{{"type", "string"}, {"description", "update only: where it was bought"}}},
                {"tastingNotes", QJsonObject{{"type", "string"}, {"description", "Roaster's tasting notes"}}},
                {"link", QJsonObject{{"type", "string"}, {"description", "Product-page URL, '' to clear"}}},
                {"teaType", QJsonObject{{"type", "string"}, {"description", "Tea bags only: black/green/oolong/white/herbal/pu-erh"}}},
                {"garden", QJsonObject{{"type", "string"}, {"description", "Tea bags only: estate/garden"}}},
                {"cultivar", QJsonObject{{"type", "string"}, {"description", "Tea bags only"}}},
                {"flush", QJsonObject{{"type", "string"}, {"description", "Tea bags only: harvest/flush"}}},
                {"brewTempC", QJsonObject{{"type", "number"}, {"description", "Tea bags only: vendor brew temperature, Celsius"}}},
                {"leafGramsPer100Ml", QJsonObject{{"type", "number"}, {"description", "Tea bags only: leaf dose per 100 ml water"}}},
                {"steepTime", QJsonObject{{"type", "string"}, {"description", "Tea bags only: display string, e.g. '3-5 minutes'"}}}
            }}
        },
        bagActions,
        McpTierStandard);

    // ----- Equipment packages (add-equipment-packages) -----

    // Flatten an EquipmentPackageView into an MCP-friendly object (units in
    // field names, no raw timestamps, isActive marks the selected package).
    // shotCount is a per-query aggregate, not a package column, so a view built
    // from the load* helpers alone reports 0 however much history the package
    // holds — and an assistant reading that concludes the package is disposable.
    // action=update learned this the hard way; action=merge, whose whole job
    // is moving shots onto the survivor, would have been the second place to get
    // it wrong. One helper, both call sites.
    auto fillShotCount = [](QSqlDatabase& db, qint64 packageId, EquipmentPackageView& view) {
        QSqlQuery shots(db);
        shots.prepare("SELECT COUNT(*) FROM shots WHERE equipment_id = :id");
        shots.bindValue(":id", packageId);
        if (shots.exec() && shots.next())
            view.shotCount = shots.value(0).toLongLong();
    };

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
        // Puck prep (add-puckprep-equipment): the set flags + derived distribution,
        // reconstructed from the canonical string in the item's model column.
        // Emitted only when the package has puck prep.
        if (!v.puckPrep.model.isEmpty()) {
            QJsonObject puck;
            for (const QString& k : PuckPrep::flagKeys())
                puck[k] = PuckPrep::has(v.puckPrep.model, k);
            puck["distribution"] = PuckPrep::distribution(v.puckPrep.model);
            o["puckPrep"] = puck;
        }
        o["inInventory"] = v.package.inInventory;
        if (!v.package.lastGrindSetting.isEmpty()) o["lastGrindSetting"] = v.package.lastGrindSetting;
        if (v.package.lastRpm > 0) o["lastRpm"] = v.package.lastRpm;
        o["shotCount"] = v.shotCount;
        o["isActive"] = (v.package.id == activeId);
        return o;
    };

    // equipment — the grinder+basket package family. `merge` is here rather than in
    // its own tool because repairing a wrongly forked package is an edit to the same
    // objects list/select/update work on.
    const QVector<McpToolAction> equipmentActions{
        McpRegistryHelpers::asyncAction("list", "read",
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
        }),
        McpRegistryHelpers::asyncAction("select", "control",
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
                // Checked, like the `list` action above it: an unopenable database
                // otherwise answers "Package not found", which is a different problem
                // with a different fix.
                const bool opened = withTempDb(dbPath, "mcp_equip_sel", [&](QSqlDatabase& db) {
                    view.package = EquipmentStorage::loadPackageStatic(db, packageId);
                    view.grinder = EquipmentStorage::loadGrinderItemStatic(db, packageId);
                    view.basket = EquipmentStorage::loadBasketItemStatic(db, packageId);
                    view.puckPrep = EquipmentStorage::loadPuckPrepItemStatic(db, packageId);
                });
                const bool found = view.package.isValid();
                const QVariantMap pkgMap = view.toVariantMap();
                QMetaObject::invokeMethod(qApp, [found, opened, pkgMap, packageId, settings, respond]() {
                    if (!opened) {
                        respond(QJsonObject{{"error", "Could not open shot database"}});
                        return;
                    }
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
        }),
        McpRegistryHelpers::asyncAction("update", "settings",
[shotHistory, settings, packageToJson, fillShotCount](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
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
            // Puck-prep flag overrides as a "puckPrep_<key>" map for canonicalMerged.
            const bool touchesPuckPrep = args.contains("puckPrep");
            QVariantMap puckOverrides;
            if (touchesPuckPrep) {
                const QJsonObject pp = args["puckPrep"].toObject();
                for (const QString& k : PuckPrep::flagKeys())
                    if (pp.contains(k))
                        puckOverrides.insert(QStringLiteral("puckPrep_") + k, pp.value(k).toBool());
            }
            const qint64 activeId = settings ? settings->dye()->activeEquipmentId() : -1;
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([=]() {
                bool ok = false;
                qint64 resultId = packageId;
                EquipmentPackageView view;
                // Active-name uniqueness (block-duplicate-active-names). This tool
                // writes the name through updatePackageFieldsStatic on its own
                // connection and never reaches EquipmentStorage::requestUpdatePackage,
                // so the guard has to be repeated here or MCP is the one surface
                // that can still mint a duplicate. Same rule as the storage path:
                // only an actual RENAME can collide (a derived name that already
                // matches another package must stay editable), and it is checked
                // before anything is written so a refusal is a clean no-op.
                bool nameInUse = false;
                withTempDb(dbPath, "mcp_equip_upd", [&](QSqlDatabase& db) {
                    if (pkgFields.contains(QStringLiteral("name"))) {
                        const QString newName = pkgFields.value(QStringLiteral("name")).toString().trimmed();
                        const QString oldName = EquipmentStorage::loadPackageStatic(db, packageId).name.trimmed();
                        if (QString::compare(newName, oldName, Qt::CaseInsensitive) != 0
                            && EquipmentStorage::findPackageByNameStatic(db, newName, packageId) > 0) {
                            nameInUse = true;
                            return;
                        }
                    }
                    if (touchesGrinder || touchesBasket || touchesPuckPrep) {
                        // Copy-on-write/merge over the full (grinder + basket + puck
                        // prep) identity; an untouched side defaults from the current
                        // items so it is preserved. May yield a new id.
                        const EquipmentItem cur = EquipmentStorage::loadGrinderItemStatic(db, packageId);
                        const EquipmentItem curBasket = EquipmentStorage::loadBasketItemStatic(db, packageId);
                        const EquipmentItem curPuck = EquipmentStorage::loadPuckPrepItemStatic(db, packageId);
                        resultId = EquipmentStorage::supersedeOrEditStatic(db, packageId,
                                haveBrand ? brand : cur.brand,
                                haveModel ? model : cur.model,
                                haveBurrs ? burrs : cur.burrs,
                                haveBasketBrand ? basketBrand : curBasket.brand,
                                haveBasketModel ? basketModel : curBasket.model,
                                PuckPrep::canonicalMerged(curPuck.model, puckOverrides));
                        // -1 = the identity edit rolled back. Stop rather than
                        // applying the rest against a sentinel id and reporting a
                        // partial save (see supersedeOrEditStatic).
                        if (resultId <= 0) {
                            ok = false;
                            return;
                        }
                        ok = true;
                    }
                    if (!pkgFields.isEmpty()) {
                        // The `ok = update(...) || ok` this replaced did TWO jobs:
                        // it masked a failed rename behind a successful identity
                        // edit (the bug), and it set ok on a successful rename (not
                        // the bug). Dropping the whole expression dropped both, so
                        // a name-only update — no identity fields, ok never set by
                        // the block above — committed the rename and then reported
                        // "update failed". Both halves are spelled out now.
                        if (!EquipmentStorage::updatePackageFieldsStatic(db, resultId, pkgFields)) {
                            ok = false;
                            return;
                        }
                        ok = true;
                    }
                    view.package = EquipmentStorage::loadPackageStatic(db, resultId);
                    view.grinder = EquipmentStorage::loadGrinderItemStatic(db, resultId);
                    view.basket = EquipmentStorage::loadBasketItemStatic(db, resultId);
                    view.puckPrep = EquipmentStorage::loadPuckPrepItemStatic(db, resultId);
                    fillShotCount(db, resultId, view);
                });
                QMetaObject::invokeMethod(qApp, [ok, nameInUse, view, activeId, packageId, packageToJson, respond]() {
                    if (nameInUse) {
                        respond(QJsonObject{{"error", "That name is already in use by another equipment "
                                                      "package — choose a different name"}});
                        return;
                    }
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
        }),
        McpRegistryHelpers::asyncAction("merge", "settings",
[shotHistory, settings, packageToJson, fillShotCount](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            if (!shotHistory || !shotHistory->isReady()) {
                respond(QJsonObject{{"error", "Storage not available"}});
                return;
            }
            const qint64 sourceId = args["sourcePackageId"].toInteger();
            const qint64 targetId = args["targetPackageId"].toInteger();
            if (sourceId <= 0 || targetId <= 0) {
                respond(QJsonObject{{"error", "Valid sourcePackageId and targetPackageId are required"}});
                return;
            }
            const qint64 activeId = settings ? settings->dye()->activeEquipmentId() : -1;
            const QString dbPath = shotHistory->databasePath();
            QThread* thread = QThread::create([=]() {
                EquipmentMergeResult merge;
                EquipmentPackageView view;
                const bool opened = withTempDb(dbPath, "mcp_equip_merge", [&](QSqlDatabase& db) {
                    merge = EquipmentStorage::mergePackagesStatic(db, sourceId, targetId);
                    if (!merge.ok)
                        return;
                    view.package = EquipmentStorage::loadPackageStatic(db, targetId);
                    view.grinder = EquipmentStorage::loadGrinderItemStatic(db, targetId);
                    view.basket = EquipmentStorage::loadBasketItemStatic(db, targetId);
                    view.puckPrep = EquipmentStorage::loadPuckPrepItemStatic(db, targetId);
                    fillShotCount(db, targetId, view);
                });
                const QVariantMap pkgMap = view.toVariantMap();
                QMetaObject::invokeMethod(qApp, [=]() {
                    if (!opened) { respond(QJsonObject{{"error", "Could not open shot database"}}); return; }
                    if (!merge.ok) {
                        static const QHash<QString, QString> kWhy{
                            {"samePackage", "sourcePackageId and targetPackageId must be two different packages"},
                            {"sourceNotFound", "Source package not found"},
                            {"targetNotFound", "Target package not found"},
                            {"sqlFailed", "Merge failed; nothing was moved"}};
                        respond(QJsonObject{{"error", kWhy.value(merge.error, QStringLiteral("Merge failed"))}});
                        return;
                    }
                    // Tell the app its inventory changed. This tool writes on its own
                    // connection, so nothing else emits — and an Equipment page left open
                    // would keep offering the package this merge just deleted. Tapping it
                    // writes the dead id onto the active bag through setActiveEquipmentId,
                    // which is the same orphaning the merge was called to repair.
                    if (settings && settings->dye()->equipmentStorage())
                        settings->dye()->equipmentStorage()->notifyPackagesChangedExternally();
                    // The active package cannot be one that no longer exists: when the
                    // merged-away source was active, move the selection to the survivor
                    // (which also re-applies its last grind + rpm to the next shot).
                    if (activeId == sourceId && settings)
                        settings->dye()->switchToEquipment(pkgMap);
                    respond(QJsonObject{{"success", true},
                        {"package", packageToJson(view, activeId == sourceId ? targetId : activeId)},
                        {"shotsMoved", merge.shotsMoved},
                        {"bagsMoved", merge.bagsMoved},
                        {"recipesMoved", merge.recipesMoved}});
                }, Qt::QueuedConnection);
            });
            QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        }, QStringLiteral("Fold one equipment package into another and delete it — irreversible")),
    };

    registry->registerActionTool(
        "equipment",
        "Equipment packages — a grinder identity plus an optional basket, shared by every bag and "
        "shot that references it. list returns the inventory with each package's last grind/rpm; "
        "select sets the active bag's package and applies that package's last dial; update edits one "
        "(and forks a new identity when a component changes); merge folds a wrongly forked package "
        "into another and deletes it. Edits "
        "apply to every referencing bag and shot: get_agent_file topic \"equipment\".",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"packageId", QJsonObject{{"type", "integer"}, {"description", "Package id from action=list (select, update)"}}},
                {"name", QJsonObject{{"type", "string"}, {"description", "update only: display name"}}},
                {"grinderBrand", QJsonObject{{"type", "string"}, {"description", "update only"}}},
                {"grinderModel", QJsonObject{{"type", "string"}, {"description", "update only; a change re-derives rpmAdjustable"}}},
                {"grinderBurrs", QJsonObject{{"type", "string"}, {"description", "update only"}}},
                {"basketBrand", QJsonObject{{"type", "string"}, {"description", "update only"}}},
                {"basketModel", QJsonObject{{"type", "string"}, {"description", "update only"}}},
                {"puckPrep", QJsonObject{{"type", "object"},
                    {"description", "update only: puck-prep flags; provided flags override, others keep their value"},
                    {"properties", QJsonObject{
                        {"wdt", QJsonObject{{"type", "boolean"}}},
                        {"shaker", QJsonObject{{"type", "boolean"}}},
                        {"puckScreen", QJsonObject{{"type", "boolean"}}},
                        {"paperFilter", QJsonObject{{"type", "boolean"}}},
                        {"rdt", QJsonObject{{"type", "boolean"}}}
                    }}}},
                {"sourcePackageId", QJsonObject{{"type", "integer"}, {"description", "merge only: package to fold in and delete"}}},
                {"targetPackageId", QJsonObject{{"type", "integer"}, {"description", "merge only: package that survives and receives the history"}}},
                {"confirmed", QJsonObject{{"type", "boolean"}, {"description", "Set to true after user confirms this action in chat"}}}
            }}
        },
        equipmentActions);








}
