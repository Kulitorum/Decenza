#pragma once

// Remote Control help page
// Instructions for installing and using scrcpy

inline constexpr const char* WEB_REMOTE_PAGE = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Remote Control - Decenza</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.6;
            min-height: 100vh;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 800px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.25rem; font-weight: 600; }
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 2rem 1.5rem;
        }
        h2 {
            color: var(--accent);
            font-size: 1.125rem;
            margin: 2rem 0 1rem 0;
            padding-bottom: 0.5rem;
            border-bottom: 1px solid var(--border);
        }
        h2:first-of-type { margin-top: 0; }
        p { margin-bottom: 1rem; }
        .step {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 1rem 1.25rem;
            margin-bottom: 1rem;
        }
        .step-number {
            display: inline-block;
            width: 28px;
            height: 28px;
            background: var(--accent);
            color: var(--bg);
            border-radius: 50%;
            text-align: center;
            line-height: 28px;
            font-weight: 600;
            margin-right: 0.75rem;
        }
        .step-title {
            font-weight: 600;
            font-size: 1rem;
        }
        .step-content {
            margin-top: 0.75rem;
            padding-left: 2.5rem;
        }
        code {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 4px;
            padding: 0.2rem 0.5rem;
            font-family: 'Consolas', 'Monaco', monospace;
            font-size: 0.9rem;
        }
        pre {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 1rem;
            overflow-x: auto;
            margin: 0.75rem 0;
        }
        pre code {
            background: none;
            border: none;
            padding: 0;
        }
        .copy-btn {
            background: var(--surface);
            border: 1px solid var(--border);
            color: var(--text-secondary);
            padding: 0.25rem 0.75rem;
            border-radius: 4px;
            cursor: pointer;
            font-size: 0.8rem;
            float: right;
            margin-top: -0.25rem;
        }
        .copy-btn:hover {
            border-color: var(--accent);
            color: var(--accent);
        }
        .note {
            background: rgba(201, 162, 39, 0.1);
            border-left: 3px solid var(--accent);
            padding: 0.75rem 1rem;
            margin: 1rem 0;
            font-size: 0.9rem;
        }
        .platform-tabs {
            display: flex;
            gap: 0.5rem;
            margin-bottom: 1rem;
        }
        .platform-tab {
            padding: 0.5rem 1rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 6px;
            cursor: pointer;
            color: var(--text-secondary);
        }
        .platform-tab.active {
            background: var(--accent);
            color: var(--bg);
            border-color: var(--accent);
        }
        .platform-content { display: none; }
        .platform-content.active { display: block; }
        a { color: var(--accent); }
        .device-info {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 1rem;
            margin: 1rem 0;
        }
        .device-info-row {
            display: flex;
            justify-content: space-between;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .device-info-row:last-child { border-bottom: none; }
        .device-info-label { color: var(--text-secondary); }
        .device-info-value { font-family: monospace; }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Remote Control Setup</h1>
        </div>
    </header>

    <main class="container">
        <p>Control your tablet from your computer using <strong>scrcpy</strong> - a free, open-source screen mirroring tool. See your tablet's screen in a window and control it with your mouse and keyboard.</p>

        <div class="device-info">
            <div class="device-info-row">
                <span class="device-info-label">Device IP Address</span>
                <span class="device-info-value" id="deviceIp">Loading...</span>
            </div>
            <div class="device-info-row">
                <span class="device-info-label">ADB Port</span>
                <span class="device-info-value">5555</span>
            </div>
        </div>

        <h2>Step 1: Install scrcpy</h2>

        <div class="platform-tabs">
            <button class="platform-tab active" onclick="showPlatform('windows')">Windows</button>
            <button class="platform-tab" onclick="showPlatform('mac')">macOS</button>
            <button class="platform-tab" onclick="showPlatform('linux')">Linux</button>
        </div>

        <div id="windows" class="platform-content active">
            <div class="step">
                <span class="step-number">1</span>
                <span class="step-title">Install with winget (recommended)</span>
                <div class="step-content">
                    <p>Open PowerShell or Command Prompt and run:</p>
                    <pre><code>winget install Genymobile.scrcpy</code></pre>
                </div>
            </div>
            <div class="note">
                Alternative: Download from <a href="https://github.com/Genymobile/scrcpy/releases" target="_blank">GitHub releases</a> and extract to a folder in your PATH.
            </div>
        </div>

        <div id="mac" class="platform-content">
            <div class="step">
                <span class="step-number">1</span>
                <span class="step-title">Install with Homebrew</span>
                <div class="step-content">
                    <pre><code>brew install scrcpy</code></pre>
                </div>
            </div>
        </div>

        <div id="linux" class="platform-content">
            <div class="step">
                <span class="step-number">1</span>
                <span class="step-title">Install from package manager</span>
                <div class="step-content">
                    <p><strong>Ubuntu/Debian:</strong></p>
                    <pre><code>sudo apt install scrcpy</code></pre>
                    <p><strong>Arch Linux:</strong></p>
                    <pre><code>sudo pacman -S scrcpy</code></pre>
                    <p><strong>Fedora:</strong></p>
                    <pre><code>sudo dnf install scrcpy</code></pre>
                </div>
            </div>
        </div>

        <h2>Step 2: Connect to the tablet</h2>

        <div class="step">
            <span class="step-number">2</span>
            <span class="step-title">Connect via WiFi</span>
            <div class="step-content">
                <p>Make sure your computer is on the same WiFi network as the tablet, then run:</p>
                <pre><code id="connectCmd">adb connect <span id="connectIp">192.168.x.x</span>:5555</code></pre>
                <button class="copy-btn" onclick="copyCommand('connectCmd')">Copy</button>
            </div>
        </div>

        <h2>Step 3: Start remote control</h2>

        <div class="step">
            <span class="step-number">3</span>
            <span class="step-title">Launch scrcpy</span>
            <div class="step-content">
                <p>Once connected, start the remote view:</p>
                <pre><code id="scrcpyCmd">scrcpy</code></pre>
                <button class="copy-btn" onclick="copyCommand('scrcpyCmd')">Copy</button>
                <p style="margin-top: 1rem;">Or with options for better performance over WiFi:</p>
                <pre><code id="scrcpyOptCmd">scrcpy --video-bit-rate 4M --max-fps 30</code></pre>
                <button class="copy-btn" onclick="copyCommand('scrcpyOptCmd')">Copy</button>
            </div>
        </div>

        <h2>Useful scrcpy options</h2>
        <div class="step">
            <div class="step-content" style="padding-left: 0;">
                <p><code>--stay-awake</code> - Keep device awake while connected</p>
                <p><code>--turn-screen-off</code> - Turn off device screen (saves battery)</p>
                <p><code>--window-title "DE1"</code> - Set window title</p>
                <p><code>--max-size 1024</code> - Limit resolution (better performance)</p>
            </div>
        </div>

        <div class="note">
            <strong>Tip:</strong> Create a shortcut or script with your preferred options for quick access.
        </div>
    </main>

    <script>
        // Get device IP from current URL
        var deviceIp = window.location.hostname;
        document.getElementById('deviceIp').textContent = deviceIp;
        document.getElementById('connectIp').textContent = deviceIp;

        function showPlatform(platform) {
            document.querySelectorAll('.platform-tab').forEach(function(tab) {
                tab.classList.remove('active');
            });
            document.querySelectorAll('.platform-content').forEach(function(content) {
                content.classList.remove('active');
            });
            event.target.classList.add('active');
            document.getElementById(platform).classList.add('active');
        }

        function copyCommand(elementId) {
            var text = document.getElementById(elementId).textContent;
            var btn = event.target;

            // Try modern clipboard API first (requires HTTPS)
            if (navigator.clipboard && window.isSecureContext) {
                navigator.clipboard.writeText(text).then(function() {
                    btn.textContent = 'Copied!';
                    setTimeout(function() { btn.textContent = 'Copy'; }, 1500);
                });
            } else {
                // Fallback for HTTP: use legacy execCommand
                var textarea = document.createElement('textarea');
                textarea.value = text;
                textarea.style.position = 'fixed';
                textarea.style.left = '-9999px';
                document.body.appendChild(textarea);
                textarea.select();
                try {
                    document.execCommand('copy');
                    btn.textContent = 'Copied!';
                    setTimeout(function() { btn.textContent = 'Copy'; }, 1500);
                } catch (err) {
                    btn.textContent = 'Failed';
                    setTimeout(function() { btn.textContent = 'Copy'; }, 1500);
                }
                document.body.removeChild(textarea);
            }
        }
    </script>
</body>
</html>
)HTML";
