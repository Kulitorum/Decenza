// ShotServer recipes surface (add-recipes): REST API + /recipes management
// page. A recipe is the whole drink (profile + linked bag + equipment +
// dose/yield/temp + grind routing + steam block).
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
#include "../history/coffeebagstorage.h"
#include "../history/recipepromotion.h"
#include "../history/recipestorage.h"
#include "../history/shothistorystorage.h"
#include "webtemplates/base_css.h"
#include "webtemplates/menu_css.h"
#include "webtemplates/menu_html.h"
#include "webtemplates/menu_js.h"

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
    o["yieldG"] = r.yieldG;
    o["temperatureOverrideC"] = r.tempOverrideC;
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
    if (shotCount >= 0)
        o["shotCount"] = shotCount;
    o["isActive"] = r.id == activeRecipeId;
    if (db) {
        // The linked bag: staleness flag (bag finished / gone — display
        // state, never a gate) and the effective grind for the inherited
        // case, which resolves from the linked bag whether or not it is
        // still in inventory.
        if (r.bagId > 0) {
            const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(*db, r.bagId);
            if (bag.isValid()) {
                if (!bag.inInventory)
                    o["bagStale"] = true;
                if (r.grindPinned.isEmpty() && !bag.grinderSetting.isEmpty())
                    o["effectiveGrind"] = bag.grinderSetting;
            } else {
                o["bagStale"] = true;
            }
        } else if (!r.beanBaseId.isEmpty() || !r.roasterName.isEmpty()
                   || !r.coffeeName.isEmpty()) {
            // A bean identity with no linked bag (unresolved migration)
            // presents as stale too — wake-on-restock will re-home it.
            o["bagStale"] = true;
        }
        if (!r.grindPinned.isEmpty())
            o["effectiveGrind"] = r.grindPinned;
    }
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
    if (body.contains("yieldG"))
        fields.insert("yieldG", body["yieldG"].toDouble());
    if (body.contains("temperatureOverrideC"))
        fields.insert("tempOverrideC", body["temperatureOverrideC"].toDouble());
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
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(recipeStorage, &RecipeStorage::recipeCreated, this,
            [conn, respondJson](qint64 recipeId, const QVariantMap& recipe) {
                disconnect(*conn);
                if (recipeId <= 0)
                    respondJson(QJsonObject{{"error", "Create failed"}}, 500);
                else
                    respondJson(QJsonObject::fromVariantMap(recipe));
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
            QMetaObject::invokeMethod(qApp, [opened, record, shotId, name, hasMilkProvided,
                                             hasMilk, fallbackSteam, recipeStorage, respondJson]() {
                if (!opened) {
                    respondJson(QJsonObject{{"error", "Could not open database"}}, 500);
                    return;
                }
                if (record.summary.id <= 0) {
                    respondJson(QJsonObject{{"error", "Shot not found"}}, 404);
                    return;
                }
                // The shared promotion semantics (bean link, bag link, grind
                // pin-vs-inherit, steam/hot-water snapshots) — exactly what
                // the MCP recipe_create_from_shot tool builds.
                const std::optional<bool> hasMilkOverride =
                    hasMilkProvided ? std::optional<bool>(hasMilk) : std::nullopt;
                const QVariantMap fields = RecipePromotion::fieldsFromShotRecord(
                    record, name, hasMilkOverride, fallbackSteam);
                auto conn = std::make_shared<QMetaObject::Connection>();
                *conn = QObject::connect(recipeStorage, &RecipeStorage::recipeCreated, qApp,
                    [conn, respondJson](qint64 recipeId, const QVariantMap& recipe) {
                        QObject::disconnect(*conn);
                        if (recipeId <= 0)
                            respondJson(QJsonObject{{"error", "Create failed"}}, 500);
                        else
                            respondJson(QJsonObject::fromVariantMap(recipe));
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
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(recipeStorage, &RecipeStorage::recipeCreated, this,
                [conn, respondJson](qint64 newId, const QVariantMap& recipe) {
                    disconnect(*conn);
                    if (newId <= 0)
                        respondJson(QJsonObject{{"error", "Clone failed"}}, 500);
                    else
                        respondJson(QJsonObject::fromVariantMap(recipe));
                });
            recipeStorage->requestCloneRecipe(recipeId, name);
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
    html += R"HTML(
        .container { max-width: 900px; margin: 0 auto; padding: 1.5rem; }
        .card { background: var(--surface); border: 1px solid var(--border); border-radius: 8px; padding: 1rem; margin-bottom: 0.75rem; }
        .card.active { border-color: var(--accent); }
        .card h3 { margin-bottom: 0.25rem; }
        .card .sub { color: var(--text-secondary); font-size: 0.875rem; margin-bottom: 0.5rem; }
        .row { display: flex; gap: 0.5rem; flex-wrap: wrap; align-items: center; }
        button { background: var(--surface-hover); color: var(--text); border: 1px solid var(--border);
                 border-radius: 6px; padding: 0.4rem 0.8rem; cursor: pointer; }
        button:hover { background: var(--border); }
        button.primary { background: var(--accent); color: #000; border-color: var(--accent); }
        .badge { color: var(--accent); font-size: 0.8rem; margin-left: 0.5rem; }
        .archived-section { margin-top: 1.5rem; }
        .muted { color: var(--text-secondary); }
        #status { margin: 0.5rem 0; color: var(--text-secondary); min-height: 1.2em; }
        dialog { background: var(--surface); color: var(--text); border: 1px solid var(--border);
                 border-radius: 8px; padding: 1.25rem; max-width: 480px; width: 92%; }
        dialog::backdrop { background: rgba(0,0,0,0.6); }
        dialog label { display: block; margin: 0.5rem 0 0.15rem; font-size: 0.85rem; color: var(--text-secondary); }
        dialog input, dialog select { width: 100%; background: var(--bg); color: var(--text);
                 border: 1px solid var(--border); border-radius: 6px; padding: 0.4rem; }
        dialog .row { margin-top: 1rem; justify-content: flex-end; }
    </style>
</head>
<body>
    <div class="header">
        <div class="header-content">
            <h1>&#128209; Recipes</h1>
)HTML";
    html += generateMenuHtml();
    html += R"HTML(
        </div>
    </div>
    <div class="container">
        <div class="row">
            <button class="primary" onclick="openEditor(null)">Add Recipe</button>
        </div>
        <div id="status"></div>
        <div id="list"></div>
        <div class="archived-section">
            <h2 class="muted" id="archivedHeader" style="display:none">Archived</h2>
            <div id="archivedList"></div>
        </div>
    </div>

    <dialog id="editor">
        <h2 id="editorTitle">Recipe</h2>
        <label>Name</label><input id="fName">
        <label>Profile title</label><input id="fProfile">
        <label>Bag (grind follows it; roaster/coffee fill in automatically)</label>
        <select id="fBag"><option value="0">(no bag)</option></select>
        <label>Roaster</label><input id="fRoaster">
        <label>Coffee</label><input id="fCoffee">
        <label>Dose (g)</label><input id="fDose" type="number" step="0.1">
        <label>Yield (g)</label><input id="fYield" type="number" step="0.1">
        <label>Temp override (&deg;C)</label><input id="fTemp" type="number" step="0.1">
        <label>Pinned grind (empty = follow the bag)</label><input id="fGrind">
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
        <label><input id="fHasMilk" type="checkbox" style="width:auto"> Milk drink</label>
        <label>Milk (g)</label><input id="fMilk" type="number" step="1">
        <label>Pitcher name</label><input id="fPitcher">
        <label><input id="fHasWater" type="checkbox" style="width:auto"> Add hot water (Americano)</label>
        <label>Water vessel name</label><input id="fVessel">
        <label>Water order</label>
        <select id="fWaterOrder">
            <option value="after">After espresso (Americano)</option>
            <option value="before">Before espresso (long black)</option>
        </select>
        <div class="row">
            <button onclick="document.getElementById('editor').close()">Cancel</button>
            <button class="primary" onclick="saveEditor()">Save</button>
        </div>
    </dialog>

    <script>
)HTML";
    html += WEB_JS_MENU;
    html += WEB_JS_POWER_CONTROL;
    html += R"HTML(
        let editingId = null;
        const status = (m) => { document.getElementById('status').textContent = m || ''; };
        const esc = (s) => String(s ?? '').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));

        let bags = [];
        function loadBags() {
            fetch('/api/bags')
                .then(r => { if (!r.ok) throw new Error('Server error (' + r.status + ')'); return r.json(); })
                .then(d => {
                    bags = d.bags || [];
                    const sel = document.getElementById('fBag');
                    sel.innerHTML = '<option value="0">(no bag)</option>' + bags.map(b =>
                        '<option value="' + b.id + '">'
                        + esc(((b.roasterName || '') + ' ' + (b.coffeeName || '')).trim()
                              + (b.roastDate ? ' · ' + b.roastDate : ''))
                        + '</option>').join('');
                })
                .catch(() => {});  // bag list is a convenience; the editor still works without it
        }

        function load() {
            status('Loading…');
            fetch('/api/recipes')
                .then(r => { if (!r.ok) throw new Error('Server error (' + r.status + ')'); return r.json(); })
                .then(d => { render(d.recipes || []); status(''); })
                .catch(e => status('Could not load recipes: ' + e.message));
        }

        function subtitle(r) {
            const parts = [];
            if (r.drinkType) parts.push(esc(r.drinkType.replace('_', ' ')));
            if (r.profileTitle) parts.push(esc(r.profileTitle));
            const bean = ((r.roasterName || '') + ' ' + (r.coffeeName || '')).trim();
            if (bean) parts.push(esc(bean));
            if (r.doseG > 0 && r.yieldG > 0) parts.push(r.doseG.toFixed(1) + 'g &rarr; ' + r.yieldG.toFixed(1) + 'g');
            if (r.effectiveGrind) parts.push('grind ' + esc(r.effectiveGrind) + (r.grindPinned ? ' (pinned)' : ''));
            if (r.steam && r.steam.hasMilk) parts.push('milk' + (r.steam.milkWeightG ? ' ' + r.steam.milkWeightG + 'g' : ''));
            if (r.hotWater && r.hotWater.hasWater) parts.push('+water' + (r.hotWater.order === 'before' ? ' (long black)' : ' (americano)'));
            if (r.bagStale) parts.push('bag finished');
            if (r.shotCount > 0) parts.push(r.shotCount + ' shots');
            return parts.join(' &middot; ');
        }

        function cardHtml(r) {
            const actions = r.archived
                ? '<button onclick="archiveRecipe(' + r.id + ', true)">Restore</button>'
                : '<button onclick="activate(' + r.id + ')"' + (r.isActive ? ' disabled' : '') + '>Activate</button>'
                  + '<button onclick="openEditor(' + r.id + ')">Edit</button>'
                  + '<button onclick="cloneRecipe(' + r.id + ', ' + JSON.stringify(esc(r.name)) + ')">Clone</button>'
                  + (r.shotCount > 0
                      ? '<button onclick="archiveRecipe(' + r.id + ', false)">Archive</button>'
                      : '<button onclick="deleteRecipe(' + r.id + ')">Delete</button>');
            return '<div class="card' + (r.isActive ? ' active' : '') + '">'
                + '<h3>' + esc(r.name) + (r.isActive ? '<span class="badge">Active</span>' : '') + '</h3>'
                + '<div class="sub">' + subtitle(r) + '</div>'
                + '<div class="row">' + actions + '</div></div>';
        }

        let recipes = [];
        function render(list) {
            recipes = list;
            const active = list.filter(r => !r.archived);
            const archived = list.filter(r => r.archived);
            document.getElementById('list').innerHTML = active.length
                ? active.map(cardHtml).join('')
                : '<p class="muted">No recipes yet. Save one from a good shot in Shot History, or add one here.</p>';
            document.getElementById('archivedHeader').style.display = archived.length ? '' : 'none';
            document.getElementById('archivedList').innerHTML = archived.map(cardHtml).join('');
        }

        function post(url, body) {
            return fetch(url, { method: 'POST', headers: {'Content-Type': 'application/json'},
                                body: JSON.stringify(body || {}) })
                .then(r => r.json().then(d => { if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')')); return d; }));
        }

        function activate(id) {
            status('Activating…');
            post('/api/recipe/' + id + '/activate').then(() => load()).catch(e => status(e.message));
        }
        function cloneRecipe(id, name) {
            const newName = prompt('Name for the copy:', 'Copy of ' + name);
            if (!newName) return;
            post('/api/recipe/' + id + '/clone', { name: newName }).then(() => load()).catch(e => status(e.message));
        }
        function archiveRecipe(id, restore) {
            post('/api/recipe/' + id + '/archive', restore ? { restore: true } : {})
                .then(() => load()).catch(e => status(e.message));
        }
        function deleteRecipe(id) {
            if (!confirm('Delete this recipe permanently?')) return;
            post('/api/recipe/' + id + '/archive', { delete: true }).then(() => load()).catch(e => status(e.message));
        }

        function openEditor(id) {
            editingId = id;
            const r = recipes.find(x => x.id === id) || {};
            document.getElementById('editorTitle').textContent = id ? 'Edit Recipe' : 'New Recipe';
            document.getElementById('fName').value = r.name || '';
            document.getElementById('fDrinkType').value = r.drinkType || '';
            document.getElementById('fProfile').value = r.profileTitle || '';
            const bagSel = document.getElementById('fBag');
            // A finished linked bag is not in the open-bag list — add it so
            // editing another field doesn't silently drop the link.
            if (r.bagId > 0 && !bags.some(b => b.id === r.bagId)) {
                const opt = document.createElement('option');
                opt.value = r.bagId;
                opt.textContent = ((r.roasterName || '') + ' ' + (r.coffeeName || '')).trim() + ' (finished)';
                bagSel.appendChild(opt);
            }
            bagSel.value = String(r.bagId > 0 ? r.bagId : 0);
            document.getElementById('fRoaster').value = r.roasterName || '';
            document.getElementById('fCoffee').value = r.coffeeName || '';
            document.getElementById('fDose').value = r.doseG > 0 ? r.doseG : '';
            document.getElementById('fYield').value = r.yieldG > 0 ? r.yieldG : '';
            document.getElementById('fTemp').value = r.temperatureOverrideC > 0 ? r.temperatureOverrideC : '';
            document.getElementById('fGrind').value = r.grindPinned || '';
            const steam = r.steam || {};
            document.getElementById('fHasMilk').checked = !!steam.hasMilk;
            document.getElementById('fMilk').value = steam.milkWeightG || '';
            document.getElementById('fPitcher').value = steam.pitcherName || '';
            const water = r.hotWater || {};
            document.getElementById('fHasWater').checked = !!water.hasWater;
            document.getElementById('fVessel').value = water.vesselName || '';
            document.getElementById('fWaterOrder').value = water.order === 'before' ? 'before' : 'after';
            document.getElementById('editor').showModal();
        }

        function saveEditor() {
            const name = document.getElementById('fName').value.trim();
            const profileTitle = document.getElementById('fProfile').value.trim();
            const hasWater = document.getElementById('fHasWater').checked;
            if (!name) { status('Name is required'); return; }
            // Profile-less recipes are valid only as hot-water drinks (tea).
            if (!profileTitle && !hasWater) {
                status('Profile title is required (unless the recipe adds hot water)'); return;
            }
            const steam = {};
            if (document.getElementById('fHasMilk').checked) steam.hasMilk = true;
            const milk = parseFloat(document.getElementById('fMilk').value);
            if (milk > 0) steam.milkWeightG = milk;
            const pitcher = document.getElementById('fPitcher').value.trim();
            if (pitcher) steam.pitcherName = pitcher;
            const hotWater = {};
            if (hasWater) hotWater.hasWater = true;
            const vessel = document.getElementById('fVessel').value.trim();
            if (vessel) hotWater.vesselName = vessel;
            hotWater.order = document.getElementById('fWaterOrder').value === 'before' ? 'before' : 'after';
            const bodyData = {
                name: name,
                profileTitle: profileTitle,
                drinkType: document.getElementById('fDrinkType').value,
                bagId: parseInt(document.getElementById('fBag').value, 10) || 0,
                roasterName: document.getElementById('fRoaster').value.trim(),
                coffeeName: document.getElementById('fCoffee').value.trim(),
                doseG: parseFloat(document.getElementById('fDose').value) || 0,
                yieldG: parseFloat(document.getElementById('fYield').value) || 0,
                temperatureOverrideC: parseFloat(document.getElementById('fTemp').value) || 0,
                grindPinned: document.getElementById('fGrind').value.trim(),
                steam: steam,
                hotWater: hotWater
            };
            const req = editingId ? post('/api/recipe/' + editingId, bodyData)
                                  : post('/api/recipes', bodyData);
            req.then(() => { document.getElementById('editor').close(); load(); })
               .catch(e => status(e.message));
        }

        load();
        loadBags();
    </script>
</body>
</html>)HTML";
    return html;
}
