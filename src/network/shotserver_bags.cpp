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
#include "../history/coffeebagstorage.h"
#include "../history/shothistorystorage.h"
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

// Editable bag columns accepted from the web form (v1: plain fields only —
// Bean Base linking and AI URL import stay in-app).
const QStringList kBagEditableKeys = {
    "roasterName", "coffeeName", "roastDate", "roastLevel", "frozenDate",
    "defrostDate", "notes", "startWeightG", "grinderSetting", "doseWeightG",
    "yieldOverrideG", "inInventory"};

QVariantMap bagFieldsFromBody(const QJsonObject& body)
{
    QVariantMap fields;
    for (const QString& key : kBagEditableKeys) {
        if (body.contains(key))
            fields.insert(key, body[key].toVariant());
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
        const QVariantMap fields = bagFieldsFromBody(bodyJson);
        if (fields.value("roasterName").toString().trimmed().isEmpty()
            && fields.value("coffeeName").toString().trimmed().isEmpty()) {
            respondJson(QJsonObject{{"error", "roasterName or coffeeName is required"}}, 400);
            return;
        }
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
            bagStorage->requestUpdateBag(bagId, fields);
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
            <h1>&#127793; Beans</h1>
)HTML";
    html += generateMenuHtml();
    html += R"HTML(
        </div>
    </div>
    <div class="container">
        <div class="row">
            <button class="primary" onclick="openEditor(null)">Add Bag</button>
        </div>
        <div id="status"></div>
        <div id="list"></div>
    </div>

    <dialog id="editor">
        <h2 id="editorTitle">Bag</h2>
        <label>Roaster</label><input id="fRoaster">
        <label>Coffee</label><input id="fCoffee">
        <label>Roast date (YYYY-MM-DD)</label><input id="fRoastDate">
        <label>Roast level</label><input id="fRoastLevel">
        <label>Frozen date (YYYY-MM-DD)</label><input id="fFrozen">
        <label>Defrost date (YYYY-MM-DD)</label><input id="fDefrost">
        <label>Start weight (g)</label><input id="fStartWeight" type="number" step="1">
        <label>Grind setting</label><input id="fGrind">
        <label>Dose (g)</label><input id="fDose" type="number" step="0.1">
        <label>Notes</label><input id="fNotes">
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
        let bags = [];
        const status = (m) => { document.getElementById('status').textContent = m || ''; };
        const esc = (s) => String(s ?? '').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));

        function load() {
            status('Loading…');
            fetch('/api/bags')
                .then(r => { if (!r.ok) throw new Error('Server error (' + r.status + ')'); return r.json(); })
                .then(d => { render(d.bags || []); status(''); })
                .catch(e => status('Could not load bags: ' + e.message));
        }

        function subtitle(b) {
            const parts = [];
            if (b.roastDate) parts.push('roasted ' + esc(b.roastDate));
            if (b.frozenDate) parts.push('frozen ' + esc(b.frozenDate));
            if (b.defrostDate) parts.push('defrosted ' + esc(b.defrostDate));
            if (b.grinderSetting) parts.push('grind ' + esc(b.grinderSetting));
            if (b.doseWeightG > 0) parts.push(b.doseWeightG.toFixed(1) + 'g dose');
            if (b.shotCount > 0) parts.push(b.shotCount + ' shots');
            return parts.join(' &middot; ');
        }

        function render(list) {
            bags = list;
            document.getElementById('list').innerHTML = list.length ? list.map(b =>
                '<div class="card' + (b.isActive ? ' active' : '') + '">'
                + '<h3>' + esc(((b.roasterName || '') + ' ' + (b.coffeeName || '')).trim())
                + (b.isActive ? '<span class="badge">Active</span>' : '') + '</h3>'
                + '<div class="sub">' + subtitle(b) + '</div>'
                + '<div class="row">'
                + '<button onclick="activate(' + b.id + ')"' + (b.isActive ? ' disabled' : '') + '>Activate</button>'
                + '<button onclick="openEditor(' + b.id + ')">Edit</button>'
                + (b.shotCount > 0
                    ? '<button onclick="finishBag(' + b.id + ')">Bag finished</button>'
                    : '<button onclick="deleteBag(' + b.id + ')">Delete</button>')
                + '</div></div>').join('')
                : '<p class="muted">No open bags.</p>';
        }

        function post(url, body) {
            return fetch(url, { method: 'POST', headers: {'Content-Type': 'application/json'},
                                body: JSON.stringify(body || {}) })
                .then(r => r.json().then(d => { if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')')); return d; }));
        }

        function activate(id) { post('/api/bag/' + id + '/activate').then(() => load()).catch(e => status(e.message)); }
        function finishBag(id) {
            if (!confirm('Mark this bag as finished? It leaves the inventory; shots keep their history.')) return;
            post('/api/bag/' + id + '/finish').then(() => load()).catch(e => status(e.message));
        }
        function deleteBag(id) {
            if (!confirm('Delete this bag permanently?')) return;
            post('/api/bag/' + id + '/delete').then(() => load()).catch(e => status(e.message));
        }

        function openEditor(id) {
            editingId = id;
            const b = bags.find(x => x.id === id) || {};
            document.getElementById('editorTitle').textContent = id ? 'Edit Bag' : 'New Bag';
            document.getElementById('fRoaster').value = b.roasterName || '';
            document.getElementById('fCoffee').value = b.coffeeName || '';
            document.getElementById('fRoastDate').value = b.roastDate || '';
            document.getElementById('fRoastLevel').value = b.roastLevel || '';
            document.getElementById('fFrozen').value = b.frozenDate || '';
            document.getElementById('fDefrost').value = b.defrostDate || '';
            document.getElementById('fStartWeight').value = b.startWeightG > 0 ? b.startWeightG : '';
            document.getElementById('fGrind').value = b.grinderSetting || '';
            document.getElementById('fDose').value = b.doseWeightG > 0 ? b.doseWeightG : '';
            document.getElementById('fNotes').value = b.notes || '';
            document.getElementById('editor').showModal();
        }

        function saveEditor() {
            const bodyData = {
                roasterName: document.getElementById('fRoaster').value.trim(),
                coffeeName: document.getElementById('fCoffee').value.trim(),
                roastDate: document.getElementById('fRoastDate').value.trim(),
                roastLevel: document.getElementById('fRoastLevel').value.trim(),
                frozenDate: document.getElementById('fFrozen').value.trim(),
                defrostDate: document.getElementById('fDefrost').value.trim(),
                startWeightG: parseFloat(document.getElementById('fStartWeight').value) || 0,
                grinderSetting: document.getElementById('fGrind').value.trim(),
                doseWeightG: parseFloat(document.getElementById('fDose').value) || 0,
                notes: document.getElementById('fNotes').value.trim()
            };
            if (!bodyData.roasterName && !bodyData.coffeeName) { status('Roaster or coffee name is required'); return; }
            const req = editingId ? post('/api/bag/' + editingId, bodyData) : post('/api/bags', bodyData);
            req.then(() => { document.getElementById('editor').close(); load(); })
               .catch(e => status(e.message));
        }

        load();
    </script>
</body>
</html>)HTML";
    return html;
}
