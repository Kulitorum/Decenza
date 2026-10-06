#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// Shared JavaScript utilities for the inventory management pages — /beans,
// /recipes, /equipment (polish-shotserver-inventory-pages). Consolidates the
// helpers that were copy-pasted verbatim across the three pages: HTML escaping,
// the status line, the fetch-JSON POST wrapper (unwraps {error}), a GET wrapper,
// a dot-joiner mirroring the app's Theme.joinWithBullet (drops empty parts), and
// the list search + sort the /recipes and /beans pages share.
// Every fetch checks r.ok and carries error handling per SHOTSERVER.md.
inline constexpr const char* WEB_JS_MANAGEMENT = R"JS(
        const el = (id) => document.getElementById(id);
        const status = (m) => { const s = el('status'); if (s) s.textContent = m || ''; };
        const esc = (s) => String(s ?? '').replace(/[&<>"]/g,
            c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
        // Join non-empty parts with the app's bold middle dot.
        const bullet = (parts) => parts.filter(p => p !== null && p !== undefined
            && String(p).trim() !== '').join(' &middot; ');

        // Read the body as text and parse it safely: a non-2xx response with a
        // non-JSON body (auth login page, proxy 502) must surface "Server error
        // (NNN)", not a misleading JSON parse error — while a JSON {error} body
        // still yields its message (SHOTSERVER.md fetch rules).
        function readJson(r) {
            return r.text().then(t => {
                let d = {};
                try { d = t ? JSON.parse(t) : {}; }
                catch (e) { if (!r.ok) throw new Error('Server error (' + r.status + ')'); throw e; }
                if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')'));
                return d;
            });
        }
        function getJson(url, opts) { return fetch(url, opts).then(readJson); }
        function post(url, body) {
            return fetch(url, { method: 'POST', headers: {'Content-Type': 'application/json'},
                                body: JSON.stringify(body || {}) }).then(readJson);
        }

        // List search and sort, as the app's RecipeSearch.js (tests/tst_recipesearch.cpp
        // checks they agree): lower-case, DELETE - / . (so "df" matches "D-Flow"),
        // split on whitespace; a match needs every token.
        function normalizeSearch(s) { return String(s || '').toLowerCase().replace(/[-\/.]/g, ''); }
        function tokenizeSearch(q) { return normalizeSearch(q).split(/\s+/).filter(Boolean); }
        // Blank keys (0 or '') last in both directions, ties by id; anything but
        // 'ASC' is descending.
        function sortedCopy(list, keyOf, direction) {
            const asc = direction === 'ASC';
            const blank = (k) => typeof k === 'number' ? k <= 0 : String(k).length === 0;
            return list.slice().sort((a, b) => {
                const ka = keyOf(a), kb = keyOf(b);
                if (blank(ka) !== blank(kb)) return blank(ka) ? 1 : -1;
                let cmp = typeof ka === 'number' ? ka - kb : String(ka).localeCompare(String(kb));
                if (cmp === 0) cmp = (Number(a.id) || 0) - (Number(b.id) || 0);
                return asc ? cmp : -cmp;
            });
        }

        // The search field and sort buttons of a list page, as the app's SearchField
        // and SortControls. initListControls fills #searchbar and calls onChange
        // after each change; the page reads listView.query, .field and .dir. The
        // sort is the device's saved one, and a change is saved back under
        // settingPrefix + 'SortField' / 'SortDirection'.
        const listView = { query: '', field: '', dir: 'DESC', sorts: [], settingPrefix: '', onChange: null };
        function initListControls(o) {
            Object.assign(listView, o);
            if (!listView.sorts.some(s => s[0] === listView.field)) listView.field = listView.sorts[0][0];
            if (listView.dir !== 'ASC') listView.dir = 'DESC';
            el('searchbar').innerHTML = '<div class="search-wrap">'
                + '<input class="search" id="search" placeholder="' + esc(o.placeholder) + '" oninput="listSearchInput()">'
                + '<button class="search-clear" id="searchClear" onclick="listClearSearch()" style="display:none">&times;</button>'
                + '</div><button id="sortField" onclick="listCycleSort()"></button>'
                + '<button id="sortDir" onclick="listToggleDir()"></button>';
            showListControls();
        }
        function showListControls() {
            el('searchClear').style.display = listView.query ? '' : 'none';
            el('sortField').textContent = 'Sort: ' + listView.sorts.find(s => s[0] === listView.field)[1];
            el('sortDir').textContent = listView.dir === 'ASC' ? 'Oldest / A→Z first' : 'Newest / Z→A first';
        }
        function listChanged() { showListControls(); listView.onChange(); }
        function listSearchInput() { listView.query = el('search').value.trim(); listChanged(); }
        function listClearSearch() { el('search').value = ''; listView.query = ''; listChanged(); }
        function saveListSort() {
            post('/api/settings', { [listView.settingPrefix + 'SortField']: listView.field,
                                    [listView.settingPrefix + 'SortDirection']: listView.dir })
                .catch(e => status('Could not save the sort: ' + e.message));
        }
        // The next field, in its default direction, as the app's sort picker.
        function listCycleSort() {
            const i = listView.sorts.findIndex(s => s[0] === listView.field);
            const next = listView.sorts[(i + 1) % listView.sorts.length];
            listView.field = next[0];
            listView.dir = next[2];
            saveListSort();
            listChanged();
        }
        function listToggleDir() {
            listView.dir = listView.dir === 'ASC' ? 'DESC' : 'ASC';
            saveListSort();
            listChanged();
        }
)JS";

// The device's saved list sort as the object initListControls merges in. '<' is
// escaped so a stored value cannot close the page's <script>.
inline QString webListSortJson(const QString& field, const QString& direction)
{
    const QJsonObject o{{QStringLiteral("field"), field}, {QStringLiteral("dir"), direction}};
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact))
        .replace(QLatin1Char('<'), QLatin1String("\\u003c"));
}
