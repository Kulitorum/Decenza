#pragma once

// Theme editor HTML body: color panel, font panel, presets, library, community
// Used by theme_page.h (header is built by the page assembler)

inline constexpr const char* WEB_HTML_THEME_BODY = R"HTML(
<div class="mode-bar">
    <div class="mode-group">
        <span class="mode-label">Theme Mode:</span>
        <div class="mode-btns" id="modeSelector"></div>
    </div>
    <div class="mode-group">
        <span class="mode-label">Editing:</span>
        <div class="mode-btns" id="paletteToggle"></div>
    </div>
</div>

<div class="main">
    <!-- Left: Colors -->
    <div class="color-panel" id="colorPanel"></div>

    <!-- Right: Fonts + Presets -->
    <div class="right-panel">
        <div class="section-title">Font Sizes</div>
        <div id="fontPanel"></div>

        <div class="presets-section">
            <div class="section-title">Presets</div>
            <div class="preset-row" id="presetRow"></div>

            <div class="actions">
                <button class="btn btn-rainbow" onclick="randomTheme()">Random Theme</button>
                <button class="btn btn-primary" onclick="openSaveDialog()">Save Theme</button>
            </div>
        </div>

        <!-- Shaders -->
        <div class="shader-panel">
            <div class="shader-panel-title">Screen Effects</div>
            <div class="shader-list" id="shaderList"></div>
            <div id="shaderParams"></div>
        </div>

        <!-- Library & Community -->
        <div class="library-section">
            <div class="section-title">Library & Community</div>
            <div class="lib-tabs">
                <button class="lib-tab active" id="libTabLocal" onclick="switchLibTab('local')">My Library</button>
                <button class="lib-tab" id="libTabComm" onclick="switchLibTab('community')">Community</button>
            </div>

            <!-- Local Library -->
            <div id="libLocalContent">
                <div class="lib-actions">
                    <button class="btn-small" onclick="saveToLibrary()">Save Current</button>
                    <button class="btn-small btn-share" id="shareBtn" onclick="shareToComm()" disabled>Share Selected</button>
                    <button class="btn-small btn-danger" id="deleteLibBtn" onclick="deleteFromLib()" disabled style="border-color:#ff4444;color:#ff4444;">Delete</button>
                </div>
                <div class="lib-grid" id="libLocalGrid">
                    <div class="lib-empty">No saved themes yet. Click "Save Current" to add one.</div>
                </div>
            </div>

            <!-- Community -->
            <div id="libCommContent" style="display:none">
                <div class="comm-filters">
                    <select class="comm-filter" id="commSchemeFilter" onchange="browseCommThemes()">
                        <option value="">All Schemes</option>
                        <option value="dark">Dark</option>
                        <option value="light">Light</option>
                    </select>
                    <select class="comm-filter" id="commSortFilter" onchange="browseCommThemes()">
                        <option value="newest">Newest</option>
                        <option value="popular">Most Popular</option>
                    </select>
                </div>
                <div class="lib-grid" id="libCommGrid">
                    <div class="lib-empty">Browse community themes.</div>
                </div>
                <div class="lib-actions">
                    <button class="btn-small" id="commDownloadBtn" onclick="downloadSelected()" disabled>Download</button>
                    <button class="btn-small btn-danger" id="commDeleteBtn" onclick="deleteFromServer()" disabled style="border-color:#ff4444;color:#ff4444;">Delete from Server</button>
                    <button class="btn-small" id="commLoadMoreBtn" onclick="loadMoreComm()" style="display:none">Load More</button>
                </div>
            </div>
        </div>
    </div>
</div>

<div class="footer">
    <button class="btn btn-danger" onclick="resetTheme()">Reset to Default</button>
</div>

<!-- Save Theme Dialog -->
<div class="save-dialog" id="saveDialog">
    <div class="save-dialog-inner">
        <h3>Save Theme</h3>
        <input type="text" id="saveNameInput" placeholder="Theme name..." onkeydown="if(event.key==='Enter') saveTheme()">
        <div class="save-dialog-btns">
            <button class="btn" onclick="closeSaveDialog()">Cancel</button>
            <button class="btn btn-primary" onclick="saveTheme()">Save</button>
        </div>
    </div>
</div>

<!-- Shader overlay (fixed, covers viewport, pointer-events:none) -->
<div class="shader-overlay" id="shaderOverlay">
    <div class="shader-scanlines" id="shaderScanlines" style="display:none"></div>
    <canvas class="shader-noise" id="shaderNoise" style="display:none"></canvas>
    <div class="shader-vignette" id="shaderVignette" style="display:none"></div>
</div>

<div class="lib-toast" id="libToast"></div>
)HTML";
