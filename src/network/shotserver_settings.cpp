#include "shotserver.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "../history/shothistorystorage.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../core/settingsserializer.h"
#include "../ai/aimanager.h"
#include "mqttclient.h"
#include "version.h"

#include <QPointer>
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUdpSocket>
#include <QSet>
#include <QFile>
#include <QBuffer>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#ifndef Q_OS_IOS
#include <QProcess>
#endif
#include <QCoreApplication>
#include <QRegularExpression>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

static void applyAiSettings(Settings* s, const QJsonObject& obj)
{
    if (obj.contains("aiProvider"))
        s->setAiProvider(obj["aiProvider"].toString());
    if (obj.contains("openaiApiKey"))
        s->setOpenaiApiKey(obj["openaiApiKey"].toString());
    if (obj.contains("anthropicApiKey"))
        s->setAnthropicApiKey(obj["anthropicApiKey"].toString());
    if (obj.contains("geminiApiKey"))
        s->setGeminiApiKey(obj["geminiApiKey"].toString());
    if (obj.contains("openrouterApiKey"))
        s->setOpenrouterApiKey(obj["openrouterApiKey"].toString());
    if (obj.contains("openrouterModel"))
        s->setOpenrouterModel(obj["openrouterModel"].toString());
    if (obj.contains("ollamaEndpoint"))
        s->setOllamaEndpoint(obj["ollamaEndpoint"].toString());
    if (obj.contains("ollamaModel"))
        s->setOllamaModel(obj["ollamaModel"].toString());
}

static void applyMqttSettings(Settings* s, const QJsonObject& obj)
{
    if (obj.contains("mqttEnabled"))
        s->setMqttEnabled(obj["mqttEnabled"].toBool());
    if (obj.contains("mqttBrokerHost"))
        s->setMqttBrokerHost(obj["mqttBrokerHost"].toString());
    if (obj.contains("mqttBrokerPort"))
        s->setMqttBrokerPort(obj["mqttBrokerPort"].toInt());
    if (obj.contains("mqttUsername"))
        s->setMqttUsername(obj["mqttUsername"].toString());
    if (obj.contains("mqttPassword"))
        s->setMqttPassword(obj["mqttPassword"].toString());
    if (obj.contains("mqttBaseTopic"))
        s->setMqttBaseTopic(obj["mqttBaseTopic"].toString());
    if (obj.contains("mqttPublishInterval"))
        s->setMqttPublishInterval(obj["mqttPublishInterval"].toInt());
    if (obj.contains("mqttClientId"))
        s->setMqttClientId(obj["mqttClientId"].toString());
    if (obj.contains("mqttRetainMessages"))
        s->setMqttRetainMessages(obj["mqttRetainMessages"].toBool());
    if (obj.contains("mqttHomeAssistantDiscovery"))
        s->setMqttHomeAssistantDiscovery(obj["mqttHomeAssistantDiscovery"].toBool());
}

QString ShotServer::generateSettingsPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Service Connections - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --success: #18c37e;
            --error: #e73249;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
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
        h1 { font-size: 1.125rem; font-weight: 600; flex: 1; }
        .container { max-width: 800px; margin: 0 auto; padding: 1.5rem; }
        .section {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 1.5rem;
            overflow: hidden;
        }
        .section-header {
            padding: 1rem 1.25rem;
            border-bottom: 1px solid var(--border);
            display: flex;
            align-items: center;
            gap: 0.75rem;
        }
        .section-header h2 {
            font-size: 1rem;
            font-weight: 600;
        }
        .section-icon { font-size: 1.25rem; }
        .section-body { padding: 1.25rem; }
        .form-group {
            margin-bottom: 1rem;
        }
        .form-group:last-child { margin-bottom: 0; }
        .form-label {
            display: block;
            font-size: 0.875rem;
            color: var(--text-secondary);
            margin-bottom: 0.375rem;
        }
        .form-input {
            width: 100%;
            padding: 0.625rem 0.875rem;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.9375rem;
            font-family: inherit;
        }
        .form-input:focus {
            outline: none;
            border-color: var(--accent);
        }
        .form-input::placeholder { color: var(--text-secondary); }
        .form-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 1rem;
        }
)HTML" R"HTML(
        @media (max-width: 600px) {
            .form-row { grid-template-columns: 1fr; }
            .section-actions { flex-wrap: wrap; }
        }
        .form-checkbox {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            cursor: pointer;
        }
        .form-checkbox input {
            width: 1.125rem;
            height: 1.125rem;
            accent-color: var(--accent);
        }
        .btn {
            padding: 0.625rem 1.25rem;
            border: none;
            border-radius: 6px;
            font-size: 0.875rem;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.15s;
            white-space: nowrap;
        }
        .btn-primary {
            background: var(--accent);
            color: var(--bg);
        }
        .btn-primary:hover { filter: brightness(1.1); }
        .btn-primary:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        .btn-secondary {
            background: transparent;
            color: var(--text);
            border: 1px solid var(--border);
        }
        .btn-secondary:hover { border-color: var(--text-secondary); }
        .btn-secondary:disabled {
            opacity: 0.5;
            cursor: not-allowed;
        }
        .btn-danger {
            background: transparent;
            color: var(--error);
            border: 1px solid var(--error);
        }
        .btn-danger:hover { background: rgba(231, 50, 73, 0.1); }
        .section-actions {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            margin-top: 1.25rem;
            padding-top: 1rem;
            border-top: 1px solid var(--border);
        }
        .section-actions .status-msg {
            flex: 1;
            min-width: 0;
        }
        .status-msg {
            font-size: 0.8125rem;
            padding: 0.375rem 0.625rem;
            border-radius: 4px;
        }
        .status-success {
            background: rgba(24, 195, 126, 0.15);
            color: var(--success);
        }
        .status-error {
            background: rgba(231, 50, 73, 0.15);
            color: var(--error);
        }
        .status-dot {
            display: inline-block;
            width: 8px;
            height: 8px;
            border-radius: 50%;
            margin-right: 0.375rem;
            vertical-align: middle;
        }
        .status-dot.connected { background: var(--success); }
        .status-dot.disconnected { background: var(--text-secondary); }
        .mqtt-status {
            display: flex;
            align-items: center;
            font-size: 0.8125rem;
            color: var(--text-secondary);
            flex: 1;
            min-width: 0;
        }
        .help-text {
            font-size: 0.75rem;
            color: var(--text-secondary);
            margin-top: 0.25rem;
        }
        .password-wrapper {
            position: relative;
        }
        .password-toggle {
            position: absolute;
            right: 0.75rem;
            top: 50%;
            transform: translateY(-50%);
            background: none;
            border: none;
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 1rem;
            padding: 0.25rem;
        }
        .password-toggle:hover { color: var(--text); }
        .provider-row {
            display: flex;
            gap: 0.5rem;
            flex-wrap: wrap;
        }
        .provider-btn {
            flex: 1;
            min-width: 5.5rem;
            padding: 0.5rem 0.375rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: var(--bg);
            color: var(--text-secondary);
            cursor: pointer;
            text-align: center;
            transition: all 0.15s;
            line-height: 1.3;
        }
        .provider-btn:hover { border-color: var(--text-secondary); }
        .provider-btn .provider-name {
            font-size: 0.8125rem;
            font-weight: 500;
        }
        .provider-btn .provider-model {
            font-size: 0.6875rem;
            opacity: 0.7;
        }
        .provider-btn.selected {
            background: var(--accent);
            border-color: var(--accent);
            color: var(--bg);
        }
        .provider-btn.selected .provider-model { opacity: 0.8; }
        .provider-btn.configured {
            background: rgba(24, 195, 126, 0.15);
            border-color: rgba(24, 195, 126, 0.5);
            color: var(--text);
        }
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&larr;</a>
            <h1>Service Connections</h1>
        </div>
    </header>

    <div class="container">
        <!-- Visualizer Section -->
        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#9749;</span>
                <h2>Visualizer.coffee</h2>
            </div>
            <div class="section-body">
                <div class="form-group">
                    <label class="form-label">Username / Email</label>
                    <input type="text" class="form-input" id="visualizerUsername" placeholder="your@email.com">
                </div>
                <div class="form-group">
                    <label class="form-label">Password</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="visualizerPassword" placeholder="Enter password">
                        <button type="button" class="password-toggle" onclick="togglePassword('visualizerPassword')">&#128065;</button>
                    </div>
                </div>
                <div class="section-actions">
                    <span id="visualizerStatus" class="status-msg"></span>
                    <button class="btn btn-secondary" id="visualizerTestBtn" onclick="testVisualizer()">Test Connection</button>
                    <button class="btn btn-primary" id="visualizerSaveBtn" onclick="saveVisualizer()">Save</button>
                </div>
            </div>
        </div>

        <!-- AI Section -->
        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#129302;</span>
                <h2>AI Dialing Assistant</h2>
            </div>
            <div class="section-body">
                <div class="form-group">
                    <label class="form-label">Provider</label>
                    <div class="provider-row" id="providerRow">
                        <button type="button" class="provider-btn" data-provider="openai" onclick="selectProvider('openai')">
                            <div class="provider-name">OpenAI</div>
                            <div class="provider-model">GPT-4.1</div>
                        </button>
                        <button type="button" class="provider-btn" data-provider="anthropic" onclick="selectProvider('anthropic')">
                            <div class="provider-name">Anthropic</div>
                            <div class="provider-model">Claude</div>
                        </button>
                        <button type="button" class="provider-btn" data-provider="gemini" onclick="selectProvider('gemini')">
                            <div class="provider-name">Gemini</div>
                            <div class="provider-model">Gemini</div>
                        </button>
                        <button type="button" class="provider-btn" data-provider="openrouter" onclick="selectProvider('openrouter')">
                            <div class="provider-name">OpenRouter</div>
                            <div class="provider-model" id="openrouterBtnModel">Multi</div>
                        </button>
                        <button type="button" class="provider-btn" data-provider="ollama" onclick="selectProvider('ollama')">
                            <div class="provider-name">Ollama</div>
                            <div class="provider-model" id="ollamaBtnModel">Local</div>
                        </button>
                    </div>
                </div>
                <div class="form-group" id="openaiGroup" style="display:none;">
                    <label class="form-label">OpenAI API Key</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="openaiApiKey" placeholder="sk-...">
                        <button type="button" class="password-toggle" onclick="togglePassword('openaiApiKey')">&#128065;</button>
                    </div>
                    <div class="help-text">Get your API key from <a href="https://platform.openai.com/api-keys" target="_blank" style="color:var(--accent)">platform.openai.com</a></div>
                </div>
                <div class="form-group" id="anthropicGroup" style="display:none;">
                    <label class="form-label">Anthropic API Key</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="anthropicApiKey" placeholder="sk-ant-...">
                        <button type="button" class="password-toggle" onclick="togglePassword('anthropicApiKey')">&#128065;</button>
                    </div>
                    <div class="help-text">Get your API key from <a href="https://console.anthropic.com/settings/keys" target="_blank" style="color:var(--accent)">console.anthropic.com</a></div>
                </div>
                <div class="form-group" id="geminiGroup" style="display:none;">
                    <label class="form-label">Google Gemini API Key</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="geminiApiKey" placeholder="AI...">
                        <button type="button" class="password-toggle" onclick="togglePassword('geminiApiKey')">&#128065;</button>
                    </div>
                    <div class="help-text">Get your API key from <a href="https://aistudio.google.com/apikey" target="_blank" style="color:var(--accent)">aistudio.google.com</a></div>
                </div>
                <div id="openrouterGroup" style="display:none;">
                    <div class="form-group">
                        <label class="form-label">OpenRouter API Key</label>
                        <div class="password-wrapper">
                            <input type="password" class="form-input" id="openrouterApiKey" placeholder="sk-or-...">
                            <button type="button" class="password-toggle" onclick="togglePassword('openrouterApiKey')">&#128065;</button>
                        </div>
                        <div class="help-text">Get your API key from <a href="https://openrouter.ai/keys" target="_blank" style="color:var(--accent)">openrouter.ai</a></div>
                    </div>
                    <div class="form-group">
                        <label class="form-label">Model</label>
                        <input type="text" class="form-input" id="openrouterModel" placeholder="anthropic/claude-sonnet-4">
                        <div class="help-text">Enter model ID from <a href="https://openrouter.ai/models" target="_blank" style="color:var(--accent)">openrouter.ai/models</a></div>
                    </div>
                </div>
                <div id="ollamaGroup" style="display:none;">
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Ollama Endpoint</label>
                            <input type="text" class="form-input" id="ollamaEndpoint" placeholder="http://localhost:11434">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Model</label>
                            <input type="text" class="form-input" id="ollamaModel" placeholder="llama3.2">
                        </div>
                    </div>
                </div>
                <div class="section-actions">
                    <span id="aiStatus" class="status-msg"></span>
                    <button class="btn btn-secondary" id="aiTestBtn" onclick="testAi()" disabled>Test Connection</button>
                    <button class="btn btn-primary" id="aiSaveBtn" onclick="saveAi()">Save</button>
                </div>
            </div>
        </div>
)HTML" R"HTML(
        <!-- MQTT Section -->
        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#127968;</span>
                <h2>MQTT (Home Automation)</h2>
            </div>
            <div class="section-body">
                <div class="form-group">
                    <label class="form-checkbox">
                        <input type="checkbox" id="mqttEnabled" onchange="updateMqttFields()">
                        <span>Enable MQTT</span>
                    </label>
                </div>
                <div id="mqttFields" style="display:none;">
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Broker Host</label>
                            <input type="text" class="form-input" id="mqttBrokerHost" placeholder="192.168.1.100">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Port</label>
                            <input type="number" class="form-input" id="mqttBrokerPort" placeholder="1883">
                        </div>
                    </div>
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Username (optional)</label>
                            <input type="text" class="form-input" id="mqttUsername" placeholder="mqtt_user">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Password (optional)</label>
                            <div class="password-wrapper">
                                <input type="password" class="form-input" id="mqttPassword" placeholder="Enter password">
                                <button type="button" class="password-toggle" onclick="togglePassword('mqttPassword')">&#128065;</button>
                            </div>
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="form-label">Base Topic</label>
                        <input type="text" class="form-input" id="mqttBaseTopic" placeholder="decenza">
                    </div>
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Publish Interval (seconds)</label>
                            <input type="number" class="form-input" id="mqttPublishInterval" placeholder="5">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Client ID (optional)</label>
                            <input type="text" class="form-input" id="mqttClientId" placeholder="decenza_de1">
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="form-checkbox">
                            <input type="checkbox" id="mqttRetainMessages">
                            <span>Retain messages</span>
                        </label>
                    </div>
                    <div class="form-group">
                        <label class="form-checkbox">
                            <input type="checkbox" id="mqttHomeAssistantDiscovery">
                            <span>Home Assistant auto-discovery</span>
                        </label>
                    </div>
                </div>
                <div class="section-actions">
                    <div class="mqtt-status">
                        <span class="status-dot disconnected" id="mqttDot"></span>
                        <span id="mqttStatusText">Disconnected</span>
                    </div>
                    <button class="btn btn-secondary" id="mqttDiscoveryBtn" onclick="publishDiscovery()" style="display:none;">Publish Discovery</button>
                    <button class="btn btn-secondary" id="mqttConnectBtn" onclick="connectMqtt()">Connect</button>
                    <button class="btn btn-primary" id="mqttSaveBtn" onclick="saveMqtt()">Save</button>
                </div>
            </div>
        </div>
    </div>
)HTML" R"HTML(
    <script>
        let mqttPollTimer = null;
        let selectedProvider = '';

        async function loadSettings() {
            try {
                const resp = await fetch('/api/settings');
                const data = await resp.json();

                document.getElementById('visualizerUsername').value = data.visualizerUsername || '';
                document.getElementById('visualizerPassword').value = data.visualizerPassword || '';

                document.getElementById('openaiApiKey').value = data.openaiApiKey || '';
                document.getElementById('anthropicApiKey').value = data.anthropicApiKey || '';
                document.getElementById('geminiApiKey').value = data.geminiApiKey || '';
                document.getElementById('openrouterApiKey').value = data.openrouterApiKey || '';
                document.getElementById('openrouterModel').value = data.openrouterModel || '';
                document.getElementById('ollamaEndpoint').value = data.ollamaEndpoint || '';
                document.getElementById('ollamaModel').value = data.ollamaModel || '';
                selectProvider(data.aiProvider || '');

                document.getElementById('mqttEnabled').checked = data.mqttEnabled || false;
                document.getElementById('mqttBrokerHost').value = data.mqttBrokerHost || '';
                document.getElementById('mqttBrokerPort').value = data.mqttBrokerPort || 1883;
                document.getElementById('mqttUsername').value = data.mqttUsername || '';
                document.getElementById('mqttPassword').value = data.mqttPassword || '';
                document.getElementById('mqttBaseTopic').value = data.mqttBaseTopic || 'decenza';
                document.getElementById('mqttPublishInterval').value = data.mqttPublishInterval || 5;
                document.getElementById('mqttClientId').value = data.mqttClientId || '';
                document.getElementById('mqttRetainMessages').checked = data.mqttRetainMessages || false;
                document.getElementById('mqttHomeAssistantDiscovery').checked = data.mqttHomeAssistantDiscovery || false;
                updateMqttFields();

                pollMqttStatus();
                startMqttPolling();
            } catch (e) {
                showSectionStatus('visualizerStatus', 'Failed to load settings', true);
                showSectionStatus('aiStatus', 'Failed to load settings', true);
                showSectionStatus('mqttStatusText', 'Failed to load settings', true);
            }
        }

        function selectProvider(id) {
            selectedProvider = id;
            document.getElementById('openaiGroup').style.display = id === 'openai' ? 'block' : 'none';
            document.getElementById('anthropicGroup').style.display = id === 'anthropic' ? 'block' : 'none';
            document.getElementById('geminiGroup').style.display = id === 'gemini' ? 'block' : 'none';
            document.getElementById('openrouterGroup').style.display = id === 'openrouter' ? 'block' : 'none';
            document.getElementById('ollamaGroup').style.display = id === 'ollama' ? 'block' : 'none';
            document.getElementById('aiTestBtn').disabled = !id;
            updateProviderButtons();
        }

        function isProviderConfigured(id) {
            switch (id) {
                case 'openai': return !!document.getElementById('openaiApiKey').value;
                case 'anthropic': return !!document.getElementById('anthropicApiKey').value;
                case 'gemini': return !!document.getElementById('geminiApiKey').value;
                case 'openrouter': return !!document.getElementById('openrouterApiKey').value && !!document.getElementById('openrouterModel').value;
                case 'ollama': return !!document.getElementById('ollamaEndpoint').value && !!document.getElementById('ollamaModel').value;
                default: return false;
            }
        }

        function updateProviderButtons() {
            document.querySelectorAll('.provider-btn').forEach(btn => {
                const id = btn.dataset.provider;
                btn.classList.remove('selected', 'configured');
                if (id === selectedProvider) {
                    btn.classList.add('selected');
                } else if (id && isProviderConfigured(id)) {
                    btn.classList.add('configured');
                }
            });
            // Update dynamic model labels
            const orModel = document.getElementById('openrouterModel').value;
            document.getElementById('openrouterBtnModel').textContent = orModel || 'Multi';
            const olModel = document.getElementById('ollamaModel').value;
            document.getElementById('ollamaBtnModel').textContent = olModel || 'Local';
        }

        function updateMqttFields() {
            const enabled = document.getElementById('mqttEnabled').checked;
            document.getElementById('mqttFields').style.display = enabled ? 'block' : 'none';
        }

        function togglePassword(id) {
            const input = document.getElementById(id);
            input.type = input.type === 'password' ? 'text' : 'password';
        }

        function showSectionStatus(id, msg, isError) {
            const el = document.getElementById(id);
            el.textContent = msg;
            el.className = 'status-msg ' + (isError ? 'status-error' : 'status-success');
            setTimeout(() => { el.textContent = ''; el.className = 'status-msg'; }, 4000);
        }
)HTML" R"HTML(
        // --- Visualizer ---
        async function saveVisualizer() {
            const btn = document.getElementById('visualizerSaveBtn');
            btn.disabled = true; btn.textContent = 'Saving...';
            try {
                const resp = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        visualizerUsername: document.getElementById('visualizerUsername').value,
                        visualizerPassword: document.getElementById('visualizerPassword').value
                    })
                });
                const r = await resp.json();
                showSectionStatus('visualizerStatus', r.success ? 'Saved' : (r.error || 'Failed'), !r.success);
            } catch (e) { showSectionStatus('visualizerStatus', 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Save';
        }

        async function testVisualizer() {
            const btn = document.getElementById('visualizerTestBtn');
            btn.disabled = true; btn.textContent = 'Testing...';
            try {
                const resp = await fetch('/api/settings/visualizer/test', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        username: document.getElementById('visualizerUsername').value,
                        password: document.getElementById('visualizerPassword').value
                    })
                });
                const r = await resp.json();
                showSectionStatus('visualizerStatus', r.message, !r.success);
            } catch (e) { showSectionStatus('visualizerStatus', 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Test Connection';
        }

        // --- AI ---
        async function saveAi() {
            const btn = document.getElementById('aiSaveBtn');
            btn.disabled = true; btn.textContent = 'Saving...';
            try {
                const resp = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        aiProvider: selectedProvider,
                        openaiApiKey: document.getElementById('openaiApiKey').value,
                        anthropicApiKey: document.getElementById('anthropicApiKey').value,
                        geminiApiKey: document.getElementById('geminiApiKey').value,
                        openrouterApiKey: document.getElementById('openrouterApiKey').value,
                        openrouterModel: document.getElementById('openrouterModel').value,
                        ollamaEndpoint: document.getElementById('ollamaEndpoint').value,
                        ollamaModel: document.getElementById('ollamaModel').value
                    })
                });
                const r = await resp.json();
                showSectionStatus('aiStatus', r.success ? 'Saved' : (r.error || 'Failed'), !r.success);
            } catch (e) { showSectionStatus('aiStatus', 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Save';
        }

        async function testAi() {
            const btn = document.getElementById('aiTestBtn');
            btn.disabled = true; btn.textContent = 'Testing...';
            try {
                const resp = await fetch('/api/settings/ai/test', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        aiProvider: selectedProvider,
                        openaiApiKey: document.getElementById('openaiApiKey').value,
                        anthropicApiKey: document.getElementById('anthropicApiKey').value,
                        geminiApiKey: document.getElementById('geminiApiKey').value,
                        openrouterApiKey: document.getElementById('openrouterApiKey').value,
                        openrouterModel: document.getElementById('openrouterModel').value,
                        ollamaEndpoint: document.getElementById('ollamaEndpoint').value,
                        ollamaModel: document.getElementById('ollamaModel').value
                    })
                });
                const r = await resp.json();
                showSectionStatus('aiStatus', r.message, !r.success);
            } catch (e) { showSectionStatus('aiStatus', 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Test Connection';
        }
)HTML" R"HTML(
        // --- MQTT ---
        async function saveMqtt() {
            const btn = document.getElementById('mqttSaveBtn');
            btn.disabled = true; btn.textContent = 'Saving...';
            try {
                const resp = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        mqttEnabled: document.getElementById('mqttEnabled').checked,
                        mqttBrokerHost: document.getElementById('mqttBrokerHost').value,
                        mqttBrokerPort: parseInt(document.getElementById('mqttBrokerPort').value) || 1883,
                        mqttUsername: document.getElementById('mqttUsername').value,
                        mqttPassword: document.getElementById('mqttPassword').value,
                        mqttBaseTopic: document.getElementById('mqttBaseTopic').value,
                        mqttPublishInterval: parseInt(document.getElementById('mqttPublishInterval').value) || 5,
                        mqttClientId: document.getElementById('mqttClientId').value,
                        mqttRetainMessages: document.getElementById('mqttRetainMessages').checked,
                        mqttHomeAssistantDiscovery: document.getElementById('mqttHomeAssistantDiscovery').checked
                    })
                });
                const r = await resp.json();
                showSectionStatus('mqttStatusText', r.success ? 'Saved' : (r.error || 'Failed'), !r.success);
            } catch (e) { showSectionStatus('mqttStatusText', 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Save';
        }

        let mqttIsConnected = false;

        async function connectMqtt() {
            const btn = document.getElementById('mqttConnectBtn');
            const wasConnect = !mqttIsConnected;
            btn.disabled = true;
            btn.textContent = wasConnect ? 'Connecting...' : 'Disconnecting...';

            const endpoint = wasConnect ? '/api/settings/mqtt/connect' : '/api/settings/mqtt/disconnect';
            try {
                let body = {};
                if (wasConnect) {
                    body = {
                        mqttEnabled: document.getElementById('mqttEnabled').checked,
                        mqttBrokerHost: document.getElementById('mqttBrokerHost').value,
                        mqttBrokerPort: parseInt(document.getElementById('mqttBrokerPort').value) || 1883,
                        mqttUsername: document.getElementById('mqttUsername').value,
                        mqttPassword: document.getElementById('mqttPassword').value,
                        mqttBaseTopic: document.getElementById('mqttBaseTopic').value,
                        mqttPublishInterval: parseInt(document.getElementById('mqttPublishInterval').value) || 5,
                        mqttClientId: document.getElementById('mqttClientId').value,
                        mqttRetainMessages: document.getElementById('mqttRetainMessages').checked,
                        mqttHomeAssistantDiscovery: document.getElementById('mqttHomeAssistantDiscovery').checked
                    };
                }
                const resp = await fetch(endpoint, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(body)
                });
                const r = await resp.json();
                if (!r.success && wasConnect) {
                    updateMqttStatusUI(false, r.message || 'Connection failed');
                }
            } catch (e) {
                updateMqttStatusUI(false, 'Network error');
            }
            btn.disabled = false;
            pollMqttStatus();
        }

        async function publishDiscovery() {
            const btn = document.getElementById('mqttDiscoveryBtn');
            btn.disabled = true; btn.textContent = 'Publishing...';
            try {
                const resp = await fetch('/api/settings/mqtt/publish-discovery', { method: 'POST' });
                const r = await resp.json();
                btn.textContent = (resp.ok && r.success) ? 'Published!' : 'Failed';
                setTimeout(() => { btn.textContent = 'Publish Discovery'; }, 2000);
            } catch (e) { btn.textContent = 'Failed'; setTimeout(() => { btn.textContent = 'Publish Discovery'; }, 2000); }
            btn.disabled = false;
        }

        let mqttPollFailures = 0;
        async function pollMqttStatus() {
            try {
                const resp = await fetch('/api/settings/mqtt/status');
                if (!resp.ok) throw new Error('HTTP ' + resp.status);
                const r = await resp.json();
                mqttPollFailures = 0;
                updateMqttStatusUI(r.connected, r.status);
            } catch (e) {
                if (++mqttPollFailures >= 3)
                    updateMqttStatusUI(false, 'Status unavailable');
            }
        }

        function updateMqttStatusUI(connected, statusText) {
            mqttIsConnected = connected;
            const dot = document.getElementById('mqttDot');
            const text = document.getElementById('mqttStatusText');
            const connectBtn = document.getElementById('mqttConnectBtn');
            const discoveryBtn = document.getElementById('mqttDiscoveryBtn');
            const haChecked = document.getElementById('mqttHomeAssistantDiscovery').checked;

            dot.className = 'status-dot ' + (connected ? 'connected' : 'disconnected');
            text.textContent = statusText || (connected ? 'Connected' : 'Disconnected');
            text.className = '';
            connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
            discoveryBtn.style.display = (connected && haChecked) ? 'inline-block' : 'none';
        }

        function startMqttPolling() {
            if (mqttPollTimer) clearInterval(mqttPollTimer);
            mqttPollTimer = setInterval(pollMqttStatus, 2000);
        }

        // Update provider button styles live as user types API keys
        document.querySelectorAll('#openaiApiKey,#anthropicApiKey,#geminiApiKey,#openrouterApiKey,#openrouterModel,#ollamaEndpoint,#ollamaModel')
            .forEach(el => el.addEventListener('input', updateProviderButtons));

        // Stop polling when page is not visible
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) {
                if (mqttPollTimer) { clearInterval(mqttPollTimer); mqttPollTimer = null; }
            } else {
                pollMqttStatus();
                startMqttPolling();
            }
        });

        loadSettings();
    </script>
</body>
</html>
)HTML");
}

void ShotServer::handleGetSettings(QTcpSocket* socket)
{
    if (!m_settings) {
        sendJson(socket, R"({"error": "Settings not available"})");
        return;
    }

    QJsonObject obj;

    // Visualizer
    obj["visualizerUsername"] = m_settings->visualizerUsername();
    obj["visualizerPassword"] = m_settings->visualizerPassword();

    // AI
    obj["aiProvider"] = m_settings->aiProvider();
    obj["openaiApiKey"] = m_settings->openaiApiKey();
    obj["anthropicApiKey"] = m_settings->anthropicApiKey();
    obj["geminiApiKey"] = m_settings->geminiApiKey();
    obj["openrouterApiKey"] = m_settings->openrouterApiKey();
    obj["openrouterModel"] = m_settings->openrouterModel();
    obj["ollamaEndpoint"] = m_settings->ollamaEndpoint();
    obj["ollamaModel"] = m_settings->ollamaModel();

    // MQTT
    obj["mqttEnabled"] = m_settings->mqttEnabled();
    obj["mqttBrokerHost"] = m_settings->mqttBrokerHost();
    obj["mqttBrokerPort"] = m_settings->mqttBrokerPort();
    obj["mqttUsername"] = m_settings->mqttUsername();
    obj["mqttPassword"] = m_settings->mqttPassword();
    obj["mqttBaseTopic"] = m_settings->mqttBaseTopic();
    obj["mqttPublishInterval"] = m_settings->mqttPublishInterval();
    obj["mqttClientId"] = m_settings->mqttClientId();
    obj["mqttRetainMessages"] = m_settings->mqttRetainMessages();
    obj["mqttHomeAssistantDiscovery"] = m_settings->mqttHomeAssistantDiscovery();

    sendJson(socket, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ShotServer::handleSaveSettings(QTcpSocket* socket, const QByteArray& body)
{
    if (!m_settings) {
        sendJson(socket, R"({"error": "Settings not available"})");
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        sendJson(socket, R"({"error": "Invalid JSON"})");
        return;
    }

    QJsonObject obj = doc.object();

    // Visualizer
    if (obj.contains("visualizerUsername"))
        m_settings->setVisualizerUsername(obj["visualizerUsername"].toString());
    if (obj.contains("visualizerPassword"))
        m_settings->setVisualizerPassword(obj["visualizerPassword"].toString());

    // AI
    applyAiSettings(m_settings, obj);

    // MQTT
    applyMqttSettings(m_settings, obj);

    sendJson(socket, R"({"success": true})");
}

void ShotServer::handleVisualizerTest(QTcpSocket* socket, const QByteArray& body)
{
    if (m_visualizerTestInFlight) {
        sendJson(socket, R"({"success": false, "message": "A test is already in progress"})");
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        sendJson(socket, R"({"success": false, "message": "Invalid JSON"})");
        return;
    }

    QJsonObject obj = doc.object();
    QString username = obj["username"].toString();
    QString password = obj["password"].toString();

    if (username.isEmpty() || password.isEmpty()) {
        sendJson(socket, R"({"success": false, "message": "Username and password are required"})");
        return;
    }

    if (!m_testNetworkManager)
        m_testNetworkManager = new QNetworkAccessManager(this);

    QNetworkRequest request(QUrl("https://visualizer.coffee/api/shots?items=1"));
    QString credentials = username + ":" + password;
    request.setRawHeader("Authorization", "Basic " + credentials.toUtf8().toBase64());
    request.setTransferTimeout(15000);

    m_visualizerTestInFlight = true;

    QPointer<QTcpSocket> safeSocket(socket);
    QNetworkReply* reply = m_testNetworkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, safeSocket, reply]() {
        reply->deleteLater();
        m_visualizerTestInFlight = false;
        if (!safeSocket || safeSocket->state() != QAbstractSocket::ConnectedState)
            return;

        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject result;
            result["success"] = true;
            result["message"] = "Connection successful";
            sendJson(safeSocket, QJsonDocument(result).toJson(QJsonDocument::Compact));
        } else if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
            QJsonObject result;
            result["success"] = false;
            result["message"] = "Invalid username or password";
            sendJson(safeSocket, QJsonDocument(result).toJson(QJsonDocument::Compact));
        } else {
            QJsonObject result;
            result["success"] = false;
            result["message"] = "Connection failed: " + reply->errorString();
            sendJson(safeSocket, QJsonDocument(result).toJson(QJsonDocument::Compact));
        }
    });
}

void ShotServer::handleAiTest(QTcpSocket* socket, const QByteArray& body)
{
    if (!m_aiManager || !m_settings) {
        sendJson(socket, R"({"success": false, "message": "AI manager not available"})");
        return;
    }

    if (m_aiTestInFlight) {
        sendJson(socket, R"({"success": false, "message": "A test is already in progress"})");
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        sendJson(socket, R"({"success": false, "message": "Invalid JSON"})");
        return;
    }

    QJsonObject obj = doc.object();

    // Save submitted settings so the AI provider reads from them
    applyAiSettings(m_settings, obj);

    m_aiTestInFlight = true;

    // One-shot connection to testResultChanged with timeout
    auto conn = std::make_shared<QMetaObject::Connection>();
    auto timer = new QTimer(this);
    timer->setSingleShot(true);
    auto fired = std::make_shared<bool>(false);
    QPointer<QTcpSocket> safeSocket(socket);

    auto cleanup = [this, conn, timer, safeSocket, fired](bool success, const QString& message) {
        if (*fired) return;
        *fired = true;
        m_aiTestInFlight = false;
        disconnect(*conn);
        timer->stop();
        timer->deleteLater();
        if (!safeSocket || safeSocket->state() != QAbstractSocket::ConnectedState)
            return;

        QJsonObject result;
        result["success"] = success;
        result["message"] = message;
        sendJson(safeSocket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    };

    *conn = connect(m_aiManager, &AIManager::testResultChanged, this, [this, cleanup]() {
        cleanup(m_aiManager->lastTestSuccess(), m_aiManager->lastTestResult());
    });

    connect(timer, &QTimer::timeout, this, [cleanup]() {
        cleanup(false, "Connection test timed out");
    });
    timer->start(15000);

    m_aiManager->testConnection();
}

void ShotServer::handleMqttConnect(QTcpSocket* socket, const QByteArray& body)
{
    if (!m_mqttClient || !m_settings) {
        sendJson(socket, R"({"success": false, "message": "MQTT client not available"})");
        return;
    }

    if (m_mqttConnectInFlight) {
        sendJson(socket, R"({"success": false, "message": "A connection attempt is already in progress"})");
        return;
    }

    // Save MQTT settings first
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        sendJson(socket, R"({"success": false, "message": "Invalid request body"})");
        return;
    }
    applyMqttSettings(m_settings, doc.object());

    m_mqttConnectInFlight = true;

    // One-shot connection to statusChanged with timeout
    auto conn = std::make_shared<QMetaObject::Connection>();
    auto timer = new QTimer(this);
    timer->setSingleShot(true);
    auto fired = std::make_shared<bool>(false);
    QPointer<QTcpSocket> safeSocket(socket);

    auto cleanup = [this, conn, timer, safeSocket, fired](bool success, const QString& message) {
        if (*fired) return;
        *fired = true;
        m_mqttConnectInFlight = false;
        disconnect(*conn);
        timer->stop();
        timer->deleteLater();
        if (!safeSocket || safeSocket->state() != QAbstractSocket::ConnectedState)
            return;

        QJsonObject result;
        result["success"] = success;
        result["message"] = message;
        sendJson(safeSocket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    };

    *conn = connect(m_mqttClient, &MqttClient::statusChanged, this, [this, cleanup]() {
        QString status = m_mqttClient->status();
        bool connected = m_mqttClient->isConnected();
        // Terminal state = anything except "Connecting" (covers error, denied, refused, etc.)
        if (connected || !status.contains("onnecting", Qt::CaseInsensitive)) {
            cleanup(connected, status);
        }
    });

    connect(timer, &QTimer::timeout, this, [cleanup]() {
        cleanup(false, "Connection timed out");
    });
    timer->start(5000);

    m_mqttClient->connectToBroker();
}

void ShotServer::handleMqttDisconnect(QTcpSocket* socket)
{
    if (!m_mqttClient) {
        sendJson(socket, R"({"success": false, "message": "MQTT client not available"})");
        return;
    }

    m_mqttClient->disconnectFromBroker();
    sendJson(socket, R"({"success": true, "message": "Disconnected"})");
}

void ShotServer::handleMqttStatus(QTcpSocket* socket)
{
    if (!m_mqttClient) {
        sendJson(socket, R"({"connected": false, "status": "MQTT not available"})");
        return;
    }

    QJsonObject result;
    result["connected"] = m_mqttClient->isConnected();
    result["status"] = m_mqttClient->status();
    sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void ShotServer::handleMqttPublishDiscovery(QTcpSocket* socket)
{
    if (!m_mqttClient) {
        sendJson(socket, R"({"success": false, "message": "MQTT client not available"})");
        return;
    }

    if (!m_mqttClient->isConnected()) {
        sendJson(socket, R"({"success": false, "message": "Not connected to MQTT broker"})");
        return;
    }

    m_mqttClient->publishDiscovery();
    sendJson(socket, R"({"success": true})");
}

