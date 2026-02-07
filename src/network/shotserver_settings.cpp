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
#include "version.h"

#include <QNetworkInterface>
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

QString ShotServer::generateSettingsPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>API Keys & Settings - Decenza DE1</title>
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
            padding: 0.75rem 1.5rem;
            border: none;
            border-radius: 6px;
            font-size: 0.9375rem;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.15s;
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
        .save-bar {
            position: sticky;
            bottom: 0;
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            display: flex;
            justify-content: flex-end;
            gap: 1rem;
            align-items: center;
        }
        .status-msg {
            font-size: 0.875rem;
            padding: 0.5rem 0.75rem;
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
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&larr;</a>
            <h1>API Keys & Settings</h1>
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
                    <select class="form-input" id="aiProvider" onchange="updateAiFields()">
                        <option value="">Disabled</option>
                        <option value="openai">OpenAI (GPT-4)</option>
                        <option value="anthropic">Anthropic (Claude)</option>
                        <option value="gemini">Google (Gemini)</option>
                        <option value="openrouter">OpenRouter (Multi)</option>
                        <option value="ollama">Ollama (Local)</option>
                    </select>
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
            </div>
        </div>

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
            </div>
        </div>
    </div>

    <div class="save-bar">
        <span id="statusMsg"></span>
        <button class="btn btn-primary" id="saveBtn" onclick="saveSettings()">Save Settings</button>
    </div>
)HTML" R"HTML(
    <script>
        // Load current settings on page load
        async function loadSettings() {
            try {
                const resp = await fetch('/api/settings');
                const data = await resp.json();

                // Visualizer
                document.getElementById('visualizerUsername').value = data.visualizerUsername || '';
                document.getElementById('visualizerPassword').value = data.visualizerPassword || '';

                // AI
                document.getElementById('aiProvider').value = data.aiProvider || '';
                document.getElementById('openaiApiKey').value = data.openaiApiKey || '';
                document.getElementById('anthropicApiKey').value = data.anthropicApiKey || '';
                document.getElementById('geminiApiKey').value = data.geminiApiKey || '';
                document.getElementById('openrouterApiKey').value = data.openrouterApiKey || '';
                document.getElementById('openrouterModel').value = data.openrouterModel || '';
                document.getElementById('ollamaEndpoint').value = data.ollamaEndpoint || 'http://localhost:11434';
                document.getElementById('ollamaModel').value = data.ollamaModel || 'llama3.2';
                updateAiFields();

                // MQTT
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
            } catch (e) {
                showStatus('Failed to load settings', true);
            }
        }

        function updateAiFields() {
            const provider = document.getElementById('aiProvider').value;
            document.getElementById('openaiGroup').style.display = provider === 'openai' ? 'block' : 'none';
            document.getElementById('anthropicGroup').style.display = provider === 'anthropic' ? 'block' : 'none';
            document.getElementById('geminiGroup').style.display = provider === 'gemini' ? 'block' : 'none';
            document.getElementById('openrouterGroup').style.display = provider === 'openrouter' ? 'block' : 'none';
            document.getElementById('ollamaGroup').style.display = provider === 'ollama' ? 'block' : 'none';
        }

        function updateMqttFields() {
            const enabled = document.getElementById('mqttEnabled').checked;
            document.getElementById('mqttFields').style.display = enabled ? 'block' : 'none';
        }

        function togglePassword(id) {
            const input = document.getElementById(id);
            input.type = input.type === 'password' ? 'text' : 'password';
        }

        async function saveSettings() {
            const btn = document.getElementById('saveBtn');
            btn.disabled = true;
            btn.textContent = 'Saving...';

            const data = {
                // Visualizer
                visualizerUsername: document.getElementById('visualizerUsername').value,
                visualizerPassword: document.getElementById('visualizerPassword').value,

                // AI
                aiProvider: document.getElementById('aiProvider').value,
                openaiApiKey: document.getElementById('openaiApiKey').value,
                anthropicApiKey: document.getElementById('anthropicApiKey').value,
                geminiApiKey: document.getElementById('geminiApiKey').value,
                openrouterApiKey: document.getElementById('openrouterApiKey').value,
                openrouterModel: document.getElementById('openrouterModel').value,
                ollamaEndpoint: document.getElementById('ollamaEndpoint').value,
                ollamaModel: document.getElementById('ollamaModel').value,

                // MQTT
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

            try {
                const resp = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(data)
                });
                const result = await resp.json();
                if (result.success) {
                    showStatus('Settings saved successfully!', false);
                } else {
                    showStatus(result.error || 'Failed to save', true);
                }
            } catch (e) {
                showStatus('Network error', true);
            }

            btn.disabled = false;
            btn.textContent = 'Save Settings';
        }

        function showStatus(msg, isError) {
            const el = document.getElementById('statusMsg');
            el.textContent = msg;
            el.className = 'status-msg ' + (isError ? 'status-error' : 'status-success');
            setTimeout(() => { el.textContent = ''; el.className = ''; }, 4000);
        }

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
    if (obj.contains("aiProvider"))
        m_settings->setAiProvider(obj["aiProvider"].toString());
    if (obj.contains("openaiApiKey"))
        m_settings->setOpenaiApiKey(obj["openaiApiKey"].toString());
    if (obj.contains("anthropicApiKey"))
        m_settings->setAnthropicApiKey(obj["anthropicApiKey"].toString());
    if (obj.contains("geminiApiKey"))
        m_settings->setGeminiApiKey(obj["geminiApiKey"].toString());
    if (obj.contains("openrouterApiKey"))
        m_settings->setOpenrouterApiKey(obj["openrouterApiKey"].toString());
    if (obj.contains("openrouterModel"))
        m_settings->setOpenrouterModel(obj["openrouterModel"].toString());
    if (obj.contains("ollamaEndpoint"))
        m_settings->setOllamaEndpoint(obj["ollamaEndpoint"].toString());
    if (obj.contains("ollamaModel"))
        m_settings->setOllamaModel(obj["ollamaModel"].toString());

    // MQTT
    if (obj.contains("mqttEnabled"))
        m_settings->setMqttEnabled(obj["mqttEnabled"].toBool());
    if (obj.contains("mqttBrokerHost"))
        m_settings->setMqttBrokerHost(obj["mqttBrokerHost"].toString());
    if (obj.contains("mqttBrokerPort"))
        m_settings->setMqttBrokerPort(obj["mqttBrokerPort"].toInt());
    if (obj.contains("mqttUsername"))
        m_settings->setMqttUsername(obj["mqttUsername"].toString());
    if (obj.contains("mqttPassword"))
        m_settings->setMqttPassword(obj["mqttPassword"].toString());
    if (obj.contains("mqttBaseTopic"))
        m_settings->setMqttBaseTopic(obj["mqttBaseTopic"].toString());
    if (obj.contains("mqttPublishInterval"))
        m_settings->setMqttPublishInterval(obj["mqttPublishInterval"].toInt());
    if (obj.contains("mqttClientId"))
        m_settings->setMqttClientId(obj["mqttClientId"].toString());
    if (obj.contains("mqttRetainMessages"))
        m_settings->setMqttRetainMessages(obj["mqttRetainMessages"].toBool());
    if (obj.contains("mqttHomeAssistantDiscovery"))
        m_settings->setMqttHomeAssistantDiscovery(obj["mqttHomeAssistantDiscovery"].toBool());

    sendJson(socket, R"({"success": true})");
}

