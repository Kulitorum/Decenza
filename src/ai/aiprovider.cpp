#include "aiprovider.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QVariant>

// ============================================================================
// AIProvider base class
// ============================================================================

AIProvider::AIProvider(QNetworkAccessManager* networkManager, QObject* parent)
    : QObject(parent)
    , m_networkManager(networkManager)
{
}

void AIProvider::setStatus(Status status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged(status);
    }
}

QString AIProvider::friendlyNetworkError(QNetworkReply* reply)
{
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
        return "Could not connect to the AI service. Check your internet connection.";
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return "Request timed out. The AI service may be slow — please try again.";
    case QNetworkReply::AuthenticationRequiredError:
        return "Authentication failed. Please check your API key in Settings.";
    case QNetworkReply::ContentAccessDenied:
        return "Access denied. Your API key may not have permission for this model.";
    default:
        return "Request failed: " + reply->errorString();
    }
}

QJsonArray AIProvider::buildOpenAIMessages(const QString& systemPrompt, const QJsonArray& messages)
{
    QJsonArray apiMessages;
    QJsonObject sysMsg;
    sysMsg["role"] = QString("system");
    sysMsg["content"] = systemPrompt;
    apiMessages.append(sysMsg);
    for (const auto& msg : messages) {
        apiMessages.append(msg);
    }
    return apiMessages;
}

bool AIProvider::isRetryableHttpStatus(int httpStatus, int retryCount)
{
    // Primary transient codes: retry up to MAX_RETRIES times
    if (httpStatus == 429 || httpStatus == 502 || httpStatus == 503 || httpStatus == 504)
        return retryCount < MAX_RETRIES;
    // Other 5xx (e.g. 500 internal server error): retry once only
    if (httpStatus >= 500 && httpStatus < 600)
        return retryCount < 1;
    return false;
}

int AIProvider::computeRetryDelayMs(int retryCount, QNetworkReply* reply)
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus == 429) {
        const QByteArray retryAfter = reply->rawHeader("Retry-After");
        if (!retryAfter.isEmpty()) {
            bool ok;
            const int seconds = retryAfter.toInt(&ok);
            if (ok && seconds > 0)
                return qMin(seconds * 1000, 30000);
        }
    }
    return 1000 << (retryCount - 1);  // 1s, 2s, 4s for retries 1, 2, 3
}

bool AIProvider::tryScheduleRetry(QNetworkReply* reply)
{
    if (!m_retryFn) return false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!isRetryableHttpStatus(status, m_retryCount)) return false;
    const int delay = computeRetryDelayMs(++m_retryCount, reply);
    const QByteArray body = reply->readAll();
    qWarning() << name() << "HTTP" << status << "- retry" << m_retryCount
               << "in" << delay << "ms" << (body.isEmpty() ? QByteArray() : ("- " + body.left(200)));
    // QTimer::singleShot is intentional: the server signalled a transient error and we must
    // wait before retrying (rate-limit or overload backoff). This is a server-driven delay,
    // not a heuristic guard. The generation counter prevents stale timers from firing if a
    // new analyze() call arrives before this one fires.
    const int gen = m_reqGen;
    QTimer::singleShot(delay, this, [this, gen]() {
        if (gen == m_reqGen) m_retryFn();
    });
    return true;
}

void AIProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    // Default fallback: flatten messages into a single string and call analyze()
    // This loses multi-turn context — providers should override for native support
    qWarning() << "AIProvider::analyzeConversation: Using flatten fallback for provider"
               << name() << "- consider implementing native multi-turn support";
    QString flatPrompt;
    for (int i = 0; i < messages.size(); i++) {
        QJsonObject msg = messages[i].toObject();
        QString role = msg["role"].toString();
        QString content = msg["content"].toString();

        if (role == "user") {
            if (i > 0) flatPrompt += "\n\n[User follow-up]:\n";
            flatPrompt += content;
        } else if (role == "assistant") {
            flatPrompt += "\n\n[Your previous response]:\n" + content;
        }
    }
    analyze(systemPrompt, flatPrompt);
}

// ============================================================================
// OpenAI Provider
// ============================================================================

OpenAIProvider::OpenAIProvider(QNetworkAccessManager* networkManager,
                               const QString& apiKey,
                               QObject* parent)
    : AIProvider(networkManager, parent)
    , m_apiKey(apiKey)
{
}

void OpenAIProvider::sendRequest(const QJsonObject& requestBody)
{
    QUrl url(QString::fromLatin1(API_URL));
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    req.setTransferTimeout(ANALYSIS_TIMEOUT_MS);

    m_retryFn = [this, requestBody]() { sendRequest(requestBody); };

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void OpenAIProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("OpenAI API key not configured");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    QJsonObject requestBody;
    requestBody["model"] = QString::fromLatin1(MODEL);
    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = QString("system");
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = userPrompt;
    messages.append(userMsg);
    requestBody["messages"] = messages;
    requestBody["max_tokens"] = 1024;

    sendRequest(requestBody);
}

void OpenAIProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed("OpenAI API key not configured");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    QJsonObject requestBody;
    requestBody["model"] = QString::fromLatin1(MODEL);
    requestBody["messages"] = buildOpenAIMessages(systemPrompt, messages);
    requestBody["max_tokens"] = 1024;

    sendRequest(requestBody);
}

void OpenAIProvider::onAnalysisReply(QNetworkReply* reply)
{
    if (tryScheduleRetry(reply)) { reply->deleteLater(); return; }
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            QJsonDocument bodyDoc = QJsonDocument::fromJson(body);
            QString apiError = bodyDoc.object()["error"].toObject()["message"].toString();
            if (!apiError.isEmpty()) {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qWarning() << "OpenAI API error" << status << "-" << apiError;
                emit analysisFailed("OpenAI error: " + apiError);
                return;
            }
            qWarning() << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << body;
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed("OpenAI error: " + errorMsg);
        return;
    }

    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        emit analysisFailed("OpenAI returned no response");
        return;
    }

    QString content = choices[0].toObject()["message"].toObject()["content"].toString();
    if (content.isEmpty()) {
        emit analysisFailed("OpenAI returned empty response content");
        return;
    }
    emit analysisComplete(content);
}

void OpenAIProvider::testConnection()
{
    if (!isConfigured()) {
        emit testResult(false, "API key not configured");
        return;
    }

    // Simple test: list models
    QUrl url(QString("https://api.openai.com/v1/models"));
    QNetworkRequest req;
    req.setUrl(url);
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    req.setTransferTimeout(TEST_TIMEOUT_MS);
    // Disable HTTP/2 -- Qt's HTTP/2 layer intercepts 401 as an auth challenge
    // instead of passing the response body through, breaking custom auth schemes
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply* reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestReply(reply);
    });
}

void OpenAIProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    QByteArray responseBody = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Handle errors in priority order: explicit 401 with response body context,
    // then network errors with JSON error parsing, then success-with-error-body,
    // then fall back to Qt's generic error string.
    if (httpStatus == 401) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, "Authentication failed: " + errorMsg);
                return;
            }
        }
        emit testResult(false, "Invalid API key");
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, "API error: " + errorMsg);
                return;
            }
        }
        emit testResult(false, "Connection failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.object().contains("error")) {
        QJsonValue errVal = doc.object()["error"];
        QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
        if (errorMsg.isEmpty())
            errorMsg = "Unknown API error";
        emit testResult(false, "API error: " + errorMsg);
        return;
    }

    emit testResult(true, "Connected to OpenAI successfully");
}

// ============================================================================
// Anthropic Provider
// ============================================================================

AnthropicProvider::AnthropicProvider(QNetworkAccessManager* networkManager,
                                     const QString& apiKey,
                                     QObject* parent)
    : AIProvider(networkManager, parent)
    , m_apiKey(apiKey)
{
    // Default to the recommended model = first catalog entry. Keeps the default
    // a single source of truth (no parallel DEFAULT_MODEL constant to keep in
    // sync with the list order). availableModels() dispatches to this class
    // since the object under construction is an AnthropicProvider.
    const QList<ModelOption> models = availableModels();
    if (!models.isEmpty())
        m_model = models.first().id;
}

QList<AIProvider::ModelOption> AnthropicProvider::availableModels() const
{
    // Order = UI order; first entry is the recommended default. Sonnet 4.6 leads
    // as the established default so upgrading users keep their current behavior;
    // Sonnet 5 is the opt-in "more capable" choice. Revisit as new models land.
    return {
        { "claude-sonnet-4-6", "Sonnet 4.6" },
        { "claude-sonnet-5", "Sonnet 5" },
    };
}

void AnthropicProvider::setModel(const QString& modelId)
{
    if (modelId.isEmpty())
        return;  // unset → keep the current default
    for (const ModelOption& opt : availableModels()) {
        if (opt.id == modelId) {
            m_model = modelId;
            return;
        }
    }
    qWarning() << "AnthropicProvider::setModel ignoring unknown model id:" << modelId;
}

QString AnthropicProvider::shortModelName() const
{
    for (const ModelOption& opt : availableModels()) {
        if (opt.id == m_model)
            return opt.displayName;
    }
    return m_model;
}

void AnthropicProvider::sendRequest(const QJsonObject& requestBody)
{
    QUrl url(QString::fromLatin1(API_URL));
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("x-api-key", m_apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");
    // 1-hour cache TTL is set on each cache_control block in the request
    // body (see buildCachedSystemPrompt + messagesWithCachedFirstUser).
    // The 1-hour TTL tier is GA — no beta header required. Cache writes
    // cost 2x base input (vs 1.25x for 5-min); reads stay at 0.1x.
    // Break-even is ~2 reads per write, easily met for any iterative dial-in.
    req.setTransferTimeout(ANALYSIS_TIMEOUT_MS);

    m_retryFn = [this, requestBody]() { sendRequest(requestBody); };

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void AnthropicProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("Anthropic API key not configured");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["max_tokens"] = 1024;
    requestBody["system"] = buildCachedSystemPrompt(systemPrompt);
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = userPrompt;
    messages.append(userMsg);
    requestBody["messages"] = messages;

    sendRequest(requestBody);
}

void AnthropicProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed("Anthropic API key not configured");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["max_tokens"] = 1024;
    requestBody["system"] = buildCachedSystemPrompt(systemPrompt);
    requestBody["messages"] = messagesWithCachedFirstUser(messages);

    sendRequest(requestBody);
}

QJsonArray AnthropicProvider::messagesWithCachedFirstUser(const QJsonArray& messages)
{
    // The first user message carries the per-shot context, which is stable
    // across follow-up turns within the cache TTL. Wrap its content in a
    // structured block with cache_control so subsequent turns read from
    // cache instead of re-billing the per-shot payload. A 1-hour TTL covers
    // a typical iterative dial-in spread across an hour-long session.
    //
    // No-op when messages[0] isn't a plain-string user message (caller
    // pre-wrapped, or first message isn't from user) — preserves input.
    if (messages.isEmpty()) return messages;
    QJsonObject first = messages[0].toObject();
    if (first.value("role").toString() != "user") return messages;
    if (!first.value("content").isString()) return messages;

    QJsonObject cacheControl;
    cacheControl["type"] = QString("ephemeral");
    cacheControl["ttl"] = QString("1h");  // Anthropic API: Literal["5m", "1h"]

    QJsonObject block;
    block["type"] = QString("text");
    block["text"] = first.value("content").toString();
    block["cache_control"] = cacheControl;

    QJsonArray contentArr;
    contentArr.append(block);
    first["content"] = contentArr;

    QJsonArray out;
    out.append(first);
    for (qsizetype i = 1; i < messages.size(); ++i)
        out.append(messages[i]);
    return out;
}

QJsonArray AnthropicProvider::buildCachedSystemPrompt(const QString& systemPrompt)
{
    // Cache the system prompt with the 1-hour extended TTL. Anthropic
    // caches give ~90% off input cost on hits; a 1-hour TTL covers most
    // dial-in patterns (back-to-back, "let me try again in 20 minutes",
    // and the typical morning-pull-evening-pull iteration). Cache writes
    // cost 2x base for the 1-hour tier (vs 1.25x for 5-min); break-even
    // is 2 reads per write — easily met for any iterative user.
    QJsonObject cacheControl;
    cacheControl["type"] = QString("ephemeral");
    cacheControl["ttl"] = QString("1h");  // Anthropic API: Literal["5m", "1h"]

    QJsonObject block;
    block["type"] = QString("text");
    block["text"] = systemPrompt;
    block["cache_control"] = cacheControl;

    QJsonArray systemArray;
    systemArray.append(block);
    return systemArray;
}

void AnthropicProvider::onAnalysisReply(QNetworkReply* reply)
{
    if (tryScheduleRetry(reply)) { reply->deleteLater(); return; }
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            QJsonDocument bodyDoc = QJsonDocument::fromJson(body);
            QString apiError = bodyDoc.object()["error"].toObject()["message"].toString();
            if (!apiError.isEmpty()) {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qWarning() << "Anthropic API error" << status << "-" << apiError;
                emit analysisFailed("Anthropic error: " + apiError);
                return;
            }
            qWarning() << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << body;
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed("Anthropic error: " + errorMsg);
        return;
    }

    QJsonArray content = root["content"].toArray();
    if (content.isEmpty()) {
        emit analysisFailed("Anthropic returned no response");
        return;
    }

    QString text = content[0].toObject()["text"].toString();
    if (text.isEmpty()) {
        emit analysisFailed("Anthropic returned empty response content");
        return;
    }
    emit analysisComplete(text);
}

void AnthropicProvider::testConnection()
{
    if (!isConfigured()) {
        emit testResult(false, "API key not configured");
        return;
    }

    // Send a minimal request to test the API key
    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["max_tokens"] = 10;
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = QString("Hi");
    messages.append(userMsg);
    requestBody["messages"] = messages;

    QUrl url(QString::fromLatin1(API_URL));
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("x-api-key", m_apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");
    req.setTransferTimeout(TEST_TIMEOUT_MS);
    // Disable HTTP/2 — Qt's HTTP/2 layer intercepts 401 as an auth challenge
    // instead of passing the response body through, breaking custom auth schemes
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestReply(reply);
    });
}

void AnthropicProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    QByteArray responseBody = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Handle errors in priority order: explicit 401 with response body context,
    // then network errors with JSON error parsing, then success-with-error-body,
    // then fall back to Qt's generic error string.
    if (httpStatus == 401) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, "Authentication failed: " + errorMsg);
                return;
            }
        }
        emit testResult(false, "Invalid API key");
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, "API error: " + errorMsg);
                return;
            }
        }
        emit testResult(false, "Connection failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.object().contains("error")) {
        QJsonValue errVal = doc.object()["error"];
        QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
        if (errorMsg.isEmpty())
            errorMsg = "Unknown API error";
        emit testResult(false, "API error: " + errorMsg);
        return;
    }

    emit testResult(true, "Connected to Anthropic successfully");
}

// ============================================================================
// Gemini Provider
// ============================================================================

GeminiProvider::GeminiProvider(QNetworkAccessManager* networkManager,
                               const QString& apiKey,
                               QObject* parent)
    : AIProvider(networkManager, parent)
    , m_apiKey(apiKey)
{
    // Default to the recommended model = first catalog entry. Keeps the default
    // a single source of truth (no parallel DEFAULT_MODEL constant to keep in
    // sync with the list order). availableModels() dispatches to this class
    // since the object under construction is a GeminiProvider.
    const QList<ModelOption> models = availableModels();
    if (!models.isEmpty())
        m_model = models.first().id;
}

QList<AIProvider::ModelOption> GeminiProvider::availableModels() const
{
    // Order = UI order; first entry is the recommended default. 2.5 Flash leads
    // as the lowest-cost sensible default for shot analysis — thinking adds
    // little here and 2.5 can disable it entirely (thinkingBudget 0), plus it
    // has more provisioned capacity (fewer 503s). 3.5 Flash is the opt-in
    // "more capable" choice. Revisit as new models / pricing land.
    return {
        { "gemini-2.5-flash", "2.5 Flash" },
        { "gemini-3.5-flash", "3.5 Flash" },
    };
}

void GeminiProvider::setModel(const QString& modelId)
{
    if (modelId.isEmpty())
        return;  // unset → keep the current default
    for (const ModelOption& opt : availableModels()) {
        if (opt.id == modelId) {
            m_model = modelId;
            return;
        }
    }
    qWarning() << "GeminiProvider::setModel ignoring unknown model id:" << modelId;
}

QString GeminiProvider::shortModelName() const
{
    for (const ModelOption& opt : availableModels()) {
        if (opt.id == m_model)
            return opt.displayName;
    }
    return m_model;
}

QString GeminiProvider::apiUrl() const
{
    // Use URL without key - key is passed via header for better security
    return QString("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
        .arg(m_model);
}

void GeminiProvider::sendRequest(const QJsonObject& requestBody)
{
    QUrl url(apiUrl());
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());
    req.setTransferTimeout(ANALYSIS_TIMEOUT_MS);

    // Thinking config differs by model family: the 2.5 family uses the integer
    // thinkingBudget (0 disables thinking), while 3.x+ uses the thinkingLevel
    // enum and ignores thinkingBudget — sending the wrong knob lets thinking
    // default to "medium" (billed at the $9/MTok output rate). Pick by family
    // so each selectable model keeps thinking minimal/off.
    QJsonObject bodyWithConfig = requestBody;
    QJsonObject thinkingConfig;
    // Gate on the gemini-2.x prefix — 2.5 Flash is the only 2.x model in the
    // catalog today, so this selects it exactly. If a future gemini-2.x model
    // with different thinking semantics is added, prefer encoding the thinking
    // API in ModelOption over widening this string check.
    if (m_model.startsWith(QStringLiteral("gemini-2"))) {
        thinkingConfig["thinkingBudget"] = 0;       // 2.x: integer budget knob, 0 = off
    } else {
        thinkingConfig["thinkingLevel"] = "minimal"; // 3.x+: thinkingLevel enum
    }
    QJsonObject generationConfig;
    generationConfig["thinkingConfig"] = thinkingConfig;
    generationConfig["maxOutputTokens"] = 1024;  // also bounds thinking tokens; matches other providers
    bodyWithConfig["generationConfig"] = generationConfig;

    m_retryFn = [this, requestBody]() { sendRequest(requestBody); };

    QByteArray body = QJsonDocument(bodyWithConfig).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void GeminiProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("Gemini API key not configured");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    // Gemini uses a different format
    QJsonObject requestBody;

    // system_instruction
    QJsonObject sysInstruction;
    QJsonArray sysParts;
    QJsonObject sysTextPart;
    sysTextPart["text"] = systemPrompt;
    sysParts.append(sysTextPart);
    sysInstruction["parts"] = sysParts;
    requestBody["system_instruction"] = sysInstruction;

    // contents
    QJsonArray contents;
    QJsonObject userContent;
    userContent["role"] = QString("user");
    QJsonArray userParts;
    QJsonObject userTextPart;
    userTextPart["text"] = userPrompt;
    userParts.append(userTextPart);
    userContent["parts"] = userParts;
    contents.append(userContent);
    requestBody["contents"] = contents;

    sendRequest(requestBody);
}

void GeminiProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed("Gemini API key not configured");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    QJsonObject requestBody;

    // system_instruction
    QJsonObject sysInstruction;
    QJsonArray sysParts;
    QJsonObject sysTextPart;
    sysTextPart["text"] = systemPrompt;
    sysParts.append(sysTextPart);
    sysInstruction["parts"] = sysParts;
    requestBody["system_instruction"] = sysInstruction;

    // contents — map from OpenAI roles to Gemini roles
    QJsonArray contents;
    for (const auto& msg : messages) {
        QJsonObject m = msg.toObject();
        QString role = m["role"].toString();
        if (role != "user" && role != "assistant") {
            qWarning() << "GeminiProvider: Skipping message with unexpected role:" << role;
            continue;
        }
        QJsonObject content;
        content["role"] = (role == "assistant") ? QString("model") : role;
        QJsonArray parts;
        QJsonObject textPart;
        textPart["text"] = m["content"].toString();
        parts.append(textPart);
        content["parts"] = parts;
        contents.append(content);
    }
    requestBody["contents"] = contents;

    sendRequest(requestBody);
}

void GeminiProvider::onAnalysisReply(QNetworkReply* reply)
{
    if (tryScheduleRetry(reply)) { reply->deleteLater(); return; }
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            QJsonDocument bodyDoc = QJsonDocument::fromJson(body);
            QString apiError = bodyDoc.object()["error"].toObject()["message"].toString();
            if (!apiError.isEmpty()) {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qWarning() << "Gemini API error" << status << "-" << apiError;
                emit analysisFailed("Gemini error: " + apiError);
                return;
            }
            qWarning() << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << body;
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed("Gemini error: " + errorMsg);
        return;
    }

    const QJsonObject usage = root["usageMetadata"].toObject();
    qInfo() << "Gemini usage — prompt:" << usage["promptTokenCount"].toInt()
            << "thoughts:" << usage["thoughtsTokenCount"].toInt()
            << "output:" << usage["candidatesTokenCount"].toInt()
            << "total:" << usage["totalTokenCount"].toInt();

    QJsonArray candidates = root["candidates"].toArray();
    if (candidates.isEmpty()) {
        emit analysisFailed("Gemini returned no response");
        return;
    }

    QJsonArray parts = candidates[0].toObject()["content"].toObject()["parts"].toArray();
    if (parts.isEmpty()) {
        emit analysisFailed("Gemini returned empty content");
        return;
    }

    QString text = parts[0].toObject()["text"].toString();
    if (text.isEmpty()) {
        emit analysisFailed("Gemini returned empty response content");
        return;
    }
    emit analysisComplete(text);
}

void GeminiProvider::testConnection()
{
    if (!isConfigured()) {
        emit testResult(false, "API key not configured");
        return;
    }

    // Send a minimal request
    QJsonObject requestBody;
    QJsonArray contents;
    QJsonObject userContent;
    userContent["role"] = QString("user");
    QJsonArray userParts;
    QJsonObject userTextPart;
    userTextPart["text"] = QString("Hi");
    userParts.append(userTextPart);
    userContent["parts"] = userParts;
    contents.append(userContent);
    requestBody["contents"] = contents;

    QUrl url(apiUrl());
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());
    req.setTransferTimeout(TEST_TIMEOUT_MS);
    // Disable HTTP/2 -- Qt's HTTP/2 layer intercepts 401 as an auth challenge
    // instead of passing the response body through, breaking custom auth schemes
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestReply(reply);
    });
}

void GeminiProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    QByteArray responseBody = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (httpStatus == 401 || httpStatus == 403) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, "Authentication failed: " + errorMsg);
                return;
            }
        }
        emit testResult(false, "Invalid API key");
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, "API error: " + errorMsg);
                return;
            }
        }
        emit testResult(false, "Connection failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.object().contains("error")) {
        QJsonValue errVal = doc.object()["error"];
        QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
        if (errorMsg.isEmpty())
            errorMsg = "Unknown API error";
        emit testResult(false, "API error: " + errorMsg);
        return;
    }

    emit testResult(true, "Connected to Gemini successfully");
}

// ============================================================================
// OpenRouter Provider
// ============================================================================

OpenRouterProvider::OpenRouterProvider(QNetworkAccessManager* networkManager,
                                         const QString& apiKey,
                                         const QString& model,
                                         QObject* parent)
    : AIProvider(networkManager, parent)
    , m_apiKey(apiKey)
    , m_model(model)
{
}

void OpenRouterProvider::sendRequest(const QJsonObject& requestBody)
{
    QUrl url(QString::fromLatin1(API_URL));
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    // Attribution headers for OpenRouter leaderboard
    req.setRawHeader("HTTP-Referer", "https://github.com/Kulitorum/Decenza");
    req.setRawHeader("X-Title", "Decenza");
    req.setTransferTimeout(ANALYSIS_TIMEOUT_MS);

    m_retryFn = [this, requestBody]() { sendRequest(requestBody); };

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void OpenRouterProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("OpenRouter API key or model not configured");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    // OpenRouter uses OpenAI-compatible format
    QJsonObject requestBody;
    requestBody["model"] = m_model;
    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = QString("system");
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = userPrompt;
    messages.append(userMsg);
    requestBody["messages"] = messages;
    requestBody["max_tokens"] = 1024;

    sendRequest(requestBody);
}

void OpenRouterProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed("OpenRouter API key or model not configured");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["messages"] = buildOpenAIMessages(systemPrompt, messages);
    requestBody["max_tokens"] = 1024;

    sendRequest(requestBody);
}

void OpenRouterProvider::onAnalysisReply(QNetworkReply* reply)
{
    if (tryScheduleRetry(reply)) { reply->deleteLater(); return; }
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        QByteArray body = reply->readAll();
        if (!body.isEmpty()) {
            QJsonDocument bodyDoc = QJsonDocument::fromJson(body);
            QString apiError = bodyDoc.object()["error"].toObject()["message"].toString();
            if (!apiError.isEmpty()) {
                int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qWarning() << "OpenRouter API error" << status << "-" << apiError;
                emit analysisFailed("OpenRouter error: " + apiError);
                return;
            }
            qWarning() << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << body;
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed("OpenRouter error: " + errorMsg);
        return;
    }

    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        emit analysisFailed("OpenRouter returned no response");
        return;
    }

    QString content = choices[0].toObject()["message"].toObject()["content"].toString();
    if (content.isEmpty()) {
        emit analysisFailed("OpenRouter returned empty response content");
        return;
    }
    emit analysisComplete(content);
}

void OpenRouterProvider::testConnection()
{
    if (!isConfigured()) {
        emit testResult(false, "API key or model not configured");
        return;
    }

    // Send a minimal request to test the API key and model
    QJsonObject requestBody;
    requestBody["model"] = m_model;
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = QString("Hi");
    messages.append(userMsg);
    requestBody["messages"] = messages;
    requestBody["max_tokens"] = 10;

    QUrl url(QString::fromLatin1(API_URL));
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    req.setRawHeader("HTTP-Referer", "https://github.com/Kulitorum/Decenza");
    req.setRawHeader("X-Title", "Decenza");
    req.setTransferTimeout(TEST_TIMEOUT_MS);
    // Disable HTTP/2 -- Qt's HTTP/2 layer intercepts 401 as an auth challenge
    // instead of passing the response body through, breaking custom auth schemes
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestReply(reply);
    });
}

void OpenRouterProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    QByteArray responseBody = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (httpStatus == 401) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, "Authentication failed: " + errorMsg);
                return;
            }
        }
        emit testResult(false, "Invalid API key");
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, "API error: " + errorMsg);
                return;
            }
        }
        emit testResult(false, "Connection failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.object().contains("error")) {
        QJsonValue errVal = doc.object()["error"];
        QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
        if (errorMsg.isEmpty())
            errorMsg = "Unknown API error";
        emit testResult(false, "API error: " + errorMsg);
        return;
    }

    emit testResult(true, "Connected to OpenRouter successfully");
}

// ============================================================================
// Ollama Provider
// ============================================================================

OllamaProvider::OllamaProvider(QNetworkAccessManager* networkManager,
                               const QString& endpoint,
                               const QString& model,
                               QObject* parent)
    : AIProvider(networkManager, parent)
    , m_endpoint(endpoint)
    , m_model(model)
{
}

void OllamaProvider::sendRequest(const QUrl& url, const QJsonObject& requestBody)
{
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setTransferTimeout(LOCAL_ANALYSIS_TIMEOUT_MS);

    m_retryFn = [this, url, requestBody]() { sendRequest(url, requestBody); };

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void OllamaProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("Ollama not configured (need endpoint and model)");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["prompt"] = userPrompt;
    requestBody["system"] = systemPrompt;
    requestBody["stream"] = false;

    QString urlStr = m_endpoint;
    if (!urlStr.endsWith(QString("/"))) urlStr += QString("/");
    urlStr += QString("api/generate");

    sendRequest(QUrl(urlStr), requestBody);
}

void OllamaProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed("Ollama not configured (need endpoint and model)");
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;

    // Use /api/chat which supports messages array natively
    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["stream"] = false;
    requestBody["messages"] = buildOpenAIMessages(systemPrompt, messages);

    QString urlStr = m_endpoint;
    if (!urlStr.endsWith(QString("/"))) urlStr += QString("/");
    urlStr += QString("api/chat");

    sendRequest(QUrl(urlStr), requestBody);
}

void OllamaProvider::onAnalysisReply(QNetworkReply* reply)
{
    if (tryScheduleRetry(reply)) { reply->deleteLater(); return; }
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        QByteArray body = reply->readAll();
        if (!body.isEmpty())
            qWarning() << "Ollama request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << body;
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        emit analysisFailed("Ollama error: " + root["error"].toString());
        return;
    }

    // Support both /api/chat (message.content) and /api/generate (response) formats
    QString response = root["message"].toObject()["content"].toString();
    if (response.isEmpty()) {
        response = root["response"].toString();
        if (!response.isEmpty()) {
            qDebug() << "OllamaProvider: Used /api/generate response format (fallback)";
        }
    }
    if (response.isEmpty()) {
        emit analysisFailed("Ollama returned empty response");
        return;
    }

    emit analysisComplete(response);
}

void OllamaProvider::testConnection()
{
    if (m_endpoint.isEmpty()) {
        emit testResult(false, "Ollama endpoint not configured");
        return;
    }

    // Test by listing models
    refreshModels();
}

void OllamaProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit testResult(false, "Cannot connect to Ollama: " + reply->errorString());
        return;
    }

    emit testResult(true, "Connected to Ollama successfully");
}

void OllamaProvider::refreshModels()
{
    QString urlStr = m_endpoint;
    if (!urlStr.endsWith(QString("/"))) urlStr += QString("/");
    urlStr += QString("api/tags");

    QUrl url(urlStr);
    QNetworkRequest req;
    req.setUrl(url);
    req.setTransferTimeout(TEST_TIMEOUT_MS);
    QNetworkReply* reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onModelsReply(reply);
    });
}

void OllamaProvider::onModelsReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit testResult(false, "Cannot list Ollama models: " + reply->errorString());
        emit modelsRefreshed({});
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonArray models = doc.object()["models"].toArray();

    QStringList modelNames;
    for (const auto& model : models) {
        modelNames.append(model.toObject()["name"].toString());
    }

    emit modelsRefreshed(modelNames);

    if (!modelNames.isEmpty()) {
        emit testResult(true, QString("Found %1 Ollama model(s)").arg(modelNames.size()));
    } else {
        emit testResult(false, "No models found. Run: ollama pull llama3.2");
    }
}
