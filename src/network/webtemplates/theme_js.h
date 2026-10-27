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
        { name: "primaryContrastColor", display: "Primary Contrast" },
        { name: "secondaryColor", display: "Secondary" },
        { name: "textColor", display: "Text" },
        { name: "textSecondaryColor", display: "Text Secondary" },
        { name: "accentColor", display: "Accent" },
        { name: "borderColor", display: "Border" },
        { name: "iconColor", display: "Icon" },
        { name: "bottomBarColor", display: "Bottom Bar" },
        { name: "actionButtonContentColor", display: "Action Button Content" }
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
        { name: "portalEcColor", display: "PORTAL EC", portal: true },
        { name: "portalTemperatureColor", display: "PORTAL Temperature", portal: true },
        { name: "temperatureGoalColor", display: "Temp Goal" },
        { name: "weightColor", display: "Weight" },
        { name: "weightFlowColor", display: "Weight Flow" },
        { name: "waterLevelColor", display: "Water Level" }
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

// Display labels only. The roles, their bounds and their defaults come from the
// server (data.fontRanges -> SettingsTheme::fontRoles()); this used to carry min/max
// too, which made it a second source of truth AND the only place bounds were checked.
const FONT_LABELS = {
    headingSize:  "Heading",
    titleSize:    "Title",
    subtitleSize: "Subtitle",
    bodySize:     "Body",
    labelSize:    "Label",
    captionSize:  "Caption",
    valueSize:    "Value",
    timerSize:    "Timer"
};

let currentTheme = null;
let lastChangeTime = 0;  // To ignore self-echo SSE events
let debounceTimers = {};

// -- Rendering --

// -- Color conversion utilities --

function hexToRgb(hex) {
    var r = parseInt(hex.slice(1,3), 16), g = parseInt(hex.slice(3,5), 16), b = parseInt(hex.slice(5,7), 16);
    return { r: r, g: g, b: b };
}

function rgbToHex(r, g, b) {
    return '#' + [r,g,b].map(function(v) { return Math.round(Math.max(0, Math.min(255, v))).toString(16).padStart(2,'0'); }).join('');
}

function rgbToHsl(r, g, b) {
    r /= 255; g /= 255; b /= 255;
    var max = Math.max(r,g,b), min = Math.min(r,g,b), d = max - min;
    var h = 0, s = 0, l = (max + min) / 2;
    if (d > 0) {
        s = l > 0.5 ? d / (2 - max - min) : d / (max - min);
        if (max === r) h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
        else if (max === g) h = ((b - r) / d + 2) / 6;
        else h = ((r - g) / d + 4) / 6;
    }
    return { h: Math.round(h * 360), s: Math.round(s * 100), l: Math.round(l * 100) };
}

function hslToRgb(h, s, l) {
    h /= 360; s /= 100; l /= 100;
    if (s === 0) { var v = Math.round(l * 255); return { r: v, g: v, b: v }; }
    function hue2rgb(p, q, t) {
        if (t < 0) t += 1; if (t > 1) t -= 1;
        if (t < 1/6) return p + (q - p) * 6 * t;
        if (t < 1/2) return q;
        if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
        return p;
    }
    var q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    return {
        r: Math.round(hue2rgb(p, q, h + 1/3) * 255),
        g: Math.round(hue2rgb(p, q, h) * 255),
        b: Math.round(hue2rgb(p, q, h - 1/3) * 255)
    };
}

function sliderGradient(mode, channel, channels) {
    var stops = [];
    for (var i = 0; i <= 4; i++) {
        var t = i / 4;
        var ch = channels.slice();
        if (mode === 'rgb') {
            ch[channel] = Math.round(t * 255);
            stops.push(rgbToHex(ch[0], ch[1], ch[2]));
        } else {
            var maxes = [360, 100, 100];
            ch[channel] = Math.round(t * maxes[channel]);
            var rgb = hslToRgb(ch[0], ch[2], ch[1]);  // channels are [H,L,S], hslToRgb wants (h,s,l)
            stops.push(rgbToHex(rgb.r, rgb.g, rgb.b));
        }
    }
    return 'linear-gradient(to right, ' + stops.join(', ') + ')';
}

// -- Active editor state --
var activeEditorName = null;
var activeEditorMode = 'hls';

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
            if (c.portal && !(currentTheme && currentTheme.portalAvailable)) continue;
            const val = colors[c.name] || '#000000';
            const isOnPage = onPage.has(c.name);
            const row = document.createElement('div');
            row.className = 'color-row' + (isOnPage ? ' on-page' : '');
            row.dataset.name = c.name;
            row.innerHTML =
                '<div class="color-row-header">' +
                    '<div class="page-dot" title="Used on current page"></div>' +
                    '<div class="color-swatch" style="background:' + val + '"></div>' +
                    '<span class="color-label" onclick="flashColor(\'' + c.name + '\')" title="Click to flash on device">' + c.display + '</span>' +
                    '<input type="text" class="color-hex" value="' + val + '" data-name="' + c.name + '" oninput="onHexInput(this)">' +
                    '<button class="copy-btn" title="Copy" onclick="copyColor(\'' + c.name + '\')">Copy</button>' +
                    '<button class="copy-btn" title="Paste" onclick="pasteColor(\'' + c.name + '\')">Paste</button>' +
                '</div>';
            panel.appendChild(row);

            // Click swatch to toggle editor
            row.querySelector('.color-swatch').onclick = function() { toggleEditor(c.name, val); };

            // If this is the active editor, re-open it
            if (activeEditorName === c.name) {
                openEditor(row, c.name, val);
            }
        }
    }
}
)JS";

inline constexpr const char* WEB_JS_THEME_EDITOR2 = R"JS(
function copyColor(name) {
    var row = document.querySelector('.color-row[data-name="' + name + '"]');
    var hex = row.querySelector('.color-hex').value;
    copiedColor = hex;
    fallbackCopy(hex);
    showCopyFeedback(row);
}

function fallbackCopy(text) {
    var ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    document.execCommand('copy');
    document.body.removeChild(ta);
}

function showCopyFeedback(row) {
    var btn = row.querySelector('.copy-btn');
    if (btn) { var o = btn.textContent; btn.textContent = 'OK'; setTimeout(function() { btn.textContent = o; }, 1000); }
}

var copiedColor = null;

function pasteColor(name) {
    if (!copiedColor) return;
    var row = document.querySelector('.color-row[data-name="' + name + '"]');
    row.querySelector('.color-hex').value = copiedColor;
    row.querySelector('.color-swatch').style.background = copiedColor;
    postOrResync('/api/theme/color', { name: name, value: copiedColor });
    if (activeEditorName === name) {
        var editor = row.querySelector('.color-editor');
        if (editor) updateEditorSliders(editor, copiedColor);
    }
}

function toggleEditor(name, hex) {
    if (activeEditorName === name) {
        closeEditor();
        return;
    }
    closeEditor();
    activeEditorName = name;
    var row = document.querySelector('.color-row[data-name="' + name + '"]');
    if (row) openEditor(row, name, hex);
}

function closeEditor() {
    var existing = document.querySelector('.color-editor');
    if (existing) existing.remove();
    activeEditorName = null;
}

function openEditor(row, name, hex) {
    var editor = document.createElement('div');
    editor.className = 'color-editor';

    var rgb = hexToRgb(hex);
    var hsl = rgbToHsl(rgb.r, rgb.g, rgb.b);

    var mode = activeEditorMode;
    var channels, labels, maxes;
    if (mode === 'rgb') {
        channels = [rgb.r, rgb.g, rgb.b];
        labels = ['R', 'G', 'B'];
        maxes = [255, 255, 255];
    } else {
        channels = [hsl.h, hsl.l, hsl.s];
        labels = ['H', 'L', 'S'];
        maxes = [360, 100, 100];
    }

    var html = '<div class="editor-toggle">';
    html += '<button class="mode-btn' + (mode === 'rgb' ? ' active' : '') + '" onclick="switchEditorMode(\'rgb\')">RGB</button>';
    html += '<button class="mode-btn' + (mode === 'hls' ? ' active' : '') + '" onclick="switchEditorMode(\'hls\')">HLS</button>';
    html += '</div>';

    for (var i = 0; i < 3; i++) {
        var pct = channels[i] / maxes[i] * 100;
        var grad = sliderGradient(mode, i, channels);
        html += '<div class="editor-slider-row">' +
            '<span class="slider-label">' + labels[i] + '</span>' +
            '<div class="slider-track" data-channel="' + i + '" data-name="' + name + '" style="background:' + grad + '">' +
                '<div class="slider-thumb" style="left:' + pct + '%"></div>' +
            '</div>' +
            '<span class="slider-value">' + channels[i] + '</span>' +
        '</div>';
    }

    editor.innerHTML = html;
    row.appendChild(editor);

    // Attach drag handlers
    editor.querySelectorAll('.slider-track').forEach(function(track) {
        function onMove(e) {
            var rect = track.getBoundingClientRect();
            var clientX = e.touches ? e.touches[0].clientX : e.clientX;
            var ratio = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
            var ch = parseInt(track.dataset.channel);
            var max = maxes[ch];
            var val = Math.round(ratio * max);
            channels[ch] = val;

            track.querySelector('.slider-thumb').style.left = (ratio * 100) + '%';
            track.closest('.editor-slider-row').querySelector('.slider-value').textContent = val;

            // Recompute hex
            var newHex;
            if (activeEditorMode === 'rgb') {
                newHex = rgbToHex(channels[0], channels[1], channels[2]);
            } else {
                var c = hslToRgb(channels[0], channels[2], channels[1]);  // [H,L,S] → (h,s,l)
                newHex = rgbToHex(c.r, c.g, c.b);
            }

            // Update all gradients
            for (var j = 0; j < 3; j++) {
                var t = editor.querySelectorAll('.slider-track')[j];
                t.style.background = sliderGradient(activeEditorMode, j, channels);
            }

            // Update row
            var rowEl = track.closest('.color-row');
            rowEl.querySelector('.color-swatch').style.background = newHex;
            rowEl.querySelector('.color-hex').value = newHex;

            clearTimeout(debounceTimers[name]);
            debounceTimers[name] = setTimeout(function() {
                postOrResync('/api/theme/color', { name: name, value: newHex });
            }, 50);
        }

        function onUp() {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
            document.removeEventListener('touchmove', onMove);
            document.removeEventListener('touchend', onUp);
        }

        track.addEventListener('mousedown', function(e) {
            e.preventDefault();
            onMove(e);
            document.addEventListener('mousemove', onMove);
            document.addEventListener('mouseup', onUp);
        });
        track.addEventListener('touchstart', function(e) {
            e.preventDefault();
            onMove(e);
            document.addEventListener('touchmove', onMove);
            document.addEventListener('touchend', onUp);
        });
    });
}

function updateEditorSliders(editor, hex) {
    var rgb = hexToRgb(hex);
    var hsl = rgbToHsl(rgb.r, rgb.g, rgb.b);
    var channels = activeEditorMode === 'rgb' ? [rgb.r, rgb.g, rgb.b] : [hsl.h, hsl.l, hsl.s];
    var maxes = activeEditorMode === 'rgb' ? [255, 255, 255] : [360, 100, 100];
    var tracks = editor.querySelectorAll('.slider-track');
    for (var i = 0; i < 3; i++) {
        tracks[i].querySelector('.slider-thumb').style.left = (channels[i] / maxes[i] * 100) + '%';
        tracks[i].closest('.editor-slider-row').querySelector('.slider-value').textContent = channels[i];
        tracks[i].style.background = sliderGradient(activeEditorMode, i, channels);
    }
}

function switchEditorMode(mode) {
    activeEditorMode = mode;
    if (activeEditorName) {
        var row = document.querySelector('.color-row[data-name="' + activeEditorName + '"]');
        if (row) {
            var hex = row.querySelector('.color-hex').value || '#000000';
            row.querySelector('.color-editor').remove();
            openEditor(row, activeEditorName, hex);
        }
    }
}

function renderFonts(fonts, ranges) {
    const panel = document.getElementById('fontPanel');
    panel.innerHTML = '';
    if (!ranges) return;   // server did not send fontRanges — render nothing rather than guess
    for (const name of Object.keys(ranges)) {
        const r = ranges[name];
        const val = fonts[name] || r.def;
        const row = document.createElement('div');
        row.className = 'font-row';
        row.innerHTML =
            '<span class="font-label">' + (FONT_LABELS[name] || name) + '</span>' +
            '<input type="range" class="font-slider" min="' + r.min + '" max="' + r.max + '" value="' + val + '" ' +
                   'data-name="' + name + '" oninput="onFontSlider(this)">' +
            '<span class="font-value" id="fv_' + name + '">' + val + 'px</span>';
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
    renderColors(data.editingColors || data.colors, data.pageColors);
    renderFonts(data.fonts, data.fontRanges);
    renderPresets(data.presets, data.activeThemeName);
    if (typeof initShaderState === 'function') initShaderState(data);
}

// -- API calls --

async function fetchTheme() {
    const data = await requestJson('/api/theme');
    if (data) renderAll(data);
}

function postJson(url, body) {
    lastChangeTime = Date.now();
    return requestJson(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
    });
}

// For a write whose value the page already shows: if it fails, show what the
// device actually holds instead.
function postOrResync(url, body) {
    return postJson(url, body).then(function(data) {
        if (!data) fetchTheme();
        return data;
    });
}

function flashColor(name) {
    requestJson('/api/theme/flash', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: name })
    });
}

function onHexInput(el) {
    const hex = el.value.trim();
    if (/^#[0-9a-fA-F]{6}$/.test(hex)) {
        const name = el.dataset.name;
        const row = el.closest('.color-row');
        row.querySelector('.color-swatch').style.background = hex;
        if (activeEditorName === name) {
            var editor = row.querySelector('.color-editor');
            if (editor) updateEditorSliders(editor, hex);
        }
        clearTimeout(debounceTimers[name]);
        debounceTimers[name] = setTimeout(function() {
            postOrResync('/api/theme/color', { name: name, value: hex });
        }, 50);
    }
}

function onFontSlider(el) {
    const name = el.dataset.name;
    const val = parseInt(el.value);
    document.getElementById('fv_' + name).textContent = val + 'px';

    clearTimeout(debounceTimers['font_' + name]);
    debounceTimers['font_' + name] = setTimeout(async function() {
        // Read the response. The server clamps out-of-range values, so without this the
        // slider could sit at 400px indefinitely for a font actually rendering at 120.
        const data = await postJson('/api/theme/font', { name: name, value: val });
        if (!data || !data.clamped) return;
        const slider = document.querySelector('.font-slider[data-name="' + name + '"]');
        if (slider) slider.value = data.applied;
        document.getElementById('fv_' + name).textContent = data.applied + 'px';
    }, 100);
}

async function applyPreset(name) {
    const data = await postJson('/api/theme/preset', { name: name });
    if (data) renderAll(data);
}

async function randomTheme() {
    const hue = Math.random() * 360;
    const sat = 65 + Math.random() * 20;
    const light = 50 + Math.random() * 10;
    const data = await postJson('/api/theme/palette', { hue: hue, saturation: sat, lightness: light });
    if (data) renderAll(data);
}

// Fonts only -- deliberately does not touch colours, unlike resetTheme() below.
async function resetFontSizes() {
    if (!confirm('Reset all font sizes to their defaults? Your theme colours are not affected.')) return;
    const data = await postJson('/api/theme/font/reset', {});
    if (data) renderAll(data);
}

async function resetTheme() {
    // Names both categories: this clears font sizes too, and someone resetting their
    // colours should not silently lose sizing they tuned (or vice versa).
    if (!confirm('Reset theme colours AND font sizes to defaults? This clears both.')) return;
    const data = await postJson('/api/theme/reset', {});
    if (data) renderAll(data);
}

async function deletePreset(name) {
    if (!confirm('Delete theme "' + name + '"?')) return;
    lastChangeTime = Date.now();
    const data = await requestJson('/api/theme/preset/' + encodeURIComponent(name), { method: 'DELETE' });
    if (data) renderAll(data);
}

function openSaveDialog() {
    document.getElementById('saveDialog').classList.add('open');
    var input = document.getElementById('saveNameInput');
    input.value = (currentTheme && currentTheme.activeThemeName) ? currentTheme.activeThemeName : '';
    input.focus();
    input.select();
}

function closeSaveDialog() {
    document.getElementById('saveDialog').classList.remove('open');
}

async function saveTheme() {
    const name = document.getElementById('saveNameInput').value.trim();
    if (!name) return;
    closeSaveDialog();
    const data = await postJson('/api/theme/save', { name: name });
    if (data) renderAll(data);
}
)JS";

// Theme library JavaScript: save/apply/delete themes
inline constexpr const char* WEB_JS_THEME_LIBRARY = R"JS(

let libLocalEntries = [];
let libSelectedId = '';

function loadLocalLib() {
    requestJson('/api/theme/library/list')
        .then(function(data) {
            if (data && data.error) showToast('Error: ' + data.error);
            if (data && data.entries) {
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
    document.getElementById('deleteLibBtn').disabled = !hasSelection;
}

function saveToLibrary() {
    var name = currentTheme ? currentTheme.activeThemeName : 'Custom';
    postJson('/api/theme/library/save', { name: name })
        .then(function(data) {
            if (!data) return;
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
        .then(function(data) {
            if (!data) return;
            if (data.colors) {
                renderAll(data);
                showToast('Theme applied!');
            } else {
                showToast('Error: ' + (data.error || 'Apply failed'));
            }
        });
}

function deleteFromLib() {
    if (!libSelectedId) return;
    if (!confirm('Delete this theme from your library?')) return;
    requestJson('/api/theme/library/' + libSelectedId, { method: 'DELETE' })
        .then(function(data) {
            if (!data) return;
            if (data.success) {
                libSelectedId = '';
                showToast('Deleted from library');
                loadLocalLib();
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
