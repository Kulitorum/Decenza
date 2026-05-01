#pragma once

// Menu CSS: burger menu dropdown styles
// Used by all web pages with the header menu

inline constexpr const char* WEB_CSS_MENU = R"CSS(
        .menu-wrapper { position: relative; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            border-bottom: 1px solid var(--border);
            white-space: nowrap;
        }
        .menu-item:last-child { border-bottom: none; }
        .menu-item:hover { background: var(--surface-hover); }
        .menu-item:first-child { border-radius: 7px 7px 0 0; }
        .menu-item:last-child { border-radius: 0 0 7px 7px; }
        .menu-item:only-child { border-radius: 7px; }
)CSS";
