#pragma once

// Base CSS: variables, reset, body styles
// Used by all web pages

inline constexpr const char* WEB_CSS_VARIABLES = R"CSS(
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --accent-dim: #a68a1f;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
            --weightFlow: #d4a574;
            --resistance: #eae83d;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
            min-height: 100vh;
        }
)CSS";

inline constexpr const char* WEB_CSS_HEADER = R"CSS(
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1200px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        .header-right { display: flex; align-items: center; gap: 1rem; }
        .logo {
            font-size: 1.25rem;
            font-weight: 600;
            color: var(--accent);
            text-decoration: none;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; }
        .container { max-width: 1200px; margin: 0 auto; padding: 1.5rem; }
)CSS";
