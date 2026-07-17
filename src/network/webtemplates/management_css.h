#pragma once

// Shared "management page" styling for the inventory web surfaces — /beans,
// /recipes, /equipment (polish-shotserver-inventory-pages). One source of truth
// for the card grid, cards, badges/chips, buttons, status line, empty state,
// toolbar/search bar, and the create/edit dialog, so the three pages stop
// re-inlining copy-pasted CSS and read as the clean, app-matching screens they
// mirror (BagCard / RecipeDrinkCard / EquipmentCard). Pair with
// WEB_CSS_VARIABLES + WEB_CSS_HEADER + WEB_CSS_MENU (chrome) and WEB_JS_MANAGEMENT.
inline constexpr const char* WEB_CSS_MANAGEMENT = R"CSS(
        .page-title { font-size: 1.05rem; font-weight: 600; color: var(--text); }
        .container { max-width: 1200px; margin: 0 auto; padding: 1.25rem; }

        /* Toolbar: primary add action(s) + optional search/sort bar */
        .toolbar { display: flex; flex-wrap: wrap; gap: 0.75rem; align-items: center;
                   margin-bottom: 1rem; }
        .toolbar .spacer { flex: 1 1 auto; }
        .searchbar { display: flex; flex-wrap: wrap; gap: 0.5rem; align-items: center;
                     margin-bottom: 1rem; }
        .searchbar .search-wrap { position: relative; flex: 1 1 220px; }
        .searchbar input.search { width: 100%; background: var(--bg); color: var(--text);
                     border: 1px solid var(--border); border-radius: 8px;
                     padding: 0.5rem 2rem 0.5rem 0.75rem; }
        .searchbar .search-clear { position: absolute; right: 0.5rem; top: 50%;
                     transform: translateY(-50%); background: none; border: none;
                     color: var(--text-secondary); cursor: pointer; font-size: 1.1rem;
                     padding: 0 0.25rem; }

        /* Responsive card grid — mirrors the app's ~380px Flow grid */
        .grid { display: grid; gap: 1rem;
                grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); }
        .card { background: var(--surface); border: 1px solid var(--border);
                border-radius: 16px; padding: 1rem; display: flex; flex-direction: column;
                gap: 0.5rem; }
        .card.active { border-width: 2px; border-color: var(--accent); padding: calc(1rem - 1px); }
        .card.dimmed { opacity: 0.7; }

        .card-head { display: flex; gap: 0.75rem; align-items: flex-start; }
        .thumb { width: 44px; height: 44px; border-radius: 10px; object-fit: cover;
                 flex: 0 0 auto; background: var(--surface-hover);
                 border: 1px solid var(--border); }
        .thumb.placeholder { display: flex; align-items: center; justify-content: center;
                 font-size: 1.4rem; color: var(--text-secondary); }
        .card-body { flex: 1 1 auto; min-width: 0; }
        .card-title { font-size: 1.05rem; font-weight: 600; color: var(--text);
                 display: flex; align-items: center; gap: 0.4rem; flex-wrap: wrap; }
        .verified { color: var(--pressure); font-size: 0.9rem; }
        .card-roaster { color: var(--text-secondary); font-size: 0.85rem; margin-top: 0.1rem; }
        .attr-line { color: var(--text-secondary); font-size: 0.82rem; margin-top: 0.25rem; }
        .notes-line { color: var(--text-secondary); font-size: 0.82rem; font-style: italic;
                 margin-top: 0.25rem; overflow: hidden; text-overflow: ellipsis;
                 white-space: nowrap; }
        .meta-line { color: var(--text); font-size: 0.8rem; margin-top: 0.25rem; }
        .plan-line { color: var(--text-secondary); font-size: 0.82rem; margin-top: 0.25rem; }

        /* Badges + chips */
        .badge { color: var(--accent); font-size: 0.75rem; font-weight: 600;
                 text-transform: uppercase; letter-spacing: 0.03em; }
        .chip { display: inline-flex; align-items: center; gap: 0.25rem; font-size: 0.75rem;
                background: var(--surface-hover); color: var(--text-secondary);
                border: 1px solid var(--border); border-radius: 999px;
                padding: 0.1rem 0.55rem; }
        .chip.warn { color: #1a1a1a; background: #ffb454; border-color: #ffb454;
                cursor: pointer; }

        /* Action row */
        .actions { display: flex; gap: 0.5rem; flex-wrap: wrap; align-items: center;
                   margin-top: 0.35rem; }
        button { background: var(--surface-hover); color: var(--text);
                 border: 1px solid var(--border); border-radius: 8px;
                 padding: 0.4rem 0.8rem; cursor: pointer; font-size: 0.85rem; }
        button:hover { background: var(--border); }
        button:disabled { opacity: 0.5; cursor: default; }
        button.primary { background: var(--accent); color: #1a1a1a; border-color: var(--accent);
                 font-weight: 600; }
        button.primary:hover { background: var(--accent-dim); }
        button.ghost { background: none; }
        button.danger:hover { background: var(--temp); color: #fff; border-color: var(--temp); }

        #status { margin: 0.5rem 0; color: var(--text-secondary); min-height: 1.2em; }
        .muted { color: var(--text-secondary); }
        .empty { text-align: center; color: var(--text-secondary); padding: 3rem 1rem; }
        .empty h2 { color: var(--text); font-size: 1.1rem; margin-bottom: 0.5rem; }

        /* Archived section (recipes) */
        .section-head { margin: 1.5rem 0 0.75rem; }
        .section-head button { font-size: 0.85rem; }

        /* Create/edit dialog */
        dialog { background: var(--surface); color: var(--text); border: 1px solid var(--border);
                 border-radius: 14px; padding: 1.25rem; max-width: 560px; width: 94%;
                 max-height: 88vh; overflow-y: auto; }
        dialog::backdrop { background: rgba(0,0,0,0.6); }
        dialog h2 { font-size: 1.1rem; margin-bottom: 0.5rem; }
        dialog label { display: block; margin: 0.6rem 0 0.15rem; font-size: 0.82rem;
                 color: var(--text-secondary); }
        dialog input, dialog select, dialog textarea { width: 100%; background: var(--bg);
                 color: var(--text); border: 1px solid var(--border); border-radius: 8px;
                 padding: 0.45rem; font-size: 0.9rem; font-family: inherit; }
        dialog textarea { min-height: 3rem; resize: vertical; }
        .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 0 0.75rem; }
        .check-row { display: flex; align-items: center; gap: 0.5rem; margin: 0.4rem 0; }
        .check-row input { width: auto; }
        .check-row label { margin: 0; color: var(--text); }
        .dialog-section { border-top: 1px solid var(--border); margin-top: 1rem; padding-top: 0.5rem; }
        .dialog-section > summary { cursor: pointer; color: var(--accent); font-size: 0.85rem;
                 margin-bottom: 0.25rem; }
        .dialog-actions { display: flex; gap: 0.5rem; justify-content: flex-end; margin-top: 1.25rem; }

        /* Bean Base search results (beans) */
        .search-results { border: 1px solid var(--border); border-radius: 8px; margin-top: 0.35rem;
                 max-height: 220px; overflow-y: auto; }
        .search-results .result { padding: 0.5rem 0.6rem; cursor: pointer;
                 border-bottom: 1px solid var(--border); }
        .search-results .result:last-child { border-bottom: none; }
        .search-results .result:hover { background: var(--surface-hover); }
        .result .r-title { font-weight: 600; }
        .result .r-sub { color: var(--text-secondary); font-size: 0.8rem; }
)CSS";
