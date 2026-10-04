#include "core/diagnosticlogging.h"
#include "shotserver.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "../history/shothistorystorage.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"
#include "../core/settings.h"
#include "../core/settings_mqtt.h"
#include "../core/settings_ai.h"
#include "../core/settings_visualizer.h"
#include "../controllers/maincontroller.h"
#include "decentaccount.h"
#include "visualizeruploader.h"
#include <QMetaEnum>
#include "../core/settings_mcp.h"
#include "../mcp/mcpremoteaccess.h"
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
#include <functional>
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

// Privacy: raw secret VALUES (API keys, passwords, usernames) are never
// emitted over the LAN web server. Instead /api/settings returns a masked
// sentinel when a secret is configured and "" when it is not. The settings
// page shows these values in <input>s, so a boolean alone is insufficient; the
// mask keeps the field visibly "filled" without disclosing the real value.
// The save path (applySecretString below) treats the mask (and empty) as
// "leave unchanged", so the owner can still set a NEW key but the current key
// is never sent to, nor round-tripped from, the browser.
static const QString kSecretMask = QStringLiteral("••••••••"); // "••••••••"

// Redact a secret for outbound JSON: mask if set, empty string if unset. The
// web JS only truthiness-checks these (e.g. isProviderConfigured's !!value), so
// a non-empty mask preserves "configured" indication without leaking the value.
static QString redactedSecret(const QString& value)
{
    return value.isEmpty() ? QString() : kSecretMask;
}

// Apply a posted secret string only when it is a REAL new value: non-empty and
// not the redaction mask we handed the page. This prevents the mask (or a blank
// field the user never touched) from overwriting the stored secret. Consequence:
// a secret cannot be CLEARED via the web UI (empty = "keep current") — this is
// the intended privacy trade-off; clearing is still possible in the native app.
static void applySecretString(const QJsonObject& obj, const QString& key,
                              const std::function<void(const QString&)>& setter)
{
    if (!obj.contains(key))
        return;
    const QString v = obj[key].toString();
    if (v.isEmpty() || v == kSecretMask)
        return;
    setter(v);
}

// Apply AI-related fields from a JSON object to Settings. Only keys present
// in obj are updated; missing keys leave the current setting unchanged.
// providerModels entries are validated against aiManager's provider list and
// per-provider model catalogs before being persisted — without this, a stale
// web tab (or a typo'd API client) could save an id the provider silently
// discards, and the page would still report "Saved". Invalid entries are
// skipped and reported in the returned error list (empty = all applied).
static QStringList applyAiSettings(Settings* s, AIManager* aiManager, const QJsonObject& obj)
{
    QStringList errors;
    auto* a = s->ai();
    if (obj.contains("aiProvider"))
        a->setAiProvider(obj["aiProvider"].toString());
    // Secrets: only overwrite when a real new key is posted (not mask/empty).
    applySecretString(obj, "openaiApiKey",     [a](const QString& v){ a->setOpenaiApiKey(v); });
    applySecretString(obj, "anthropicApiKey",  [a](const QString& v){ a->setAnthropicApiKey(v); });
    applySecretString(obj, "geminiApiKey",     [a](const QString& v){ a->setGeminiApiKey(v); });
    applySecretString(obj, "openrouterApiKey", [a](const QString& v){ a->setOpenrouterApiKey(v); });
    if (obj.contains("openrouterModel"))
        a->setOpenrouterModel(obj["openrouterModel"].toString());
    if (obj.contains("ollamaEndpoint"))
        a->setOllamaEndpoint(obj["ollamaEndpoint"].toString());
    if (obj.contains("ollamaModel"))
        a->setOllamaModel(obj["ollamaModel"].toString());
    if (obj.contains("openaiEndpoint"))
        a->setOpenaiEndpoint(obj["openaiEndpoint"].toString());
    if (obj.contains("anthropicEndpoint"))
        a->setAnthropicEndpoint(obj["anthropicEndpoint"].toString());
    // Per-provider selected model for fixed-catalog providers (see
    // SettingsAI::setProviderModel). Shape: {"gemini": "gemini-2.5-flash"}.
    if (obj["providerModels"].isObject()) {
        const QJsonObject models = obj["providerModels"].toObject();
        for (auto it = models.begin(); it != models.end(); ++it) {
            const QString providerId = it.key();
            if (!aiManager) {
                errors << QStringLiteral("AI manager not available; model selection not applied");
                break;
            }
            if (!it.value().isString()) {
                errors << QStringLiteral("Model for provider '%1' must be a string").arg(providerId);
                continue;
            }
            const QString modelId = it.value().toString();
            if (!aiManager->availableProviders().contains(providerId)) {
                errors << QStringLiteral("Unknown AI provider '%1'").arg(providerId);
                continue;
            }
            // Empty = "use the provider default" and is always legal.
            if (!modelId.isEmpty()) {
                const QVariantList catalog = aiManager->availableModels(providerId);
                bool known = false;
                for (const QVariant& entry : catalog) {
                    if (entry.toMap().value("id").toString() == modelId) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    errors << QStringLiteral("Unknown model '%1' for provider '%2'")
                                  .arg(modelId, providerId);
                    continue;
                }
            }
            a->setProviderModel(providerId, modelId);
        }
    }
    return errors;
}

// Apply MQTT-related fields from a JSON object to Settings. Only keys present
// in obj are updated; missing keys leave the current setting unchanged.
// Returns the user-facing reasons for anything refused (see the guard below), so
// callers can surface them and/or decline to connect. Empty = all applied.
static QStringList applyMqttSettings(Settings* s, const QJsonObject& obj)
{
    QStringList errors;
    auto* m = s->mqtt();
    if (obj.contains("mqttEnabled"))
        m->setMqttEnabled(obj["mqttEnabled"].toBool());

    // Security (broker-redirect guard): host/port are not secrets and are applied
    // verbatim, but the stored MQTT password is emitted to whatever broker we then
    // connect to. Changing mqttBrokerHost also fires mqttBrokerHostChanged, which
    // makes MqttClient reconnect on its own -- so the guard must live HERE, at the
    // shared chokepoint, not only in handleMqttConnect: both /api/settings (save)
    // and the connect endpoint funnel through this function. Refuse to retarget the
    // broker while the password field carries only the mask/empty; otherwise a LAN
    // client could point the broker at an attacker and have the stored password sent
    // there in the CONNECT packet. Compute the decision from the STORED password
    // (m->mqttPassword()) BEFORE applySecretString below overwrites it.
    // TLS turned off and a different CA expose the password the same way a retarget
    // does; SettingsMqtt::passwordExposingChanges() is the one definition (MCP uses it too).
    const QString postedCa = obj.value("mqttCaCertificate").toString().trimmed();
    const bool caChanging = obj.contains("mqttCaCertificate") && postedCa != m->mqttCaCertificate();
    const QString postedPassword = obj.value("mqttPassword").toString();
    const bool passwordReentered = !postedPassword.isEmpty() && postedPassword != kSecretMask;
    const bool brokerRedirectBlocked =
        !m->passwordExposingChanges(obj).isEmpty() && !passwordReentered;

    // Apply the credentials FIRST, before the host/port. Every mqtt* setter here fires
    // a *Changed signal wired to MqttClient::onSettingsChanged() (the SettingsMqtt
    // connects in the MqttClient constructor), which reconnects synchronously -- the connection is same-thread, so the
    // default AutoConnection resolves to a direct call -- and reads the host and
    // password live out of Settings at connect time. The leak-prevention is purely
    // about ORDER, not about any setter being inert: the credential setters DO trigger
    // a reconnect too, but to the still-old (legitimately configured) host, which is
    // harmless. If we instead set the new host before the re-entered password, the
    // reconnect that host change triggers would pair the NEW broker with the OLD stored
    // password -- exactly the leak the guard exists to prevent. Applying credentials
    // first guarantees the post-host-change reconnect reads the new password.
    // Only overwrite when a real new value is posted (not mask/empty).
    applySecretString(obj, "mqttUsername", [m](const QString& v){ m->setMqttUsername(v); });
    applySecretString(obj, "mqttPassword", [m](const QString& v){ m->setMqttPassword(v); });

    if (brokerRedirectBlocked) {
        DIAG_WARN(NETWORK, "ShotServer") << "refused MQTT broker host/port/TLS/CA change without password"
                      " re-entry (broker-redirect guard)";
        errors << QStringLiteral("Re-enter the MQTT password when changing the broker host, port, TLS or CA certificate.");
    } else {
        if (obj.contains("mqttBrokerHost"))
            m->setMqttBrokerHost(obj["mqttBrokerHost"].toString());
        if (obj.contains("mqttBrokerPort"))
            m->setMqttBrokerPort(obj["mqttBrokerPort"].toInt());
        if (obj.contains("mqttUseTls"))
            m->setMqttUseTls(obj["mqttUseTls"].toBool());
        if (caChanging) {
            if (!postedCa.isEmpty() && SettingsMqtt::describeCaCertificate(postedCa).isEmpty())
                errors << QStringLiteral("The MQTT CA certificate is not a PEM certificate, so it was not saved.");
            else
                m->setMqttCaCertificate(postedCa);
        }
    }
    if (obj.contains("mqttBaseTopic"))
        m->setMqttBaseTopic(obj["mqttBaseTopic"].toString());
    if (obj.contains("mqttPublishInterval"))
        m->setMqttPublishInterval(obj["mqttPublishInterval"].toInt());
    if (obj.contains("mqttClientId"))
        m->setMqttClientId(obj["mqttClientId"].toString());
    if (obj.contains("mqttRetainMessages"))
        m->setMqttRetainMessages(obj["mqttRetainMessages"].toBool());
    if (obj.contains("mqttHomeAssistantDiscovery"))
        m->setMqttHomeAssistantDiscovery(obj["mqttHomeAssistantDiscovery"].toBool());
    return errors;
}

// Apply MCP-related fields (local server + remote connector) from a JSON object
// to Settings. Only keys present in obj are updated. The setters fire *Changed
// signals that McpRemoteAccess::refresh() is wired to, so toggling remote access
// or switching mode from the web page starts/stops the tunnel automatically —
// no separate action endpoint needed. Access/confirmation levels are clamped to
// the valid 0..2 range; an unknown remote mode is ignored (left unchanged).
// Returns an error list (empty = all applied).
static QStringList applyMcpSettings(Settings* s, const QJsonObject& obj)
{
    QStringList errors;
    auto* m = s->mcp();

    if (obj.contains("mcpEnabled"))
        m->setMcpEnabled(obj["mcpEnabled"].toBool());
    if (obj.contains("mcpAccessLevel"))
        m->setMcpAccessLevel(std::clamp(obj["mcpAccessLevel"].toInt(), 0, 2));
    if (obj.contains("mcpConfirmationLevel"))
        m->setMcpConfirmationLevel(std::clamp(obj["mcpConfirmationLevel"].toInt(), 0, 2));

    if (obj.contains("remoteMcpEnabled"))
        m->setRemoteMcpEnabled(obj["remoteMcpEnabled"].toBool());
    if (obj.contains("remoteMcpMode")) {
        const QString mode = obj["remoteMcpMode"].toString();
        if (mode == QLatin1String(SettingsMcp::ModeCustom)
            || mode == QLatin1String(SettingsMcp::ModeTailscale)) {
            m->setRemoteMcpMode(mode);
        } else {
            errors << QStringLiteral("Unknown remote MCP mode '%1'").arg(mode);
        }
    }
    if (obj.contains("remoteMcpCustomBaseUrl"))
        m->setRemoteMcpCustomBaseUrl(obj["remoteMcpCustomBaseUrl"].toString().trimmed());
    return errors;
}

// One destination card of the Shot Upload settings, as the app's
// UploadDestinationCard: its switch, then Connect or Disconnect.
static QString uploadAccountSection(const QString& dest, const QString& icon, const QString& title,
                                    const QString& description, const QString& identityLabel)
{
    return QStringLiteral(R"HTML(
        <div class="section">
            <div class="section-header">
                <span class="section-icon">%2</span>
                <h2>%3</h2>
            </div>
            <div class="section-body">
                <div class="form-group">
                    <label class="form-checkbox">
                        <input type="checkbox" id="%1-enabled" onchange="saveUploadSetting('%1Enabled', this.checked)">
                        <span>Upload shots to %3</span>
                    </label>
                    <div class="form-hint">%4</div>
                </div>
                <div class="form-hint" id="%1-state"></div>
                <div id="%1-connected" style="display:none;">
                    <div class="section-actions">
                        <button class="btn btn-secondary" onclick="disconnectAccount('%1')">Disconnect</button>
                    </div>
                </div>
                <div id="%1-signin">
                    <div class="form-group">
                        <label class="form-label">%5</label>
                        <input type="text" class="form-input" id="%1-identity" placeholder="your@email.com" autocomplete="username">
                    </div>
                    <div class="form-group">
                        <label class="form-label">Password</label>
                        <div class="password-wrapper">
                            <input type="password" class="form-input" id="%1-password" placeholder="Enter password" autocomplete="current-password">
                            <button type="button" class="password-toggle" onclick="togglePassword('%1-password')">&#128065;</button>
                        </div>
                    </div>
                    <div class="section-actions">
                        <button class="btn btn-primary" id="%1-connect" onclick="connectAccount('%1')">Connect</button>
                    </div>
                </div>
                <div id="%1-status" class="status-msg"></div>
            </div>
        </div>
)HTML").arg(dest, icon, title, description, identityLabel);
}

QString ShotServer::generateSettingsPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>AI and Web Service Connections - Decenza</title>
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
        .section-desc {
            margin: 0 0 1rem;
            font-size: 0.85rem;
            color: #94a3b8;
            line-height: 1.4;
        }
        .field-hint {
            margin: 0.35rem 0 0;
            font-size: 0.78rem;
            color: #94a3b8;
            line-height: 1.35;
        }
        .link-line {
            display: inline-block;
            color: #60a5fa;
            text-decoration: none;
            font-size: 0.9rem;
            word-break: break-all;
        }
        .link-line:hover { text-decoration: underline; }
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
        /* Own row under the actions: the MQTT row's live status and three buttons leave no room. */
        .status-line { display: block; margin-top: 0.5rem; }
        .status-line:empty { display: none; }
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
        .form-hint {
            font-size: 0.8125rem;
            color: var(--text-secondary);
            margin-top: 0.25rem;
        }
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&larr;</a>
            <h1>AI and Web Service Connections</h1>
        </div>
    </header>

    <div class="container">
        <!-- Shot Upload: a card per destination, then one set of upload settings -->
        <!--UPLOAD_ACCOUNT:visualizer-->
        <!--UPLOAD_ACCOUNT:decent-->
        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#9881;</span>
                <h2>Upload Settings</h2>
            </div>
            <div class="section-body">
                <div class="form-hint">When shots are uploaded, for every destination that is switched on</div>
                <div class="form-group">
                    <label class="form-checkbox">
                        <input type="checkbox" id="uploadAutomatically" onchange="saveUploadSetting('uploadAutomatically', this.checked)">
                        <span>Auto-upload shots: upload espresso shots after completion</span>
                    </label>
                </div>
                <div class="form-group">
                    <label class="form-checkbox">
                        <input type="checkbox" id="updateAutomatically" onchange="saveUploadSetting('updateAutomatically', this.checked)">
                        <span>Auto-update shots: re-send a shot after you edit it</span>
                    </label>
                </div>
                <div class="form-group">
                    <label class="form-label">Minimum upload duration (seconds): skip aborted shots</label>
                    <input type="number" class="form-input" id="uploadMinDurationSec" min="0" max="60" step="1"
                           onchange="saveUploadSetting('uploadMinDurationSec', Number(this.value))">
                </div>
                <div id="uploadSettingsStatus" class="status-msg"></div>
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
                            <div class="provider-model" id="openaiBtnModel">GPT</div>
                        </button>
                        <button type="button" class="provider-btn" data-provider="anthropic" onclick="selectProvider('anthropic')">
                            <div class="provider-name">Anthropic</div>
                            <div class="provider-model" id="anthropicBtnModel">Claude</div>
                        </button>
                        <button type="button" class="provider-btn" data-provider="gemini" onclick="selectProvider('gemini')">
                            <div class="provider-name">Gemini</div>
                            <div class="provider-model" id="geminiBtnModel">Gemini</div>
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
                    <label class="form-label" style="margin-top:8px">Custom Endpoint (optional)</label>
                    <input type="text" class="form-input" id="openaiEndpoint" placeholder="https://api.openai.com">
                </div>
                <div class="form-group" id="anthropicGroup" style="display:none;">
                    <label class="form-label">Anthropic API Key</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="anthropicApiKey" placeholder="sk-ant-...">
                        <button type="button" class="password-toggle" onclick="togglePassword('anthropicApiKey')">&#128065;</button>
                    </div>
                    <div class="help-text">Get your API key from <a href="https://console.anthropic.com/settings/keys" target="_blank" style="color:var(--accent)">console.anthropic.com</a></div>
                    <label class="form-label" style="margin-top:8px">Custom Endpoint (optional)</label>
                    <input type="text" class="form-input" id="anthropicEndpoint" placeholder="https://api.anthropic.com">
                </div>
                <div class="form-group" id="geminiGroup" style="display:none;">
                    <label class="form-label">Google Gemini API Key</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-input" id="geminiApiKey" placeholder="AI...">
                        <button type="button" class="password-toggle" onclick="togglePassword('geminiApiKey')">&#128065;</button>
                    </div>
                    <div class="help-text">Get your API key from <a href="https://aistudio.google.com/apikey" target="_blank" style="color:var(--accent)">aistudio.google.com</a></div>
                </div>
                <!-- Model picker for providers exposing a fixed catalog of >1 model
                     (mirrors the in-app AI settings tab; OpenAI/Anthropic/Gemini today). -->
                <div class="form-group" id="modelGroup" style="display:none;">
                    <label class="form-label">Model</label>
                    <select class="form-input" id="providerModelSelect" onchange="onModelSelected()"></select>
                    <div class="help-text" id="modelHint" style="display:none;"></div>
                    <div class="help-text" id="modelCost" style="display:none;"></div>
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
                    <div class="form-group">
                        <label class="form-checkbox">
                            <input type="checkbox" id="mqttUseTls" onchange="onMqttTlsToggled()">
                            <span>Encrypted connection (TLS)</span>
                        </label>
                    </div>
                    <div class="form-group" id="mqttCaGroup" style="display:none;">
                        <label class="form-label" for="mqttCaCertificate">CA certificate (optional, for a self-signed broker)</label>
                        <textarea class="form-input" id="mqttCaCertificate" rows="4" spellcheck="false"
                                  style="font-family: monospace; font-size: 0.75rem;"
                                  placeholder="-----BEGIN CERTIFICATE-----"></textarea>
                        <input type="file" id="mqttCaFile" accept=".pem,.crt,.cer,text/plain" onchange="loadMqttCaFile(this)"
                               aria-label="Load CA certificate from a file" style="margin-top: 0.5rem;">
                        <div id="mqttCaSummary" style="font-size: 0.8125rem; color: var(--text-secondary); margin-top: 0.25rem;"></div>
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
                            <input type="number" class="form-input" id="mqttPublishInterval" placeholder="1" min="0.1" step="0.1">
                        </div>
                        <div class="form-group">
                            <label class="form-label">Client ID (optional)</label>
                            <input type="text" class="form-input" id="mqttClientId" placeholder="decenza_de1">
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="form-label" for="mqttDeviceId">Home Assistant device ID</label>
                        <div style="display:flex; gap:0.5rem;">
                            <input type="text" class="form-input" id="mqttDeviceId" readonly>
                            <button type="button" class="btn btn-secondary" onclick="newMqttDeviceId()">New device ID</button>
                        </div>
                        <div style="font-size: 0.8125rem; color: var(--text-secondary); margin-top: 0.25rem;">
                            Only for a second device restored from the same backup: it then shows up in Home Assistant as its own device.
                            The client ID must be unique on the broker.
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
                <span id="mqttSaveStatus" class="status-msg status-line"></span>
            </div>
        </div>

        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#128268;</span>
                <h2>MCP Server (AI control)</h2>
            </div>
            <div class="section-body">
                <p class="section-desc">Let an AI assistant (Claude, ChatGPT) read from and control this machine. Local clients on your network connect to the URL below.</p>
                <div class="form-group">
                    <label class="form-checkbox">
                        <input type="checkbox" id="mcpEnabled" onchange="updateMcpFields()">
                        <span>Enable MCP server</span>
                    </label>
                </div>
                <div id="mcpFields" style="display:none;">
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label">Access Level</label>
                            <select class="form-input" id="mcpAccessLevel">
                                <option value="0">Monitor Only — read state, history, profiles</option>
                                <option value="1">Control — monitor + start/stop, wake/sleep</option>
                                <option value="2">Full Automation — control + upload profiles, change settings</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label class="form-label">Confirmation</label>
                            <select class="form-input" id="mcpConfirmationLevel">
                                <option value="0">None — commands execute immediately</option>
                                <option value="1">Dangerous Only — confirm operations &amp; writes</option>
                                <option value="2">All Control — confirm every control/write</option>
                            </select>
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="form-label">Setup page</label>
                        <a id="mcpSetupLink" href="/mcp/setup" target="_blank" rel="noopener" class="link-line">Open setup page &#8599;</a>
                        <p class="field-hint">Open this on the computer running Claude Desktop for a ready-to-paste configuration. The Local URL below is for manual clients (curl, MCP Inspector).</p>
                    </div>
                    <div class="form-group">
                        <label class="form-label">Local URL (for clients on this network)</label>
                        <div class="password-wrapper">
                            <input type="text" class="form-input" id="mcpLocalUrl" readonly placeholder="Save &amp; enable to generate">
                            <button type="button" class="password-toggle" onclick="copyField('mcpLocalUrl')" title="Copy">&#128203;</button>
                        </div>
                    </div>
                </div>
                <div class="section-actions">
                    <span id="mcpStatus" class="status-msg"></span>
                    <button class="btn btn-primary" id="mcpSaveBtn" onclick="saveMcp()">Save</button>
                </div>
            </div>
        </div>

        <div class="section">
            <div class="section-header">
                <span class="section-icon">&#127760;</span>
                <h2>Remote Access (from anywhere)</h2>
            </div>
            <div class="section-body">
                <p class="section-desc">Reach the MCP server over the public internet — e.g. from the Claude or ChatGPT mobile apps when you're away from home. Off by default. Only the AI connector is exposed, protected by a secret token you can rotate.</p>
                <div class="form-group">
                    <label class="form-checkbox">
                        <input type="checkbox" id="remoteMcpEnabled" onchange="updateRemoteMcpFields()">
                        <span>Enable remote access</span>
                    </label>
                </div>
                <div id="remoteMcpFields" style="display:none;">
                    <div class="form-group">
                        <label class="form-label">Method</label>
                        <select class="form-input" id="remoteMcpMode" onchange="updateRemoteMcpFields()">
                            <option value="tailscale">Tailscale (built-in) — private public link, one-time setup</option>
                            <option value="custom">Custom — your own tunnel / reverse-proxy URL</option>
                        </select>
                    </div>
                    <div class="form-group" id="remoteMcpCustomGroup">
                        <label class="form-label">Public base URL</label>
                        <input type="text" class="form-input" id="remoteMcpCustomBaseUrl" placeholder="https://your-domain.example.com">
                        <p class="field-hint">The externally reachable HTTPS address that forwards to this device's MCP port.</p>
                    </div>
                    <div class="form-group" id="remoteMcpLoginGroup" style="display:none;">
                        <label class="form-label">Tailscale sign-in</label>
                        <a id="remoteMcpLoginLink" href="#" target="_blank" rel="noopener" class="link-line">Open sign-in page &#8599;</a>
                        <p class="field-hint">Open this once to authorize this device on your tailnet.</p>
                    </div>
                    <div class="form-group">
                        <label class="form-label">Connector URL (paste into your AI app)</label>
                        <div class="password-wrapper">
                            <input type="text" class="form-input" id="remoteMcpConnectorUrl" readonly placeholder="Appears once the link is reachable">
                            <button type="button" class="password-toggle" onclick="copyField('remoteMcpConnectorUrl')" title="Copy">&#128203;</button>
                        </div>
                        <p class="field-hint" id="remoteMcpStatusLine"></p>
                    </div>
                </div>
                <div class="section-actions">
                    <span id="remoteMcpStatus" class="status-msg"></span>
                    <button class="btn btn-secondary" id="remoteMcpRotateBtn" onclick="rotateRemoteMcpToken()" style="display:none;">Rotate token</button>
                    <button class="btn btn-primary" id="remoteMcpSaveBtn" onclick="saveRemoteMcp()">Save</button>
                </div>
            </div>
        </div>
    </div>
)HTML" R"HTML(
    <script>
        let mqttPollTimer = null;
        let remoteMcpTunnelAvailable = false;
        let selectedProvider = '';
        let modelCatalogs = {};      // providerId -> [{id, name}] (multi-model providers only)
        let selectedModels = {};     // providerId -> selected model id ('' = provider default)
        let modelDisplayNames = {};  // providerId -> current model short label
        let modelHints = {};         // providerId -> guidance line under the model picker
        let modelCosts = {};         // providerId -> { modelId -> running-cost line }

        async function loadSettings() {
            try {
                const resp = await fetch('/api/settings');
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const data = await resp.json();

                showUploadAccount('visualizer', data.visualizerEnabled, data.visualizerConnected, false);
                showUploadAccount('decent', data.decentEnabled, data.decentAccountState === 'Linked',
                                  data.decentAccountState === 'NeedsSignIn');
                document.getElementById('uploadAutomatically').checked = !!data.uploadAutomatically;
                document.getElementById('updateAutomatically').checked = !!data.updateAutomatically;
                document.getElementById('uploadMinDurationSec').value = data.uploadMinDurationSec ?? 0;

                document.getElementById('openaiApiKey').value = data.openaiApiKey || '';
                document.getElementById('anthropicApiKey').value = data.anthropicApiKey || '';
                document.getElementById('geminiApiKey').value = data.geminiApiKey || '';
                document.getElementById('openrouterApiKey').value = data.openrouterApiKey || '';
                document.getElementById('openrouterModel').value = data.openrouterModel || '';
                document.getElementById('ollamaEndpoint').value = data.ollamaEndpoint || '';
                document.getElementById('ollamaModel').value = data.ollamaModel || '';
                document.getElementById('openaiEndpoint').value = data.openaiEndpoint || '';
                document.getElementById('anthropicEndpoint').value = data.anthropicEndpoint || '';
                modelCatalogs = data.providerModelCatalogs || {};
                selectedModels = data.providerModels || {};
                modelDisplayNames = data.providerModelNames || {};
                modelHints = data.providerModelHints || {};
                modelCosts = data.providerModelCosts || {};
                selectProvider(data.aiProvider || '');

                document.getElementById('mqttEnabled').checked = data.mqttEnabled || false;
                document.getElementById('mqttBrokerHost').value = data.mqttBrokerHost || '';
                document.getElementById('mqttBrokerPort').value = data.mqttBrokerPort || 1883;
                document.getElementById('mqttUsername').value = data.mqttUsername || '';
                document.getElementById('mqttPassword').value = data.mqttPassword || '';
                document.getElementById('mqttBaseTopic').value = data.mqttBaseTopic || 'decenza';
                document.getElementById('mqttPublishInterval').value = (data.mqttPublishInterval || 1000) / 1000;
                document.getElementById('mqttClientId').value = data.mqttClientId || '';
                document.getElementById('mqttRetainMessages').checked = data.mqttRetainMessages || false;
                document.getElementById('mqttHomeAssistantDiscovery').checked = data.mqttHomeAssistantDiscovery || false;
                document.getElementById('mqttUseTls').checked = data.mqttUseTls || false;
                document.getElementById('mqttDeviceId').value = data.mqttDeviceId || '';
                document.getElementById('mqttCaCertificate').value = data.mqttCaCertificate || '';
                document.getElementById('mqttCaSummary').textContent = data.mqttCaCertificateSummary
                    ? 'Trusting: ' + data.mqttCaCertificateSummary
                    : "Using the system's trusted certificates.";
                document.getElementById('mqttCaGroup').style.display = data.mqttUseTls ? 'block' : 'none';
                updateMqttFields();

                // MCP — local server
                document.getElementById('mcpEnabled').checked = data.mcpEnabled || false;
                document.getElementById('mcpAccessLevel').value = String(data.mcpAccessLevel ?? 0);
                document.getElementById('mcpConfirmationLevel').value = String(data.mcpConfirmationLevel ?? 0);
                document.getElementById('mcpLocalUrl').value = data.mcpLocalUrl || '';
                updateMcpFields();

                // MCP — remote connector
                remoteMcpTunnelAvailable = !!data.remoteMcpTunnelAvailable;
                document.getElementById('remoteMcpEnabled').checked = data.remoteMcpEnabled || false;
                document.getElementById('remoteMcpMode').value = data.remoteMcpMode || 'tailscale';
                document.getElementById('remoteMcpCustomBaseUrl').value = data.remoteMcpCustomBaseUrl || '';
                document.getElementById('remoteMcpConnectorUrl').value = data.remoteMcpConnectorUrl || '';
                const loginUrl = data.remoteMcpLoginUrl || '';
                const loginLink = document.getElementById('remoteMcpLoginLink');
                loginLink.href = loginUrl || '#';
                loginLink.dataset.url = loginUrl;
                document.getElementById('remoteMcpStatusLine').textContent =
                    remoteStatusLine(data.remoteMcpStatus, data.remoteMcpStatusDetail);
                updateRemoteMcpFields();

                pollMqttStatus();
                startMqttPolling();
            } catch (e) {
                showSectionStatus('uploadSettingsStatus', 'Failed to load settings', true);
                showSectionStatus('aiStatus', 'Failed to load settings', true);
                showSectionStatus('mqttSaveStatus', 'Failed to load settings', true);
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
            updateModelGroup();
            updateProviderButtons();
        }

        // Show the generic model dropdown when the selected provider has a
        // fixed catalog of >1 model. Unset/stale selection falls back to the
        // catalog's first entry (the recommended default), matching the app --
        // and matching the wire model, since every provider's constructor
        // defaults m_model to its catalog's first entry.
        function updateModelGroup() {
            const catalog = modelCatalogs[selectedProvider] || [];
            document.getElementById('modelGroup').style.display = catalog.length > 1 ? 'block' : 'none';
            const hint = document.getElementById('modelHint');
            hint.textContent = modelHints[selectedProvider] || '';
            hint.style.display = hint.textContent ? 'block' : 'none';
            // Before the early return: a single-model provider still needs the
            // stale line from the previous provider cleared.
            updateModelCost();
            if (catalog.length <= 1) return;
            const sel = document.getElementById('providerModelSelect');
            sel.innerHTML = '';
            const stored = selectedModels[selectedProvider];
            const current = catalog.some(m => m.id === stored) ? stored : catalog[0].id;
            catalog.forEach(m => {
                const opt = document.createElement('option');
                opt.value = m.id;
                opt.textContent = m.name;
                opt.selected = m.id === current;
                sel.appendChild(opt);
            });
        }

        // Running-cost line for the SELECTED model. Keyed by model, not
        // provider: the OpenAI catalog alone spans 10x, so one figure per
        // provider is wrong for most of it.
        function updateModelCost() {
            const el = document.getElementById('modelCost');
            const perModel = modelCosts[selectedProvider] || {};
            const catalog = modelCatalogs[selectedProvider] || [];
            const stored = selectedModels[selectedProvider];
            const current = catalog.some(m => m.id === stored)
                ? stored : (catalog.length ? catalog[0].id : '');
            el.textContent = perModel[current] || '';
            el.style.display = el.textContent ? 'block' : 'none';
        }

        function onModelSelected() {
            selectedModels[selectedProvider] = document.getElementById('providerModelSelect').value;
            updateModelCost();
            updateProviderButtons();
        }

        // Sublabel under a provider button: the catalog display name of the
        // (possibly unsaved) selection when a catalog exists, else the current
        // model name reported by the app.
        function providerModelLabel(id, fallback) {
            const catalog = modelCatalogs[id] || [];
            if (catalog.length) {
                const opt = catalog.find(m => m.id === selectedModels[id]) || catalog[0];
                return opt.name;
            }
            return modelDisplayNames[id] || fallback;
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
            document.getElementById('openaiBtnModel').textContent = providerModelLabel('openai', 'GPT');
            document.getElementById('anthropicBtnModel').textContent = providerModelLabel('anthropic', 'Claude');
            document.getElementById('geminiBtnModel').textContent = providerModelLabel('gemini', 'Gemini');
            const orModel = document.getElementById('openrouterModel').value;
            document.getElementById('openrouterBtnModel').textContent = orModel || 'Multi';
            const olModel = document.getElementById('ollamaModel').value;
            document.getElementById('ollamaBtnModel').textContent = olModel || 'Local';
        }

        function updateMqttFields() {
            const enabled = document.getElementById('mqttEnabled').checked;
            document.getElementById('mqttFields').style.display = enabled ? 'block' : 'none';
        }

        function updateMcpFields() {
            const enabled = document.getElementById('mcpEnabled').checked;
            document.getElementById('mcpFields').style.display = enabled ? 'block' : 'none';
        }

        function remoteStatusLine(status, detail) {
            if (!status) return '';
            const labels = { off: 'Off', starting: 'Starting…', publishing: 'Funnel is on',
                             active: 'Active — public link is live',
                             reconnecting: 'Reconnecting…', error: 'Problem' };
            let line = labels[status] || status;
            if (detail) line += ' — ' + detail;
            return line;
        }

        function updateRemoteMcpFields() {
            const enabled = document.getElementById('remoteMcpEnabled').checked;
            document.getElementById('remoteMcpFields').style.display = enabled ? 'block' : 'none';

            // Hide the built-in Tailscale option on builds that don't ship it.
            const modeSel = document.getElementById('remoteMcpMode');
            const tsOption = modeSel.querySelector('option[value="tailscale"]');
            if (tsOption) {
                tsOption.disabled = !remoteMcpTunnelAvailable;
                tsOption.hidden = !remoteMcpTunnelAvailable;
            }
            if (!remoteMcpTunnelAvailable && modeSel.value === 'tailscale')
                modeSel.value = 'custom';

            const mode = modeSel.value;
            document.getElementById('remoteMcpCustomGroup').style.display = mode === 'custom' ? 'block' : 'none';
            // Show the Tailscale sign-in link only in Tailscale mode when a login URL exists.
            const loginUrl = document.getElementById('remoteMcpLoginLink').dataset.url || '';
            document.getElementById('remoteMcpLoginGroup').style.display =
                (mode === 'tailscale' && loginUrl) ? 'block' : 'none';
            // Rotate is meaningful once there's a token in play (connector URL present).
            const hasConnector = !!document.getElementById('remoteMcpConnectorUrl').value;
            document.getElementById('remoteMcpRotateBtn').style.display = hasConnector ? 'inline-block' : 'none';
        }

        async function copyField(id) {
            const el = document.getElementById(id);
            if (!el.value) return;
            // This page is served over plain LAN HTTP, where navigator.clipboard is
            // unavailable (non-secure context) — so the execCommand fallback is the
            // common path, not a rare one. Report both outcomes; a silently-failed
            // copy of the connector URL would otherwise let the user paste stale text.
            const statusId = (id === 'remoteMcpConnectorUrl') ? 'remoteMcpStatus' : 'mcpStatus';
            let ok = false;
            try {
                await navigator.clipboard.writeText(el.value);
                ok = true;
            } catch (e) {
                el.focus(); el.select();
                ok = document.execCommand('copy');
            }
            showSectionStatus(statusId, ok ? 'Copied' : 'Copy failed — select the field and copy manually', !ok);
        }

        function togglePassword(id) {
            const input = document.getElementById(id);
            input.type = input.type === 'password' ? 'text' : 'password';
        }

        function showSectionStatus(id, msg, isError) {
            const el = document.getElementById(id);
            el.textContent = msg;
            el.classList.remove('status-error', 'status-success');
            el.classList.add(isError ? 'status-error' : 'status-success');
            clearTimeout(el.clearTimer);
            el.clearTimer = setTimeout(() => {
                el.textContent = '';
                el.classList.remove('status-error', 'status-success');
            }, 4000);
        }
)HTML" R"HTML(
        // --- Shot Upload: one set of functions for every destination ---
        function showUploadAccount(dest, enabled, connected, needsSignIn) {
            document.getElementById(dest + '-enabled').checked = !!enabled;
            document.getElementById(dest + '-connected').style.display = connected ? '' : 'none';
            document.getElementById(dest + '-signin').style.display = connected ? 'none' : '';
            document.getElementById(dest + '-state').textContent = connected ? 'Connected'
                : needsSignIn ? 'Sign in again: the server no longer accepts the saved sign-in' : '';
        }

        async function postJson(url, body) {
            const resp = await fetch(url, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            });
            if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
            return resp.json();
        }

        async function saveUploadSetting(key, value) {
            try {
                const r = await postJson('/api/settings', { [key]: value });
                showSectionStatus('uploadSettingsStatus', r.success ? 'Saved' : (r.error || 'Failed'), !r.success);
            } catch (e) { showSectionStatus('uploadSettingsStatus', e.message || 'Network error', true); }
        }

        async function connectAccount(dest) {
            const btn = document.getElementById(dest + '-connect');
            const password = document.getElementById(dest + '-password');
            btn.disabled = true; btn.textContent = 'Connecting...';
            try {
                const r = await postJson('/api/settings/' + dest + '/connect', {
                    identity: document.getElementById(dest + '-identity').value,
                    password: password.value
                });
                showSectionStatus(dest + '-status', r.message, !r.success);
                if (r.success) { password.value = ''; loadSettings(); }
            } catch (e) { showSectionStatus(dest + '-status', e.message || 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Connect';
        }

        async function disconnectAccount(dest) {
            try {
                await postJson('/api/settings/' + dest + '/disconnect', {});
                loadSettings();
            } catch (e) { showSectionStatus(dest + '-status', e.message || 'Network error', true); }
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
                        ollamaModel: document.getElementById('ollamaModel').value,
                        openaiEndpoint: document.getElementById('openaiEndpoint').value,
                        anthropicEndpoint: document.getElementById('anthropicEndpoint').value,
                        providerModels: selectedModels
                    })
                });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                showSectionStatus('aiStatus', r.success ? 'Saved' : (r.error || 'Failed'), !r.success);
            } catch (e) { showSectionStatus('aiStatus', e.message || 'Network error', true); }
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
                        ollamaModel: document.getElementById('ollamaModel').value,
                        openaiEndpoint: document.getElementById('openaiEndpoint').value,
                        anthropicEndpoint: document.getElementById('anthropicEndpoint').value,
                        providerModels: selectedModels
                    })
                });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                showSectionStatus('aiStatus', r.message, !r.success);
            } catch (e) { showSectionStatus('aiStatus', e.message || 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Test Connection';
        }
)HTML" R"HTML(
        // --- MQTT ---
        // The one place the form is read: Save and Connect both post exactly this.
        // The interval is shown in seconds but stored in milliseconds.
        function mqttFormValues() {
            const seconds = parseFloat(document.getElementById('mqttPublishInterval').value);
            return {
                mqttEnabled: document.getElementById('mqttEnabled').checked,
                mqttBrokerHost: document.getElementById('mqttBrokerHost').value,
                mqttBrokerPort: parseInt(document.getElementById('mqttBrokerPort').value) || 1883,
                mqttUsername: document.getElementById('mqttUsername').value,
                mqttPassword: document.getElementById('mqttPassword').value,
                mqttBaseTopic: document.getElementById('mqttBaseTopic').value,
                mqttPublishInterval: seconds > 0 ? Math.max(100, Math.round(seconds * 1000)) : 1000,
                mqttClientId: document.getElementById('mqttClientId').value,
                mqttRetainMessages: document.getElementById('mqttRetainMessages').checked,
                mqttHomeAssistantDiscovery: document.getElementById('mqttHomeAssistantDiscovery').checked,
                mqttUseTls: document.getElementById('mqttUseTls').checked,
                mqttCaCertificate: document.getElementById('mqttCaCertificate').value.trim()
            };
        }

        // Follow the checkbox to the standard port, but leave a custom one alone (as the app does).
        function onMqttTlsToggled() {
            const tls = document.getElementById('mqttUseTls').checked;
            const port = document.getElementById('mqttBrokerPort');
            if (tls && port.value === '1883') port.value = '8883';
            else if (!tls && port.value === '8883') port.value = '1883';
            document.getElementById('mqttCaGroup').style.display = tls ? 'block' : 'none';
        }

        function loadMqttCaFile(input) {
            const file = input.files && input.files[0];
            if (!file) return;
            const reader = new FileReader();
            reader.onload = () => {
                document.getElementById('mqttCaCertificate').value = String(reader.result).trim();
                document.getElementById('mqttCaSummary').textContent = 'Loaded ' + file.name + ' - Save to apply.';
            };
            reader.readAsText(file);
        }

        async function saveMqtt() {
            const btn = document.getElementById('mqttSaveBtn');
            btn.disabled = true; btn.textContent = 'Saving...';
            try {
                const resp = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(mqttFormValues())
                });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                showSectionStatus('mqttSaveStatus', r.success ? 'Saved' : (r.error || 'Failed'), !r.success);
            } catch (e) { showSectionStatus('mqttSaveStatus', e.message || 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Save';
        }

        async function saveMcp() {
            const btn = document.getElementById('mcpSaveBtn');
            btn.disabled = true; btn.textContent = 'Saving...';
            try {
                const resp = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        mcpEnabled: document.getElementById('mcpEnabled').checked,
                        mcpAccessLevel: parseInt(document.getElementById('mcpAccessLevel').value) || 0,
                        mcpConfirmationLevel: parseInt(document.getElementById('mcpConfirmationLevel').value) || 0
                    })
                });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                showSectionStatus('mcpStatus', r.success ? 'Saved' : (r.error || 'Failed'), !r.success);
                if (r.success) await loadSettings();  // refresh derived URL / key fields
            } catch (e) { showSectionStatus('mcpStatus', e.message || 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Save';
        }

        async function saveRemoteMcp() {
            const btn = document.getElementById('remoteMcpSaveBtn');
            btn.disabled = true; btn.textContent = 'Saving...';
            try {
                const resp = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        remoteMcpEnabled: document.getElementById('remoteMcpEnabled').checked,
                        remoteMcpMode: document.getElementById('remoteMcpMode').value,
                        remoteMcpCustomBaseUrl: document.getElementById('remoteMcpCustomBaseUrl').value
                    })
                });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                showSectionStatus('remoteMcpStatus', r.success ? 'Saved' : (r.error || 'Failed'), !r.success);
                // Reload so the connector URL / login link / status reflect the new
                // mode. (Tailscale needs a moment to become reachable — the status
                // line shows progress; re-open the page or Save again to refresh.)
                if (r.success) await loadSettings();
            } catch (e) { showSectionStatus('remoteMcpStatus', e.message || 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Save';
        }

        async function rotateRemoteMcpToken() {
            if (!confirm('Rotate the token? The current connector URL stops working immediately — you\'ll need to update it in your AI app.')) return;
            const btn = document.getElementById('remoteMcpRotateBtn');
            btn.disabled = true; btn.textContent = 'Rotating...';
            try {
                const resp = await fetch('/api/settings/mcp/rotate-token', { method: 'POST' });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                showSectionStatus('remoteMcpStatus', r.success ? 'Token rotated' : (r.error || 'Failed'), !r.success);
                if (r.success) await loadSettings();
            } catch (e) { showSectionStatus('remoteMcpStatus', e.message || 'Network error', true); }
            btn.disabled = false; btn.textContent = 'Rotate token';
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
                    body = mqttFormValues();
                }
                const resp = await fetch(endpoint, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(body)
                });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                if (!r.success) {
                    updateMqttStatusUI(wasConnect ? false : true,
                        r.message || (wasConnect ? 'Connection failed' : 'Disconnect failed'));
                }
            } catch (e) {
                updateMqttStatusUI(false, e.message || 'Network error');
            }
            btn.disabled = false;
            pollMqttStatus();
        }

        async function newMqttDeviceId() {
            if (!confirm('Give this device a new Home Assistant device ID? It will appear in Home Assistant as a separate device.')) return;
            try {
                const resp = await fetch('/api/settings/mqtt/new-device-id', { method: 'POST' });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                if (r.success) document.getElementById('mqttDeviceId').value = r.deviceId || '';
                else showSectionStatus('mqttSaveStatus', r.message || 'Failed', true);
            } catch (e) { showSectionStatus('mqttSaveStatus', e.message || 'Network error', true); }
        }

        async function publishDiscovery() {
            const btn = document.getElementById('mqttDiscoveryBtn');
            btn.disabled = true; btn.textContent = 'Publishing...';
            try {
                const resp = await fetch('/api/settings/mqtt/publish-discovery', { method: 'POST' });
                if (!resp.ok) throw new Error('Server error (' + resp.status + ')');
                const r = await resp.json();
                btn.textContent = r.success ? 'Published!' : 'Failed';
                setTimeout(() => { btn.textContent = 'Publish Discovery'; }, 2000);
            } catch (e) { btn.textContent = e.message || 'Failed'; setTimeout(() => { btn.textContent = 'Publish Discovery'; }, 2000); }
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
        document.querySelectorAll('#openaiApiKey,#anthropicApiKey,#geminiApiKey,#openrouterApiKey,#openrouterModel,#ollamaEndpoint,#ollamaModel,#openaiEndpoint,#anthropicEndpoint')
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
)HTML")
        .replace(QStringLiteral("<!--UPLOAD_ACCOUNT:visualizer-->"),
                 uploadAccountSection(QStringLiteral("visualizer"), QStringLiteral("&#9749;"), QStringLiteral("Visualizer"),
                                      QStringLiteral("Upload your shots to visualizer.coffee for tracking and analysis"),
                                      QStringLiteral("Username / Email")))
        .replace(QStringLiteral("<!--UPLOAD_ACCOUNT:decent-->"),
                 uploadAccountSection(QStringLiteral("decent"), QStringLiteral("&#9749;"), QStringLiteral("Decent Account"),
                                      QStringLiteral("Upload your shots to your account at decentespresso.com"),
                                      QStringLiteral("Email")));
}

void ShotServer::handleGetSettings(QTcpSocket* socket)
{
    if (!m_settings) {
        sendJson(socket, R"({"error": "Settings not available"})");
        return;
    }

    QJsonObject obj;

    // Shot Upload: a switch and account state per destination, one set of
    // upload settings. No account names or passwords, as before.
    obj["visualizerEnabled"] = m_settings->visualizer()->visualizerEnabled();
    obj["visualizerConnected"] = m_settings->visualizer()->visualizerConnected();
    obj["decentEnabled"] = m_settings->decent()->enabled();
    if (DecentAccount* decent = m_mainController ? m_mainController->decentAccount() : nullptr)
        obj["decentAccountState"] = QString::fromLatin1(QMetaEnum::fromType<DecentAccount::State>().valueToKey(int(decent->state())));
    obj["uploadAutomatically"] = m_settings->upload()->autoUpload();
    obj["updateAutomatically"] = m_settings->upload()->autoUpdate();
    obj["uploadMinDurationSec"] = m_settings->upload()->minDuration();

    // AI — API keys redacted; provider/model/endpoint are not secrets.
    {
        auto* a = m_settings->ai();
        obj["aiProvider"] = a->aiProvider();
        obj["openaiApiKey"] = redactedSecret(a->openaiApiKey());
        obj["anthropicApiKey"] = redactedSecret(a->anthropicApiKey());
        obj["geminiApiKey"] = redactedSecret(a->geminiApiKey());
        obj["openrouterApiKey"] = redactedSecret(a->openrouterApiKey());
        obj["openrouterModel"] = a->openrouterModel();
        obj["ollamaEndpoint"] = a->ollamaEndpoint();
        obj["ollamaModel"] = a->ollamaModel();
        obj["openaiEndpoint"] = a->openaiEndpoint();
        obj["anthropicEndpoint"] = a->anthropicEndpoint();

        // Per-provider model catalogs, stored selections, current display
        // names, and guidance hints, so the web page can offer the same model
        // picker as the in-app AI settings tab. Catalogs/hints only include
        // providers with a non-empty catalog (OpenAI/Anthropic/Gemini today;
        // the page shows the picker only for catalogs with >1 entry);
        // providerModels round-trips through POST /api/settings.
        if (m_aiManager) {
            QJsonObject catalogs;
            QJsonObject selections;
            QJsonObject names;
            QJsonObject hints;
            QJsonObject costs;   // providerId -> { modelId -> cost line }
            const QStringList providers = m_aiManager->availableProviders();
            for (const QString& p : providers) {
                const QVariantList models = m_aiManager->availableModels(p);
                if (!models.isEmpty())
                    catalogs[p] = QJsonArray::fromVariantList(models);
                const QString hint = m_aiManager->modelHint(p);
                if (!hint.isEmpty())
                    hints[p] = hint;
                // Cost is keyed by provider AND model, not provider alone: the
                // page switches models client-side with no round trip, and a
                // per-provider figure is wrong across a catalog spanning 10x.
                QJsonObject perModel;
                for (const QVariant& m : models) {
                    const QString modelId = m.toMap().value("id").toString();
                    const QString cost = m_aiManager->costHint(p, modelId);
                    if (!cost.isEmpty())
                        perModel[modelId] = cost;
                }
                if (!perModel.isEmpty())
                    costs[p] = perModel;
                selections[p] = a->providerModel(p);
                names[p] = m_aiManager->modelDisplayName(p);
            }
            obj["providerModelCatalogs"] = catalogs;
            obj["providerModels"] = selections;
            obj["providerModelNames"] = names;
            obj["providerModelHints"] = hints;
            obj["providerModelCosts"] = costs;
        } else {
            // Unreachable with the current main.cpp wiring order, but if that
            // ever regresses the model picker vanishes from the web page with
            // no other symptom -- leave a breadcrumb.
            DIAG_WARN(NETWORK, "ShotServer") << "/api/settings served without AIManager -- model picker suppressed";
        }
    }

    // MQTT
    auto* mqttSettings = m_settings->mqtt();
    obj["mqttEnabled"] = mqttSettings->mqttEnabled();
    obj["mqttBrokerHost"] = mqttSettings->mqttBrokerHost();
    obj["mqttBrokerPort"] = mqttSettings->mqttBrokerPort();
    // MQTT credentials redacted (masked if set, "" if unset).
    obj["mqttUsername"] = redactedSecret(mqttSettings->mqttUsername());
    obj["mqttPassword"] = redactedSecret(mqttSettings->mqttPassword());
    obj["mqttBaseTopic"] = mqttSettings->mqttBaseTopic();
    obj["mqttPublishInterval"] = mqttSettings->mqttPublishInterval();
    obj["mqttClientId"] = mqttSettings->mqttClientId();
    obj["mqttRetainMessages"] = mqttSettings->mqttRetainMessages();
    obj["mqttHomeAssistantDiscovery"] = mqttSettings->mqttHomeAssistantDiscovery();
    obj["mqttUseTls"] = mqttSettings->mqttUseTls();
    obj["mqttDeviceId"] = mqttSettings->mqttDeviceId();
    // A CA certificate is public, not a secret; sent so the page can show what is trusted.
    obj["mqttCaCertificate"] = mqttSettings->mqttCaCertificate();
    obj["mqttCaCertificateSummary"] = SettingsMqtt::describeCaCertificate(mqttSettings->mqttCaCertificate());

    // MCP — local server config + live remote-access status. The local API key
    // is NOT emitted: the app hides it (the /mcp/setup page handles local client
    // config), and it only matters when the optional TOTP web-security is on.
    // The remote connector URL below does embed its capability token — that URL
    // is deliberately copied into the owner's AI client, unlike the masked
    // third-party secrets above; see the SettingsMcp header note.
    {
        auto* mcp = m_settings->mcp();
        obj["mcpEnabled"] = mcp->mcpEnabled();
        obj["mcpAccessLevel"] = mcp->mcpAccessLevel();
        obj["mcpConfirmationLevel"] = mcp->mcpConfirmationLevel();
        // LAN endpoint for local clients (Claude Desktop via mcp-remote, curl).
        // Empty until the server is listening (url() reports the bound address).
        obj["mcpLocalUrl"] = (mcp->mcpEnabled() && !url().isEmpty())
                                 ? url() + QStringLiteral("/mcp") : QString();

        obj["remoteMcpEnabled"] = mcp->remoteMcpEnabled();
        obj["remoteMcpMode"] = mcp->remoteMcpMode();
        obj["remoteMcpCustomBaseUrl"] = mcp->remoteMcpCustomBaseUrl();
        // Live coordinator state: status, the composed connector URL (only
        // populated once actually reachable), and — for Tailscale mode — the
        // one-time login URL. tunnelAvailable reports whether this build even
        // includes the embedded Tailscale option.
        obj["remoteMcpTunnelAvailable"] = McpRemoteAccess::tunnelAvailable();
        if (m_remoteMcpAccess) {
            obj["remoteMcpStatus"] = m_remoteMcpAccess->statusString();
            obj["remoteMcpStatusDetail"] = m_remoteMcpAccess->statusDetail();
            obj["remoteMcpConnectorUrl"] = m_remoteMcpAccess->connectorUrl();
            obj["remoteMcpLoginUrl"] = m_remoteMcpAccess->loginUrl();
        }
    }

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

    // Shot Upload switches and shared settings; accounts go through Connect.
    if (obj.contains("visualizerEnabled")) m_settings->visualizer()->setVisualizerEnabled(obj["visualizerEnabled"].toBool());
    if (obj.contains("decentEnabled")) m_settings->decent()->setEnabled(obj["decentEnabled"].toBool());
    if (obj.contains("uploadAutomatically")) m_settings->upload()->setAutoUpload(obj["uploadAutomatically"].toBool());
    if (obj.contains("updateAutomatically")) m_settings->upload()->setAutoUpdate(obj["updateAutomatically"].toBool());
    if (obj.contains("uploadMinDurationSec")) m_settings->upload()->setMinDuration(obj["uploadMinDurationSec"].toDouble());

    // AI
    const QStringList aiErrors = applyAiSettings(m_settings, m_aiManager, obj);

    // MQTT — applyMqttSettings enforces the broker-redirect guard and validates the CA.
    // Surfaced the same way handleMqttConnect does, so the user isn't told the save
    // succeeded when part of it was dropped.
    const QStringList mqttErrors = applyMqttSettings(m_settings, obj);

    // MCP (local server + remote connector). Setters fire signals that
    // McpRemoteAccess reacts to, so a web toggle starts/stops the tunnel.
    const QStringList mcpErrors = applyMcpSettings(m_settings, obj);

    // Valid fields (including valid providerModels entries) are applied even
    // when some entries were rejected; the error tells the client which
    // selections did not take.
    QStringList saveErrors = aiErrors + mqttErrors + mcpErrors;
    if (!saveErrors.isEmpty()) {
        QJsonObject resp;
        resp["success"] = false;
        resp["error"] = saveErrors.join(QStringLiteral("; "));
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    sendJson(socket, R"({"success": true})");
}

// Rotate the remote MCP capability token. Rotation IS revocation, so route it
// through McpRemoteAccess::rotateToken(), which both replaces the token AND
// calls closeAllSockets() to sever every in-flight session on the old URL —
// matching the native app's rotate. (Calling SettingsMcp::rotateRemoteMcpToken()
// directly would swap the token but leave live connections open, since
// remoteMcpTokenChanged is not wired to a restart.) The connector URL is
// re-composed from the unchanged cert domain / custom base URL, so the fresh
// URL is available immediately in the response.
void ShotServer::handleRotateRemoteMcpToken(QTcpSocket* socket)
{
    if (!m_settings) {
        sendJson(socket, R"({"success": false, "error": "Settings not available"})");
        return;
    }
    QJsonObject resp;
    resp["success"] = true;
    if (m_remoteMcpAccess) {
        m_remoteMcpAccess->rotateToken();  // rotate + drop live connections
        resp["connectorUrl"] = m_remoteMcpAccess->connectorUrl();
    } else {
        // No coordinator (not expected in production): at least rotate the
        // stored token so the old URL stops authorizing new requests.
        m_settings->mcp()->rotateRemoteMcpToken();
    }
    sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
}

namespace {
QString accountLinkMessage(AccountLink::Error error) {
    switch (error) {
    case AccountLink::Error::None: return QStringLiteral("Connected");
    case AccountLink::Error::Rejected: return QStringLiteral("Email or password not accepted");
    case AccountLink::Error::Unreachable: return QStringLiteral("Could not reach the server - check the connection");
    case AccountLink::Error::ServerError: return QStringLiteral("The server answered with an error - try again later");
    case AccountLink::Error::Cancelled: return QStringLiteral("Disconnected while connecting");
    }
    return QString();
}
}

// Connect verifies the account with its service and saves it only if it works,
// through the same code the Shot Upload tab uses. Connecting switches the
// destination on.
void ShotServer::handleAccountConnect(QTcpSocket* socket, const QByteArray& body, const QString& destination)
{
    VisualizerUploader* visualizer = m_mainController ? m_mainController->visualizer() : nullptr;
    DecentAccount* decent = m_mainController ? m_mainController->decentAccount() : nullptr;
    const bool isDecent = destination == QLatin1String("decent");
    if ((isDecent && !decent) || (!isDecent && !visualizer)) {
        sendJson(socket, R"({"success": false, "message": "Not available"})");
        return;
    }
    if (isDecent ? decent->busy() : visualizer->connecting()) {
        sendJson(socket, R"({"success": false, "message": "Already connecting"})");
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    const QString identity = obj["identity"].toString();
    const QString password = obj["password"].toString();
    if (identity.trimmed().isEmpty() || password.isEmpty()) {
        sendJson(socket, R"({"success": false, "message": "Enter the email and password"})");
        return;
    }

    QPointer<QTcpSocket> safeSocket(socket);
    auto respond = [this, safeSocket](AccountLink::Error error) {
        if (!safeSocket || safeSocket->state() != QAbstractSocket::ConnectedState) return;
        QJsonObject result;
        result["success"] = error == AccountLink::Error::None;
        result["message"] = accountLinkMessage(error);
        sendJson(safeSocket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    };
    if (isDecent) {
        connect(decent, &DecentAccount::linkFinished, this, respond, Qt::SingleShotConnection);
        decent->link(identity, password);
    } else {
        connect(visualizer, &VisualizerUploader::accountConnectFinished, this, respond, Qt::SingleShotConnection);
        visualizer->connectAccount(identity, password);
    }
}

void ShotServer::handleAccountDisconnect(QTcpSocket* socket, const QString& destination)
{
    if (destination == QLatin1String("decent")) {
        if (DecentAccount* decent = m_mainController ? m_mainController->decentAccount() : nullptr)
            decent->unlink();
    } else if (VisualizerUploader* visualizer = m_mainController ? m_mainController->visualizer() : nullptr) {
        visualizer->disconnectAccount();
    }
    sendJson(socket, R"({"success": true})");
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

    // Apply submitted settings before testing -- AIManager::testConnection()
    // creates a provider from current Settings values, so these must be
    // written first for the test to use the web form's credentials.
    // Note: this means "Test" also persists settings as a side effect.
    const QStringList aiErrors = applyAiSettings(m_settings, m_aiManager, obj);
    if (!aiErrors.isEmpty()) {
        // Don't run the test: it would exercise the provider default and
        // report "works" for a selection that was never applied.
        QJsonObject resp;
        resp["success"] = false;
        resp["message"] = aiErrors.join(QStringLiteral("; "));
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

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
        if (!success)
            DIAG_WARN(NETWORK, "ShotServer") << "AI test failed:" << message;
        else
            DIAG_DEBUG(NETWORK, "ShotServer") << "AI test succeeded";
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

    // Apply submitted settings before connecting -- connectToBroker() reads
    // connection parameters from Settings, so they must reflect the web form values.
    // Note: this means "Connect" also persists settings as a side effect.
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        sendJson(socket, R"({"success": false, "message": "Invalid request body"})");
        return;
    }
    // applyMqttSettings enforces the broker-redirect guard and rejects an invalid CA.
    // Decline to connect when it refused anything, so the stored password is never sent to a newly-supplied broker.
    if (const QStringList errors = applyMqttSettings(m_settings, doc.object()); !errors.isEmpty()) {
        QJsonObject resp;
        resp["success"] = false;
        resp["message"] = errors.join(QStringLiteral("; "));
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

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
        if (!success)
            DIAG_WARN(NETWORK, "ShotServer") << "MQTT connect failed:" << message;
        else
            DIAG_DEBUG(NETWORK, "ShotServer") << "MQTT connect succeeded";
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
        // Terminal state = connected, or not actively connecting/reconnecting.
        // MqttClient status strings: "Resolving..." (Android .local), "Connecting...",
        // "Disconnected - reconnecting (N/M)..."
        if (connected
            || (!status.startsWith("Connecting", Qt::CaseInsensitive)
                && !status.startsWith("Resolving", Qt::CaseInsensitive)
                && !status.contains("reconnecting", Qt::CaseInsensitive))) {
            cleanup(connected, status);
        }
    });

    connect(timer, &QTimer::timeout, this, [cleanup]() {
        cleanup(false, "Connection timed out");
    });
    // Longer than MqttClient's own 30 s attempt deadline, which ends a stalled attempt
    // with a reason; this only covers a client that never reports at all.
    timer->start(35000);

    m_mqttClient->connectToBroker();
}

void ShotServer::handleMqttDisconnect(QTcpSocket* socket)
{
    if (!m_mqttClient) {
        sendJson(socket, R"({"success": false, "message": "MQTT client not available"})");
        return;
    }

    m_mqttClient->disconnectFromBroker();
    // Disconnect is async -- report that it was requested, not that it completed.
    // The MQTT status polling (every 2s) will show the actual state.
    sendJson(socket, R"({"success": true, "message": "Disconnect requested"})");
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
    // publishDiscovery() does not wait for the broker's PUBACK, so success means "sent",
    // not "received". The isConnected() check above is our best guard.
    sendJson(socket, R"({"success": true})");
}

void ShotServer::handleMqttNewDeviceId(QTcpSocket* socket)
{
    if (!m_mqttClient || !m_settings) {
        sendJson(socket, R"({"success": false, "message": "MQTT client not available"})");
        return;
    }
    m_mqttClient->newDeviceId();
    QJsonObject result;
    result["success"] = true;
    result["deviceId"] = m_settings->mqtt()->mqttDeviceId();
    sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
}

