#include "core/diagnosticlogging.h"

#define PROVIDER_DEBUG(tag) DECENZA_SUBSYS_VALUE_STREAM(diagnosticOwner(), tag, qDebug) << diagnosticFields()
#define PROVIDER_INFO(tag) DECENZA_SUBSYS_VALUE_STREAM(diagnosticOwner(), tag, qInfo) << diagnosticFields()
#define PROVIDER_WARN(tag) DECENZA_SUBSYS_VALUE_STREAM(diagnosticOwner(), tag, qWarning) << diagnosticFields()
#include "aiprovider.h"
#include "airequestshape.h"
#include "../core/translationmanager.h"
#include <QJsonDocument>
#include <algorithm>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QStringList>
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

QString AIProvider::tr_(const char* key, const char* fallback) const {
    return translateOrFallback(m_translationManager, key, fallback);
}

QString AIProvider::truncatedResponseError() const
{
    // Deliberately does NOT say "ask a more specific question": analyzeUrl() is
    // the recipe-wizard URL extraction, where there is no question to narrow —
    // the user pasted a link. Keep the remedy true on every path.
    return tr_("ai.error.truncated",
               "The AI's reply was cut off before it finished. Please try again.");
}

QString AIProvider::truncationNotice() const
{
    return QStringLiteral("\n\n_") +
           tr_("ai.notice.truncated", "This reply was cut off before it finished.") +
           QStringLiteral("_");
}

bool AIProvider::dispatchTruncatedOrEmpty(const QString& text, bool truncated,
                                          const QString& emptyMessage)
{
    if (!truncated && !text.isEmpty())
        return false;  // ordinary reply — caller emits it

    if (truncated && !text.isEmpty() && m_truncationPolicy == TruncationPolicy::ShowPartial) {
        emit analysisComplete(text + truncationNotice());
        return true;
    }
    emit analysisFailed(truncated ? truncatedResponseError() : emptyMessage);
    return true;
}

QString AIProvider::logSafeErrorBody(const QByteArray& body)
{
    // Even error.type/code and HTML prefixes are remote text and may echo a
    // prompt or credentials. Shape + byte count are sufficient beside HTTP and
    // QNetworkReply's numeric status, captured on the operation itself.
    return QStringLiteral("bodyFormat=%1 bodyBytes=%2 contentOmitted")
        .arg(QJsonDocument::fromJson(body).isObject() ? QStringLiteral("json") : QStringLiteral("other"))
        .arg(body.size());
}

QString AIProvider::friendlyNetworkError(QNetworkReply* reply) const
{
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
        return tr_("ai.error.noConnection", "Could not connect to the AI service. Check your internet connection.");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return tr_("ai.error.timeout", "Request timed out. The AI service may be slow — please try again.");
    case QNetworkReply::AuthenticationRequiredError:
        return tr_("ai.error.authFailed", "Authentication failed. Please check your API key in Settings.");
    case QNetworkReply::ContentAccessDenied:
        return tr_("ai.error.accessDenied", "Access denied. Your API key may not have permission for this model.");
    default:
        return tr_("ai.error.requestFailed", "Request failed: %1").arg(reply->errorString());
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
    if (auto operation = m_logOperation.lock())
        operation->network(QStringLiteral("provider"), reply->url().toString(),
                           reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                           int(reply->error()));
    if (!m_retryFn) return false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!isRetryableHttpStatus(status, m_retryCount)) return false;
    const int delay = computeRetryDelayMs(++m_retryCount, reply);
    const QByteArray body = reply->readAll();
    PROVIDER_DEBUG("Retry") << "HTTP" << status << "- retry" << m_retryCount
               << "in" << delay << "ms"
               << (body.isEmpty() ? QString() : QStringLiteral("- ") + logSafeErrorBody(body));
    // QTimer::singleShot is intentional: the server signalled a transient error and we must
    // wait before retrying (rate-limit or overload backoff). This is a server-driven delay,
    // not a heuristic guard. The generation counter prevents stale timers from firing if a
    // new analyze() call arrives before this one fires.
    const int gen = m_reqGen;
    QTimer::singleShot(delay, this, [this, gen]() {
        if (gen != m_reqGen) return;
        // sendRequest replaces m_retryFn. Keep this callable and its captured
        // request alive until the resend has finished reading that request.
        const auto retry = m_retryFn;
        retry();
    });
    return true;
}

QString AIProvider::shortModelName() const
{
    const QString model = modelName();
    for (const ModelOption& opt : availableModels()) {
        if (opt.id == model)
            return opt.displayName;
    }
    return model;
}

// Per-shot cost lines, from the tokens each model used on a real shot-analysis
// request (system + user prompt, cold cache) at the provider's published rate;
// monthly = 3 shots a day. AI_ADVISOR.md carries the rates and token counts.
// These rot: a catalog change updates this table in the same diff. A model with
// no line shows none, rather than a borrowed price.
namespace {
struct CostLine { const char* model; const char* key; const char* text; };
const CostLine kCostLines[] = {
    { "gpt-6.1-sol", "ai.cost.openai.sol61",
      "About $0.03 per shot — roughly $2.75/month at 3 shots a day." },
    { "gpt-6-luna", "ai.cost.openai.luna6",
      "About $0.0015 per shot — roughly $0.13/month at 3 shots a day." },
    // Sonnet's tokenizer counts ~60% more tokens than OpenAI's for the same
    // prompt, and the first call writes the 5-minute cache at 1.25x.
    { "claude-sonnet-5-5", "ai.cost.anthropic.sonnet55",
      "About $0.06 per shot — roughly $5.30/month at 3 shots a day. "
      "Follow-up questions within a few minutes cost much less." },
    { "gemini-3.8-flash", "ai.cost.gemini.flash38",
      "About $0.012 per shot — roughly $1.10/month at 3 shots a day." },
    // OpenRouter-only lines, from the cost it reported (2026-10-06). Its other
    // models price as their direct counterpart; Sonnet differs, with no cache write.
    { "anthropic/claude-sonnet-5.5", "ai.cost.openrouter.sonnet55",
      "About $0.05 per shot — roughly $4.50/month at 3 shots a day." },
    { "z-ai/glm-5.3-flash", "ai.cost.openrouter.glm53flash",
      "About $0.001 per shot — roughly $0.10/month at 3 shots a day." },
    { "google/gemma-4-31b-it", "ai.cost.openrouter.gemma4",
      "About $0.002 per shot — roughly $0.17/month at 3 shots a day." },
};
}  // namespace

bool AIProvider::offersModel(const QString& modelId) const
{
    const QList<ModelOption> models = availableModels();
    return std::any_of(models.cbegin(), models.cend(),
                       [&modelId](const ModelOption& m) { return m.id == modelId; });
}

QString AIProvider::costHintFor(const QString& modelId) const
{
    if (!offersModel(modelId))
        return {};
    for (const QString& id : { modelId, modelId.section(QLatin1Char('/'), -1) }) {
        for (const CostLine& line : kCostLines) {
            if (id == QLatin1String(line.model))
                return tr_(line.key, line.text);
        }
    }
    return {};
}

QString AIProvider::defaultCatalogModel() const
{
    const QList<ModelOption> models = availableModels();
    return models.isEmpty() ? QString() : models.first().id;
}

void AIProvider::selectCatalogModel(QString& model, const QString& modelId)
{
    if (modelId.isEmpty()) {
        model = defaultCatalogModel();
        return;
    }
    if (offersModel(modelId)) {
        model = modelId;
        return;
    }
    PROVIDER_WARN("AIProvider") << name() << "ignoring unknown model id:" << modelId;
}

void AIProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    // Default fallback: flatten messages into a single string and call analyze()
    // This loses multi-turn context — providers should override for native support
    PROVIDER_WARN("AIProvider") << "analyzeConversation: Using flatten fallback for provider"
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
    m_model = defaultCatalogModel();
}

QList<AIProvider::ModelOption> OpenAIProvider::availableModels() const
{
    // One balanced and one value pick, each the newest of its tier; rationale
    // and measurements in docs/CLAUDE_MD/AI_ADVISOR.md.
    return {
        { "gpt-6.1-sol", "GPT-6.1 Sol" },
        { "gpt-6-luna", "GPT-6 Luna" },
    };
}

QString OpenAIProvider::modelHint() const
{
    return QStringLiteral(
               "GPT-6.1 Sol is recommended: OpenAI's newest, concise and careful. GPT-6 Luna costs "
               "about a twentieth as much, the best value of any model tested, with shorter replies.");
}

void OpenAIProvider::sendRequest(const QJsonObject& requestBody)
{
    QString urlStr = m_baseUrl.isEmpty()
        ? QString::fromLatin1(API_URL)
        : m_baseUrl + QStringLiteral("/v1/chat/completions");
    QUrl url(urlStr);
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
        emit analysisFailed(tr_("ai.openai.keyMissing", "OpenAI API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

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
    // gpt-5-family reasoning models REJECT the legacy max_tokens parameter on
    // chat/completions ("Unsupported parameter") — max_completion_tokens is
    // the accepted cap. Live-caught July 2026: stage-1 extraction and the
    // advisor both 400'd on gpt-5.4/gpt-5.4-mini.
    requestBody["max_completion_tokens"] = MAX_OUTPUT_TOKENS;
    // Lowest reasoning, per model; shared with the bulk translator.
    AIRequestShape::disableOpenAIReasoning(requestBody, m_model);

    sendRequest(requestBody);
}

void OpenAIProvider::analyzeUrl(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.openai.keyMissing", "OpenAI API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

    // The web_search tool lives on the Responses API, not chat/completions.
    // Its open_page action lets the model retrieve the specific URL named in
    // the user prompt (add-recipe-wizard-tea stage-2 extraction).
    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["instructions"] = systemPrompt;
    requestBody["input"] = userPrompt;
    QJsonObject searchTool;
    searchTool["type"] = QString("web_search");
    requestBody["tools"] = QJsonArray{searchTool};
    // Reasoning "low": the lowest value every catalog entry accepts here
    // (gpt-6.1-sol rejects "none"; both entries verified with web_search at
    // "low", 2026-10-05). max_output_tokens covers reasoning + the JSON answer.
    // Unlike analyze(), effort is no nextShot-block risk: this path extracts
    // recipe JSON from a URL and never emits that block.
    QJsonObject reasoning;
    reasoning["effort"] = QString("low");
    requestBody["reasoning"] = reasoning;
    requestBody["max_output_tokens"] = MAX_OUTPUT_TOKENS;

    sendResponsesRequest(requestBody);
}

void OpenAIProvider::searchWeb(const QString& systemPrompt, const QString& userPrompt)
{
    // OpenAI is the one provider whose single Responses `web_search` tool both
    // searches and opens a page, so this request is analyzeUrl's request. An
    // alias, not a copy: two bodies that must stay identical are two bodies
    // free to drift.
    analyzeUrl(systemPrompt, userPrompt);
}


void OpenAIProvider::sendResponsesRequest(const QJsonObject& requestBody)
{
    QString urlStr = m_baseUrl.isEmpty()
        ? QString::fromLatin1(RESPONSES_API_URL)
        : m_baseUrl + QStringLiteral("/v1/responses");
    QUrl url(urlStr);
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    req.setTransferTimeout(ANALYSIS_TIMEOUT_MS);

    m_retryFn = [this, requestBody]() { sendResponsesRequest(requestBody); };

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onResponsesReply(reply);
    });
}

void OpenAIProvider::onResponsesReply(QNetworkReply* reply)
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
                PROVIDER_DEBUG("aiprovider") << "OpenAI Responses API error" << status << "remoteErrorContentOmitted";
                emit analysisFailed(tr_("ai.openai.error", "OpenAI error: %1").arg(apiError));
                return;
            }
            // Bounded/classified, never the raw body — see logSafeErrorBody().
            PROVIDER_DEBUG("aiprovider") << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << logSafeErrorBody(body);
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error") && root["error"].isObject()) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        if (!errorMsg.isEmpty()) {
            emit analysisFailed(tr_("ai.openai.error", "OpenAI error: %1").arg(errorMsg));
            return;
        }
    }

    // The Responses output array interleaves reasoning/web_search_call items
    // with message items; the answer is the message items' output_text parts.
    // Collect the part types too: when the answer is missing, WHAT came back
    // instead is the whole diagnosis (#1691's lesson — log shape, not content).
    QString text;
    QString refusal;
    QStringList partTypes;
    const QJsonArray output = root["output"].toArray();
    for (const QJsonValue& itemVal : output) {
        const QJsonObject item = itemVal.toObject();
        if (item["type"].toString() != QLatin1String("message"))
            continue;
        const QJsonArray content = item["content"].toArray();
        for (const QJsonValue& partVal : content) {
            const QJsonObject part = partVal.toObject();
            const QString partType = part["type"].toString();
            partTypes << partType;
            if (partType == QLatin1String("output_text"))
                text += part["text"].toString();
            else if (partType == QLatin1String("refusal"))
                refusal = part["refusal"].toString();
        }
    }

    // Gate on completion, not on one reason. `incomplete_details.reason` is
    // also "content_filter", and status can be "failed"/"cancelled" — anything
    // other than "completed" means the text in hand is not the whole answer.
    const QString status = root["status"].toString();
    const QString incompleteReason = root["incomplete_details"].toObject()["reason"].toString();
    const bool truncated = status != QLatin1String("completed");
    if (text.isEmpty() || truncated) {
        PROVIDER_DEBUG("aiprovider") << "OpenAI Responses: model" << diagnosticModel() << "status" << diagnosticCode(status)
                   << "incomplete_reason" << diagnosticCode(incompleteReason)
                   << "part count" << partTypes.size() << "text chars" << text.size();
        // A refusal explains itself; surfacing it beats the generic message,
        // which is the failure #1691 took three days to place.
        if (text.isEmpty() && !refusal.isEmpty()) {
            emit analysisFailed(tr_("ai.openai.refused", "OpenAI declined the request: %1").arg(refusal));
            return;
        }
        if (dispatchTruncatedOrEmpty(text, truncated,
                tr_("ai.openai.emptyContent", "OpenAI returned empty response content")))
            return;
    }
    emit analysisComplete(text);
}

void OpenAIProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.openai.keyMissing", "OpenAI API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    // A conversation turn is prose the user reads, so a cut-off reply still has
    // value — show it with a notice rather than discarding it (see
    // TruncationPolicy). The one-shot analyze()/analyzeUrl() paths keep Fail:
    // their result is machine-parsed.
    m_truncationPolicy = TruncationPolicy::ShowPartial;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["messages"] = buildOpenAIMessages(systemPrompt, messages);
    requestBody["max_completion_tokens"] = MAX_OUTPUT_TOKENS;
    // Sol runs at "low", its lowest; it emitted the nextShot block on every
    // tasted scenario in the 2026-10-05 replay (tools/ai_model_eval/README.md).
    AIRequestShape::disableOpenAIReasoning(requestBody, m_model);

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
                PROVIDER_DEBUG("aiprovider") << "OpenAI API error" << status << "remoteErrorContentOmitted";
                emit analysisFailed(tr_("ai.openai.error", "OpenAI error: %1").arg(apiError));
                return;
            }
            // Bounded/classified, never the raw body — see logSafeErrorBody().
            PROVIDER_DEBUG("aiprovider") << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << logSafeErrorBody(body);
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed(tr_("ai.openai.error", "OpenAI error: %1").arg(errorMsg));
        return;
    }

    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        emit analysisFailed(tr_("ai.openai.noResponse", "OpenAI returned no response"));
        return;
    }

    const QJsonObject choice = choices[0].toObject();
    const QString finishReason = choice["finish_reason"].toString();
    const QJsonObject message = choice["message"].toObject();
    QString content = message["content"].toString();
    // "length" = hit max_completion_tokens; "content_filter" = moderation cut
    // it short. Either way the text in hand is not the whole answer.
    const bool truncated = finishReason == QLatin1String("length")
                        || finishReason == QLatin1String("content_filter");
    if (content.isEmpty() || truncated) {
        PROVIDER_DEBUG("OpenAI") << "model" << diagnosticModel() << "finish_reason" << diagnosticCode(finishReason)
                   << "content chars" << content.size()
                   << "reasoning tokens"
                   << root["usage"].toObject()["completion_tokens_details"]
                          .toObject()["reasoning_tokens"].toInt();
        // `message.refusal` carries the model's own stated reason; without this
        // it reads as a bare "empty response content".
        const QString refusal = message["refusal"].toString();
        if (content.isEmpty() && !refusal.isEmpty()) {
            emit analysisFailed(tr_("ai.openai.refused", "OpenAI declined the request: %1").arg(refusal));
            return;
        }
        if (dispatchTruncatedOrEmpty(content, truncated,
                tr_("ai.openai.emptyContent", "OpenAI returned empty response content")))
            return;
    }
    emit analysisComplete(content);
}

void OpenAIProvider::testConnection()
{
    if (!isConfigured()) {
        emit testResult(false, tr_("ai.test.keyNotConfigured", "API key not configured"));
        return;
    }

    // Simple test: list models
    QString urlStr = m_baseUrl.isEmpty()
        ? QStringLiteral("https://api.openai.com/v1/models")
        : m_baseUrl + QStringLiteral("/v1/models");
    QUrl url(urlStr);
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
                emit testResult(false, tr_("ai.test.authFailed", "Authentication failed: %1").arg(errorMsg));
                return;
            }
        }
        emit testResult(false, tr_("ai.test.invalidKey", "Invalid API key"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, tr_("ai.test.apiError", "API error: %1").arg(errorMsg));
                return;
            }
        }
        emit testResult(false, tr_("ai.test.connectionFailed", "Connection failed: %1").arg(reply->errorString()));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.object().contains("error")) {
        QJsonValue errVal = doc.object()["error"];
        QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
        if (errorMsg.isEmpty())
            errorMsg = tr_("ai.test.unknownError", "Unknown API error");
        emit testResult(false, tr_("ai.test.apiError", "API error: %1").arg(errorMsg));
        return;
    }

    emit testResult(true, tr_("ai.openai.connected", "Connected to OpenAI successfully"));
}

// ============================================================================
// Anthropic Provider
// ============================================================================

// Shared with the bulk translator; see src/ai/airequestshape.h.
using AIRequestShape::disableAnthropicThinking;

AnthropicProvider::AnthropicProvider(QNetworkAccessManager* networkManager,
                                     const QString& apiKey,
                                     QObject* parent)
    : AIProvider(networkManager, parent)
    , m_apiKey(apiKey)
{
    m_model = defaultCatalogModel();
}

QList<AIProvider::ModelOption> AnthropicProvider::availableModels() const
{
    // No value pick passed; see docs/CLAUDE_MD/AI_ADVISOR.md.
    return {
        { "claude-sonnet-5-5", "Sonnet 5.5" },
    };
}

QString AnthropicProvider::modelHint() const
{
    return QStringLiteral(
               "Sonnet 5.5: the most thorough dial-in reasoning, with the longest replies.");
}

void AnthropicProvider::sendRequest(const QJsonObject& requestBody, const QByteArray& betaFeature)
{
    QString urlStr = m_baseUrl.isEmpty()
        ? QString::fromLatin1(API_URL)
        : m_baseUrl + QStringLiteral("/v1/messages");
    QUrl url(urlStr);
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("x-api-key", m_apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");
    // Beta opt-in, for the request bodies that carry a beta tool. Empty for
    // everything else — the header is per-feature, not a blanket flag.
    if (!betaFeature.isEmpty())
        req.setRawHeader("anthropic-beta", betaFeature);
    // Caching is per block in the body: see ephemeralCache().
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
        emit analysisFailed(tr_("ai.anthropic.keyMissing", "Anthropic API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["max_tokens"] = MAX_OUTPUT_TOKENS;
    disableAnthropicThinking(requestBody, m_model);
    requestBody["system"] = buildCachedSystemPrompt(systemPrompt);
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = userPrompt;
    messages.append(userMsg);
    requestBody["messages"] = messages;

    sendRequest(requestBody);
}

void AnthropicProvider::analyzeUrl(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.anthropic.keyMissing", "Anthropic API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["max_tokens"] = MAX_OUTPUT_TOKENS;
    disableAnthropicThinking(requestBody, m_model);
    requestBody["system"] = buildCachedSystemPrompt(systemPrompt);
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = userPrompt;
    messages.append(userMsg);
    requestBody["messages"] = messages;
    // The web_fetch server tool (add-recipe-wizard-tea stage-2 extraction):
    // the API fetches the URL named in the user prompt during the request.
    // max_uses 2 allows one retry; max_content_tokens bounds the token cost
    // of a huge page (fetched content is billed as input tokens).
    QJsonObject fetchTool;
    fetchTool["type"] = QString("web_fetch_20250910");
    fetchTool["name"] = QString("web_fetch");
    fetchTool["max_uses"] = 2;
    fetchTool["max_content_tokens"] = 20000;
    requestBody["tools"] = QJsonArray{fetchTool};

    // web_fetch is a BETA tool: without the opt-in header the API rejects the
    // request outright, so stage-2 extraction on Anthropic could never have
    // worked. (web_search, used by searchWeb, is GA and needs no header.)
    sendRequest(requestBody, QByteArrayLiteral("web-fetch-2025-09-10"));
}

void AnthropicProvider::searchWeb(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.anthropic.keyMissing", "Anthropic API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["max_tokens"] = MAX_OUTPUT_TOKENS;
    disableAnthropicThinking(requestBody, m_model);
    requestBody["system"] = buildCachedSystemPrompt(systemPrompt);
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = userPrompt;
    messages.append(userMsg);
    requestBody["messages"] = messages;
    // web_search, NOT web_fetch: this path has no URL to fetch — finding one is
    // the whole question. web_fetch validates that the URL appears in the
    // message, so it cannot serve a search at all.
    QJsonObject searchTool;
    searchTool["type"] = QString("web_search_20250305");
    searchTool["name"] = QString("web_search");
    searchTool["max_uses"] = 3;
    requestBody["tools"] = QJsonArray{searchTool};

    sendRequest(requestBody);
}

void AnthropicProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.anthropic.keyMissing", "Anthropic API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    // A conversation turn is prose the user reads, so a cut-off reply still has
    // value — show it with a notice rather than discarding it (see
    // TruncationPolicy). The one-shot analyze()/analyzeUrl() paths keep Fail:
    // their result is machine-parsed.
    m_truncationPolicy = TruncationPolicy::ShowPartial;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["max_tokens"] = MAX_OUTPUT_TOKENS;
    disableAnthropicThinking(requestBody, m_model);
    requestBody["system"] = buildCachedSystemPrompt(systemPrompt);
    requestBody["messages"] = messagesWithCachedFirstUser(messages);

    sendRequest(requestBody);
}

// The cache_control every cached block carries. 5 minutes, not 1 hour: a
// 5-minute write costs 1.25x input against 2x, reads cost 0.1x either way, and
// each read restarts the TTL, so back-to-back questions stay cached. 1 hour
// only pays when the next question comes 5-60 minutes later, the uncommon
// case here (2026-10-05; the arithmetic is in AI_ADVISOR.md).
static QJsonObject ephemeralCache()
{
    QJsonObject cacheControl;
    cacheControl["type"] = QString("ephemeral");
    cacheControl["ttl"] = QString("5m");  // Anthropic API: Literal["5m", "1h"]
    return cacheControl;
}

QJsonArray AnthropicProvider::messagesWithCachedFirstUser(const QJsonArray& messages)
{
    // The first user message carries the per-shot context, which follow-up
    // turns resend: cache it so they read it at 0.1x.
    //
    // No-op when messages[0] isn't a plain-string user message (caller
    // pre-wrapped, or first message isn't from user) — preserves input.
    if (messages.isEmpty()) return messages;
    QJsonObject first = messages[0].toObject();
    if (first.value("role").toString() != "user") return messages;
    if (!first.value("content").isString()) return messages;

    QJsonObject block;
    block["type"] = QString("text");
    block["text"] = first.value("content").toString();
    block["cache_control"] = ephemeralCache();

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
    // The system prompt is the same for every shot, so it caches across shots too.
    QJsonObject block;
    block["type"] = QString("text");
    block["text"] = systemPrompt;
    block["cache_control"] = ephemeralCache();

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
                PROVIDER_DEBUG("aiprovider") << "Anthropic API error" << status << "remoteErrorContentOmitted";
                emit analysisFailed(tr_("ai.anthropic.error", "Anthropic error: %1").arg(apiError));
                return;
            }
            // Bounded/classified, never the raw body — see logSafeErrorBody().
            PROVIDER_DEBUG("aiprovider") << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << logSafeErrorBody(body);
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed(tr_("ai.anthropic.error", "Anthropic error: %1").arg(errorMsg));
        return;
    }

    // Read stop_reason BEFORE any content gate. An early return above it makes
    // the truncation branch unreachable for exactly the case that matters —
    // the reply that stopped with nothing to show. That ordering bug shipped
    // in this file's Gemini handler and is fixed there too.
    const QString stopReason = root["stop_reason"].toString();

    QJsonArray content = root["content"].toArray();
    if (content.isEmpty()) {
        PROVIDER_DEBUG("Anthropic") << "model" << diagnosticModel() << "no content blocks, stop_reason" << diagnosticCode(stopReason);
        emit analysisFailed(stopReason == QLatin1String("max_tokens")
            ? truncatedResponseError()
            : tr_("ai.anthropic.noResponse", "Anthropic returned no response"));
        return;
    }

    // Join every text block: plain replies have exactly one, but a server-
    // tool response (analyzeUrl's web_fetch) interleaves server_tool_use and
    // web_fetch_tool_result blocks with the text — content[0] alone would
    // miss the answer.
    QString text;
    for (const QJsonValue& block : content) {
        const QJsonObject obj = block.toObject();
        if (obj["type"].toString() == QLatin1String("text"))
            text += obj["text"].toString();
    }
    // Treat anything that is not a natural end as an unfinished turn, rather
    // than listing the bad reasons. "pause_turn" is the one that forced this:
    // it means the API paused a long-running server-tool turn and the response
    // must be fed back to continue, so its text is partial by definition — and
    // analyzeUrl() attaches the web_fetch server tool, so this path can reach
    // it. "refusal" and "model_context_window_exceeded" are likewise not
    // answers. Allow-listing the two good reasons fails safe as Anthropic adds
    // more; enumerating the bad ones does not.
    const bool unfinished = stopReason != QLatin1String("end_turn")
                         && stopReason != QLatin1String("stop_sequence");
    if (text.isEmpty() || unfinished) {
        // Log the block types on any text-less reply: content can be non-empty
        // while carrying no text block at all (a thinking-only reply — #1691),
        // and the generic message alone made that indistinguishable from a
        // refusal. The model matters too: the #1691 root cause was that the
        // thinking default differs BETWEEN models.
        QStringList blockTypes;
        for (const QJsonValue& block : content)
            blockTypes << block.toObject()["type"].toString();
        PROVIDER_DEBUG("Anthropic") << "model" << diagnosticModel() << "stop_reason" << diagnosticCode(stopReason)
                   << "block count" << blockTypes.size() << "text chars" << text.size()
                   << "output tokens" << root["usage"].toObject()["output_tokens"].toInt();
        if (dispatchTruncatedOrEmpty(text, unfinished,
                tr_("ai.anthropic.emptyContent", "Anthropic returned empty response content")))
            return;
    }
    emit analysisComplete(text);
}

void AnthropicProvider::testConnection()
{
    if (!isConfigured()) {
        emit testResult(false, tr_("ai.test.keyNotConfigured", "API key not configured"));
        return;
    }

    // Send a minimal request to test the API key. Thinking off for the same
    // reason as the analysis paths — with thinking on, a 10-token budget
    // produces a reply with no text block. This check only looks for an error,
    // so it PASSED while every real request failed (#1691): the user's key
    // tested fine and the advisor was unusable.
    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["max_tokens"] = 10;
    disableAnthropicThinking(requestBody, m_model);
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = QString("Hi");
    messages.append(userMsg);
    requestBody["messages"] = messages;

    QString urlStr = m_baseUrl.isEmpty()
        ? QString::fromLatin1(API_URL)
        : m_baseUrl + QStringLiteral("/v1/messages");
    QUrl url(urlStr);
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
                emit testResult(false, tr_("ai.test.authFailed", "Authentication failed: %1").arg(errorMsg));
                return;
            }
        }
        emit testResult(false, tr_("ai.test.invalidKey", "Invalid API key"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, tr_("ai.test.apiError", "API error: %1").arg(errorMsg));
                return;
            }
        }
        emit testResult(false, tr_("ai.test.connectionFailed", "Connection failed: %1").arg(reply->errorString()));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.object().contains("error")) {
        QJsonValue errVal = doc.object()["error"];
        QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
        if (errorMsg.isEmpty())
            errorMsg = tr_("ai.test.unknownError", "Unknown API error");
        emit testResult(false, tr_("ai.test.apiError", "API error: %1").arg(errorMsg));
        return;
    }

    emit testResult(true, tr_("ai.anthropic.connected", "Connected to Anthropic successfully"));
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
    m_model = defaultCatalogModel();
}

QList<AIProvider::ModelOption> GeminiProvider::availableModels() const
{
    // 3.8 Flash only: it costs half of 3.5 Flash, and every cheaper Gemini
    // (3.5 and 3.1 Flash-Lite, 2.5 Flash) gave wrong grind advice in the
    // 2026-10-05 replay (tools/ai_model_eval/README.md).
    return {
        { "gemini-3.8-flash", "3.8 Flash" },
    };
}

QString GeminiProvider::modelHint() const
{
    return QStringLiteral(
               "3.8 Flash: Google's newest Flash, sound advice at a low price.");
}

QString GeminiProvider::apiUrl() const
{
    // Use URL without key - key is passed via header for better security
    const QString host = m_baseUrl.isEmpty()
        ? QStringLiteral("https://generativelanguage.googleapis.com")
        : m_baseUrl;
    return host + QStringLiteral("/v1beta/models/%1:generateContent").arg(m_model);
}

void GeminiProvider::sendRequest(const QJsonObject& requestBody)
{
    QUrl url(apiUrl());
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());
    req.setTransferTimeout(ANALYSIS_TIMEOUT_MS);

    QJsonObject bodyWithConfig = requestBody;
    bodyWithConfig["generationConfig"] = AIRequestShape::geminiGenerationConfig(m_model);

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
        emit analysisFailed(tr_("ai.gemini.keyMissing", "Gemini API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

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

void GeminiProvider::analyzeUrl(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.gemini.keyMissing", "Gemini API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

    // Same body as analyze() plus the url_context server tool: the API
    // fetches the URL named in the user prompt during generateContent
    // (add-recipe-wizard-tea stage-2 extraction).
    QJsonObject requestBody;
    QJsonObject sysInstruction;
    QJsonArray sysParts;
    QJsonObject sysTextPart;
    sysTextPart["text"] = systemPrompt;
    sysParts.append(sysTextPart);
    sysInstruction["parts"] = sysParts;
    requestBody["system_instruction"] = sysInstruction;

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

    QJsonObject urlContextTool;
    urlContextTool["url_context"] = QJsonObject{};
    requestBody["tools"] = QJsonArray{urlContextTool};

    sendRequest(requestBody);
}

void GeminiProvider::searchWeb(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.gemini.keyMissing", "Gemini API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

    QJsonObject requestBody;
    QJsonObject sysInstruction;
    QJsonArray sysParts;
    QJsonObject sysTextPart;
    sysTextPart["text"] = systemPrompt;
    sysParts.append(sysTextPart);
    sysInstruction["parts"] = sysParts;
    requestBody["system_instruction"] = sysInstruction;

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

    // google_search grounding, NOT url_context: url_context only fetches URLs
    // the prompt already names, so asked to FIND a page the model answers from
    // memory — a hallucinated URL wearing a tool's credibility.
    QJsonObject searchTool;
    searchTool["google_search"] = QJsonObject{};
    requestBody["tools"] = QJsonArray{searchTool};

    sendRequest(requestBody);
}

void GeminiProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.gemini.keyMissing", "Gemini API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    // A conversation turn is prose the user reads, so a cut-off reply still has
    // value — show it with a notice rather than discarding it (see
    // TruncationPolicy). The one-shot analyze()/analyzeUrl() paths keep Fail:
    // their result is machine-parsed.
    m_truncationPolicy = TruncationPolicy::ShowPartial;

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
            PROVIDER_WARN("GeminiProvider") << "Skipping message with unexpected role:" << role;
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
                PROVIDER_DEBUG("aiprovider") << "Gemini API error" << status << "remoteErrorContentOmitted";
                emit analysisFailed(tr_("ai.gemini.error", "Gemini error: %1").arg(apiError));
                return;
            }
            // Bounded/classified, never the raw body — see logSafeErrorBody().
            PROVIDER_DEBUG("aiprovider") << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << logSafeErrorBody(body);
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed(tr_("ai.gemini.error", "Gemini error: %1").arg(errorMsg));
        return;
    }

    const QJsonObject usage = root["usageMetadata"].toObject();
    PROVIDER_DEBUG("aiprovider") << "Gemini usage — prompt:" << usage["promptTokenCount"].toInt()
            << "thoughts:" << usage["thoughtsTokenCount"].toInt()
            << "output:" << usage["candidatesTokenCount"].toInt()
            << "total:" << usage["totalTokenCount"].toInt();

    QJsonArray candidates = root["candidates"].toArray();
    if (candidates.isEmpty()) {
        // A prompt-level block returns promptFeedback.blockReason and no
        // candidates. That reason IS the explanation; discarding it left the
        // user with five generic words and the log with nothing.
        const QString blockReason = root["promptFeedback"].toObject()["blockReason"].toString();
        PROVIDER_DEBUG("Gemini") << "model" << diagnosticModel() << "no candidates, blockReason" << diagnosticCode(blockReason);
        emit analysisFailed(blockReason.isEmpty()
            ? tr_("ai.gemini.noResponse", "Gemini returned no response")
            : tr_("ai.gemini.blocked", "Gemini refused the request (%1).").arg(blockReason));
        return;
    }

    // Read finishReason BEFORE the parts gate. When Gemini stops on MAX_TOKENS
    // *while still thinking* — and gemini-3.5-flash runs thinkingLevel
    // "minimal", which is on, not off — the candidate comes back carrying a
    // finishReason and NO content key at all. Reading it after an
    // `parts.isEmpty()` early return made this branch unreachable for exactly
    // the #1691-shaped failure it exists to catch: budget eaten by hidden
    // reasoning, no text, opaque error. SAFETY/RECITATION/PROHIBITED_CONTENT
    // arrive the same way.
    const QString finishReason = candidates[0].toObject()["finishReason"].toString();
    const bool truncated = finishReason == QLatin1String("MAX_TOKENS");

    // Join every non-thought text part: plain replies have exactly one, but
    // a url_context response (analyzeUrl) may split the answer across parts;
    // thought parts are hidden reasoning and must not leak into the answer.
    const QJsonArray parts = candidates[0].toObject()["content"].toObject()["parts"].toArray();
    QString text;
    for (const QJsonValue& partVal : parts) {
        const QJsonObject part = partVal.toObject();
        if (part["thought"].toBool())
            continue;
        text += part["text"].toString();
    }

    if (text.isEmpty() || truncated) {
        // thoughtsTokenCount is the field that names a thinking-ate-the-budget
        // failure on sight, so log it next to the reason rather than only in
        // the qInfo line above.
        PROVIDER_DEBUG("Gemini") << "model" << diagnosticModel() << "finishReason" << diagnosticCode(finishReason)
                   << "parts" << parts.size() << "text chars" << text.size()
                   << "thought tokens" << usage["thoughtsTokenCount"].toInt()
                   << "output tokens" << usage["candidatesTokenCount"].toInt();
        // One message for both empty cases. There used to be two strings one
        // word apart ("empty content" vs "empty response content"), only one of
        // which logged — a user reporting either could not say which they hit.
        if (dispatchTruncatedOrEmpty(text, truncated,
                tr_("ai.gemini.emptyContent", "Gemini returned empty response content")))
            return;
    }
    emit analysisComplete(text);
}

void GeminiProvider::testConnection()
{
    if (!isConfigured()) {
        emit testResult(false, tr_("ai.test.keyNotConfigured", "API key not configured"));
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
                emit testResult(false, tr_("ai.test.authFailed", "Authentication failed: %1").arg(errorMsg));
                return;
            }
        }
        emit testResult(false, tr_("ai.test.invalidKey", "Invalid API key"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, tr_("ai.test.apiError", "API error: %1").arg(errorMsg));
                return;
            }
        }
        emit testResult(false, tr_("ai.test.connectionFailed", "Connection failed: %1").arg(reply->errorString()));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.object().contains("error")) {
        QJsonValue errVal = doc.object()["error"];
        QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
        if (errorMsg.isEmpty())
            errorMsg = tr_("ai.test.unknownError", "Unknown API error");
        emit testResult(false, tr_("ai.test.apiError", "API error: %1").arg(errorMsg));
        return;
    }

    emit testResult(true, tr_("ai.gemini.connected", "Connected to Gemini successfully"));
}

// ============================================================================
// OpenRouter Provider
// ============================================================================

OpenRouterProvider::OpenRouterProvider(QNetworkAccessManager* networkManager,
                                         const QString& apiKey,
                                         QObject* parent)
    : AIProvider(networkManager, parent)
    , m_apiKey(apiKey)
{
    m_model = defaultCatalogModel();
}

QList<AIProvider::ModelOption> OpenRouterProvider::availableModels() const
{
    // The direct providers' models, Luna first as the default, plus the two
    // cheap models that gave right grind advice with reasoning on. The others
    // tried gave wrong advice or no reply (tools/ai_model_eval/README.md,
    // 2026-10-06).
    return {
        { AIRequestShape::kOpenRouterDefaultModel, "GPT-6 Luna" },
        { "openai/gpt-6.1-sol", "GPT-6.1 Sol" },
        { "anthropic/claude-sonnet-5.5", "Sonnet 5.5" },
        { "google/gemini-3.8-flash", "Gemini 3.8 Flash" },
        { "z-ai/glm-5.3-flash", "GLM-5.3 Flash" },
        { AIRequestShape::kOpenRouterGemmaModel, "Gemma 4 31B" },
    };
}

QString OpenRouterProvider::modelHint() const
{
    return QStringLiteral(
               "GPT-6 Luna is the best value of any model tested. GPT-6.1 Sol, Sonnet 5.5 and "
               "Gemini 3.8 Flash are the same models the direct providers offer, at the same "
               "per-token price. GLM-5.3 Flash and Gemma 4 31B cost about what Luna does, with less "
               "testing behind them. OpenRouter adds a 5.5% fee when you buy credits.");
}

QUrl OpenRouterProvider::chatCompletionsUrl() const
{
    return QUrl(m_baseUrl.isEmpty()
        ? QString::fromLatin1(API_URL)
        : m_baseUrl + QStringLiteral("/api/v1/chat/completions"));
}

void OpenRouterProvider::sendRequest(const QJsonObject& requestBody)
{
    QNetworkRequest req;
    req.setUrl(chatCompletionsUrl());
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
        emit analysisFailed(tr_("ai.openrouter.keyMissing", "OpenRouter API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

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
    requestBody["max_tokens"] = MAX_OUTPUT_TOKENS;
    AIRequestShape::setOpenRouterReasoning(requestBody, m_model);

    sendRequest(requestBody);
}

void OpenRouterProvider::analyzeConversation(const QString& systemPrompt, const QJsonArray& messages)
{
    if (!isConfigured()) {
        emit analysisFailed(tr_("ai.openrouter.keyMissing", "OpenRouter API key not configured"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    // A conversation turn is prose the user reads, so a cut-off reply still has
    // value — show it with a notice rather than discarding it (see
    // TruncationPolicy). The one-shot analyze()/analyzeUrl() paths keep Fail:
    // their result is machine-parsed.
    m_truncationPolicy = TruncationPolicy::ShowPartial;

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["messages"] = buildOpenAIMessages(systemPrompt, messages);
    requestBody["max_tokens"] = MAX_OUTPUT_TOKENS;
    AIRequestShape::setOpenRouterReasoning(requestBody, m_model);

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
                PROVIDER_DEBUG("aiprovider") << "OpenRouter API error" << status << "remoteErrorContentOmitted";
                emit analysisFailed(tr_("ai.openrouter.error", "OpenRouter error: %1").arg(apiError));
                return;
            }
            // Bounded/classified, never the raw body — see logSafeErrorBody().
            PROVIDER_DEBUG("aiprovider") << "AI request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << logSafeErrorBody(body);
        }
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed(tr_("ai.openrouter.error", "OpenRouter error: %1").arg(errorMsg));
        return;
    }

    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        emit analysisFailed(tr_("ai.openrouter.noResponse", "OpenRouter returned no response"));
        return;
    }

    const QJsonObject choice = choices[0].toObject();

    // OpenRouter reports an upstream provider failure as a 200 carrying an
    // error object on the CHOICE, which the top-level root["error"] check above
    // does not see.
    const QJsonObject choiceError = choice["error"].toObject();
    if (!choiceError.isEmpty()) {
        const QString message = choiceError["message"].toString();
        PROVIDER_DEBUG("OpenRouter") << "model" << diagnosticModel() << "upstream error"
                   << "remoteErrorContentOmitted";
        emit analysisFailed(tr_("ai.openrouter.error", "OpenRouter error: %1")
            .arg(message.isEmpty() ? tr_("ai.error.unknown", "unknown error") : message));
        return;
    }

    // finish_reason "length" = the answer hit max_tokens, e.g. when hidden
    // reasoning tokens eat the cap the way #1691's did. "content_filter" and
    // "error" likewise mean the text in hand is not the whole answer.
    const QString finishReason = choice["finish_reason"].toString();
    const QJsonObject message = choice["message"].toObject();
    QString content = message["content"].toString();
    const bool truncated = finishReason == QLatin1String("length")
                        || finishReason == QLatin1String("content_filter")
                        || finishReason == QLatin1String("error");
    if (content.isEmpty() || truncated) {
        PROVIDER_DEBUG("OpenRouter") << "model" << diagnosticModel() << "finish_reason" << diagnosticCode(finishReason)
                   << "content chars" << content.size();
        const QString refusal = message["refusal"].toString();
        if (content.isEmpty() && !refusal.isEmpty()) {
            emit analysisFailed(tr_("ai.openrouter.refused",
                                    "OpenRouter's model declined the request: %1").arg(refusal));
            return;
        }
        if (dispatchTruncatedOrEmpty(content, truncated,
                tr_("ai.openrouter.emptyContent", "OpenRouter returned empty response content")))
            return;
    }
    emit analysisComplete(content);
}

void OpenRouterProvider::testConnection()
{
    if (!isConfigured()) {
        emit testResult(false, tr_("ai.openrouter.testKey", "API key not configured"));
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
    AIRequestShape::setOpenRouterReasoning(requestBody, m_model);

    QNetworkRequest req;
    req.setUrl(chatCompletionsUrl());
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
                emit testResult(false, tr_("ai.test.authFailed", "Authentication failed: %1").arg(errorMsg));
                return;
            }
        }
        emit testResult(false, tr_("ai.test.invalidKey", "Invalid API key"));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (doc.isObject() && doc.object().contains("error")) {
            QJsonValue errVal = doc.object()["error"];
            QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
            if (!errorMsg.isEmpty()) {
                emit testResult(false, tr_("ai.test.apiError", "API error: %1").arg(errorMsg));
                return;
            }
        }
        emit testResult(false, tr_("ai.test.connectionFailed", "Connection failed: %1").arg(reply->errorString()));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.object().contains("error")) {
        QJsonValue errVal = doc.object()["error"];
        QString errorMsg = errVal.isObject() ? errVal.toObject()["message"].toString() : errVal.toString();
        if (errorMsg.isEmpty())
            errorMsg = tr_("ai.test.unknownError", "Unknown API error");
        emit testResult(false, tr_("ai.test.apiError", "API error: %1").arg(errorMsg));
        return;
    }

    emit testResult(true, tr_("ai.openrouter.connected", "Connected to OpenRouter successfully"));
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
        emit analysisFailed(tr_("ai.ollama.notConfigured", "Ollama not configured (need endpoint and model)"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    m_truncationPolicy = TruncationPolicy::Fail;

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
        emit analysisFailed(tr_("ai.ollama.notConfigured", "Ollama not configured (need endpoint and model)"));
        return;
    }

    setStatus(Status::Busy);
    m_retryCount = 0;
    ++m_reqGen;
    // A conversation turn is prose the user reads, so a cut-off reply still has
    // value — show it with a notice rather than discarding it (see
    // TruncationPolicy). The one-shot analyze()/analyzeUrl() paths keep Fail:
    // their result is machine-parsed.
    m_truncationPolicy = TruncationPolicy::ShowPartial;

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
            PROVIDER_DEBUG("aiprovider") << "Ollama request failed"
                       << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                       << "-" << logSafeErrorBody(body);
        emit analysisFailed(friendlyNetworkError(reply));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        emit analysisFailed(tr_("ai.ollama.error", "Ollama error: %1").arg(root["error"].toString()));
        return;
    }

    // Support both /api/chat (message.content) and /api/generate (response) formats
    const QJsonObject message = root["message"].toObject();
    QString response = message["content"].toString();
    if (response.isEmpty()) {
        response = root["response"].toString();
        // Warn, not qDebug: taking the /api/generate shape off what may have
        // been an /api/chat request means one of the two assumptions is wrong,
        // and this fallback otherwise masks a legitimately-empty chat reply
        // (truncation, refusal, thinking-only) behind a shape probe.
        if (!response.isEmpty())
            PROVIDER_WARN("OllamaProvider") << "/api/chat message.content was empty; "
                          "fell back to the /api/generate response field";
    }

    // This app sets no token cap for Ollama, but that is not the same as "it
    // cannot truncate": num_predict and num_ctx live in the USER's Modelfile
    // and server config, and Ollama reports done_reason "length" when either
    // one stops generation. Without this check a locally-truncated reply was
    // emitted as complete — the same defect this change fixes on the four
    // cloud providers, on the fifth.
    const QString doneReason = root["done_reason"].toString();
    const bool truncated = doneReason == QLatin1String("length");

    if (response.isEmpty() || truncated) {
        // Thinking models (deepseek-r1, qwen3) return reasoning in
        // message.thinking with a possibly-empty message.content — literally
        // #1691's shape, on a local model. Say so rather than logging nothing,
        // which is what this branch did.
        PROVIDER_DEBUG("Ollama") << "model" << diagnosticModel() << "done_reason" << diagnosticCode(doneReason)
                   << "content chars" << response.size()
                   << "thinking chars" << message["thinking"].toString().size()
                   << "eval_count" << root["eval_count"].toInt();
        if (dispatchTruncatedOrEmpty(response, truncated,
                tr_("ai.ollama.emptyResponse", "Ollama returned empty response")))
            return;
    }

    emit analysisComplete(response);
}

void OllamaProvider::testConnection()
{
    if (m_endpoint.isEmpty()) {
        emit testResult(false, tr_("ai.ollama.endpointMissing", "Ollama endpoint not configured"));
        return;
    }

    // Test by listing models
    refreshModels();
}

void OllamaProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit testResult(false, tr_("ai.ollama.cannotConnect", "Cannot connect to Ollama: %1").arg(reply->errorString()));
        return;
    }

    emit testResult(true, tr_("ai.ollama.connected", "Connected to Ollama successfully"));
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
        emit testResult(false, tr_("ai.ollama.cannotList", "Cannot list Ollama models: %1").arg(reply->errorString()));
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
        emit testResult(true, tr_("ai.ollama.foundModels", "Found %1 Ollama model(s)").arg(modelNames.size()));
    } else {
        emit testResult(false, tr_("ai.ollama.noModels", "No models found. Run: ollama pull llama3.2"));
    }
}
