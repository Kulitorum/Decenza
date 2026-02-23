#pragma once

// TOTP Login page - 6-digit code input
inline constexpr const char* WEB_AUTH_LOGIN_PAGE = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Sign In - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --accent-hover: #d4ad2e;
            --error: #f85149;
            --success: #3fb950;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 40px;
            max-width: 380px;
            width: 90%;
            text-align: center;
        }
        .logo { font-size: 48px; margin-bottom: 16px; }
        h1 { font-size: 22px; margin-bottom: 8px; }
        .desc {
            color: var(--text-secondary);
            font-size: 14px;
            line-height: 1.5;
            margin-bottom: 24px;
        }
        .code-input {
            display: flex;
            justify-content: center;
            gap: 8px;
            margin-bottom: 20px;
        }
        .code-input input {
            width: 44px;
            height: 56px;
            background: var(--bg);
            border: 2px solid var(--border);
            border-radius: 8px;
            color: var(--text);
            font-size: 24px;
            font-weight: 600;
            text-align: center;
            outline: none;
            transition: border-color 0.2s;
        }
        .code-input input:focus {
            border-color: var(--accent);
        }
        .btn {
            background: var(--accent);
            color: #000;
            border: none;
            border-radius: 8px;
            padding: 14px 28px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            width: 100%;
            transition: background 0.2s;
        }
        .btn:hover { background: var(--accent-hover); }
        .btn:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        .status {
            margin-top: 16px;
            font-size: 13px;
            min-height: 20px;
        }
        .status.error { color: var(--error); }
        .status.success { color: var(--success); }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">&#9749;</div>
        <h1>Sign In</h1>
        <p class="desc">
            Enter the 6-digit code from your authenticator app.
        </p>
        <div class="code-input" id="codeInputs">
            <input type="text" inputmode="numeric" maxlength="1" autocomplete="one-time-code" autofocus>
            <input type="text" inputmode="numeric" maxlength="1">
            <input type="text" inputmode="numeric" maxlength="1">
            <input type="text" inputmode="numeric" maxlength="1">
            <input type="text" inputmode="numeric" maxlength="1">
            <input type="text" inputmode="numeric" maxlength="1">
        </div>
        <button class="btn" id="loginBtn" onclick="doLogin()">Sign In</button>
        <div class="status" id="status"></div>
    </div>
    <script>
        var inputs = document.querySelectorAll('.code-input input');
        inputs.forEach(function(inp, i) {
            inp.addEventListener('input', function() {
                if (inp.value.length === 1 && i < 5) inputs[i + 1].focus();
                if (getCode().length === 6) doLogin();
            });
            inp.addEventListener('keydown', function(e) {
                if (e.key === 'Backspace' && !inp.value && i > 0) {
                    inputs[i - 1].focus();
                    inputs[i - 1].value = '';
                }
            });
            inp.addEventListener('paste', function(e) {
                e.preventDefault();
                var text = (e.clipboardData || window.clipboardData).getData('text').replace(/\D/g, '');
                for (var j = 0; j < 6 && j < text.length; j++) {
                    inputs[j].value = text[j];
                }
                if (text.length >= 6) doLogin();
                else if (text.length > 0) inputs[Math.min(text.length, 5)].focus();
            });
        });

        function getCode() {
            var code = '';
            inputs.forEach(function(inp) { code += inp.value; });
            return code;
        }

        async function doLogin() {
            var code = getCode();
            if (code.length !== 6 || !/^\d{6}$/.test(code)) return;

            var btn = document.getElementById('loginBtn');
            var status = document.getElementById('status');
            btn.disabled = true;
            status.textContent = 'Verifying...';
            status.className = 'status';

            try {
                var res = await fetch('/api/auth/login', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ code: code })
                });
                var data = await res.json();
                if (res.ok) {
                    status.textContent = 'Authenticated! Redirecting...';
                    status.className = 'status success';
                    setTimeout(function() { window.location.href = '/'; }, 500);
                } else {
                    status.textContent = data.error || 'Invalid code';
                    status.className = 'status error';
                    btn.disabled = false;
                    inputs.forEach(function(inp) { inp.value = ''; });
                    inputs[0].focus();
                }
            } catch (e) {
                status.textContent = 'Connection error';
                status.className = 'status error';
                btn.disabled = false;
            }
        }
    </script>
</body>
</html>
)HTML";

// Setup Required page - shown when no TOTP secret is configured
inline constexpr const char* WEB_AUTH_SETUP_REQUIRED_PAGE = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Setup Required - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
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
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 40px;
            max-width: 420px;
            width: 90%;
            text-align: center;
        }
        .logo { font-size: 48px; margin-bottom: 16px; }
        h1 { font-size: 22px; margin-bottom: 8px; }
        .desc {
            color: var(--text-secondary);
            font-size: 14px;
            line-height: 1.6;
            margin-bottom: 16px;
        }
        .steps {
            text-align: left;
            margin: 20px 0;
            padding: 16px;
            background: rgba(201, 162, 39, 0.08);
            border: 1px solid rgba(201, 162, 39, 0.2);
            border-radius: 8px;
        }
        .steps ol {
            padding-left: 20px;
            color: var(--text-secondary);
            font-size: 13px;
            line-height: 1.8;
        }
        .steps li { margin-bottom: 4px; }
        .steps strong { color: var(--text); }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">&#128274;</div>
        <h1>Security Setup Required</h1>
        <p class="desc">
            Authenticator app verification needs to be configured in the Decenza app before you can access the web interface.
        </p>
        <div class="steps">
            <ol>
                <li>Open the <strong>Decenza DE1</strong> app on your device</li>
                <li>Go to <strong>Settings</strong> &rarr; <strong>Data</strong> tab</li>
                <li>Enable <strong>Security</strong> and follow the setup steps</li>
                <li>Come back here and <a href="/auth/login" style="color: #c9a227;">sign in</a></li>
            </ol>
        </div>
    </div>
</body>
</html>
)HTML";
