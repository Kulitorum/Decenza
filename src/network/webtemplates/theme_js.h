#pragma once

// Theme editor JavaScript: color/font editing, presets, API calls, SSE sync
// Used by theme_page.h

inline constexpr const char* WEB_JS_THEME_EDITOR = R"JS(
// Color definitions (matching Theme.qml categories)
const COLOR_DEFS = [
    { category: "Core UI", colors: [
        { name: "backgroundColor", display: "Background" },
        { name: "surfaceColor", display: "Surface" },
        { name: "primaryColor", display: "Primary" },
        { name: "secondaryColor", display: "Secondary" },
        { name: "textColor", display: "Text" },
        { name: "textSecondaryColor", display: "Text Secondary" },
        { name: "accentColor", display: "Accent" },
        { name: "borderColor", display: "Border" }
    ]},
    { category: "Status", colors: [
        { name: "successColor", display: "Success" },
        { name: "warningColor", display: "Warning" },
        { name: "highlightColor", display: "Highlight" },
        { name: "errorColor", display: "Error" }
    ]},
    { category: "Chart", colors: [
        { name: "pressureColor", display: "Pressure" },
        { name: "pressureGoalColor", display: "Pressure Goal" },
        { name: "flowColor", display: "Flow" },
        { name: "flowGoalColor", display: "Flow Goal" },
        { name: "temperatureColor", display: "Temperature" },
        { name: "temperatureGoalColor", display: "Temp Goal" },
        { name: "weightColor", display: "Weight" },
        { name: "weightFlowColor", display: "Weight Flow" }
    ]},
    { category: "DYE Metadata", colors: [
        { name: "dyeDoseColor", display: "Dose" },
        { name: "dyeOutputColor", display: "Output" },
        { name: "dyeTdsColor", display: "TDS" },
        { name: "dyeEyColor", display: "EY" }
    ]},
    { category: "UI Indicators", colors: [
        { name: "buttonDisabled", display: "Button Disabled" },
        { name: "stopMarkerColor", display: "Stop Marker" },
        { name: "frameMarkerColor", display: "Frame Marker" },
        { name: "modifiedIndicatorColor", display: "Modified" },
        { name: "simulationIndicatorColor", display: "Simulation" },
        { name: "warningButtonColor", display: "Warning Button" },
        { name: "successButtonColor", display: "Success Button" }
    ]},
    { category: "Lists & Badges", colors: [
        { name: "rowAlternateColor", display: "Row Alternate" },
        { name: "rowAlternateLightColor", display: "Row Alt Light" },
        { name: "sourceBadgeBlueColor", display: "Badge Blue" },
        { name: "sourceBadgeGreenColor", display: "Badge Green" },
        { name: "sourceBadgeOrangeColor", display: "Badge Orange" }
    ]}
];

const FONT_DEFS = [
    { name: "headingSize", display: "Heading", min: 16, max: 64 },
    { name: "titleSize", display: "Title", min: 12, max: 48 },
    { name: "subtitleSize", display: "Subtitle", min: 10, max: 36 },
    { name: "bodySize", display: "Body", min: 10, max: 36 },
    { name: "labelSize", display: "Label", min: 8, max: 28 },
    { name: "captionSize", display: "Caption", min: 8, max: 24 },
    { name: "valueSize", display: "Value", min: 24, max: 96 },
    { name: "timerSize", display: "Timer", min: 36, max: 120 }
];

let currentTheme = null;
let lastChangeTime = 0;  // To ignore self-echo SSE events
let debounceTimers = {};

// -- Rendering --

function renderColors(colors, pageColors) {
    const panel = document.getElementById('colorPanel');
    panel.innerHTML = '';
    const onPage = new Set(pageColors || []);

    for (const cat of COLOR_DEFS) {
        const title = document.createElement('div');
        title.className = 'category-title';
        title.textContent = cat.category;
        panel.appendChild(title);

        for (const c of cat.colors) {
            const val = colors[c.name] || '#000000';
            const isOnPage = onPage.has(c.name);
            const row = document.createElement('div');
            row.className = 'color-row' + (isOnPage ? ' on-page' : '');
            row.innerHTML =
                '<div class="page-dot" title="Used on current page"></div>' +
                '<div class="color-swatch" style="background:' + val + '">' +
                    '<input type="color" value="' + val + '" data-name="' + c.name + '" ' +
                           'oninput="onColorInput(this)" onchange="onColorChange(this)">' +
                '</div>' +
                '<span class="color-label" onclick="flashColor(\'' + c.name + '\')" title="Click to flash on device">' + c.display + '</span>' +
                '<input type="text" class="color-hex" value="' + val + '" data-name="' + c.name + '" ' +
                       'oninput="onHexInput(this)">';
            panel.appendChild(row);
        }
    }
}

function renderFonts(fonts) {
    const panel = document.getElementById('fontPanel');
    panel.innerHTML = '';
    for (const f of FONT_DEFS) {
        const val = fonts[f.name] || 16;
        const row = document.createElement('div');
        row.className = 'font-row';
        row.innerHTML =
            '<span class="font-label">' + f.display + '</span>' +
            '<input type="range" class="font-slider" min="' + f.min + '" max="' + f.max + '" value="' + val + '" ' +
                   'data-name="' + f.name + '" oninput="onFontSlider(this)">' +
            '<span class="font-value" id="fv_' + f.name + '">' + val + 'px</span>';
        panel.appendChild(row);
    }
}

function renderPresets(presets, activeName) {
    const row = document.getElementById('presetRow');
    row.innerHTML = '';
    document.getElementById('themeName').textContent = activeName;

    for (const p of presets) {
        const btn = document.createElement('button');
        btn.className = 'preset-btn' + (p.name === activeName ? ' active' : '');
        btn.style.background = p.primaryColor || '#4e85f4';

        const nameSpan = document.createElement('span');
        nameSpan.textContent = p.name;
        btn.appendChild(nameSpan);

        if (!p.isBuiltIn) {
            const deleteX = document.createElement('span');
            deleteX.className = 'delete-x';
            deleteX.textContent = 'x';
            deleteX.onclick = function(e) {
                e.stopPropagation();
                deletePreset(p.name);
            };
            btn.appendChild(deleteX);
        }

        btn.onclick = function() { applyPreset(p.name); };
        row.appendChild(btn);
    }
}

function renderAll(data) {
    currentTheme = data;
    renderColors(data.colors, data.pageColors);
    renderFonts(data.fonts);
    renderPresets(data.presets, data.activeThemeName);
}

// -- API calls --

async function fetchTheme() {
    const res = await fetch('/api/theme');
    const data = await res.json();
    renderAll(data);
}

function postJson(url, body) {
    lastChangeTime = Date.now();
    return fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
    });
}

function flashColor(name) {
    fetch('/api/theme/flash', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: name })
    });
}

function onColorInput(el) {
    // Live preview: update swatch and hex immediately
    const name = el.dataset.name;
    const val = el.value;
    el.parentElement.style.background = val;
    const hexInput = el.closest('.color-row').querySelector('.color-hex');
    if (hexInput) hexInput.value = val;

    // Debounced POST
    clearTimeout(debounceTimers[name]);
    debounceTimers[name] = setTimeout(function() {
        postJson('/api/theme/color', { name: name, value: val });
    }, 50);
}

function onColorChange(el) {
    // Final value on picker close
    const name = el.dataset.name;
    clearTimeout(debounceTimers[name]);
    postJson('/api/theme/color', { name: name, value: el.value });
}

function onHexInput(el) {
    const hex = el.value.trim();
    if (/^#[0-9a-fA-F]{6}$/.test(hex)) {
        const name = el.dataset.name;
        const swatch = el.closest('.color-row').querySelector('.color-swatch');
        swatch.style.background = hex;
        swatch.querySelector('input[type="color"]').value = hex;
        clearTimeout(debounceTimers[name]);
        debounceTimers[name] = setTimeout(function() {
            postJson('/api/theme/color', { name: name, value: hex });
        }, 50);
    }
}

function onFontSlider(el) {
    const name = el.dataset.name;
    const val = parseInt(el.value);
    document.getElementById('fv_' + name).textContent = val + 'px';

    clearTimeout(debounceTimers['font_' + name]);
    debounceTimers['font_' + name] = setTimeout(function() {
        postJson('/api/theme/font', { name: name, value: val });
    }, 100);
}

async function applyPreset(name) {
    const res = await postJson('/api/theme/preset', { name: name });
    const data = await res.json();
    renderAll(data);
}

async function randomTheme() {
    const hue = Math.random() * 360;
    const sat = 65 + Math.random() * 20;
    const light = 50 + Math.random() * 10;
    const res = await postJson('/api/theme/palette', { hue: hue, saturation: sat, lightness: light });
    const data = await res.json();
    renderAll(data);
}

async function resetTheme() {
    if (!confirm('Reset theme to defaults?')) return;
    const res = await postJson('/api/theme/reset', {});
    const data = await res.json();
    renderAll(data);
}

async function deletePreset(name) {
    if (!confirm('Delete theme "' + name + '"?')) return;
    lastChangeTime = Date.now();
    const res = await fetch('/api/theme/preset/' + encodeURIComponent(name), { method: 'DELETE' });
    const data = await res.json();
    renderAll(data);
}

function openSaveDialog() {
    document.getElementById('saveDialog').classList.add('open');
    var input = document.getElementById('saveNameInput');
    input.value = '';
    input.focus();
}

function closeSaveDialog() {
    document.getElementById('saveDialog').classList.remove('open');
}

async function saveTheme() {
    const name = document.getElementById('saveNameInput').value.trim();
    if (!name) return;
    closeSaveDialog();
    const res = await postJson('/api/theme/save', { name: name });
    const data = await res.json();
    renderAll(data);
}
)JS";

// Theme library & community JavaScript: save/apply/share themes, browse community
inline constexpr const char* WEB_JS_THEME_LIBRARY = R"JS(
// -- Library & Community --

let libCurrentTab = 'local';
let libLocalEntries = [];
let libSelectedId = '';
let commEntries = [];
let commSelectedId = '';
let commPage = 1;
let commTotal = 0;

function showToast(msg) {
    var t = document.getElementById('libToast');
    t.textContent = msg;
    t.classList.add('show');
    setTimeout(function() { t.classList.remove('show'); }, 2000);
}

function switchLibTab(tab) {
    libCurrentTab = tab;
    document.getElementById('libTabLocal').classList.toggle('active', tab === 'local');
    document.getElementById('libTabComm').classList.toggle('active', tab === 'community');
    document.getElementById('libLocalContent').style.display = tab === 'local' ? '' : 'none';
    document.getElementById('libCommContent').style.display = tab === 'community' ? '' : 'none';
    if (tab === 'local') loadLocalLib();
    else if (commEntries.length === 0) browseCommThemes();
}

function loadLocalLib() {
    fetch('/api/theme/library/list')
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.entries) {
                libLocalEntries = data.entries;
                renderLocalLib();
            }
        });
}

function renderLocalLib() {
    var grid = document.getElementById('libLocalGrid');
    if (libLocalEntries.length === 0) {
        grid.innerHTML = '<div class="lib-empty">No saved themes yet. Click "Save Current" to add one.</div>';
        updateLibButtons();
        return;
    }
    grid.innerHTML = '';
    for (var i = 0; i < libLocalEntries.length; i++) {
        var e = libLocalEntries[i];
        var card = document.createElement('div');
        card.className = 'lib-card' + (e.id === libSelectedId ? ' selected' : '');
        card.dataset.id = e.id;
        card.onclick = (function(id) { return function() { selectLibEntry(id); }; })(e.id);
        card.ondblclick = (function(id) { return function() { applyLibEntry(id); }; })(e.id);

        var img = document.createElement('img');
        img.src = e.hasThumbnail ? '/api/theme/library/' + e.id + '/thumbnail' : '';
        img.alt = 'Theme preview';
        if (!e.hasThumbnail) {
            img.style.background = 'var(--surface)';
            img.style.minHeight = '60px';
        }
        card.appendChild(img);

        var nameEl = document.createElement('div');
        nameEl.className = 'lib-card-name';
        nameEl.textContent = e.name || 'Theme';
        card.appendChild(nameEl);

        grid.appendChild(card);
    }
    updateLibButtons();
}

function selectLibEntry(id) {
    libSelectedId = libSelectedId === id ? '' : id;
    renderLocalLib();
}

function updateLibButtons() {
    var hasSelection = libSelectedId !== '';
    document.getElementById('shareBtn').disabled = !hasSelection;
    document.getElementById('deleteLibBtn').disabled = !hasSelection;
}

function saveToLibrary() {
    var name = currentTheme ? currentTheme.activeThemeName : 'Custom';
    postJson('/api/theme/library/save', { name: name })
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.success) {
                showToast('Saved to library!');
                loadLocalLib();
            } else {
                showToast('Error: ' + (data.error || 'Unknown'));
            }
        });
}

function applyLibEntry(id) {
    postJson('/api/theme/library/apply', { entryId: id })
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.colors) {
                renderAll(data);
                showToast('Theme applied!');
            }
        });
}

function deleteFromLib() {
    if (!libSelectedId) return;
    if (!confirm('Delete this theme from your library?')) return;
    fetch('/api/theme/library/' + libSelectedId, { method: 'DELETE' })
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.success) {
                libSelectedId = '';
                showToast('Deleted from library');
                loadLocalLib();
            }
        });
}

function shareToComm() {
    if (!libSelectedId) return;
    // Find current name from local entries
    var currentName = '';
    for (var i = 0; i < libLocalEntries.length; i++) {
        if (libLocalEntries[i].id === libSelectedId) { currentName = libLocalEntries[i].name || ''; break; }
    }
    var name = prompt('Theme name for community:', currentName || (currentTheme ? currentTheme.activeThemeName : 'My Theme'));
    if (name === null) return;
    name = name.trim();
    if (!name) return;
    var entryId = libSelectedId;
    // Rename entry first, then upload (sequential to avoid race)
    postJson('/api/theme/library/rename', { entryId: entryId, name: name })
        .then(function(r) { return r.json(); })
        .then(function() {
            loadLocalLib();
            return postJson('/api/community/upload', { entryId: entryId });
        })
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.success) {
                showToast('Shared to community!');
                loadLocalLib();
            } else if (data.error === 'Already shared') {
                var serverId = data.existingId || entryId;
                if (confirm('This theme is already shared. Delete the old version and re-upload?')) {
                    postJson('/api/community/delete', { serverId: serverId })
                        .then(function(r2) { return r2.json(); })
                        .then(function(d2) {
                            if (d2.success) {
                                return postJson('/api/community/upload', { entryId: serverId });
                            } else {
                                showToast('Error: ' + (d2.error || 'Delete failed'));
                            }
                        })
                        .then(function(r3) { if (r3) return r3.json(); })
                        .then(function(d3) {
                            if (d3 && d3.success) {
                                showToast('Re-shared to community!');
                                loadLocalLib();
                            } else if (d3) {
                                showToast('Error: ' + (d3.error || 'Re-upload failed'));
                            }
                        });
                }
            } else {
                showToast('Error: ' + (data.error || 'Upload failed'));
            }
        });
}

function browseCommThemes() {
    commPage = 1;
    commEntries = [];
    fetchCommPage(1);
}

function loadMoreComm() {
    commPage++;
    fetchCommPage(commPage);
}

function fetchCommPage(page) {
    var scheme = document.getElementById('commSchemeFilter').value;
    var sort = document.getElementById('commSortFilter').value;
    var url = '/api/community/browse?type=theme&sort=' + encodeURIComponent(sort) + '&page=' + page;
    if (scheme) url += '&search=' + encodeURIComponent('scheme:' + scheme);

    fetch(url)
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.entries) {
                if (page === 1) commEntries = data.entries;
                else commEntries = commEntries.concat(data.entries);
                commTotal = data.total || 0;
                renderCommGrid();
            }
        })
        .catch(function() {
            document.getElementById('libCommGrid').innerHTML =
                '<div class="lib-empty">Failed to load community themes.</div>';
        });
}

function renderCommGrid() {
    var grid = document.getElementById('libCommGrid');
    if (commEntries.length === 0) {
        grid.innerHTML = '<div class="lib-empty">No community themes found.</div>';
        document.getElementById('commLoadMoreBtn').style.display = 'none';
        updateCommButtons();
        return;
    }
    grid.innerHTML = '';
    for (var i = 0; i < commEntries.length; i++) {
        var e = commEntries[i];
        var card = document.createElement('div');
        card.className = 'lib-card' + (e.id === commSelectedId ? ' selected' : '');
        card.onclick = (function(id) { return function() { selectCommEntry(id); }; })(e.id);
        card.ondblclick = (function(id) { return function() { downloadCommTheme(id); }; })(e.id);

        var thumbUrl = e.thumbnailFullUrl || e.thumbnailCompactUrl || '';
        var img = document.createElement('img');
        img.alt = 'Theme preview';
        if (thumbUrl) {
            img.src = thumbUrl;
        } else {
            img.style.background = 'var(--surface)';
            img.style.minHeight = '60px';
        }
        card.appendChild(img);

        var nameEl = document.createElement('div');
        nameEl.className = 'lib-card-name';
        var themeName = (e.data && e.data.theme && e.data.theme.name) ? e.data.theme.name : '';
        if (!themeName) {
            var tags = e.tags || [];
            for (var t = 0; t < tags.length; t++) {
                if (tags[t].indexOf('name:') === 0) { themeName = tags[t].substring(5); break; }
            }
        }
        if (!themeName) {
            var tags2 = e.tags || [];
            for (var t = 0; t < tags2.length; t++) {
                if (tags2[t].indexOf('scheme:') === 0) { themeName = tags2[t].substring(7); break; }
            }
        }
        nameEl.textContent = themeName || 'Theme';
        card.appendChild(nameEl);

        grid.appendChild(card);
    }
    var totalPages = Math.ceil(commTotal / 20);
    document.getElementById('commLoadMoreBtn').style.display = commPage < totalPages ? '' : 'none';
    updateCommButtons();
}

function selectCommEntry(id) {
    commSelectedId = commSelectedId === id ? '' : id;
    renderCommGrid();
}

function updateCommButtons() {
    var has = commSelectedId !== '';
    document.getElementById('commDownloadBtn').disabled = !has;
    // Only enable delete for entries uploaded by this device
    var canDelete = false;
    if (has && typeof LOCAL_DEVICE_ID !== 'undefined' && LOCAL_DEVICE_ID) {
        var entry = commEntries.find(function(e) { return e.id === commSelectedId; });
        canDelete = entry && entry.deviceId === LOCAL_DEVICE_ID;
    }
    document.getElementById('commDeleteBtn').disabled = !canDelete;
}

function downloadSelected() {
    if (!commSelectedId) return;
    downloadCommTheme(commSelectedId);
}

function downloadCommTheme(serverId) {
    postJson('/api/community/download', { serverId: serverId })
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.localEntryId) {
                showToast('Downloaded! Double-click in My Library to apply.');
                loadLocalLib();
                switchLibTab('local');
            } else {
                showToast('Error: ' + (data.error || 'Download failed'));
            }
        });
}

function deleteFromServer() {
    if (!commSelectedId) return;
    if (!confirm('Delete this theme from the community server?')) return;
    postJson('/api/community/delete', { serverId: commSelectedId })
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data.success) {
                showToast('Deleted from server');
                commEntries = commEntries.filter(function(e) { return e.id !== commSelectedId; });
                commSelectedId = '';
                renderCommGrid();
            } else {
                showToast('Error: ' + (data.error || 'Delete failed'));
            }
        });
}

// -- SSE for real-time sync --

function connectSSE() {
    const evtSource = new EventSource('/api/theme/subscribe');

    evtSource.addEventListener('theme-changed', function() {
        // Ignore if this was our own change (within 200ms)
        if (Date.now() - lastChangeTime < 200) return;
        fetchTheme();
    });

    evtSource.onerror = function() {
        evtSource.close();
        setTimeout(connectSSE, 3000);
    };
}

// -- Init --
fetchTheme();
connectSSE();
loadLocalLib();
)JS";
