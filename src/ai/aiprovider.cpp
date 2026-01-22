#include "aiprovider.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
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

void OpenAIProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("OpenAI API key not configured");
        return;
    }

    setStatus(Status::Busy);

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

    QUrl url(QString::fromLatin1(API_URL));
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void OpenAIProvider::onAnalysisReply(QNetworkReply* reply)
{
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        emit analysisFailed("OpenAI request failed: " + reply->errorString());
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

    QNetworkReply* reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestReply(reply);
    });
}

void OpenAIProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit testResult(false, "Connection failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.object().contains("error")) {
        QString errorMsg = doc.object()["error"].toObject()["message"].toString();
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
}

void AnthropicProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("Anthropic API key not configured");
        return;
    }

    setStatus(Status::Busy);

    QJsonObject requestBody;
    requestBody["model"] = QString::fromLatin1(MODEL);
    requestBody["max_tokens"] = 1024;
    requestBody["system"] = systemPrompt;
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = QString("user");
    userMsg["content"] = userPrompt;
    messages.append(userMsg);
    requestBody["messages"] = messages;

    QUrl url(QString::fromLatin1(API_URL));
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("x-api-key", m_apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void AnthropicProvider::onAnalysisReply(QNetworkReply* reply)
{
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        emit analysisFailed("Anthropic request failed: " + reply->errorString());
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
    requestBody["model"] = QString::fromLatin1(MODEL);
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

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestReply(reply);
    });
}

void AnthropicProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit testResult(false, "Connection failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.object().contains("error")) {
        QString errorMsg = doc.object()["error"].toObject()["message"].toString();
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
}

QString GeminiProvider::apiUrl() const
{
    // Use URL without key - key is passed via header for better security
    return QString("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
        .arg(MODEL);
}

void GeminiProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("Gemini API key not configured");
        return;
    }

    setStatus(Status::Busy);

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

    QUrl url(apiUrl());
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void GeminiProvider::onAnalysisReply(QNetworkReply* reply)
{
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        emit analysisFailed("Gemini request failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        QString errorMsg = root["error"].toObject()["message"].toString();
        emit analysisFailed("Gemini error: " + errorMsg);
        return;
    }

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

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestReply(reply);
    });
}

void GeminiProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit testResult(false, "Connection failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.object().contains("error")) {
        QString errorMsg = doc.object()["error"].toObject()["message"].toString();
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

void OpenRouterProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("OpenRouter API key or model not configured");
        return;
    }

    setStatus(Status::Busy);

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

    QUrl url(QString::fromLatin1(API_URL));
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));
    req.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    // Attribution headers for OpenRouter leaderboard
    req.setRawHeader("HTTP-Referer", "https://github.com/Kulitorum/Decenza");
    req.setRawHeader("X-Title", "Decenza DE1");

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void OpenRouterProvider::onAnalysisReply(QNetworkReply* reply)
{
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        emit analysisFailed("OpenRouter request failed: " + reply->errorString());
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
    req.setRawHeader("X-Title", "Decenza DE1");

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onTestReply(reply);
    });
}

void OpenRouterProvider::onTestReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit testResult(false, "Connection failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.object().contains("error")) {
        QString errorMsg = doc.object()["error"].toObject()["message"].toString();
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

void OllamaProvider::analyze(const QString& systemPrompt, const QString& userPrompt)
{
    if (!isConfigured()) {
        emit analysisFailed("Ollama not configured (need endpoint and model)");
        return;
    }

    setStatus(Status::Busy);

    QJsonObject requestBody;
    requestBody["model"] = m_model;
    requestBody["prompt"] = userPrompt;
    requestBody["system"] = systemPrompt;
    requestBody["stream"] = false;

    QString urlStr = m_endpoint;
    if (!urlStr.endsWith(QString("/"))) urlStr += QString("/");
    urlStr += QString("api/generate");

    QUrl url(urlStr);
    QNetworkRequest req;
    req.setUrl(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QString("application/json")));

    QByteArray body = QJsonDocument(requestBody).toJson();
    QNetworkReply* reply = m_networkManager->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAnalysisReply(reply);
    });
}

void OllamaProvider::onAnalysisReply(QNetworkReply* reply)
{
    reply->deleteLater();
    setStatus(Status::Ready);

    if (reply->error() != QNetworkReply::NoError) {
        emit analysisFailed("Ollama request failed: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject root = doc.object();

    if (root.contains("error")) {
        emit analysisFailed("Ollama error: " + root["error"].toString());
        return;
    }

    QString response = root["response"].toString();
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
