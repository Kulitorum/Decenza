// ShotServer recipes surface (add-recipes): REST API + /recipes management
// page. A recipe is the whole drink (profile + linked bag + equipment +
// dose/yield/temp + the recipe's own grind + steam block).
//
// Routing conventions follow the other domains: reads run storage statics on
// a background thread (never the main thread — CLAUDE.md); mutations go
// through the app's RecipeStorage instance via one-shot signal connections so
// the in-app UI's recipesChanged refreshes fire exactly like local edits; and
// /activate calls MainController's single activation path — the same code the
// idle pill tap runs. All routes sit behind the server's auth gate.

#include "shotserver.h"
#include "../controllers/maincontroller.h"
#include "../controllers/profilemanager.h"
#include "../core/dbutils.h"
#include "../core/settings.h"
#include "../core/settings_dye.h"
#include "../core/yieldspec.h"
#include "../history/coffeebagstorage.h"
#include "../history/recipepromotion.h"
#include "../history/recipestorage.h"
#include "../history/shothistorystorage.h"
#include "webtemplates/base_css.h"
#include "webtemplates/menu_css.h"
#include "webtemplates/menu_html.h"
#include "webtemplates/menu_js.h"
#include "webtemplates/management_css.h"
#include "webtemplates/management_html.h"
#include "webtemplates/management_js.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QThread>
#include <QUuid>
#include <optional>

namespace {

QJsonObject webRecipeJson(const Recipe& r, int activeRecipeId, QSqlDatabase* db,
                          qint64 shotCount = -1,
                          const QHash<QString, QString>& bevByTitle = {})
{
    QJsonObject o;
    o["id"] = r.id;
    o["name"] = r.name;
    o["profileTitle"] = r.profileTitle;
    // Stored drink type; legacy rows derive from the blocks (embedded profile
    // JSON supplies beverage_type when present).
    if (!r.drinkType.isEmpty()) {
        o["drinkType"] = r.drinkType;
    } else {
        QString bev;
        if (!r.profileJson.isEmpty())
            bev = QJsonDocument::fromJson(r.profileJson.toUtf8())
                      .object().value(QStringLiteral("beverage_type")).toString();
        // Installed profiles embed no JSON — the main-thread-captured catalog
        // snapshot supplies their beverage_type (else tea derives as espresso).
        if (bev.isEmpty())
            bev = bevByTitle.value(r.profileTitle.trimmed().toLower());
        o["drinkType"] = Recipe::deriveDrinkType(r, bev);
    }
    o["bagId"] = r.bagId;
    o["beanBaseId"] = r.beanBaseId;
    o["roasterName"] = r.roasterName;
    o["coffeeName"] = r.coffeeName;
    o["equipmentId"] = r.equipmentId;
    o["doseG"] = r.doseG;
    // Yield spec (add-yield-ratio-anchor): sparse, mutually exclusive keys —
    // yieldG (grams) for an absolute anchor, yieldRatio (dose multiplier) for
    // a ratio; mode "none" emits neither.
    if (r.yieldMode == QLatin1String("absolute") && r.yieldValue > 0)
        o["yieldG"] = r.yieldValue;
    else if (r.yieldMode == QLatin1String("ratio") && r.yieldValue > 0)
        o["yieldRatio"] = r.yieldValue;
    o["tempOffsetC"] = r.tempOffsetC;
    o["grindPinned"] = r.grindPinned;
    o["rpmPinned"] = r.rpmPinned;
    if (!r.steamJson.isEmpty())
        o["steam"] = QJsonDocument::fromJson(r.steamJson.toUtf8()).object();
    if (!r.hotWaterJson.isEmpty())
        o["hotWater"] = QJsonDocument::fromJson(r.hotWaterJson.toUtf8()).object();
    o["archived"] = r.archived;
    o["createdFromShotId"] = r.createdFromShotId;
    o["clonedFromRecipeId"] = r.clonedFromRecipeId;
    if (r.lastUsedEpoch > 0) {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(r.lastUsedEpoch);
        o["lastUsed"] = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
    }
    if (r.createdEpoch > 0) {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(r.createdEpoch);
        o["created"] = dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
    }
    if (shotCount >= 0)
        o["shotCount"] = shotCount;
    o["isActive"] = r.id == activeRecipeId;
    if (db) {
        // The linked bag: staleness flag (bag finished / gone — display
        // state, never a gate). Grind is the recipe's own value
        // (fix-recipe-grind-integrity); nothing resolves from the bag.
        if (r.bagId > 0) {
            const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(*db, r.bagId);
            if (!bag.isValid() || !bag.inInventory)
                o["bagStale"] = true;
        } else if (!r.beanBaseId.isEmpty() || !r.roasterName.isEmpty()
                   || !r.coffeeName.isEmpty()) {
            // A bean identity with no linked bag (unresolved migration)
            // presents as stale too — wake-on-restock will re-home it.
            o["bagStale"] = true;
        }
    }
    // Field name kept as "effectiveGrind" for JS/API compatibility — since
    // fix-recipe-grind-integrity it is simply the recipe's own grind.
    if (!r.grindPinned.isEmpty())
        o["effectiveGrind"] = r.grindPinned;
    return o;
}

// Recipe field map from a JSON body (shared by create/update). Only present
// keys land in the map; the `steam` object is stored compact.
QVariantMap recipeFieldsFromBody(const QJsonObject& body)
{
    QVariantMap fields;
    static const QStringList kStringKeys = {
        "name", "profileTitle", "profileJson", "beanBaseId", "roasterName", "coffeeName",
        "grindPinned", "drinkType"};
    for (const QString& key : kStringKeys) {
        if (body.contains(key))
            fields.insert(key, body[key].toString());
    }
    if (body.contains("bagId"))
        fields.insert("bagId", body["bagId"].toInteger());
    if (body.contains("equipmentId"))
        fields.insert("equipmentId", body["equipmentId"].toInteger());
    if (body.contains("rpmPinned"))
        fields.insert("rpmPinned", body["rpmPinned"].toInteger());
    if (body.contains("doseG"))
        fields.insert("doseG", body["doseG"].toDouble());
    // Yield spec (add-yield-ratio-anchor): sparse, mutually exclusive keys —
    // writing one IS setting the mode, implicitly clearing the other. The
    // both-present rejection happens in the route handlers before this runs.
    // 0/negative clears the anchor entirely.
    if (body.contains("yieldG")) {
        const double g = body["yieldG"].toDouble();
        fields.insert("yieldValue", g > 0 ? g : 0.0);
        fields.insert("yieldMode", g > 0 ? QStringLiteral("absolute") : QStringLiteral("none"));
    } else if (body.contains("yieldRatio")) {
        const double ratio = body["yieldRatio"].toDouble();
        fields.insert("yieldValue", ratio > 0 ? YieldSpec::clampRatio(ratio) : 0.0);
        fields.insert("yieldMode", ratio > 0 ? QStringLiteral("ratio") : QStringLiteral("none"));
    }
    if (body.contains("tempOffsetC"))
        fields.insert("tempOffsetC", body["tempOffsetC"].toDouble());
    if (body.contains("steam"))
        fields.insert("steamJson", QString::fromUtf8(
            QJsonDocument(body["steam"].toObject()).toJson(QJsonDocument::Compact)));
    if (body.contains("hotWater"))
        fields.insert("hotWaterJson", QString::fromUtf8(
            QJsonDocument(body["hotWater"].toObject()).toJson(QJsonDocument::Compact)));
    if (body.contains("createdFromShotId"))
        fields.insert("createdFromShotId", body["createdFromShotId"].toInteger());
    return fields;
}

} // namespace

void ShotServer::handleRecipesApi(QTcpSocket* socket, const QString& method,
                                  const QString& path, const QByteArray& body)
{
    RecipeStorage* recipeStorage =
        m_mainController ? m_mainController->recipeStorage() : nullptr;
    if (!m_storage || !m_storage->isReady() || !recipeStorage) {
        sendJson(socket, QJsonDocument(QJsonObject{{"error", "Storage not available"}})
                             .toJson(QJsonDocument::Compact));
        return;
    }
    const int activeRecipeId = m_settings ? m_settings->dye()->activeRecipeId() : -1;
    const QString dbPath = m_storage->databasePath();
    QPointer<QTcpSocket> safeSocket = socket;
    QPointer<ShotServer> safeThis = this;
    const QJsonObject bodyJson = QJsonDocument::fromJson(body).object();

    auto respondJson = [safeThis, safeSocket](const QJsonObject& o, int status = 200) {
        if (!safeThis || !safeSocket)
            return;
        if (status == 200)
            safeThis->sendJson(safeSocket, QJsonDocument(o).toJson(QJsonDocument::Compact));
        else
            safeThis->sendResponse(safeSocket, status, "application/json",
                                   QJsonDocument(o).toJson(QJsonDocument::Compact));
    };

    // GET /api/recipes — list (query ?archived=1 to include archived)
    if (path == "/api/recipes" && method == "GET") {
        const QHash<QString, QString> bevByTitle =
            (m_mainController && m_mainController->profileManager())
                ? m_mainController->profileManager()->beverageTypeByTitleSnapshot()
                : QHash<QString, QString>();
        QThread* t = QThread::create([dbPath, activeRecipeId, respondJson, bevByTitle]() {
            QJsonArray recipes;
            const bool opened = withTempDb(dbPath, "web_recipes", [&](QSqlDatabase& db) {
                for (const InventoryRecipe& e : RecipeStorage::loadInventoryStatic(db, false))
                    recipes.append(webRecipeJson(e.recipe, activeRecipeId, &db, e.shotCount, bevByTitle));
                for (const InventoryRecipe& e : RecipeStorage::loadInventoryStatic(db, true))
                    recipes.append(webRecipeJson(e.recipe, activeRecipeId, &db, e.shotCount, bevByTitle));
            });
            QMetaObject::invokeMethod(qApp, [opened, recipes, respondJson]() {
                if (!opened)
                    respondJson(QJsonObject{{"error", "Could not open database"}}, 500);
                else
                    respondJson(QJsonObject{{"recipes", recipes}, {"count", recipes.size()}});
            }, Qt::QueuedConnection);
        });
        connect(t, &QThread::finished, t, &QThread::deleteLater);
        t->start();
        return;
    }

    // POST /api/recipes — create. Profile required unless the recipe is
    // hot-water-only (hasWater in the block) — the shared validation rule.
    if (path == "/api/recipes" && method == "POST") {
        // The retired absolute field fails LOUD: an old client writing ~90
        // into the delta column would be a 90° offset.
        if (bodyJson.contains(QStringLiteral("temperatureOverrideC"))) {
            respondJson(QJsonObject{{"error", "temperatureOverrideC was replaced by tempOffsetC — a SIGNED DELTA in Celsius against the recipe's profile (recipe-relative-temp-offset). Rejected rather than silently dropped: an absolute written into the delta field would corrupt the recipe's temperature."}}, 400);
            return;
        }
        // One yield anchor per recipe: both keys at once is a contradiction,
        // rejected loudly (add-yield-ratio-anchor; mirrors the MCP tools).
        if (bodyJson.contains(QStringLiteral("yieldG")) && bodyJson.contains(QStringLiteral("yieldRatio"))) {
            respondJson(QJsonObject{{"error", "yieldG and yieldRatio are mutually exclusive — a recipe holds ONE yield anchor (an absolute gram target OR a ratio of the dose). Send exactly one; writing it replaces the other automatically."}}, 400);
            return;
        }
        QVariantMap fields = recipeFieldsFromBody(bodyJson);
        // The REST route accepts free text — enforce the drink-type vocabulary
        // (the bundled page's <select> constrains only the browser form).
        const QString requestedType = fields.value("drinkType").toString();
        if (!requestedType.isEmpty() && !Recipe::isKnownDrinkType(requestedType)) {
            respondJson(QJsonObject{{"error", QString("Unknown drinkType '%1'").arg(requestedType)}}, 400);
            return;
        }
        if (!Recipe::saveValidationPasses(fields.value("name").toString(),
                                          fields.value("profileTitle").toString(),
                                          fields.value("hotWaterJson").toString())) {
            respondJson(QJsonObject{{"error", "name is required, and profileTitle is required "
                                              "unless the recipe has a hot-water block with "
                                              "hasWater true"}}, 400);
            return;
        }
        // Derive the drink type when the caller didn't state one.
        if (fields.value("drinkType").toString().isEmpty()) {
            const Recipe shaped = Recipe::fromVariantMap(fields);
            QString bev;
            if (!shaped.profileJson.isEmpty())
                bev = QJsonDocument::fromJson(shaped.profileJson.toUtf8())
                          .object().value(QStringLiteral("beverage_type")).toString();
            if (bev.isEmpty() && m_mainController && m_mainController->profileManager())
                bev = m_mainController->profileManager()->beverageTypeForTitle(shaped.profileTitle);
            fields.insert("drinkType", Recipe::deriveDrinkType(shaped, bev));
        }
        // Correlate on a request token: recipeCreated is a broadcast and a
        // concurrent create from another surface must not satisfy (or steal)
        // this listener.
        const QString createToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
        fields.insert(QStringLiteral("requestToken"), createToken);
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(recipeStorage, &RecipeStorage::recipeCreated, this,
            [conn, respondJson, createToken](qint64 recipeId, const QVariantMap& recipe) {
                if (recipe.value(QStringLiteral("requestToken")).toString() != createToken)
                    return;  // another surface's create
                disconnect(*conn);
                if (recipeId <= 0) {
                    respondJson(QJsonObject{{"error", "Create failed"}}, 500);
                } else {
                    QVariantMap clean = recipe;
                    clean.remove(QStringLiteral("requestToken"));
                    respondJson(QJsonObject::fromVariantMap(clean));
                }
            });
        recipeStorage->requestCreateRecipe(fields);
        return;
    }

    // POST /api/recipes/from-shot/<shotId> — promotion
    if (path.startsWith("/api/recipes/from-shot/") && method == "POST") {
        const qint64 shotId = path.mid(QStringLiteral("/api/recipes/from-shot/").size()).toLongLong();
        const QString name = bodyJson["name"].toString().trimmed();
        if (shotId <= 0 || name.isEmpty()) {
            respondJson(QJsonObject{{"error", "shot id and a non-empty name are required"}}, 400);
            return;
        }
        const bool hasMilkProvided = bodyJson.contains("hasMilk");
        const bool hasMilk = bodyJson["hasMilk"].toBool();
        const QString fallbackSteam =
            m_mainController ? m_mainController->currentSteamSpecJson() : QString();
        QThread* t = QThread::create([dbPath, shotId, name, hasMilkProvided, hasMilk,
                                      fallbackSteam, recipeStorage, respondJson]() {
            ShotRecord record;
            const bool opened = withTempDb(dbPath, "web_recipe_promote", [&](QSqlDatabase& db) {
                record = ShotHistoryStorage::loadShotRecordStatic(db, shotId);
            });
            QMetaObject::invokeMethod(qApp, [opened, record, name, hasMilkProvided,
                                             hasMilk, fallbackSteam, recipeStorage, respondJson]() {
                if (!opened) {
                    respondJson(QJsonObject{{"error", "Could not open database"}}, 500);
                    return;
                }
                if (record.summary.id <= 0) {
                    respondJson(QJsonObject{{"error", "Shot not found"}}, 404);
                    return;
                }
                // The shared promotion semantics (bean link, bag link, the
                // shot's own grind, steam/hot-water snapshots) — exactly what
                // the MCP recipe_create_from_shot tool builds.
                const std::optional<bool> hasMilkOverride =
                    hasMilkProvided ? std::optional<bool>(hasMilk) : std::nullopt;
                QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
                    record, name, hasMilkOverride, fallbackSteam);
                const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
                fields.insert(QStringLiteral("requestToken"), token);
                auto conn = std::make_shared<QMetaObject::Connection>();
                *conn = QObject::connect(recipeStorage, &RecipeStorage::recipeCreated, qApp,
                    [conn, respondJson, token](qint64 recipeId, const QVariantMap& recipe) {
                        if (recipe.value(QStringLiteral("requestToken")).toString() != token)
                            return;  // another surface's create
                        QObject::disconnect(*conn);
                        if (recipeId <= 0) {
                            respondJson(QJsonObject{{"error", "Create failed"}}, 500);
                        } else {
                            QVariantMap clean = recipe;
                            clean.remove(QStringLiteral("requestToken"));
                            respondJson(QJsonObject::fromVariantMap(clean));
                        }
                    });
                recipeStorage->requestCreateRecipe(fields);
            }, Qt::QueuedConnection);
        });
        connect(t, &QThread::finished, t, &QThread::deleteLater);
        t->start();
        return;
    }

    // /api/recipe/<id>[/<action>]
    if (path.startsWith("/api/recipe/")) {
        const QStringList parts = path.mid(QStringLiteral("/api/recipe/").size()).split('/');
        const qint64 recipeId = parts.value(0).toLongLong();
        const QString action = parts.value(1);
        if (recipeId <= 0) {
            respondJson(QJsonObject{{"error", "Invalid recipe id"}}, 400);
            return;
        }

        // GET /api/recipe/<id>
        if (action.isEmpty() && method == "GET") {
            QThread* t = QThread::create([dbPath, recipeId, activeRecipeId, respondJson]() {
                QJsonObject result;
                bool found = false;
                const bool opened = withTempDb(dbPath, "web_recipe_get", [&](QSqlDatabase& db) {
                    const Recipe r = RecipeStorage::loadRecipeStatic(db, recipeId);
                    if (r.isValid()) {
                        result = webRecipeJson(r, activeRecipeId, &db);
                        found = true;
                    }
                });
                QMetaObject::invokeMethod(qApp, [opened, found, result, respondJson]() {
                    if (!opened)
                        respondJson(QJsonObject{{"error", "Could not open database"}}, 500);
                    else if (!found)
                        respondJson(QJsonObject{{"error", "Recipe not found"}}, 404);
                    else
                        respondJson(result);
                }, Qt::QueuedConnection);
            });
            connect(t, &QThread::finished, t, &QThread::deleteLater);
            t->start();
            return;
        }

        if (method != "POST") {
            respondJson(QJsonObject{{"error", "Method not allowed"}}, 405);
            return;
        }

        // POST /api/recipe/<id> — update
        if (action.isEmpty()) {
            // The retired absolute field fails LOUD (see the create route).
            if (bodyJson.contains(QStringLiteral("temperatureOverrideC"))) {
                respondJson(QJsonObject{{"error", "temperatureOverrideC was replaced by tempOffsetC — a SIGNED DELTA in Celsius against the recipe's profile (recipe-relative-temp-offset). Rejected rather than silently dropped: an absolute written into the delta field would corrupt the recipe's temperature."}}, 400);
                return;
            }
            // One yield anchor per recipe (see the create route).
            if (bodyJson.contains(QStringLiteral("yieldG")) && bodyJson.contains(QStringLiteral("yieldRatio"))) {
                respondJson(QJsonObject{{"error", "yieldG and yieldRatio are mutually exclusive — a recipe holds ONE yield anchor (an absolute gram target OR a ratio of the dose). Send exactly one; writing it replaces the other automatically."}}, 400);
                return;
            }
            QVariantMap fields = recipeFieldsFromBody(bodyJson);
            const QString requestedType = fields.value("drinkType").toString();
            if (!requestedType.isEmpty() && !Recipe::isKnownDrinkType(requestedType)) {
                respondJson(QJsonObject{{"error", QString("Unknown drinkType '%1'").arg(requestedType)}}, 400);
                return;
            }
            // Mirror the MCP recipe_update guard: clearing the profile is only
            // valid when this same call makes the recipe hot-water-only. The
            // storage layer now enforces the resulting-row invariant too; this
            // early check just gives the caller a named error instead of a
            // generic update failure.
            if (fields.contains("profileTitle")
                && fields.value("profileTitle").toString().trimmed().isEmpty()
                && !Recipe::hotWaterActive(fields.value("hotWaterJson").toString())) {
                respondJson(QJsonObject{{"error", "profileTitle can only be cleared when the same "
                                                  "call sets a hot-water block with hasWater true"}}, 400);
                return;
            }
            if (fields.isEmpty()) {
                respondJson(QJsonObject{{"error", "No editable fields provided"}}, 400);
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(recipeStorage, &RecipeStorage::recipeUpdated, this,
                [conn, recipeId, respondJson](qint64 updatedId, bool success) {
                    if (updatedId != recipeId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{"updated", true}, {"recipeId", recipeId}});
                    else
                        respondJson(QJsonObject{{"error", "Recipe not found or update failed"}}, 404);
                });
            // Installed profiles embed no JSON: resolve the new title's
            // beverage_type (main thread) for the storage-side drink-type
            // re-derivation. Transient hint — RecipeStorage strips it.
            if (!fields.value("profileTitle").toString().trimmed().isEmpty()
                && fields.value("drinkType").toString().isEmpty()
                && m_mainController && m_mainController->profileManager()) {
                const QString bev = m_mainController->profileManager()->beverageTypeForTitle(
                    fields.value("profileTitle").toString());
                if (!bev.isEmpty())
                    fields.insert("profileBeverageType", bev);
            }
            recipeStorage->requestUpdateRecipe(recipeId, fields);
            return;
        }

        // POST /api/recipe/<id>/clone {name}
        if (action == "clone") {
            const QString name = bodyJson["name"].toString().trimmed();
            if (name.isEmpty()) {
                respondJson(QJsonObject{{"error", "name is required"}}, 400);
                return;
            }
            const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(recipeStorage, &RecipeStorage::recipeCreated, this,
                [conn, respondJson, token](qint64 newId, const QVariantMap& recipe) {
                    if (recipe.value(QStringLiteral("requestToken")).toString() != token)
                        return;  // another surface's create
                    disconnect(*conn);
                    if (newId <= 0) {
                        respondJson(QJsonObject{{"error", "Clone failed"}}, 500);
                    } else {
                        QVariantMap clean = recipe;
                        clean.remove(QStringLiteral("requestToken"));
                        respondJson(QJsonObject::fromVariantMap(clean));
                    }
                });
            recipeStorage->requestCloneRecipe(recipeId, name, token);
            return;
        }

        // POST /api/recipe/<id>/archive {restore?, delete?}
        if (action == "archive") {
            const bool restore = bodyJson["restore"].toBool();
            const bool hardDelete = bodyJson["delete"].toBool();
            if (!restore && m_settings && m_settings->dye()->activeRecipeId() == recipeId
                && m_mainController)
                m_mainController->deactivateRecipe();
            if (hardDelete) {
                auto conn = std::make_shared<QMetaObject::Connection>();
                *conn = connect(recipeStorage, &RecipeStorage::recipeDeleted, this,
                    [conn, recipeId, respondJson](qint64 deletedId, bool success) {
                        if (deletedId != recipeId)
                            return;
                        disconnect(*conn);
                        if (success)
                            respondJson(QJsonObject{{"deleted", true}});
                        else
                            respondJson(QJsonObject{{"error",
                                "Delete refused — the recipe has shots (archive instead) or was not found"}}, 409);
                    });
                recipeStorage->requestDeleteRecipe(recipeId);
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(recipeStorage, &RecipeStorage::recipeUpdated, this,
                [conn, recipeId, restore, respondJson](qint64 updatedId, bool success) {
                    if (updatedId != recipeId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{restore ? "restored" : "archived", true}});
                    else
                        respondJson(QJsonObject{{"error", "Recipe not found"}}, 404);
                });
            if (restore)
                recipeStorage->requestUnarchiveRecipe(recipeId);
            else
                recipeStorage->requestArchiveRecipe(recipeId);
            return;
        }

        // POST /api/recipe/<id>/activate — the pill-tap equivalent
        if (action == "activate") {
            if (!m_mainController) {
                respondJson(QJsonObject{{"error", "Controller not available"}}, 500);
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(m_mainController, &MainController::recipeActivated, this,
                [conn, recipeId, respondJson](qint64 activatedId, bool success) {
                    if (activatedId != recipeId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{"activated", true}, {"recipeId", recipeId}});
                    else
                        respondJson(QJsonObject{{"error", "Recipe not found or activation failed"}}, 404);
                });
            m_mainController->activateRecipe(recipeId);
            return;
        }
    }

    respondJson(QJsonObject{{"error", "Unknown recipes endpoint"}}, 404);
}

QString ShotServer::generateRecipesPage() const
{
    QString html = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Decenza — Recipes</title>
    <style>
)HTML";
    html += WEB_CSS_VARIABLES;
    html += WEB_CSS_HEADER;
    html += WEB_CSS_MENU;
    html += WEB_CSS_MANAGEMENT;
    html += R"HTML(
    </style>
</head>
)HTML";
    html += generateManagementHeader(QStringLiteral("&#128209; Recipes"));
    html += R"HTML(
    <div class="container">
        <div class="toolbar">
            <button class="primary" onclick="openEditor(null)">+ Add Recipe</button>
        </div>
        <div class="searchbar" id="searchbar" style="display:none">
            <div class="search-wrap">
                <input class="search" id="search" placeholder="Search recipes…" oninput="applyFilter()">
                <button class="search-clear" id="searchClear" onclick="clearSearch()" style="display:none">&times;</button>
            </div>
            <button id="sortField" onclick="cycleSort()">Sort: Date used</button>
            <button id="sortDir" onclick="toggleDir()">Newest first</button>
        </div>
        <div id="status"></div>
        <div id="list"></div>
        <div class="section-head" id="archivedHead" style="display:none">
            <button onclick="toggleArchived()" id="archivedToggle">Show archived (0)</button>
        </div>
        <div id="archivedList" style="display:none"></div>
    </div>

    <dialog id="editor">
        <h2 id="editorTitle">Recipe</h2>
        <label>Name</label><input id="fName">
        <label>Profile title</label><input id="fProfile">
        <label>Bag (roaster/coffee fill in automatically)</label>
        <select id="fBag"><option value="0">(no bag)</option></select>
        <div class="grid-2">
            <div><label>Roaster</label><input id="fRoaster"></div>
            <div><label>Coffee</label><input id="fCoffee"></div>
        </div>
        <div class="grid-2">
            <div><label>Dose (g)</label><input id="fDose" type="number" step="0.1"></div>
            <div><label>Temp offset (&deg;C)</label><input id="fTemp" type="number" step="0.1"></div>
        </div>
        <label>Yield anchor (a fixed weight OR a ratio of the dose — never both)</label>
        <div class="grid-2">
            <select id="fYieldMode">
                <option value="none">(none — profile default)</option>
                <option value="absolute">Fixed (g)</option>
                <option value="ratio">Ratio (1:x)</option>
            </select>
            <input id="fYieldValue" type="number" step="0.1">
        </div>
        <div class="grid-2">
            <div><label>Grind (blank adopts the bag's dial on create)</label><input id="fGrind"></div>
            <div><label>RPM (0 = unset)</label><input id="fRpm" type="number" step="1"></div>
        </div>
        <label>Drink type</label>
        <select id="fDrinkType">
            <option value="">(automatic from blocks)</option>
            <option value="espresso">Espresso</option>
            <option value="filter">Filter</option>
            <option value="americano">Americano</option>
            <option value="long_black">Long black</option>
            <option value="latte">Latte / Cappuccino</option>
            <option value="tea">Tea (portafilter)</option>
            <option value="tea_hotwater">Tea (hot water)</option>
        </select>
        <details class="dialog-section">
            <summary>Milk / steam</summary>
            <div class="check-row"><input type="checkbox" id="fHasMilk"><label for="fHasMilk">Milk drink</label></div>
            <div class="grid-2">
                <div><label>Milk (g)</label><input id="fMilk" type="number" step="1"></div>
                <div><label>Pitcher name</label><input id="fPitcher"></div>
            </div>
            <div class="grid-2">
                <div><label>Steam duration (s)</label><input id="fSteamDuration" type="number" step="1"></div>
                <div><label>Steam flow</label><input id="fSteamFlow" type="number" step="0.1"></div>
            </div>
            <label>Steam temperature (&deg;C)</label><input id="fSteamTemp" type="number" step="1">
        </details>
        <details class="dialog-section">
            <summary>Hot water</summary>
            <div class="check-row"><input type="checkbox" id="fHasWater"><label for="fHasWater">Add hot water (Americano / long black)</label></div>
            <label>Water vessel name</label><input id="fVessel">
            <label>Water order</label>
            <select id="fWaterOrder">
                <option value="after">After espresso (Americano)</option>
                <option value="before">Before espresso (long black)</option>
            </select>
        </details>
        <div class="dialog-actions">
            <button onclick="el('editor').close()">Cancel</button>
            <button class="primary" onclick="saveEditor()">Save</button>
        </div>
    </dialog>

    <dialog id="repoint">
        <h2>Choose beans</h2>
        <div class="muted" id="repointHint">Pick an open bag to re-point this recipe.</div>
        <div class="search-results" id="repointList"></div>
        <div class="dialog-actions">
            <button onclick="el('repoint').close()">Cancel</button>
        </div>
    </dialog>

    <script>
)HTML";
    html += WEB_JS_MENU;
    html += WEB_JS_POWER_CONTROL;
    html += WEB_JS_MANAGEMENT;
    html += R"HTML(
        let editingId = null;
        let recipes = [];          // full list (active + archived), as fetched
        let bags = [];
        let bagsLoaded = false;
        let filterText = '';
        let showArchived = false;

        // Sort field cycle mirrors the app's Recipes sort bar.
        const SORTS = [
            ['lastUsed', 'Date used'],
            ['created', 'Date created'],
            ['coffee', 'Coffee'],
            ['profileTitle', 'Profile'],
            ['name', 'Name'],
        ];
        let sortIdx = 0;
        let sortAsc = false;   // date fields default newest-first

        const DRINK_LABELS = {
            espresso: 'Espresso', filter: 'Filter', americano: 'Americano',
            long_black: 'Long black', latte: 'Latte', tea: 'Tea', tea_hotwater: 'Tea',
        };
        const drinkLabel = (t) => DRINK_LABELS[t] || (t ? t.replace('_', ' ') : '');
        const isTeaType = (t) => t === 'tea' || t === 'tea_hotwater';

        function makePlaceholder() {
            const d = document.createElement('div');
            d.className = 'thumb placeholder';
            d.innerHTML = '&#9749;';
            return d;
        }
        function thumbHtml(r) {
            if (r.bagId > 0 && !r.bagStale)
                return '<img class="thumb" src="/api/bag/' + r.bagId + '/image" alt="" '
                     + 'onerror="this.replaceWith(makePlaceholder())">';
            return '<div class="thumb placeholder">&#9749;</div>';
        }

        function loadBags() {
            getJson('/api/bags')
                .then(d => {
                    bags = d.bags || [];
                    bagsLoaded = true;
                    const sel = el('fBag');
                    sel.innerHTML = '<option value="0">(no bag)</option>' + bags.map(b =>
                        '<option value="' + b.id + '">'
                        + esc(((b.roasterName || '') + ' ' + (b.coffeeName || '')).trim()
                              + (b.roastDate ? ' · ' + b.roastDate : ''))
                        + '</option>').join('');
                })
                .catch(e => {
                    // Say so — a silently empty select reads as "no bags exist".
                    console.warn('Could not load bags:', e);
                    const sel = el('fBag');
                    const opt = document.createElement('option');
                    opt.value = ''; opt.disabled = true; opt.textContent = '(bags unavailable)';
                    sel.appendChild(opt);
                });
        }

        // Keep the roaster/coffee fields in lockstep with the chosen bag —
        // the label promises "roaster/coffee fill in automatically", and the
        // backend adopts identity only for fields the form does NOT send.
        function onBagChanged() {
            const id = parseInt(el('fBag').value, 10) || 0;
            const b = bags.find(x => x.id === id);
            if (!b) return;  // "(no bag)" or the fallback option: leave the fields as typed
            el('fRoaster').value = b.roasterName || '';
            el('fCoffee').value = b.coffeeName || '';
        }

        function load() {
            status('Loading…');
            getJson('/api/recipes')
                .then(d => { recipes = d.recipes || []; render(); status(''); })
                .catch(e => status('Could not load recipes: ' + e.message));
        }

        // --- Card rendering (matches the app's RecipeDrinkCard hierarchy) ---
        function planLine(r) {
            const parts = [];
            if (r.doseG > 0 && r.yieldG > 0)
                parts.push(r.doseG.toFixed(1) + 'g &rarr; ' + r.yieldG.toFixed(1) + 'g');
            else if (r.yieldRatio > 0)
                parts.push(r.doseG > 0
                    ? r.doseG.toFixed(1) + 'g &rarr; ' + (r.doseG * r.yieldRatio).toFixed(1) + 'g (1:' + r.yieldRatio + ')'
                    : '1:' + r.yieldRatio);
            else if (r.doseG > 0)
                parts.push(r.doseG.toFixed(1) + 'g');
            if ((r.tempOffsetC || 0) !== 0)
                parts.push((r.tempOffsetC > 0 ? '+' : '') + r.tempOffsetC + '&deg;C');
            if (r.effectiveGrind) parts.push('grind ' + esc(r.effectiveGrind));
            if (r.rpmPinned > 0) parts.push(r.rpmPinned + ' rpm');
            return bullet(parts);
        }

        function drinkLine(r) {
            const parts = [];
            if (r.profileTitle) parts.push(esc(r.profileTitle));
            if (r.steam && r.steam.hasMilk && (r.steam.milkWeightG || 0) > 0)
                parts.push(r.steam.milkWeightG + 'g milk');
            if (r.hotWater && r.hotWater.hasWater)
                parts.push(r.hotWater.order === 'before' ? '+water (long black)' : '+water (americano)');
            const chip = r.drinkType
                ? '<span class="chip">' + esc(drinkLabel(r.drinkType)) + '</span> '
                : '';
            return chip + bullet(parts);
        }

        function beanLine(r) {
            if (r.bagStale)
                return '<span class="chip warn" onclick="openRepoint(' + r.id + ')">'
                     + 'Bag finished — tap to choose beans</span>';
            const bean = ((r.roasterName || '') + ' ' + (r.coffeeName || '')).trim();
            const parts = [];
            if (bean) parts.push(esc(bean));
            if (r.shotCount > 0) parts.push(r.shotCount + ' shots');
            return bullet(parts);
        }

        function cardHtml(r) {
            const actions = r.archived
                ? '<button onclick="archiveRecipe(' + r.id + ', true)">Restore</button>'
                : '<button class="primary" onclick="activate(' + r.id + ')"' + (r.isActive ? ' disabled' : '') + '>Activate</button>'
                  + '<button onclick="openEditor(' + r.id + ')">Edit</button>'
                  + '<button onclick="cloneRecipe(' + r.id + ')">Clone</button>'
                  + (r.shotCount > 0
                      ? '<button onclick="archiveRecipe(' + r.id + ', false)">Archive</button>'
                      : '<button class="danger" onclick="deleteRecipe(' + r.id + ')">Delete</button>');
            const drink = drinkLine(r);
            const bean = beanLine(r);
            const plan = planLine(r);
            let body = '<div class="card-body">'
                + '<div class="card-title">' + esc(r.name)
                + (r.isActive ? '<span class="badge">Active</span>' : '') + '</div>';
            if (drink) body += '<div class="attr-line">' + drink + '</div>';
            if (bean) body += '<div class="attr-line">' + bean + '</div>';
            if (plan) body += '<div class="plan-line">' + plan + '</div>';
            body += '</div>';
            return '<div class="card' + (r.isActive ? ' active' : '') + (r.archived ? ' dimmed' : '') + '">'
                + '<div class="card-head">' + thumbHtml(r) + body + '</div>'
                + '<div class="actions">' + actions + '</div></div>';
        }

        function matchesFilter(r) {
            if (!filterText) return true;
            const hay = [r.name, r.profileTitle, r.roasterName, r.coffeeName, drinkLabel(r.drinkType)]
                .join(' ').toLowerCase();
            return hay.indexOf(filterText) !== -1;
        }
        function sortRecipes(list) {
            const key = SORTS[sortIdx][0];
            const val = (r) => {
                if (key === 'coffee') return ((r.roasterName || '') + ' ' + (r.coffeeName || '')).trim().toLowerCase();
                if (key === 'name') return (r.name || '').toLowerCase();
                if (key === 'profileTitle') return (r.profileTitle || '').toLowerCase();
                return r[key] || '';   // lastUsed / created are ISO strings — lexical order works
            };
            const sorted = list.slice().sort((a, b) => {
                const va = val(a), vb = val(b);
                return va < vb ? -1 : (va > vb ? 1 : 0);
            });
            if (!sortAsc) sorted.reverse();
            return sorted;
        }

        function render() {
            const active = sortRecipes(recipes.filter(r => !r.archived && matchesFilter(r)));
            const archived = recipes.filter(r => r.archived);
            el('searchbar').style.display = recipes.some(r => !r.archived) ? '' : 'none';
            el('searchClear').style.display = filterText ? '' : 'none';

            if (!active.length) {
                el('list').innerHTML = filterText
                    ? '<p class="muted">No recipes match &ldquo;' + esc(filterText) + '&rdquo;.</p>'
                    : '<div class="empty"><h2>No recipes yet</h2>'
                      + '<div>Save one from a good shot in Shot History, or add one here.</div></div>';
            } else {
                el('list').innerHTML = '<div class="grid">' + active.map(cardHtml).join('') + '</div>';
            }

            el('archivedHead').style.display = archived.length ? '' : 'none';
            el('archivedToggle').textContent = (showArchived ? 'Hide archived (' : 'Show archived (')
                + archived.length + ')';
            const al = el('archivedList');
            al.style.display = showArchived && archived.length ? '' : 'none';
            al.innerHTML = showArchived
                ? '<div class="grid">' + archived.map(cardHtml).join('') + '</div>' : '';
        }

        // --- Search / sort controls ---
        function applyFilter() { filterText = el('search').value.trim().toLowerCase(); render(); }
        function clearSearch() { el('search').value = ''; filterText = ''; render(); }
        function cycleSort() {
            sortIdx = (sortIdx + 1) % SORTS.length;
            el('sortField').textContent = 'Sort: ' + SORTS[sortIdx][1];
            render();
        }
        function toggleDir() {
            sortAsc = !sortAsc;
            el('sortDir').textContent = sortAsc ? 'Oldest / A→Z first' : 'Newest / Z→A first';
            render();
        }
        function toggleArchived() { showArchived = !showArchived; render(); }

        // --- Actions ---
        function activate(id) {
            status('Activating…');
            post('/api/recipe/' + id + '/activate').then(load).catch(e => status(e.message));
        }
        function cloneRecipe(id) {
            const r = recipes.find(x => x.id === id) || {};
            const newName = prompt('Name for the copy:', 'Copy of ' + (r.name || ''));
            if (!newName) return;
            post('/api/recipe/' + id + '/clone', { name: newName }).then(load).catch(e => status(e.message));
        }
        function archiveRecipe(id, restore) {
            post('/api/recipe/' + id + '/archive', restore ? { restore: true } : {})
                .then(load).catch(e => status(e.message));
        }
        function deleteRecipe(id) {
            if (!confirm('Delete this recipe permanently?')) return;
            post('/api/recipe/' + id + '/archive', { delete: true }).then(load).catch(e => status(e.message));
        }

        // --- Stale-bag re-point picker ---
        function openRepoint(recipeId) {
            const r = recipes.find(x => x.id === recipeId) || {};
            const wantTea = isTeaType(r.drinkType);
            const source = () => {
                // Filter to open bags of the matching kind (unknown kind → allow).
                const open = bags.filter(b => {
                    if (!b.kind) return true;
                    return wantTea ? b.kind === 'tea' : b.kind !== 'tea';
                });
                const list = el('repointList');
                if (!open.length) {
                    list.innerHTML = '<div class="result muted">No open '
                        + (wantTea ? 'tea' : 'coffee') + ' bags to choose from.</div>';
                } else {
                    list.innerHTML = open.map(b =>
                        '<div class="result" onclick="pickBag(' + recipeId + ', ' + b.id + ')">'
                        + '<div class="r-title">' + esc(((b.roasterName || '') + ' ' + (b.coffeeName || '')).trim() || 'Bag ' + b.id) + '</div>'
                        + (b.roastDate ? '<div class="r-sub">roasted ' + esc(b.roastDate) + '</div>' : '')
                        + '</div>').join('');
                }
                el('repoint').showModal();
            };
            // Bags may not be loaded yet (or may be stale) — ensure fresh.
            getJson('/api/bags').then(d => { bags = d.bags || []; source(); })
                .catch(e => {
                    // Don't present a fetch failure as an empty inventory — say so.
                    el('repointList').innerHTML = '<div class="result muted">Could not load bags: '
                        + esc(e.message) + '</div>';
                    el('repoint').showModal();
                });
        }
        function pickBag(recipeId, bagId) {
            post('/api/recipe/' + recipeId, { bagId: bagId })
                .then(() => { el('repoint').close(); load(); })
                .catch(e => status(e.message));
        }

        // --- Create / edit ---
        function openEditor(id) {
            editingId = id;
            const r = recipes.find(x => x.id === id) || {};
            el('editorTitle').textContent = id ? 'Edit Recipe' : 'New Recipe';
            el('fName').value = r.name || '';
            el('fDrinkType').value = r.drinkType || '';
            el('fProfile').value = r.profileTitle || '';
            const bagSel = el('fBag');
            bagSel.onchange = onBagChanged;
            // A finished linked bag is not in the open-bag list — add it so
            // editing another field doesn't silently drop the link.
            if (r.bagId > 0 && !bags.some(b => b.id === r.bagId)) {
                const opt = document.createElement('option');
                opt.value = r.bagId;
                opt.textContent = ((r.roasterName || '') + ' ' + (r.coffeeName || '')).trim()
                    + (bagsLoaded ? ' (finished)' : ' (current bag)');
                bagSel.appendChild(opt);
            }
            bagSel.value = String(r.bagId > 0 ? r.bagId : 0);
            el('fRoaster').value = r.roasterName || '';
            el('fCoffee').value = r.coffeeName || '';
            el('fDose').value = r.doseG > 0 ? r.doseG : '';
            const yMode = r.yieldRatio > 0 ? 'ratio' : (r.yieldG > 0 ? 'absolute' : 'none');
            el('fYieldMode').value = yMode;
            el('fYieldValue').value = yMode === 'ratio' ? r.yieldRatio : (yMode === 'absolute' ? r.yieldG : '');
            el('fTemp').value = (r.tempOffsetC || 0) !== 0 ? r.tempOffsetC : '';
            el('fGrind').value = r.grindPinned || '';
            el('fRpm').value = r.rpmPinned > 0 ? r.rpmPinned : '';
            const steam = r.steam || {};
            el('fHasMilk').checked = !!steam.hasMilk;
            el('fMilk').value = steam.milkWeightG || '';
            el('fPitcher').value = steam.pitcherName || '';
            el('fSteamDuration').value = steam.durationSec || '';
            el('fSteamFlow').value = steam.flow || '';
            el('fSteamTemp').value = steam.temperatureC || '';
            const water = r.hotWater || {};
            el('fHasWater').checked = !!water.hasWater;
            el('fVessel').value = water.vesselName || '';
            el('fWaterOrder').value = water.order === 'before' ? 'before' : 'after';
            el('editor').showModal();
        }

        function saveEditor() {
            const name = el('fName').value.trim();
            const profileTitle = el('fProfile').value.trim();
            const hasWater = el('fHasWater').checked;
            if (!name) { status('Name is required'); return; }
            // Profile-less recipes are valid only as hot-water drinks (tea).
            if (!profileTitle && !hasWater) {
                status('Profile title is required (unless the recipe adds hot water)'); return;
            }
            const steam = {};
            if (el('fHasMilk').checked) steam.hasMilk = true;
            const milk = parseFloat(el('fMilk').value);
            if (milk > 0) steam.milkWeightG = milk;
            const pitcher = el('fPitcher').value.trim();
            if (pitcher) steam.pitcherName = pitcher;
            const dur = parseInt(el('fSteamDuration').value, 10);
            if (dur > 0) steam.durationSec = dur;
            const flow = parseFloat(el('fSteamFlow').value);
            if (flow > 0) steam.flow = flow;
            const stemp = parseFloat(el('fSteamTemp').value);
            if (stemp > 0) steam.temperatureC = stemp;
            const hotWater = {};
            if (hasWater) hotWater.hasWater = true;
            const vessel = el('fVessel').value.trim();
            if (vessel) hotWater.vesselName = vessel;
            hotWater.order = el('fWaterOrder').value === 'before' ? 'before' : 'after';
            const bodyData = {
                name: name,
                profileTitle: profileTitle,
                drinkType: el('fDrinkType').value,
                bagId: parseInt(el('fBag').value, 10) || 0,
                roasterName: el('fRoaster').value.trim(),
                coffeeName: el('fCoffee').value.trim(),
                doseG: parseFloat(el('fDose').value) || 0,
                tempOffsetC: parseFloat(el('fTemp').value) || 0,
                grindPinned: el('fGrind').value.trim(),
                rpmPinned: parseInt(el('fRpm').value, 10) || 0,
                steam: steam,
                hotWater: hotWater
            };
            // Yield anchor: send exactly ONE of yieldG / yieldRatio (mutually
            // exclusive server-side). Mode "none"/blank sends an explicit
            // yieldG: 0 clear — a deliberate choice in the mode select.
            {
                const yMode = el('fYieldMode').value;
                const yVal = parseFloat(el('fYieldValue').value) || 0;
                if (yMode === 'ratio' && yVal > 0) bodyData.yieldRatio = yVal;
                else if (yMode === 'absolute' && yVal > 0) bodyData.yieldG = yVal;
                else bodyData.yieldG = 0;
            }
            // Create: OMIT a blank grind so storage adopts the linked bag's
            // dial (the omit-adopts rule). Update keeps sending '' — there it
            // means "clear this recipe's grind".
            if (!editingId && bodyData.grindPinned === '')
                delete bodyData.grindPinned;
            const req = editingId ? post('/api/recipe/' + editingId, bodyData)
                                  : post('/api/recipes', bodyData);
            req.then(() => { el('editor').close(); load(); })
               .catch(e => status(e.message));
        }

        load();
        loadBags();
    </script>
</body>
</html>)HTML";
    return html;
}
