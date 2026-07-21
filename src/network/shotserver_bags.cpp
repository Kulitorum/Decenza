// ShotServer bags surface (add-recipes): REST API + /beans management page —
// the bag inventory's first web presence. Reads and writes both go through
// the app's CoffeeBagStorage instance (one-shot signal connections), so
// write-through semantics, Visualizer sync gating, and the in-app inventory
// refreshes behave exactly as for local edits. The bag lifecycle rule is
// storage-enforced: a bag with shots can only be marked empty ("finished"),
// never deleted. All routes sit behind the server's auth gate.

#include "shotserver.h"
#include "../controllers/maincontroller.h"
#include "../core/settings.h"
#include "../core/settings_dye.h"
#include "webtemplates/grind_datalist_js.h"
#include "../core/yieldspec.h"
#include "../core/dbutils.h"
#include "../ai/aimanager.h"
#include "../network/beanbaseclient.h"
#include "../history/coffeebagstorage.h"
#include "../history/shothistorystorage.h"
#include "webtemplates/base_css.h"
#include "webtemplates/menu_css.h"
#include "webtemplates/menu_html.h"
#include "webtemplates/menu_js.h"
#include "webtemplates/management_css.h"
#include "webtemplates/management_html.h"
#include "webtemplates/management_js.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

// Editable bag columns accepted from the web form. Full app parity
// (polish-shotserver-inventory-pages): the descriptive bean attributes ride in
// the beanBaseData JSON blob (the client parses/merges it), and beanBaseId +
// beanBaseData together carry a Bean Base canonical link. openedDate/storageHint
// drive the freeze-lifecycle actions; equipmentId/rpm are the per-bag equipment
// link + rpm dial-in.
const QStringList kBagEditableKeys = {
    "roasterName", "coffeeName", "roastDate", "roastLevel", "frozenDate",
    "defrostDate", "openedDate", "storageHint", "notes", "startWeightG",
    "grinderSetting", "doseWeightG", "equipmentId", "rpm",
    "beanBaseId", "beanBaseData", "inInventory"};

QVariantMap bagFieldsFromBody(const QJsonObject& body)
{
    QVariantMap fields;
    for (const QString& key : kBagEditableKeys) {
        if (body.contains(key))
            fields.insert(key, body[key].toVariant());
    }
    // Yield spec (add-yield-ratio-anchor): sparse, mutually exclusive wire
    // keys — grams (yieldG) or a dose multiplier (yieldRatio). Writing one
    // IS setting the mode, which implicitly clears the other; 0 clears the
    // anchor entirely. Both-present is rejected in the route handler.
    if (body.contains(QStringLiteral("yieldG"))) {
        const double g = body[QStringLiteral("yieldG")].toDouble();
        fields.insert("yieldValue", g > 0 ? YieldSpec::clampAbsolute(g) : 0.0);
        fields.insert("yieldMode", g > 0 ? QStringLiteral("absolute") : QStringLiteral("none"));
    } else if (body.contains(QStringLiteral("yieldRatio"))) {
        const double ratio = body[QStringLiteral("yieldRatio")].toDouble();
        fields.insert("yieldValue", ratio > 0 ? YieldSpec::clampRatio(ratio) : 0.0);
        fields.insert("yieldMode", ratio > 0 ? QStringLiteral("ratio") : QStringLiteral("none"));
    }
    return fields;
}

} // namespace

void ShotServer::handleBagsApi(QTcpSocket* socket, const QString& method,
                               const QString& path, const QByteArray& body)
{
    CoffeeBagStorage* bagStorage =
        m_mainController ? m_mainController->bagStorage() : nullptr;
    if (!bagStorage) {
        sendJson(socket, QJsonDocument(QJsonObject{{"error", "Storage not available"}})
                             .toJson(QJsonDocument::Compact));
        return;
    }
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

    // GET /api/beans/search?q=… — Bean Base canonical lookup for the web
    // create/link flow. Reuses BeanBaseClient (the same backend as the app's
    // search bar and the MCP bean_search tool); the search response already
    // carries every descriptive field, so entries are returned as-is. Async
    // with a server-side timeout so a slow/failed lookup never hangs the socket.
    if (path.startsWith(QStringLiteral("/api/beans/search")) && method == "GET") {
        BeanBaseClient* beanbase = m_mainController ? m_mainController->beanbase() : nullptr;
        if (!beanbase) {
            respondJson(QJsonObject{{"error", "Bean Base not available"}}, 503);
            return;
        }
        QString q;
        const qsizetype qm = path.indexOf('?');
        if (qm >= 0)
            q = QUrlQuery(path.mid(qm + 1)).queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded);
        q = q.trimmed();
        if (q.isEmpty()) {
            respondJson(QJsonObject{{"results", QJsonArray{}}, {"count", 0}});
            return;
        }
        auto fired = std::make_shared<bool>(false);
        auto conns = std::make_shared<QList<QMetaObject::Connection>>();
        auto cleanup = [conns]() {
            for (const auto& c : *conns)
                QObject::disconnect(c);
            conns->clear();
        };
        *conns << connect(beanbase, &BeanBaseClient::searchResults, this,
            [fired, cleanup, respondJson, q](const QString& query, const QVariantList& entries) {
                // The client is shared with the in-app bar; match our query
                // (case-insensitive, mirroring the MCP tool) and ignore others.
                if (*fired || query.compare(q, Qt::CaseInsensitive) != 0)
                    return;
                *fired = true;
                cleanup();
                QJsonArray arr;
                for (const QVariant& v : entries)
                    arr.append(QJsonObject::fromVariantMap(v.toMap()));
                respondJson(QJsonObject{{"results", arr}, {"count", arr.size()}});
            });
        *conns << connect(beanbase, &BeanBaseClient::searchFailed, this,
            [fired, cleanup, respondJson, q](const QString& query, const QString& statusToken) {
                if (*fired || query.compare(q, Qt::CaseInsensitive) != 0)
                    return;
                *fired = true;
                cleanup();
                respondJson(QJsonObject{{"error", QStringLiteral("Bean Base search failed: ") + statusToken}}, 502);
            });
        QTimer::singleShot(60000, this, [fired, cleanup, respondJson]() {
            if (*fired)
                return;
            *fired = true;
            cleanup();
            respondJson(QJsonObject{{"error", "Bean Base search timed out"}}, 504);
        });
        beanbase->search(q);
        return;
    }

    // POST /api/beans/extract {url, kind} — the "get info from page" AI
    // extraction, decoupled from a bag (the web form extracts while creating).
    // Reuses BeanBaseClient::fetchPageText + AIManager, mirroring the MCP
    // bag_extract_details pipeline: stage 1 = local page fetch → extraction;
    // stage 2 = provider-side web fetch for JS-rendered shops. Returns the
    // extracted fields WITHOUT writing a bag (the client fills the form).
    if (path == "/api/beans/extract" && method == "POST") {
        BeanBaseClient* beanbase = m_mainController ? m_mainController->beanbase() : nullptr;
        AIManager* aiManager = m_mainController ? m_mainController->aiManager() : nullptr;
        if (!beanbase || !aiManager) {
            respondJson(QJsonObject{{"error", "Extraction dependencies not available"}}, 503);
            return;
        }
        if (!aiManager->isConfigured()) {
            respondJson(QJsonObject{{"error", "No AI provider configured"}}, 400);
            return;
        }
        const QString url = bodyJson.value(QStringLiteral("url")).toString().trimmed();
        if (url.isEmpty()) {
            respondJson(QJsonObject{{"error", "A product-page url is required"}}, 400);
            return;
        }
        const QString kind = bodyJson.value(QStringLiteral("kind")).toString() == QLatin1String("tea")
            ? QStringLiteral("tea") : QStringLiteral("coffee");

        struct ExtractState {
            QList<QMetaObject::Connection> conns;
            int stage = 1;
            QString stage1Error;
            bool fetchArmed = false;
            bool done = false;
        };
        auto st = std::make_shared<ExtractState>();
        auto finish = [st](std::function<void()> reply) {
            if (st->done)
                return;
            st->done = true;
            for (const auto& c : st->conns)
                QObject::disconnect(c);
            reply();
        };
        st->conns << connect(beanbase, &BeanBaseClient::pageTextReady, this,
            [st, aiManager, url, kind](const QString& u, const QString& text) {
                if (st->done || st->fetchArmed || u != url)
                    return;
                st->fetchArmed = true;
                aiManager->extractCoffeeBagDetails(url, text, kind);
            });
        st->conns << connect(beanbase, &BeanBaseClient::pageTextFailed, this,
            [st, aiManager, url, kind, finish, respondJson](const QString& u, const QString& error) {
                if (st->done || st->fetchArmed || u != url)
                    return;
                st->fetchArmed = true;
                st->stage1Error = error;
                if (error == QLatin1String("emptyPage") && aiManager->supportsUrlExtraction()) {
                    st->stage = 2;
                    aiManager->extractCoffeeBagDetailsFromUrl(url, url, kind);
                } else {
                    finish([respondJson, error]() {
                        respondJson(QJsonObject{{"error", QStringLiteral("Page fetch failed: ") + error}}, 502);
                    });
                }
            });
        st->conns << connect(aiManager, &AIManager::bagDetailsExtracted, this,
            [st, kind, url, finish, respondJson](const QString& token, const QVariantMap& fields) {
                if (st->done || token != url)
                    return;
                finish([respondJson, st, kind, url, fields]() {
                    respondJson(QJsonObject{{"stage", st->stage}, {"kind", kind}, {"url", url},
                                            {"fields", QJsonObject::fromVariantMap(fields)}});
                });
            });
        st->conns << connect(aiManager, &AIManager::bagDetailsExtractionFailed, this,
            [st, url, finish, respondJson](const QString& token, const QString& error) {
                if (st->done || token != url)
                    return;
                finish([respondJson, st, error]() {
                    QString msg = QStringLiteral("Extraction failed at stage %1: %2")
                                      .arg(st->stage).arg(error);
                    if (st->stage == 2 && !st->stage1Error.isEmpty())
                        msg += QStringLiteral(" (stage 1: %1)").arg(st->stage1Error);
                    respondJson(QJsonObject{{"error", msg}}, 502);
                });
            });
        QTimer::singleShot(90000, this, [finish, respondJson]() {
            finish([respondJson]() {
                respondJson(QJsonObject{{"error", "Extraction timed out"}}, 504);
            });
        });
        beanbase->fetchPageText(url);
        return;
    }

    // GET /api/bags — the open-bag inventory (maps include shotCount).
    if (path == "/api/bags" && method == "GET") {
        const int activeBagId = m_settings ? m_settings->dye()->activeBagId() : -1;
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(bagStorage, &CoffeeBagStorage::inventoryReady, this,
            [conn, activeBagId, respondJson](const QVariantList& bags) {
                disconnect(*conn);
                QJsonArray arr;
                for (const QVariant& v : bags) {
                    QJsonObject o = QJsonObject::fromVariantMap(v.toMap());
                    o["isActive"] = o["id"].toInteger() == activeBagId;
                    arr.append(o);
                }
                respondJson(QJsonObject{{"bags", arr}, {"count", arr.size()}});
            });
        bagStorage->requestInventory();
        return;
    }

    // POST /api/bags — create
    if (path == "/api/bags" && method == "POST") {
        // Retired key — rejected, not dropped (see the bag_update MCP twin).
        if (bodyJson.contains(QStringLiteral("yieldOverrideG"))) {
            respondJson(QJsonObject{{"error", "yieldOverrideG was replaced by yieldG (an absolute gram target) / yieldRatio (a multiple of the dose) — the bag now holds an explicit yield anchor rather than a deviation from the profile (add-yield-ratio-anchor). Rejected rather than silently dropped: send yieldG for the same behaviour as before."}}, 400);
            return;
        }
        if (bodyJson.contains(QStringLiteral("yieldG")) && bodyJson.contains(QStringLiteral("yieldRatio"))) {
            respondJson(QJsonObject{{"error", "yieldG and yieldRatio are mutually exclusive — the bag holds ONE yield anchor. Send exactly one; writing it replaces the other automatically."}}, 400);
            return;
        }
        QVariantMap fields = bagFieldsFromBody(bodyJson);
        if (fields.value("roasterName").toString().trimmed().isEmpty()
            && fields.value("coffeeName").toString().trimmed().isEmpty()) {
            respondJson(QJsonObject{{"error", "roasterName or coffeeName is required"}}, 400);
            return;
        }
        // kind is creation-time only (deliberately NOT in kBagEditableKeys, so
        // the update route can never touch it): accept it here, default coffee.
        const QString kind = bodyJson.value("kind").toString();
        if (!kind.isEmpty() && kind != QLatin1String("coffee") && kind != QLatin1String("tea")) {
            respondJson(QJsonObject{{"error", "kind must be 'coffee' or 'tea'"}}, 400);
            return;
        }
        if (!kind.isEmpty())
            fields.insert("kind", kind);
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(bagStorage, &CoffeeBagStorage::bagCreated, this,
            [conn, respondJson](qint64 bagId, const QVariantMap& bag) {
                disconnect(*conn);
                if (bagId <= 0)
                    respondJson(QJsonObject{{"error", "Create failed"}}, 500);
                else
                    respondJson(QJsonObject::fromVariantMap(bag));
            });
        bagStorage->requestCreateBag(fields);
        return;
    }

    // /api/bag/<id>[/<action>]
    if (path.startsWith("/api/bag/")) {
        const QStringList parts = path.mid(QStringLiteral("/api/bag/").size()).split('/');
        const qint64 bagId = parts.value(0).toLongLong();
        const QString action = parts.value(1);
        if (bagId <= 0) {
            respondJson(QJsonObject{{"error", "Invalid bag id"}}, 400);
            return;
        }

        // GET /api/bag/<id>/image — serve the cached bean photo for the web
        // card (BeanBaseClient's file cache, keyed by canonical id). Loads the
        // bag off-thread to resolve its canonical id + product URL, serves the
        // cached file when present, else triggers a best-effort resolve and
        // returns 404 so the client shows its placeholder (next load has it).
        if (action == "image" && method == "GET") {
            BeanBaseClient* beanbase = m_mainController ? m_mainController->beanbase() : nullptr;
            if (!beanbase) {
                respondJson(QJsonObject{{"error", "Bean images not available"}}, 404);
                return;
            }
            const QString dbPath = bagStorage->databasePath();
            QPointer<BeanBaseClient> safeBeanbase = beanbase;
            QThread* loadThread = QThread::create(
                [safeThis, safeSocket, safeBeanbase, dbPath, bagId, respondJson]() {
                    QString canonicalId, roastName, productUrl;
                    withTempDb(dbPath, "web_bag_image", [&](QSqlDatabase& db) {
                        const CoffeeBag bag = CoffeeBagStorage::loadBagStatic(db, bagId);
                        if (bag.isValid()) {
                            canonicalId = bag.beanBaseId;
                            roastName = bag.coffeeName;
                            productUrl = QJsonDocument::fromJson(bag.beanBaseData.toUtf8())
                                             .object().value(QStringLiteral("link")).toString();
                        }
                    });
                    QMetaObject::invokeMethod(qApp,
                        [safeThis, safeSocket, safeBeanbase, canonicalId, roastName, productUrl, respondJson]() {
                            if (!safeThis || !safeSocket)
                                return;
                            if (canonicalId.isEmpty() || !safeBeanbase) {
                                respondJson(QJsonObject{{"error", "No image"}}, 404);
                                return;
                            }
                            const QString filePath = safeBeanbase->bagImagePath(canonicalId);
                            if (!filePath.isEmpty() && QFileInfo::exists(filePath)) {
                                const QString ct = filePath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
                                    ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
                                safeThis->sendFile(safeSocket, filePath, ct);
                            } else {
                                // Kick off a best-effort resolve for next time.
                                safeBeanbase->ensureBagImage(canonicalId, roastName, productUrl);
                                respondJson(QJsonObject{{"error", "No image yet"}}, 404);
                            }
                        }, Qt::QueuedConnection);
                });
            QObject::connect(loadThread, &QThread::finished, loadThread, &QObject::deleteLater);
            loadThread->start();
            return;
        }

        // GET /api/bag/<id>
        if (action.isEmpty() && method == "GET") {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(bagStorage, &CoffeeBagStorage::bagReady, this,
                [conn, bagId, respondJson](qint64 readyId, const QVariantMap& bag) {
                    if (readyId != bagId)
                        return;
                    disconnect(*conn);
                    if (bag.isEmpty())
                        respondJson(QJsonObject{{"error", "Bag not found"}}, 404);
                    else
                        respondJson(QJsonObject::fromVariantMap(bag));
                });
            bagStorage->requestBag(bagId);
            return;
        }

        if (method != "POST") {
            respondJson(QJsonObject{{"error", "Method not allowed"}}, 405);
            return;
        }

        // POST /api/bag/<id> — update (same write-through path as app edits)
        if (action.isEmpty()) {
            // Retired key — rejected, not dropped (see the bag_update MCP twin).
            if (bodyJson.contains(QStringLiteral("yieldOverrideG"))) {
                respondJson(QJsonObject{{"error", "yieldOverrideG was replaced by yieldG (an absolute gram target) / yieldRatio (a multiple of the dose) — the bag now holds an explicit yield anchor rather than a deviation from the profile (add-yield-ratio-anchor). Rejected rather than silently dropped: send yieldG for the same behaviour as before."}}, 400);
                return;
            }
            if (bodyJson.contains(QStringLiteral("yieldG")) && bodyJson.contains(QStringLiteral("yieldRatio"))) {
                respondJson(QJsonObject{{"error", "yieldG and yieldRatio are mutually exclusive — the bag holds ONE yield anchor. Send exactly one; writing it replaces the other automatically."}}, 400);
                return;
            }
            const QVariantMap fields = bagFieldsFromBody(bodyJson);
            if (fields.isEmpty()) {
                respondJson(QJsonObject{{"error", "No editable fields provided"}}, 400);
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(bagStorage, &CoffeeBagStorage::bagUpdated, this,
                [conn, bagId, respondJson](qint64 updatedId, bool success) {
                    if (updatedId != bagId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{"updated", true}, {"bagId", bagId}});
                    else
                        respondJson(QJsonObject{{"error", "Bag not found or update failed"}}, 404);
                });
            // Setting a Bean Base link propagates it onto the bag's shots, exactly
            // as the in-app edit dialog's link path does.
            const bool propagate = fields.contains(QStringLiteral("beanBaseId"))
                && !fields.value(QStringLiteral("beanBaseId")).toString().isEmpty();
            bagStorage->requestUpdateBag(bagId, fields, propagate);
            return;
        }

        // POST /api/bag/<id>/finish — mark empty ("Bag finished"; never deletes)
        if (action == "finish") {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(bagStorage, &CoffeeBagStorage::bagUpdated, this,
                [conn, bagId, respondJson](qint64 updatedId, bool success) {
                    if (updatedId != bagId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{"finished", true}, {"bagId", bagId}});
                    else
                        respondJson(QJsonObject{{"error", "Bag not found"}}, 404);
                });
            bagStorage->requestMarkEmpty(bagId);
            return;
        }

        // POST /api/bag/<id>/delete — mistaken creations only (storage
        // refuses when any shot references the bag).
        if (action == "delete") {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(bagStorage, &CoffeeBagStorage::bagDeleted, this,
                [conn, bagId, respondJson](qint64 deletedId, bool success) {
                    if (deletedId != bagId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{"deleted", true}});
                    else
                        respondJson(QJsonObject{{"error",
                            "Delete refused — the bag has shots (finish it instead) or was not found"}}, 409);
                });
            bagStorage->requestDeleteBag(bagId);
            return;
        }

        // POST /api/bag/<id>/activate — select as the active bag (applies its
        // fields to the dye cache exactly like tapping its pill on idle).
        if (action == "activate") {
            if (!m_settings) {
                respondJson(QJsonObject{{"error", "Settings not available"}}, 500);
                return;
            }
            m_settings->dye()->setActiveBagId(static_cast<int>(bagId));
            respondJson(QJsonObject{{"activated", true}, {"bagId", bagId}});
            return;
        }
    }

    respondJson(QJsonObject{{"error", "Unknown bags endpoint"}}, 404);
}

QString ShotServer::generateBeansPage() const
{
    QString html = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Decenza — Beans</title>
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
    html += generateManagementHeader(QStringLiteral("&#127793; Beans"));
    html += R"HTML(
    <div class="container">
        <div class="toolbar">
            <button class="primary" onclick="openEditor(null, 'coffee')">+ Add Coffee</button>
            <button onclick="openEditor(null, 'tea')">+ Add Tea</button>
        </div>
        <div id="status"></div>
        <div id="list"></div>
    </div>

    <dialog id="editor">
        <h2 id="editorTitle">Bag</h2>

        <div id="searchSection">
            <label>Search the Bean Base</label>
            <div class="search-wrap">
                <input id="fSearch" class="search" placeholder="Roaster or coffee name…" oninput="onSearchInput()">
            </div>
            <div id="searchResults" class="search-results" style="display:none"></div>
        </div>

        <div class="grid-2">
            <div><label>Roaster</label><input id="fRoaster"></div>
            <div><label id="lblCoffee">Coffee</label><input id="fCoffee"></div>
        </div>
        <div class="grid-2">
            <div><label>Roast date (YYYY-MM-DD)</label><input id="fRoastDate"></div>
            <div><label>Roast level</label><input id="fRoastLevel"></div>
        </div>

        <!-- One Product URL field, same as the app's bag form: it is the bag's
             stored product link AND the page the AI extraction reads. A second,
             extraction-only URL box used to live here and was never cleared
             between editor opens, so a new bag arrived carrying the previous
             bag's URL (#1588). -->
        <label>Product URL</label><input id="dLink" placeholder="https://…">
        <div class="actions"><button onclick="extractInfo()">Get info from page</button></div>

        <details class="dialog-section" id="coffeeDetails">
            <summary>Bean details</summary>
            <div class="grid-2">
                <div><label>Origin</label><input id="dOrigin"></div>
                <div><label>Region</label><input id="dRegion"></div>
                <div><label>Farm</label><input id="dFarm"></div>
                <div><label>Producer</label><input id="dProducer"></div>
                <div><label>Variety</label><input id="dVariety"></div>
                <div><label>Elevation</label><input id="dElevation"></div>
                <div><label>Process</label><input id="dProcess"></div>
                <div><label>Harvest</label><input id="dHarvest"></div>
                <div><label>Quality score</label><input id="dQuality"></div>
                <div><label>Purchased at</label><input id="dPlace"></div>
            </div>
            <label>Tasting notes</label><textarea id="dNotes"></textarea>
        </details>

        <details class="dialog-section" id="teaDetails">
            <summary>Tea details</summary>
            <div class="grid-2">
                <div><label>Tea type</label><input id="tType"></div>
                <div><label>Garden</label><input id="tGarden"></div>
                <div><label>Cultivar</label><input id="tCultivar"></div>
                <div><label>Flush</label><input id="tFlush"></div>
                <div><label>Brew temp (&deg;C)</label><input id="tBrewTemp" type="number" step="1"></div>
                <div><label>Leaf g / 100ml</label><input id="tLeaf" type="number" step="0.1"></div>
                <div><label>Steep time</label><input id="tSteep"></div>
                <div><label>Origin</label><input id="tOrigin"></div>
            </div>
        </details>

        <details class="dialog-section">
            <summary>Storage &amp; freshness</summary>
            <div class="grid-2">
                <div><label>Frozen date</label><input id="fFrozen"></div>
                <div><label>Defrost date</label><input id="fDefrost"></div>
                <div><label>Opened date</label><input id="fOpened"></div>
            </div>
        </details>

        <details class="dialog-section">
            <summary>Dial-in</summary>
            <!-- Equipment first: the grinder gates the RPM field and scopes the
                 grind candidates, so it is chosen before the dial-in that
                 depends on it — same ordering as the app's bag form and the
                 recipe wizard's equipment-then-grind windows. -->
            <label>Equipment</label>
            <select id="fEquipment" onchange="refreshGrindCandidates()"><option value="0">None</option></select>
            <div class="grid-2">
                <div><label>Grind setting</label><input id="fGrind"></div>
                <div><label>RPM</label><input id="fRpm" type="number" step="1"></div>
                <div><label>Dose (g)</label><input id="fDose" type="number" step="0.1"></div>
                <div><label>Start weight (g)</label><input id="fStartWeight" type="number" step="1"></div>
            </div>
            <label>Yield</label>
            <div class="grid-2">
                <div><select id="fYieldMode" onchange="updateYieldLabel()">
                    <option value="none">No target</option>
                    <option value="absolute">Grams</option>
                    <option value="ratio">Ratio (× dose)</option>
                </select></div>
                <div><input id="fYieldValue" type="number" step="0.1" placeholder="value"></div>
            </div>
        </details>

        <label>Notes</label><textarea id="fNotes"></textarea>

        <div class="dialog-actions">
            <button onclick="el('editor').close()">Cancel</button>
            <button class="primary" onclick="saveEditor()">Save</button>
        </div>
    </dialog>

    <dialog id="infoDialog">
        <h2>Bean details</h2>
        <div id="infoBody"></div>
        <div class="dialog-actions"><button onclick="el('infoDialog').close()">Close</button></div>
    </dialog>

    <script>
)HTML";
    html += WEB_JS_MENU;
    html += WEB_JS_POWER_CONTROL;
    html += WEB_JS_MANAGEMENT;
    html += WEB_JS_GRIND_DATALIST;
    html += R"HTML(
        let editingId = null;
        let editingKind = 'coffee';
        let bags = [];
        let equipmentList = [];     // for the per-bag equipment link picker
        let editBlob = {};          // working copy of the bag's beanBaseData blob
        let editBeanBaseId = '';    // canonical id when linked

        // Descriptive blob working keys ↔ form ids (mirrors BeanBaseBlob keys).
        const COFFEE_KEYS = [['dOrigin','origin'],['dRegion','region'],['dFarm','farm'],
            ['dProducer','producer'],['dVariety','variety'],['dElevation','elevation'],
            ['dProcess','process'],['dHarvest','harvest'],['dQuality','qualityScore'],
            ['dPlace','placeOfPurchase'],['dNotes','tastingNotes']];
        const TEA_KEYS = [['tType','teaType'],['tGarden','garden'],['tCultivar','cultivar'],
            ['tFlush','flush'],['tBrewTemp','brewTempC'],['tLeaf','leafGramsPer100Ml'],
            ['tSteep','steepTime'],['tOrigin','origin']];
        // The product link belongs to both kinds — tea bags are bought from a
        // page too, and the extraction reads it — so it sits outside the
        // kind-specific lists and is applied on top of whichever is active.
        const LINK_KEYS = [['dLink','link']];

        const parseBlob = (s) => { try { return s ? JSON.parse(s) : {}; } catch (e) { return {}; } };
        function thumbErr(img, emoji) {
            img.outerHTML = '<div class="thumb placeholder">' + emoji + '</div>';
        }
        const daysAgo = (d) => {
            const t = Date.parse(d); if (isNaN(t)) return null;
            return Math.floor((Date.now() - t) / 86400000);
        };
        const today = () => new Date().toISOString().slice(0, 10);

        function load() {
            status('Loading…');
            // Equipment list backs the per-bag link picker; best-effort.
            getJson('/api/equipment')
                .then(d => { equipmentList = d.equipment || []; })
                .catch(() => { equipmentList = []; });
            getJson('/api/bags')
                .then(d => { render(d.bags || []); status(''); })
                .catch(e => status('Could not load bags: ' + e.message));
        }

        function equipmentLabel(p) {
            return p.name || ((p.grinderBrand || '') + ' ' + (p.grinderModel || '')).trim()
                || ('Package ' + p.id);
        }
        function fillEquipmentSelect(selectedId) {
            const sel = el('fEquipment');
            sel.innerHTML = '<option value="0">None</option>'
                + equipmentList.map(p => '<option value="' + p.id + '">' + esc(equipmentLabel(p)) + '</option>').join('');
            // The equipment fetch can fail or still be in flight. Without this,
            // a bag whose package is missing from the list falls back to the
            // "None" option and SAVE SILENTLY UNLINKS it — a data loss with no
            // message. Keep the id selectable via a placeholder so the link
            // round-trips untouched.
            if (selectedId > 0 && !equipmentList.some(p => p.id === selectedId)) {
                const opt = document.createElement('option');
                opt.value = String(selectedId);
                opt.textContent = 'Package ' + selectedId + ' (not loaded)';
                sel.appendChild(opt);
            }
            sel.value = String(selectedId || 0);
        }

        // Stepped grind/RPM candidates for the BAG's linked package — the
        // record's own grinder, never the active one (grind-value-entry).
        // Re-attached when the equipment selection changes mid-edit, so the
        // candidates follow the grinder the bag will actually be ground on.
        function refreshGrindCandidates() {
            const pkgId = parseInt(el('fEquipment').value, 10) || 0;
            const pkg = equipmentList.find(p => p.id === pkgId);
            attachGrindDatalist(el('fGrind'), el('fRpm'),
                                pkg ? pkg.grinderBrand : '', pkg ? pkg.grinderModel : '');
        }

        function attrLine(b, bb) {
            if (b.kind === 'tea')
                return bullet([bb.teaType, bb.origin,
                    bb.brewTempC ? bb.brewTempC + '°C' : '', bb.steepTime].map(esc));
            return bullet([bb.origin, bb.variety, bb.process].map(esc));
        }
        function metaLine(b) {
            const parts = [];
            if (b.roastDate) parts.push('Roasted ' + esc(b.roastDate));
            if (b.frozenDate && !b.defrostDate) parts.push('Frozen ' + esc(b.frozenDate));
            if (b.defrostDate) {
                const n = daysAgo(b.defrostDate);
                parts.push('Thawed ' + esc(b.defrostDate) + (n !== null ? ' (' + n + 'd)' : ''));
            }
            if (b.openedDate) {
                const n = daysAgo(b.openedDate);
                parts.push('Opened ' + esc(b.openedDate) + (n !== null ? ' (' + n + 'd)' : ''));
            }
            return parts.join(' &middot; ');
        }

        function cardHtml(b) {
            const bb = parseBlob(b.beanBaseData);
            const linked = !!(b.beanBaseId && b.beanBaseData);
            const title = esc(b.coffeeName || b.roasterName || 'Bag ' + b.id);
            const ph = b.kind === 'tea' ? '&#127861;' : '&#127793;';
            const thumb = linked
                ? '<img class="thumb" src="/api/bag/' + b.id + '/image" alt="" '
                  + 'onerror="thumbErr(this,' + "'" + ph + "'" + ')">'
                : '<div class="thumb placeholder">' + ph + '</div>';

            let body = '<div class="card-body"><div class="card-title">'
                + (linked ? '<span class="verified" title="Linked to Bean Base">&#10003;</span>' : '')
                + title + (b.isActive ? '<span class="badge">Active</span>' : '') + '</div>';
            if (b.roasterName && b.coffeeName)
                body += '<div class="card-roaster">' + esc(b.roasterName) + '</div>';
            const attrs = attrLine(b, bb);
            if (attrs) body += '<div class="attr-line">' + attrs + '</div>';
            if (bb.tastingNotes) body += '<div class="notes-line">' + esc(bb.tastingNotes) + '</div>';
            const meta = metaLine(b);
            if (meta) body += '<div class="meta-line">' + meta + '</div>';
            const dial = bullet([
                b.grinderSetting ? 'grind ' + esc(b.grinderSetting) + (b.rpm > 0 ? ' · ' + b.rpm + ' RPM' : '') : '',
                b.doseWeightG > 0 ? b.doseWeightG.toFixed(1) + 'g dose' : '',
                b.shotCount > 0 ? b.shotCount + ' shots' : '']);
            if (dial) body += '<div class="plan-line">' + dial + '</div>';
            body += '</div>';

            let acts = '<div class="actions">'
                + '<button class="primary" onclick="activate(' + b.id + ')"' + (b.isActive ? ' disabled' : '') + '>Activate</button>'
                + '<button onclick="openEditor(' + b.id + ')">Edit</button>';
            if (!linked && b.kind !== 'tea')
                acts += '<button onclick="openEditor(' + b.id + ',null,true)">Find in Bean Base</button>';
            if (linked)
                acts += '<button onclick="showInfo(' + b.id + ')">Info</button>';
            if (b.frozenDate && !b.defrostDate)
                acts += '<button onclick="quickSet(' + b.id + ',{defrostDate:today()})">Thaw</button>';
            if (!b.openedDate)
                acts += '<button onclick="quickSet(' + b.id + ',{openedDate:today()})">Mark opened</button>';
            acts += (b.shotCount > 0
                ? '<button class="danger" onclick="finishBag(' + b.id + ')">Bag finished</button>'
                : '<button class="danger" onclick="deleteBag(' + b.id + ')">Delete</button>');
            acts += '</div>';

            return '<div class="card' + (b.isActive ? ' active' : '') + '">'
                + '<div class="card-head">' + thumb + body + '</div>' + acts + '</div>';
        }

        function render(list) {
            bags = list;
            el('list').innerHTML = list.length
                ? '<div class="grid">' + list.map(cardHtml).join('') + '</div>'
                : '<div class="empty"><h2>No bags yet</h2>'
                  + '<div>Track your beans, freshness and grinder settings here.</div></div>';
        }

        function activate(id) { post('/api/bag/' + id + '/activate').then(load).catch(e => status(e.message)); }
        function quickSet(id, fields) { post('/api/bag/' + id, fields).then(load).catch(e => status(e.message)); }
        function finishBag(id) {
            if (!confirm('Mark this bag as finished? It leaves the inventory; shots keep their history.')) return;
            post('/api/bag/' + id + '/finish').then(load).catch(e => status(e.message));
        }
        function deleteBag(id) {
            if (!confirm('Delete this bag permanently?')) return;
            post('/api/bag/' + id + '/delete').then(load).catch(e => status(e.message));
        }

        function showInfo(id) {
            const b = bags.find(x => x.id === id) || {};
            const bb = parseBlob(b.beanBaseData);
            const rows = [];
            const add = (label, v) => { if (v) rows.push('<div><span class="muted">' + label + ':</span> ' + esc(v) + '</div>'); };
            add('Roaster', b.roasterName); add('Coffee', b.coffeeName);
            add('Origin', bb.origin); add('Region', bb.region); add('Farm', bb.farm);
            add('Producer', bb.producer); add('Variety', bb.variety); add('Elevation', bb.elevation);
            add('Process', bb.process); add('Harvest', bb.harvest); add('Quality', bb.qualityScore);
            add('Purchased at', bb.placeOfPurchase); add('Tasting notes', bb.tastingNotes);
            // Only render http(s) links — a javascript:/data: URI in the
            // canonical/extracted blob would otherwise be a clickable XSS vector
            // (esc() escapes markup, not the scheme).
            const safeLink = /^https?:\/\//i.test(bb.link || '') ? bb.link : '';
            if (safeLink) rows.push('<div><a href="' + esc(safeLink) + '" target="_blank" rel="noopener noreferrer">Reorder / product page</a></div>');
            el('infoBody').innerHTML = rows.join('') || '<div class="muted">No details recorded.</div>';
            el('infoDialog').showModal();
        }

        function applyKindUi() {
            const isTea = editingKind === 'tea';
            el('searchSection').style.display = isTea ? 'none' : '';
            el('coffeeDetails').style.display = isTea ? 'none' : '';
            el('teaDetails').style.display = isTea ? '' : 'none';
            el('lblCoffee').textContent = isTea ? 'Tea name' : 'Coffee';
        }

        function openEditor(id, kind, focusSearch) {
            editingId = id;
            const b = bags.find(x => x.id === id) || {};
            editingKind = kind || b.kind || 'coffee';
            editBlob = parseBlob(b.beanBaseData);
            editBeanBaseId = b.beanBaseId || '';
            el('editorTitle').textContent = id ? 'Edit Bag' : (editingKind === 'tea' ? 'New Tea' : 'New Coffee');
            el('fRoaster').value = b.roasterName || '';
            el('fCoffee').value = b.coffeeName || '';
            el('fRoastDate').value = b.roastDate || '';
            el('fRoastLevel').value = b.roastLevel || '';
            el('fFrozen').value = b.frozenDate || '';
            el('fDefrost').value = b.defrostDate || '';
            el('fOpened').value = b.openedDate || '';
            el('fGrind').value = b.grinderSetting || '';
            el('fRpm').value = b.rpm > 0 ? b.rpm : '';
            el('fDose').value = b.doseWeightG > 0 ? b.doseWeightG : '';
            el('fStartWeight').value = b.startWeightG > 0 ? b.startWeightG : '';
            el('fYieldMode').value = b.yieldMode || 'none';
            el('fYieldValue').value = b.yieldValue > 0 ? b.yieldValue : '';
            // A NEW bag defaults to the ACTIVE equipment package (app parity —
            // the in-app form arrives with the current equipment resolved, so
            // the grind picker's RPM half matches the real grinder instead of
            // the "unknown grinder -> assume rpm-capable" fallback an empty
            // identity triggers). An existing bag keeps its own link.
            const defaultPkg = id ? (b.equipmentId || 0)
                : ((equipmentList.find(p => p.isActive) || { id: 0 }).id);
            fillEquipmentSelect(defaultPkg);
            refreshGrindCandidates();
            el('fNotes').value = b.notes || '';
            COFFEE_KEYS.concat(TEA_KEYS, LINK_KEYS).forEach(([fid, key]) => { const e = el(fid); if (e) e.value = editBlob[key] || ''; });
            el('fSearch').value = '';
            el('searchResults').style.display = 'none';
            // Every field above is (re)assigned from the record, so nothing
            // survives from the bag edited before this one — including a status
            // line left over from that bag's search or extraction.
            status('');
            updateYieldLabel();
            applyKindUi();
            el('editor').showModal();
            if (focusSearch) el('fSearch').focus();
        }

        function updateYieldLabel() {
            const m = el('fYieldMode').value;
            el('fYieldValue').placeholder = m === 'ratio' ? '× dose (e.g. 2.0)' : (m === 'absolute' ? 'grams' : 'no target');
            el('fYieldValue').disabled = m === 'none';
        }

        // --- Bean Base search (debounced client-side; server also debounces) ---
        let searchTimer = null;
        function onSearchInput() {
            clearTimeout(searchTimer);
            const q = el('fSearch').value.trim();
            if (q.length < 2) { el('searchResults').style.display = 'none'; return; }
            searchTimer = setTimeout(() => runSearch(q), 300);
        }
        function runSearch(q) {
            const ctrl = new AbortController();
            const to = setTimeout(() => ctrl.abort(), 45000);
            getJson('/api/beans/search?q=' + encodeURIComponent(q), { signal: ctrl.signal })
                .then(d => renderResults(d.results || []))
                .catch(e => { if (e.name !== 'AbortError') status('Search failed: ' + e.message); })
                .finally(() => clearTimeout(to));
        }
        function renderResults(results) {
            const box = el('searchResults');
            if (!results.length) { box.innerHTML = '<div class="result muted">No matches</div>'; box.style.display = ''; return; }
            box.innerHTML = results.slice(0, 8).map((r, i) =>
                '<div class="result" onclick="pickResult(' + i + ')">'
                + '<div class="r-title">' + esc(r.roastName || r.roasterName || 'Unnamed') + '</div>'
                + '<div class="r-sub">' + bullet([r.roasterName, r.origin, r.process].map(esc)) + '</div></div>').join('');
            box._results = results;
            box.style.display = '';
        }
        function pickResult(i) {
            const r = (el('searchResults')._results || [])[i];
            if (!r) return;
            // Copy the canonical entry into the working blob (id/visualizerCanonicalId
            // + descriptive keys) and stamp the identity fields, mirroring the app.
            editBlob = Object.assign({}, r);
            editBeanBaseId = r.id || '';
            if (r.roasterName) el('fRoaster').value = r.roasterName;
            if (r.roastName) el('fCoffee').value = r.roastName;
            COFFEE_KEYS.concat(LINK_KEYS).forEach(([fid, key]) => { const e = el(fid); if (e && r[key] != null) e.value = r[key]; });
            el('searchResults').style.display = 'none';
            status('Linked to Bean Base — review and Save.');
        }

        // --- AI "get info from page" ---
        function extractInfo() {
            const url = el('dLink').value.trim();
            if (!url) { status('Enter a product URL first'); return; }
            status('Fetching page and extracting…');
            const ctrl = new AbortController();
            const to = setTimeout(() => ctrl.abort(), 90000);
            fetch('/api/beans/extract', { method: 'POST', headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ url: url, kind: editingKind }), signal: ctrl.signal })
                .then(readJson)
                .then(d => {
                    const f = d.fields || {};
                    if (f.roasterName) el('fRoaster').value = f.roasterName;
                    if (f.roastName || f.coffeeName) el('fCoffee').value = f.roastName || f.coffeeName;
                    if (f.roastLevel) el('fRoastLevel').value = f.roastLevel;   // column, not a blob key
                    (editingKind === 'tea' ? TEA_KEYS : COFFEE_KEYS).forEach(([fid, key]) => {
                        const e = el(fid); if (e && f[key] != null && f[key] !== '') e.value = f[key];
                    });
                    // f.link is deliberately not written back: extraction runs
                    // from the URL in that same field, so the user's own URL is
                    // what gets stored, not whatever canonical link the page
                    // advertises for itself.
                    status('Extracted — review and Save.');
                })
                .catch(e => { if (e.name !== 'AbortError') status('Extraction failed: ' + e.message);
                              else status('Extraction timed out'); })
                .finally(() => clearTimeout(to));
        }

        function saveEditor() {
            const bodyData = {
                roasterName: el('fRoaster').value.trim(),
                coffeeName: el('fCoffee').value.trim(),
                roastDate: el('fRoastDate').value.trim(),
                roastLevel: el('fRoastLevel').value.trim(),
                frozenDate: el('fFrozen').value.trim(),
                defrostDate: el('fDefrost').value.trim(),
                openedDate: el('fOpened').value.trim(),
                grinderSetting: el('fGrind').value.trim(),
                rpm: parseInt(el('fRpm').value) || 0,
                doseWeightG: parseFloat(el('fDose').value) || 0,
                startWeightG: parseFloat(el('fStartWeight').value) || 0,
                equipmentId: parseInt(el('fEquipment').value, 10) || 0,
                notes: el('fNotes').value.trim()
            };
            if (!bodyData.roasterName && !bodyData.coffeeName) { status('Roaster or coffee name is required'); return; }

            // Yield anchor: send exactly one of yieldG / yieldRatio (0 clears).
            const ym = el('fYieldMode').value;
            const yv = parseFloat(el('fYieldValue').value) || 0;
            if (ym === 'absolute') bodyData.yieldG = yv;
            else if (ym === 'ratio') bodyData.yieldRatio = yv;
            else bodyData.yieldG = 0;

            // Merge edited descriptive fields into the working blob and send it.
            (editingKind === 'tea' ? TEA_KEYS : COFFEE_KEYS).concat(LINK_KEYS).forEach(([fid, key]) => {
                const e = el(fid); if (!e) return;
                const v = e.value.trim();
                if (v) editBlob[key] = v; else delete editBlob[key];
            });
            if (Object.keys(editBlob).length) bodyData.beanBaseData = JSON.stringify(editBlob);
            if (editBeanBaseId) bodyData.beanBaseId = editBeanBaseId;
            if (!editingId) bodyData.kind = editingKind;

            const req = editingId ? post('/api/bag/' + editingId, bodyData) : post('/api/bags', bodyData);
            req.then(() => { el('editor').close(); load(); }).catch(e => status(e.message));
        }

        load();
    </script>
</body>
</html>)HTML";
    return html;
}
