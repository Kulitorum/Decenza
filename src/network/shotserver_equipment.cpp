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
#include "webtemplates/base_css.h"
#include "webtemplates/menu_css.h"
#include "webtemplates/menu_html.h"
#include "webtemplates/menu_js.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpSocket>

namespace {

// Editable package fields accepted from the web form (matches the create/edit
// dialog's field set; puck-prep flags stay in-app for v1).
const QStringList kPackageEditableKeys = {
    "name", "grinderBrand", "grinderModel", "grinderBurrs",
    "basketBrand", "basketModel"};

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

    // GET /api/equipment — package inventory
    if (path == "/api/equipment" && method == "GET") {
        const qint64 activeId = m_settings ? m_settings->dye()->activeEquipmentId() : -1;
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(equipmentStorage, &EquipmentStorage::inventoryReady, this,
            [conn, activeId, respondJson](const QVariantList& packages) {
                disconnect(*conn);
                QJsonArray arr;
                for (const QVariant& v : packages) {
                    QJsonObject o = QJsonObject::fromVariantMap(v.toMap());
                    o["isActive"] = o["id"].toInteger() == activeId;
                    arr.append(o);
                }
                respondJson(QJsonObject{{"equipment", arr}, {"count", arr.size()}});
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
                if (packageId <= 0)
                    respondJson(QJsonObject{{"error", "Create failed"}}, 500);
                else
                    respondJson(QJsonObject::fromVariantMap(package));
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
            *conn = connect(equipmentStorage, &EquipmentStorage::packageUpdated, this,
                [conn, packageId, respondJson](qint64 updatedId, bool success) {
                    if (updatedId != packageId)
                        return;
                    disconnect(*conn);
                    if (success)
                        respondJson(QJsonObject{{"updated", true}, {"packageId", packageId}});
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
        .muted { color: var(--text-secondary); }
        #status { margin: 0.5rem 0; color: var(--text-secondary); min-height: 1.2em; }
        dialog { background: var(--surface); color: var(--text); border: 1px solid var(--border);
                 border-radius: 8px; padding: 1.25rem; max-width: 480px; width: 92%; }
        dialog::backdrop { background: rgba(0,0,0,0.6); }
        dialog label { display: block; margin: 0.5rem 0 0.15rem; font-size: 0.85rem; color: var(--text-secondary); }
        dialog input { width: 100%; background: var(--bg); color: var(--text);
                 border: 1px solid var(--border); border-radius: 6px; padding: 0.4rem; }
        dialog .row { margin-top: 1rem; justify-content: flex-end; }
    </style>
</head>
<body>
    <div class="header">
        <div class="header-content">
            <h1>&#9881; Equipment</h1>
)HTML";
    html += generateMenuHtml();
    html += R"HTML(
        </div>
    </div>
    <div class="container">
        <div class="row">
            <button class="primary" onclick="openEditor(null)">Add Equipment</button>
        </div>
        <div id="status"></div>
        <div id="list"></div>
    </div>

    <dialog id="editor">
        <h2 id="editorTitle">Equipment</h2>
        <label>Name (optional label)</label><input id="fName">
        <label>Grinder brand</label><input id="fGrinderBrand">
        <label>Grinder model</label><input id="fGrinderModel">
        <label>Burrs</label><input id="fBurrs">
        <label>Basket brand</label><input id="fBasketBrand">
        <label>Basket model</label><input id="fBasketModel">
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
        let packages = [];
        const status = (m) => { document.getElementById('status').textContent = m || ''; };
        const esc = (s) => String(s ?? '').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));

        function load() {
            status('Loading…');
            fetch('/api/equipment')
                .then(r => { if (!r.ok) throw new Error('Server error (' + r.status + ')'); return r.json(); })
                .then(d => { render(d.equipment || []); status(''); })
                .catch(e => status('Could not load equipment: ' + e.message));
        }

        function label(p) {
            return p.name || ((p.grinderBrand || '') + ' ' + (p.grinderModel || '')).trim() || ('Package ' + p.id);
        }

        function subtitle(p) {
            const parts = [];
            const grinder = ((p.grinderBrand || '') + ' ' + (p.grinderModel || '')).trim();
            if (grinder) parts.push(esc(grinder));
            if (p.grinderBurrs) parts.push(esc(p.grinderBurrs));
            const basket = ((p.basketBrand || '') + ' ' + (p.basketModel || '')).trim();
            if (basket) parts.push('basket ' + esc(basket));
            if (p.lastGrindSetting) parts.push('last grind ' + esc(p.lastGrindSetting));
            return parts.join(' &middot; ');
        }

        function render(list) {
            packages = list;
            document.getElementById('list').innerHTML = list.length ? list.map(p =>
                '<div class="card' + (p.isActive ? ' active' : '') + '">'
                + '<h3>' + esc(label(p)) + (p.isActive ? '<span class="badge">Active</span>' : '') + '</h3>'
                + '<div class="sub">' + subtitle(p) + '</div>'
                + '<div class="row">'
                + '<button onclick="activate(' + p.id + ')"' + (p.isActive ? ' disabled' : '') + '>Activate</button>'
                + '<button onclick="openEditor(' + p.id + ')">Edit</button>'
                + '<button onclick="removePackage(' + p.id + ')">Remove</button>'
                + '</div></div>').join('')
                : '<p class="muted">No equipment packages yet.</p>';
        }

        function post(url, body) {
            return fetch(url, { method: 'POST', headers: {'Content-Type': 'application/json'},
                                body: JSON.stringify(body || {}) })
                .then(r => r.json().then(d => { if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')')); return d; }));
        }

        function activate(id) { post('/api/equipment/' + id + '/activate').then(() => load()).catch(e => status(e.message)); }
        function removePackage(id) {
            if (!confirm('Remove this package from the inventory? Shot history keeps its attribution.')) return;
            // Try hard delete first (mistaken creations); fall back to soft
            // remove when the server refuses because it is referenced.
            post('/api/equipment/' + id + '/delete')
                .then(() => load())
                .catch(() => post('/api/equipment/' + id + '/remove').then(() => load()).catch(e => status(e.message)));
        }

        function openEditor(id) {
            editingId = id;
            const p = packages.find(x => x.id === id) || {};
            document.getElementById('editorTitle').textContent = id ? 'Edit Equipment' : 'New Equipment';
            document.getElementById('fName').value = p.name || '';
            document.getElementById('fGrinderBrand').value = p.grinderBrand || '';
            document.getElementById('fGrinderModel').value = p.grinderModel || '';
            document.getElementById('fBurrs').value = p.grinderBurrs || '';
            document.getElementById('fBasketBrand').value = p.basketBrand || '';
            document.getElementById('fBasketModel').value = p.basketModel || '';
            document.getElementById('editor').showModal();
        }

        function saveEditor() {
            const bodyData = {
                name: document.getElementById('fName').value.trim(),
                grinderBrand: document.getElementById('fGrinderBrand').value.trim(),
                grinderModel: document.getElementById('fGrinderModel').value.trim(),
                grinderBurrs: document.getElementById('fBurrs').value.trim(),
                basketBrand: document.getElementById('fBasketBrand').value.trim(),
                basketModel: document.getElementById('fBasketModel').value.trim()
            };
            if (!bodyData.name && !bodyData.grinderBrand && !bodyData.grinderModel) {
                status('A name or grinder identity is required'); return;
            }
            const req = editingId ? post('/api/equipment/' + editingId, bodyData) : post('/api/equipment', bodyData);
            req.then(() => { document.getElementById('editor').close(); load(); })
               .catch(e => status(e.message));
        }

        load();
    </script>
</body>
</html>)HTML";
    return html;
}
