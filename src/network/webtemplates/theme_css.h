#pragma once

// Theme editor CSS: color panel, font sliders, presets, library grid, community
// Used by theme_page.h

inline constexpr const char* WEB_CSS_THEME_EDITOR = R"CSS(
.theme-name {
    font-size: 14px;
    color: var(--text-secondary);
}

/* Mode selector bar */
.mode-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    max-width: 1200px;
    margin: 0 auto;
    padding: 12px 20px;
    border-bottom: 1px solid var(--border);
    flex-wrap: wrap;
    gap: 12px;
}
.mode-group {
    display: flex;
    align-items: center;
    gap: 8px;
}
.mode-label {
    font-size: 13px;
    color: var(--text-secondary);
    font-weight: 600;
}
.mode-btns {
    display: flex;
    gap: 0;
}
.mode-btn {
    background: var(--surface);
    color: var(--text);
    border: 1px solid var(--border);
    padding: 6px 16px;
    font-size: 13px;
    cursor: pointer;
    transition: background 0.15s, color 0.15s;
}
.mode-btn:first-child { border-radius: 6px 0 0 6px; }
.mode-btn:last-child { border-radius: 0 6px 6px 0; }
.mode-btn:not(:first-child) { border-left: none; }
.mode-btn.active {
    background: var(--primary);
    color: white;
    border-color: var(--primary);
}
.mode-btn:hover:not(.active) {
    background: rgba(255, 255, 255, 0.08);
}

.main {
    display: flex;
    gap: 0;
    max-width: 1200px;
    margin: 0 auto;
    min-height: calc(100vh - 120px);
}

/* Left panel - colors */
.color-panel {
    flex: 1;
    padding: 20px;
    overflow-y: auto;
    border-right: 1px solid var(--border);
    min-width: 300px;
}
.category-title {
    font-size: 13px;
    font-weight: 600;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-top: 20px;
    margin-bottom: 8px;
}
.category-title:first-child { margin-top: 0; }

.color-row {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 6px 0;
}
.color-row.on-page {
    background: rgba(255, 255, 255, 0.05);
    border-radius: 4px;
    padding: 6px 6px;
    margin: 0 -6px;
}
.page-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--accent);
    flex-shrink: 0;
    opacity: 0;
    transition: opacity 0.2s;
}
.on-page .page-dot {
    opacity: 1;
}
.color-swatch {
    width: 32px;
    height: 32px;
    border-radius: 6px;
    border: 2px solid var(--border);
    cursor: pointer;
    position: relative;
    overflow: hidden;
    flex-shrink: 0;
}
.color-swatch input[type="color"] {
    position: absolute;
    top: -4px;
    left: -4px;
    width: 40px;
    height: 40px;
    border: none;
    cursor: pointer;
    opacity: 0;
}
.color-label {
    flex: 1;
    font-size: 14px;
    min-width: 100px;
    cursor: pointer;
    user-select: none;
}
.color-label:hover {
    color: var(--text);
    text-decoration: underline;
}
.color-hex {
    font-family: 'SF Mono', 'Fira Code', monospace;
    font-size: 13px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 4px 8px;
    color: var(--text);
    width: 90px;
    text-align: center;
}
.color-hex:focus {
    outline: none;
    border-color: var(--accent);
}

/* Right panel - fonts + presets */
.right-panel {
    flex: 1;
    padding: 20px;
    overflow-y: auto;
    min-width: 300px;
}

.section-title {
    font-size: 15px;
    font-weight: 600;
    margin-bottom: 12px;
    color: var(--text);
}

.font-row {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 8px 0;
}
.font-label {
    width: 80px;
    font-size: 14px;
    color: var(--text-secondary);
}
.font-slider {
    flex: 1;
    -webkit-appearance: none;
    appearance: none;
    height: 4px;
    background: var(--border);
    border-radius: 2px;
    outline: none;
}
.font-slider::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background: var(--accent);
    cursor: pointer;
}
.font-slider::-moz-range-thumb {
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background: var(--accent);
    cursor: pointer;
    border: none;
}
.font-value {
    width: 50px;
    text-align: right;
    font-size: 13px;
    font-family: monospace;
    color: var(--text-secondary);
}

/* Preset themes */
.presets-section {
    margin-top: 24px;
}
.preset-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 12px;
}
.preset-btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 8px 14px;
    border-radius: 8px;
    border: 2px solid transparent;
    cursor: pointer;
    font-size: 13px;
    font-weight: 500;
    color: white;
    transition: border-color 0.15s, opacity 0.15s;
}
.preset-btn:hover { opacity: 0.85; }
.preset-btn.active { border-color: white; }
.preset-btn .delete-x {
    margin-left: 4px;
    font-size: 11px;
    opacity: 0.7;
    cursor: pointer;
    padding: 2px 4px;
    border-radius: 50%;
}
.preset-btn .delete-x:hover {
    opacity: 1;
    background: rgba(0,0,0,0.3);
}
)CSS";

inline constexpr const char* WEB_CSS_THEME_ACTIONS = R"CSS(
/* Action buttons */
.actions {
    display: flex;
    gap: 10px;
    margin-top: 16px;
    flex-wrap: wrap;
}
.btn {
    padding: 10px 18px;
    border-radius: 8px;
    border: 1px solid var(--border);
    background: var(--surface);
    color: var(--text);
    font-size: 14px;
    cursor: pointer;
    transition: background 0.15s;
}
.btn:hover { background: var(--surface-hover); }
.btn-danger {
    border-color: #ff4444;
    color: #ff4444;
}
.btn-danger:hover { background: rgba(255,68,68,0.1); }
.btn-rainbow {
    background: linear-gradient(90deg, #ff6b6b, #ffd93d, #6bcb77, #4d96ff, #9b59b6);
    color: white;
    border: none;
    font-weight: 600;
}
.btn-rainbow:hover { opacity: 0.85; }
.btn-primary {
    background: var(--accent);
    border-color: var(--accent);
    color: white;
}
.btn-primary:hover { opacity: 0.85; }

/* Library & Community */
.library-section {
    margin-top: 28px;
    border-top: 1px solid var(--border);
    padding-top: 16px;
}
.lib-tabs {
    display: flex;
    gap: 0;
    margin-bottom: 12px;
}
.lib-tab {
    flex: 1;
    padding: 8px 12px;
    border: 1px solid var(--border);
    background: var(--bg);
    color: var(--text-secondary);
    font-size: 13px;
    font-weight: 500;
    cursor: pointer;
    transition: background 0.15s, color 0.15s;
}
.lib-tab:first-child { border-radius: 6px 0 0 6px; }
.lib-tab:last-child { border-radius: 0 6px 6px 0; }
.lib-tab.active {
    background: var(--accent);
    border-color: var(--accent);
    color: white;
}
.lib-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(100px, 1fr));
    gap: 8px;
    margin-top: 8px;
    min-height: 60px;
}
.lib-card {
    border: 2px solid var(--border);
    border-radius: 8px;
    overflow: hidden;
    cursor: pointer;
    transition: border-color 0.15s;
    position: relative;
}
.lib-card:hover { border-color: var(--text-secondary); }
.lib-card.selected { border-color: var(--accent); }
.lib-card img {
    width: 100%;
    aspect-ratio: 3/2;
    display: block;
    object-fit: cover;
}
.lib-card .lib-card-name {
    font-size: 11px;
    padding: 4px 6px;
    color: var(--text-secondary);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.lib-card .lib-card-actions {
    position: absolute;
    top: 4px;
    right: 4px;
    display: none;
}
.lib-card:hover .lib-card-actions { display: flex; gap: 2px; }
.lib-card-btn {
    width: 20px;
    height: 20px;
    border-radius: 50%;
    border: none;
    background: rgba(0,0,0,0.6);
    color: white;
    font-size: 11px;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
}
.lib-card-btn:hover { background: rgba(0,0,0,0.8); }
.lib-empty {
    color: var(--text-secondary);
    font-size: 13px;
    text-align: center;
    padding: 20px;
    grid-column: 1 / -1;
}
.lib-actions {
    display: flex;
    gap: 8px;
    margin-top: 8px;
    flex-wrap: wrap;
}
.btn-small {
    padding: 6px 12px;
    font-size: 12px;
    border-radius: 6px;
    border: 1px solid var(--border);
    background: var(--surface);
    color: var(--text);
    cursor: pointer;
    transition: background 0.15s;
}
.btn-small:hover { background: var(--surface-hover); }
.btn-small:disabled { opacity: 0.4; cursor: not-allowed; }
.btn-small.btn-share {
    background: #2563eb;
    border-color: #2563eb;
    color: white;
}
.btn-small.btn-share:hover { opacity: 0.85; }
.btn-small.btn-share:disabled { opacity: 0.3; }
.lib-toast {
    position: fixed;
    bottom: 20px;
    left: 50%;
    transform: translateX(-50%);
    background: var(--accent);
    color: white;
    padding: 10px 20px;
    border-radius: 8px;
    font-size: 14px;
    z-index: 300;
    opacity: 0;
    transition: opacity 0.3s;
    pointer-events: none;
}
.lib-toast.show { opacity: 1; }
.comm-filters {
    display: flex;
    gap: 6px;
    margin-bottom: 8px;
}
.comm-filter {
    padding: 5px 8px;
    font-size: 12px;
    border-radius: 4px;
    border: 1px solid var(--border);
    background: var(--bg);
    color: var(--text);
}

/* Save dialog */
.save-dialog {
    display: none;
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0,0,0,0.6);
    z-index: 200;
    align-items: center;
    justify-content: center;
}
.save-dialog.open { display: flex; }
.save-dialog-inner {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 24px;
    min-width: 300px;
}
.save-dialog-inner h3 { margin-bottom: 12px; }
.save-dialog-inner input {
    width: 100%;
    padding: 10px;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--bg);
    color: var(--text);
    font-size: 14px;
    margin-bottom: 12px;
}
.save-dialog-inner input:focus { outline: none; border-color: var(--accent); }
.save-dialog-btns { display: flex; gap: 8px; justify-content: flex-end; }

/* Footer */
.footer {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 16px 20px;
    border-top: 1px solid var(--border);
    background: var(--surface);
}

/* Responsive */
@media (max-width: 700px) {
    .main { flex-direction: column; }
    .color-panel { border-right: none; border-bottom: 1px solid var(--border); }
}
)CSS";
