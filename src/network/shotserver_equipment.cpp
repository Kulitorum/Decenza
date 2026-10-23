// ShotServer equipment surface (add-recipes): REST API + /equipment page —
// the equipment packages' first web presence. All reads and writes go through
// the app's EquipmentStorage instance (one-shot signal connections) so
// copy-on-write forking, identity rules, and in-app refreshes behave exactly
// as for local edits. Lifecycle mirrors the app: used packages soft-remove
// (markRemoved), unused ones may hard-delete. Behind the server's auth gate.

#include "shotserver.h"
#include "../controllers/maincontroller.h"
#include "../core/settings.h"
#include "../core/settings_dye.h"
#include "../history/equipmentstorage.h"
#include "../ai/aimanager.h"
#include "webtemplates/base_css.h"
#include "webtemplates/menu_css.h"
#include "webtemplates/menu_html.h"
#include "webtemplates/menu_js.h"
#include "webtemplates/management_css.h"
#include "webtemplates/management_html.h"
#include "webtemplates/management_js.h"
#include "webtemplates/escape_js.h"
#include "webtemplates/grind_calibration_js.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpSocket>

namespace {

// Editable package fields accepted from the web form (matches the create/edit
// dialog's field set). The puck-prep flags travel as "puckPrep_<key>" booleans
// (PuckPrep::flagKeys) — EquipmentStorage's create/update read exactly those keys
// via PuckPrep::canonicalMerged / mapTouches, so passing them through here is all
// the wiring the web form needs.
const QStringList kPackageEditableKeys = {
    "name", "grinderBrand", "grinderModel", "grinderBurrs",
    "basketBrand", "basketModel",
    "puckPrep_wdt", "puckPrep_shaker", "puckPrep_puckScreen",
    "puckPrep_paperFilter", "puckPrep_rdt"};

QVariantMap packageFieldsFromBody(const QJsonObject& body)
{
    QVariantMap fields;
    for (const QString& key : kPackageEditableKeys) {
        if (body.contains(key))
            fields.insert(key, body[key].toVariant());
    }
    return fields;
}

} // namespace

void ShotServer::handleEquipmentApi(QTcpSocket* socket, const QString& method,
                                    const QString& path, const QByteArray& body)
{
    EquipmentStorage* equipmentStorage =
        m_mainController ? m_mainController->equipmentStorage() : nullptr;
    if (!equipmentStorage) {
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

    // GET /api/equipment — package inventory, plus the archived packages
    if (path == "/api/equipment" && method == "GET") {
        const qint64 activeId = m_settings ? m_settings->dye()->activeEquipmentId() : -1;
        const auto toArray = [activeId](const QVariantList& packages) {
            QJsonArray arr;
            for (const QVariant& v : packages) {
                QJsonObject o = QJsonObject::fromVariantMap(v.toMap());
                o["isActive"] = o["id"].toInteger() == activeId;
                arr.append(o);
            }
            return arr;
        };
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(equipmentStorage, &EquipmentStorage::inventoryReady, this,
            [this, conn, toArray, respondJson, equipmentStorage](const QVariantList& packages) {
                disconnect(*conn);
                const QJsonArray inventory = toArray(packages);
                auto archivedConn = std::make_shared<QMetaObject::Connection>();
                *archivedConn = connect(equipmentStorage, &EquipmentStorage::archivedReady, this,
                    [archivedConn, inventory, toArray, respondJson](const QVariantList& archived) {
                        disconnect(*archivedConn);
                        respondJson(QJsonObject{{"equipment", inventory}, {"count", inventory.size()},
                                                {"archived", toArray(archived)}});
                    });
                equipmentStorage->requestArchived();
            });
        equipmentStorage->requestInventory();
        return;
    }

    // POST /api/equipment — create package
    if (path == "/api/equipment" && method == "POST") {
        const QVariantMap fields = packageFieldsFromBody(bodyJson);
        if (fields.value("grinderBrand").toString().trimmed().isEmpty()
            && fields.value("grinderModel").toString().trimmed().isEmpty()
            && fields.value("name").toString().trimmed().isEmpty()) {
            respondJson(QJsonObject{{"error", "A name or grinder identity is required"}}, 400);
            return;
        }
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(equipmentStorage, &EquipmentStorage::packageCreated, this,
            [conn, respondJson](qint64 packageId, const QVariantMap& package) {
                disconnect(*conn);
                if (packageId > 0)
                    respondJson(QJsonObject::fromVariantMap(package));
                else if (package.value("error").toString() == "nameInUse")
                    respondJson(QJsonObject{{"error", "That name is already in use by another equipment package"}}, 409);
                else
                    respondJson(QJsonObject{{"error", "Create failed"}}, 500);
            });
        equipmentStorage->requestCreatePackage(fields);
        return;
    }

    // /api/equipment/<id>[/<action>]
    if (path.startsWith("/api/equipment/")) {
        const QStringList parts = path.mid(QStringLiteral("/api/equipment/").size()).split('/');
        const qint64 packageId = parts.value(0).toLongLong();
        const QString action = parts.value(1);
        if (packageId <= 0) {
            respondJson(QJsonObject{{"error", "Invalid package id"}}, 400);
            return;
        }

        // GET /api/equipment/<id>
        if (action.isEmpty() && method == "GET") {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(equipmentStorage, &EquipmentStorage::packageReady, this,
                [conn, packageId, respondJson](qint64 readyId, const QVariantMap& package) {
                    if (readyId != packageId)
                        return;
                    disconnect(*conn);
                    if (package.isEmpty())
                        respondJson(QJsonObject{{"error", "Package not found"}}, 404);
                    else
                        respondJson(QJsonObject::fromVariantMap(package));
                });
            equipmentStorage->requestPackage(packageId);
            return;
        }

        // GET  /api/equipment/<id>/calibration — the package's grind calibration
        // POST /api/equipment/<id>/calibration {side, shotId} — mark (shotId > 0) or clear
        // Same AIManager entry points as the app; a refusal is 200 {ok:false, code}.
        if (action == "calibration") {
            AIManager* ai = m_mainController ? m_mainController->aiManager() : nullptr;
            if (!ai) {
                respondJson(QJsonObject{{"error", "AI manager not available"}}, 500);
                return;
            }
            if (method == "GET") {
                auto conn = std::make_shared<QMetaObject::Connection>();
                *conn = connect(ai, &AIManager::grindCalibrationReady, this,
                    [conn, packageId, respondJson](qint64 readyId, const QVariantMap& calibration) {
                        if (readyId != packageId)
                            return;
                        disconnect(*conn);
                        respondJson(QJsonObject::fromVariantMap(calibration));
                    });
                ai->requestGrindCalibration(packageId);
                return;
            }
            const QString side = bodyJson.value("side").toString();
            const qint64 shotId = bodyJson.value("shotId").toInteger();
            if (method != "POST" || (side != "fine" && side != "coarse")) {
                respondJson(QJsonObject{{"error", "POST {side: fine|coarse, shotId}"}}, 400);
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            if (shotId > 0) {
                *conn = connect(ai, &AIManager::grindAnchorMarked, this,
                    [conn, shotId, side, respondJson](qint64 markedId, const QString& markedSide, const QString& error) {
                        if (markedId != shotId || markedSide != side)
                            return;
                        disconnect(*conn);
                        respondJson(QJsonObject{{"ok", error.isEmpty()}, {"code", error}});
                    });
                ai->requestMarkGrindAnchor(shotId, side, packageId);
            } else {
                *conn = connect(ai, &AIManager::grindAnchorCleared, this,
                    [conn, packageId, side, respondJson](qint64 clearedId, const QString& clearedSide, bool ok) {
                        if (clearedId != packageId || clearedSide != side)
                            return;
                        disconnect(*conn);
                        respondJson(QJsonObject{{"ok", ok}});
                    });
                ai->requestClearGrindAnchor(packageId, side);
            }
            return;
        }

        if (method != "POST") {
            respondJson(QJsonObject{{"error", "Method not allowed"}}, 405);
            return;
        }

        // POST /api/equipment/<id> — update (storage applies its own
        // copy-on-write identity rules, same as in-app edits)
        if (action.isEmpty()) {
            const QVariantMap fields = packageFieldsFromBody(bodyJson);
            if (fields.isEmpty()) {
                respondJson(QJsonObject{{"error", "No editable fields provided"}}, 400);
                return;
            }
            auto conn = std::make_shared<QMetaObject::Connection>();
            // packageUpdateFailed lands just before packageUpdated and names the
            // cause, so a rename collision reads the same here as in the app.
            auto reasonConn = std::make_shared<QMetaObject::Connection>();
            auto failReason = std::make_shared<QString>();
            *reasonConn = connect(equipmentStorage, &EquipmentStorage::packageUpdateFailed, this,
                [reasonConn, packageId, failReason](qint64 failedId, const QString& reason) {
                    if (failedId == packageId)
                        *failReason = reason;
                });
            *conn = connect(equipmentStorage, &EquipmentStorage::packageUpdated, this,
                [conn, reasonConn, respondJson, failReason](qint64 updatedId, bool success) {
                    // Deliberately NOT filtered on `updatedId == packageId`. The
                    // identity edit is copy-on-write: editing a package that HAS
                    // SHOTS forks it, and the terminal signal then carries the new
                    // id. Filtering on the requested id dropped that signal, so the
                    // handler never responded and never disconnected — the request
                    // hung until the browser gave up, for what is the ordinary case
                    // of editing a grinder you have actually pulled shots on.
                    //
                    // Failures always carry the requested id (requestUpdatePackage
                    // resets it before emitting), so only a success can differ. The
                    // residual risk is a package update issued from another surface
                    // in the same instant being taken for ours; the in-app dialog
                    // correlates exactly the same way (a one-shot "awaiting" flag,
                    // not the id), and answering with an equivalent result beats
                    // never answering at all.
                    disconnect(*conn);
                    disconnect(*reasonConn);
                    if (success)
                        // The RESULTING id, which is what the client must address
                        // from here on when the edit forked or merged the package.
                        respondJson(QJsonObject{{"updated", true}, {"packageId", updatedId}});
                    else if (*failReason == QLatin1String("nameInUse"))
                        respondJson(QJsonObject{{"error", "That name is already in use by another equipment package"}}, 409);
                    else if (*failReason == QLatin1String("partiallyApplied"))
                        // The identity edit committed in its own transaction and
                        // cannot be undone from the storage layer, so this is not a
                        // "nothing happened" 404: re-sending the same body is not a
                        // no-op, and the client needs to know which half landed.
                        respondJson(QJsonObject{{"error", "The grinder, basket or puck-prep change was saved, "
                                                          "but the name was not — reopen the package and rename it"}}, 500);
                    else
                        respondJson(QJsonObject{{"error", "Package not found or update failed"}}, 404);
                });
            equipmentStorage->requestUpdatePackage(packageId, fields);
            return;
        }

        // POST /api/equipment/<id>/remove — soft-remove from inventory
        // (shot history keeps its attribution; mirrors in-app "remove").
        if (action == "remove") {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(equipmentStorage, &EquipmentStorage::packageUpdated, this,
                [conn, packageId, respondJson](qint64 updatedId, bool success) {
                    if (updatedId != packageId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{"removed", true}, {"packageId", packageId}});
                    else
                        respondJson(QJsonObject{{"error", "Package not found"}}, 404);
                });
            equipmentStorage->requestMarkRemoved(packageId);
            return;
        }

        // POST /api/equipment/<id>/restore — an archived package back into inventory
        if (action == "restore") {
            auto reason = std::make_shared<QString>();
            auto failConn = std::make_shared<QMetaObject::Connection>();
            *failConn = connect(equipmentStorage, &EquipmentStorage::packageUpdateFailed, this,
                [packageId, reason](qint64 failedId, const QString& why) {
                    if (failedId == packageId) *reason = why;
                });
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(equipmentStorage, &EquipmentStorage::packageUpdated, this,
                [conn, failConn, reason, packageId, respondJson](qint64 updatedId, bool success) {
                    if (updatedId != packageId)
                        return;
                    disconnect(*conn);
                    disconnect(*failConn);
                    if (success)
                        respondJson(QJsonObject{{"restored", true}, {"packageId", packageId}});
                    else
                        respondJson(QJsonObject{{"error", "Package is not archived"},
                                                {"code", *reason}}, 409);
                });
            equipmentStorage->requestRestorePackage(packageId);
            return;
        }

        // POST /api/equipment/<id>/delete — mistaken creations only
        // (storage refuses when shots/bags reference the package).
        if (action == "delete") {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(equipmentStorage, &EquipmentStorage::packageDeleted, this,
                [conn, packageId, respondJson](qint64 deletedId, bool success) {
                    if (deletedId != packageId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{"deleted", true}});
                    else
                        respondJson(QJsonObject{{"error",
                            "Delete refused — the package is referenced (remove it instead) or was not found"}}, 409);
                });
            equipmentStorage->requestDeletePackage(packageId);
            return;
        }

        // POST /api/equipment/<id>/activate — select as active package
        if (action == "activate") {
            if (!m_settings) {
                respondJson(QJsonObject{{"error", "Settings not available"}}, 500);
                return;
            }
            m_settings->dye()->setActiveEquipmentId(packageId);
            respondJson(QJsonObject{{"activated", true}, {"packageId", packageId}});
            return;
        }
    }

    respondJson(QJsonObject{{"error", "Unknown equipment endpoint"}}, 404);
}

QString ShotServer::generateEquipmentPage() const
{
    QString html = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Decenza — Equipment</title>
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
    html += generateManagementHeader(QStringLiteral("&#9881; Equipment"));
    html += R"HTML(
    <div class="container">
        <div class="toolbar">
            <span>Add</span>
            <button onclick="openEditor(null)">Equipment</button>
        </div>
        <div id="status"></div>
        <div id="list"></div>
    </div>

    <dialog id="editor">
        <h2 id="editorTitle">Equipment</h2>
        <label>Name (optional label)</label><input id="fName">
        <div class="grid-2">
            <div><label>Grinder brand</label><input id="fGrinderBrand"></div>
            <div><label>Grinder model</label><input id="fGrinderModel"></div>
        </div>
        <label>Burrs</label><input id="fBurrs">
        <div class="grid-2">
            <div><label>Basket brand</label><input id="fBasketBrand"></div>
            <div><label>Basket model</label><input id="fBasketModel"></div>
        </div>
        <details class="dialog-section" open>
            <summary>Puck prep</summary>
            <div class="check-row"><input type="checkbox" id="fWdt"><label for="fWdt">WDT</label></div>
            <div class="check-row"><input type="checkbox" id="fShaker"><label for="fShaker">Shaker</label></div>
            <div class="check-row"><input type="checkbox" id="fPuckScreen"><label for="fPuckScreen">Puck screen</label></div>
            <div class="check-row"><input type="checkbox" id="fPaperFilter"><label for="fPaperFilter">Bottom paper filter</label></div>
            <div class="check-row"><input type="checkbox" id="fRdt"><label for="fRdt">RDT spritz</label></div>
        </details>
        <div class="dialog-actions">
            <button onclick="el('editor').close()">Cancel</button>
            <button class="primary" onclick="saveEditor()">Save</button>
        </div>
    </dialog>

    <script>
)HTML";
    html += WEB_JS_MENU;
    html += WEB_JS_POWER_CONTROL;
    html += WEB_JS_MANAGEMENT;
    html += WEB_JS_ESCAPE_HTML;
    html += WEB_JS_GRIND_CALIBRATION;
    html += R"HTML(
        let editingId = null;
        let packages = [];
        let archived = [];
        let showArchived = false;

        // Puck-prep flags: [wire key, DOM id, label] — mirrors PuckPrep::flagKeys.
        const PUCK = [
            ['puckPrep_wdt', 'fWdt', 'WDT'],
            ['puckPrep_shaker', 'fShaker', 'Shaker'],
            ['puckPrep_puckScreen', 'fPuckScreen', 'Puck screen'],
            ['puckPrep_paperFilter', 'fPaperFilter', 'Bottom paper filter'],
            ['puckPrep_rdt', 'fRdt', 'RDT spritz'],
        ];

        function load() {
            status('Loading…');
            getJson('/api/equipment')
                .then(d => { archived = d.archived || []; render(d.equipment || []); status(''); })
                .catch(e => status('Could not load equipment: ' + e.message));
        }

        function label(p) {
            return p.name || ((p.grinderBrand || '') + ' ' + (p.grinderModel || '')).trim()
                || ('Package ' + p.id);
        }

        function prepLine(p) {
            return PUCK.filter(f => p[f[0]]).map(f => f[2]).join(', ');
        }

        function cardHtml(p, isArchived) {
            const basket = ((p.basketBrand || '') + ' ' + (p.basketModel || '')).trim();
            const prep = prepLine(p);
            let body = '<div class="card-body">'
                + '<div class="card-title">' + esc(label(p))
                + (p.isActive ? '<span class="badge">Active</span>' : '') + '</div>';
            if (p.grinderBurrs) body += '<div class="attr-line">' + esc(p.grinderBurrs) + '</div>';
            if (basket) body += '<div class="attr-line">Basket: ' + esc(basket) + '</div>';
            if (prep) body += '<div class="attr-line">Prep: ' + esc(prep) + '</div>';
            body += '<div id="gc-' + p.id + '"></div>';
            body += '</div>';
            if (isArchived)
                return '<div class="card dimmed">'
                    + '<div class="card-head">' + body + '</div>'
                    + '<div class="actions"><button onclick="restorePackage(' + p.id + ')">Restore</button></div></div>';
            return '<div class="card' + (p.isActive ? ' active' : '') + '">'
                + '<div class="card-head">' + body + '</div>'
                + '<div class="actions">'
                + '<button class="primary" onclick="activate(' + p.id + ')"' + (p.isActive ? ' disabled' : '') + '>Activate</button>'
                + '<button onclick="openEditor(' + p.id + ')">Edit</button>'
                + (p.shotCount > 0
                    ? '<button class="danger" onclick="removePackage(' + p.id + ')">Archive</button>'
                    : '<button class="danger" onclick="deletePackage(' + p.id + ')">Delete</button>')
                + '</div></div>';
        }

        function render(list) {
            packages = list;
            let html = list.length
                ? '<div class="grid">' + list.map(p => cardHtml(p, false)).join('') + '</div>'
                : '<div class="empty"><h2>No equipment yet</h2>'
                  + '<div>Add your grinder to track its model, burrs and dial-in per bag.</div></div>';
            if (archived.length) {
                html += '<div class="toolbar"><button onclick="toggleArchived()">'
                    + (showArchived ? 'Hide archived' : 'Show archived') + ' (' + archived.length + ')</button></div>';
                if (showArchived)
                    html += '<div class="grid">' + archived.map(p => cardHtml(p, true)).join('') + '</div>';
            }
            el('list').innerHTML = html;
            list.forEach(p => loadGrindCalibration('gc-' + p.id, p.id, {}));
            if (showArchived) archived.forEach(p => loadGrindCalibration('gc-' + p.id, p.id, {}));
        }
        function toggleArchived() { showArchived = !showArchived; render(packages); }
        function restorePackage(id) {
            post('/api/equipment/' + id + '/restore').then(load).catch(e => status(e.message));
        }

        function activate(id) { post('/api/equipment/' + id + '/activate').then(load).catch(e => status(e.message)); }
        // Lifecycle-driven, matching the app and the /beans page: a referenced
        // package (shotCount>0) archives (Show archived restores it); an unused one
        // hard-deletes. Each surfaces its own error rather than masking a real failure.
        function removePackage(id) {
            if (!confirm('Archive this package? Its shot history is kept and you can restore it.')) return;
            post('/api/equipment/' + id + '/remove').then(load).catch(e => status(e.message));
        }
        function deletePackage(id) {
            if (!confirm('Delete this package permanently?')) return;
            post('/api/equipment/' + id + '/delete').then(load).catch(e => status(e.message));
        }

        function openEditor(id) {
            editingId = id;
            const p = packages.find(x => x.id === id) || {};
            el('editorTitle').textContent = id ? 'Edit Equipment' : 'New Equipment';
            el('fName').value = p.name || '';
            el('fGrinderBrand').value = p.grinderBrand || '';
            el('fGrinderModel').value = p.grinderModel || '';
            el('fBurrs').value = p.grinderBurrs || '';
            el('fBasketBrand').value = p.basketBrand || '';
            el('fBasketModel').value = p.basketModel || '';
            PUCK.forEach(f => { el(f[1]).checked = !!p[f[0]]; });
            el('editor').showModal();
        }

        function saveEditor() {
            const bodyData = {
                name: el('fName').value.trim(),
                grinderBrand: el('fGrinderBrand').value.trim(),
                grinderModel: el('fGrinderModel').value.trim(),
                grinderBurrs: el('fBurrs').value.trim(),
                basketBrand: el('fBasketBrand').value.trim(),
                basketModel: el('fBasketModel').value.trim()
            };
            PUCK.forEach(f => { bodyData[f[0]] = el(f[1]).checked; });
            if (!bodyData.name && !bodyData.grinderBrand && !bodyData.grinderModel) {
                status('A name or grinder identity is required'); return;
            }
            const req = editingId ? post('/api/equipment/' + editingId, bodyData)
                                  : post('/api/equipment', bodyData);
            req.then(() => { el('editor').close(); load(); })
               .catch(e => status(e.message));
        }

        load();
    </script>
</body>
</html>)HTML";
    return html;
}
