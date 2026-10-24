#include "browserhistorycontroller.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSEngine>
#include <QQmlEngine>
#include <QUrl>
#include <QUuid>
#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, installDecenzaHistoryListener, (), {
    if (window.__decenzaHistoryListenerInstalled)
        return;
    window.__decenzaHistoryListenerInstalled = true;
    window.addEventListener("popstate", function(event) {
        const marker = event.state && event.state.__decenzaHistory === 1
            ? event.state : null;
        let json = JSON.stringify(marker);
        if (json.length > 262144)
            json = "null";
        const pointer = stringToNewUTF8(json);
        Module._decenzaBrowserHistoryPopState(pointer);
        _free(pointer);
    });
    window.addEventListener("hashchange", function() {
        const marker = window.history.state && window.history.state.__decenzaHistory === 1
            ? window.history.state : null;
        const json = JSON.stringify(marker);
        const statePointer = stringToNewUTF8(json.length <= 262144 ? json : "null");
        const rawHash = window.location.hash.slice(1);
        const hashPointer = stringToNewUTF8(rawHash.length <= 2048 ? rawHash : "%00");
        Module._decenzaBrowserHistoryHashChange(hashPointer, statePointer);
        _free(hashPointer);
        _free(statePointer);
    });
});

EM_JS(char*, readDecenzaHistoryState, (), {
    const marker = window.history.state && window.history.state.__decenzaHistory === 1
        ? window.history.state : null;
    const json = JSON.stringify(marker);
    return stringToNewUTF8(json.length <= 262144 ? json : "null");
});

EM_JS(char*, readDecenzaLocationHash, (), {
    const rawHash = window.location.hash.slice(1);
    return stringToNewUTF8(rawHash.length <= 2048 ? rawHash : "%00");
});

EM_JS(void, writeDecenzaHistoryState, (const char* json, const char* routeHash, int push), {
    let state;
    try {
        state = JSON.parse(UTF8ToString(json));
    } catch (_) {
        return;
    }
    if (!state || typeof state !== "object" || state.__decenzaHistory !== 1)
        return;
    if (JSON.stringify(state).length > 262144)
        return;
    const hash = UTF8ToString(routeHash);
    if (hash.length > 2048)
        return;
    const url = window.location.pathname + window.location.search
        + (hash ? "#" + hash : "");
    if (push)
        window.history.pushState(state, "", url);
    else
        window.history.replaceState(state, "", url);
});

EM_JS(void, traverseDecenzaHistory, (int delta), {
    if (delta !== 0)
        window.history.go(delta);
});

EM_JS(void, setDecenzaBeforeUnloadRequired, (int required), {
    window.__decenzaBeforeUnloadRequired = required !== 0;
    if (window.__decenzaBeforeUnloadListenerInstalled)
        return;
    window.__decenzaBeforeUnloadListenerInstalled = true;
    window.addEventListener("beforeunload", function(event) {
        if (!window.__decenzaBeforeUnloadRequired)
            return;
        event.preventDefault();
        event.returnValue = "";
    });
});
#endif

QPointer<BrowserHistoryController> BrowserHistoryController::s_instance;

BrowserHistoryController* BrowserHistoryController::create(QQmlEngine* engine, QJSEngine*)
{
    auto* controller = new BrowserHistoryController(engine);
    QQmlEngine::setObjectOwnership(controller, QQmlEngine::CppOwnership);
    return controller;
}

BrowserHistoryController::BrowserHistoryController(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
#ifdef __EMSCRIPTEN__
    installDecenzaHistoryListener();
#endif
}

BrowserHistoryController::~BrowserHistoryController()
{
    if (s_instance == this)
        s_instance = nullptr;
}

QString BrowserHistoryController::currentStateJson() const
{
#ifdef __EMSCRIPTEN__
    char* json = readDecenzaHistoryState();
    const QString result = QString::fromUtf8(json ? json : "null");
    std::free(json);
    return result;
#else
    return QStringLiteral("null");
#endif
}

QString BrowserHistoryController::currentLocationHash() const
{
#ifdef __EMSCRIPTEN__
    char* hash = readDecenzaLocationHash();
    const QString result = QString::fromUtf8(hash ? hash : "");
    std::free(hash);
    return result;
#else
    return {};
#endif
}

QString BrowserHistoryController::newSessionId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void BrowserHistoryController::replaceState(const QString& stateJson, const QString& routeHash)
{
#ifdef __EMSCRIPTEN__
    const QByteArray json = stateJson.toUtf8();
    const QByteArray hash = routeHash.toUtf8();
    writeDecenzaHistoryState(json.constData(), hash.constData(), 0);
#else
    Q_UNUSED(stateJson);
    Q_UNUSED(routeHash);
#endif
}

void BrowserHistoryController::pushState(const QString& stateJson, const QString& routeHash)
{
#ifdef __EMSCRIPTEN__
    const QByteArray json = stateJson.toUtf8();
    const QByteArray hash = routeHash.toUtf8();
    writeDecenzaHistoryState(json.constData(), hash.constData(), 1);
#else
    Q_UNUSED(stateJson);
    Q_UNUSED(routeHash);
#endif
}

void BrowserHistoryController::goFromApp(int delta, const QString& expectedSessionId, int expectedIndex)
{
#ifdef __EMSCRIPTEN__
    if (delta == 0 || expectedSessionId.isEmpty() || expectedIndex < 0)
        return;
    m_appBackPending = true;
    m_appBackExpectedSessionId = expectedSessionId;
    m_appBackExpectedIndex = expectedIndex;
    traverseDecenzaHistory(delta);
#else
    Q_UNUSED(delta);
    Q_UNUSED(expectedSessionId);
    Q_UNUSED(expectedIndex);
#endif
}

void BrowserHistoryController::restoreAfterPop(int delta, const QString& expectedStateJson,
                                                const QString& expectedRouteHash)
{
#ifdef __EMSCRIPTEN__
    if (m_restorePending || delta == 0)
        return;
    m_restorePending = true;
    m_popRestored = false;
    m_expectedStateJson = expectedStateJson;
    m_expectedRouteHash = expectedRouteHash;
    traverseDecenzaHistory(delta);
#else
    Q_UNUSED(delta);
    Q_UNUSED(expectedStateJson);
    Q_UNUSED(expectedRouteHash);
#endif
}

void BrowserHistoryController::commitRestoredPop(int delta, const QString& expectedStateJson,
                                                  const QString& expectedRouteHash)
{
#ifdef __EMSCRIPTEN__
    if (!m_popRestored || delta == 0)
        return;
    m_popRestored = false;
    m_commitPending = true;
    m_expectedStateJson = expectedStateJson;
    m_expectedRouteHash = expectedRouteHash;
    traverseDecenzaHistory(delta);
#else
    Q_UNUSED(delta);
    Q_UNUSED(expectedStateJson);
    Q_UNUSED(expectedRouteHash);
#endif
}

void BrowserHistoryController::cancelRestoredPop()
{
    m_restorePending = false;
    m_popRestored = false;
    m_commitPending = false;
    m_appBackPending = false;
    m_appBackExpectedSessionId.clear();
    m_appBackExpectedIndex = -1;
    m_expectedStateJson.clear();
    m_expectedRouteHash.clear();
}

void BrowserHistoryController::setBeforeUnloadRequired(bool required)
{
#ifdef __EMSCRIPTEN__
    setDecenzaBeforeUnloadRequired(required ? 1 : 0);
#else
    Q_UNUSED(required);
#endif
}

void BrowserHistoryController::receivePopStateFromBrowser(const QString& stateJson)
{
    if (!s_instance)
        return;
    if (s_instance->m_restorePending) {
        if (stateJson == s_instance->m_expectedStateJson) {
            s_instance->m_restorePending = false;
            s_instance->m_popRestored = true;
            emit s_instance->positionRestored();
            return;
        }
        const QJsonObject actual = QJsonDocument::fromJson(stateJson.toUtf8()).object();
        const QJsonObject expected = QJsonDocument::fromJson(
            s_instance->m_expectedStateJson.toUtf8()).object();
        const bool sameSession = actual.value(QStringLiteral("__decenzaHistory")).toInt() == 1
            && actual.value(QStringLiteral("sessionId")).toString()
                == expected.value(QStringLiteral("sessionId")).toString();
        if (sameSession && actual.value(QStringLiteral("index")).isDouble()
                && expected.value(QStringLiteral("index")).isDouble()) {
            const int delta = expected.value(QStringLiteral("index")).toInt()
                - actual.value(QStringLiteral("index")).toInt();
            if (delta != 0) {
                // Another browser traversal moved while a guarded pop was
                // restoring its source position. Re-serialize that traversal
                // instead of losing the original correlated request.
                traverseDecenzaHistory(delta);
            } else {
                s_instance->replaceState(s_instance->m_expectedStateJson,
                                         s_instance->m_expectedRouteHash);
                s_instance->m_restorePending = false;
                s_instance->m_popRestored = true;
                emit s_instance->positionRestored();
            }
        }
        return;
    } else if (s_instance->m_popRestored && !s_instance->m_commitPending) {
        const QJsonObject actual = QJsonDocument::fromJson(stateJson.toUtf8()).object();
        const QJsonObject expected = QJsonDocument::fromJson(
            s_instance->m_expectedStateJson.toUtf8()).object();
        const bool sameSession = actual.value(QStringLiteral("__decenzaHistory")).toInt() == 1
            && actual.value(QStringLiteral("sessionId")).toString()
                == expected.value(QStringLiteral("sessionId")).toString();
        if (sameSession && actual.value(QStringLiteral("index")).isDouble()
                && expected.value(QStringLiteral("index")).isDouble()) {
            const int delta = expected.value(QStringLiteral("index")).toInt()
                - actual.value(QStringLiteral("index")).toInt();
            if (delta != 0) {
                s_instance->m_popRestored = false;
                s_instance->m_restorePending = true;
                traverseDecenzaHistory(delta);
            } else {
                s_instance->replaceState(s_instance->m_expectedStateJson,
                                         s_instance->m_expectedRouteHash);
            }
        }
        return;
    } else if (s_instance->m_commitPending) {
        if (stateJson == s_instance->m_expectedStateJson) {
            s_instance->m_commitPending = false;
            s_instance->m_popRestored = false;
            s_instance->m_expectedStateJson.clear();
            s_instance->m_expectedRouteHash.clear();
            emit s_instance->popStateCommitted(stateJson);
            return;
        }
        const QJsonObject actual = QJsonDocument::fromJson(stateJson.toUtf8()).object();
        const QJsonObject expected = QJsonDocument::fromJson(
            s_instance->m_expectedStateJson.toUtf8()).object();
        const bool sameSession = actual.value(QStringLiteral("__decenzaHistory")).toInt() == 1
            && actual.value(QStringLiteral("sessionId")).toString()
                == expected.value(QStringLiteral("sessionId")).toString();
        if (sameSession && actual.value(QStringLiteral("index")).isDouble()
                && expected.value(QStringLiteral("index")).isDouble()) {
            const int delta = expected.value(QStringLiteral("index")).toInt()
                - actual.value(QStringLiteral("index")).toInt();
            if (delta != 0) {
                traverseDecenzaHistory(delta);
            } else {
                const QString committedState = s_instance->m_expectedStateJson;
                s_instance->replaceState(s_instance->m_expectedStateJson,
                                         s_instance->m_expectedRouteHash);
                s_instance->m_commitPending = false;
                s_instance->m_expectedStateJson.clear();
                s_instance->m_expectedRouteHash.clear();
                emit s_instance->popStateCommitted(committedState);
            }
        }
        return;
    } else if (s_instance->m_appBackPending) {
        const QJsonDocument document = QJsonDocument::fromJson(stateJson.toUtf8());
        const QJsonObject state = document.object();
        const bool expectedTarget = document.isObject()
            && state.value(QStringLiteral("__decenzaHistory")).toInt() == 1
            && state.value(QStringLiteral("sessionId")).toString() == s_instance->m_appBackExpectedSessionId
            && state.value(QStringLiteral("index")).toInt(-1) == s_instance->m_appBackExpectedIndex;
        s_instance->m_appBackPending = false;
        s_instance->m_appBackExpectedSessionId.clear();
        s_instance->m_appBackExpectedIndex = -1;
        if (expectedTarget)
            emit s_instance->appBackCompleted(stateJson);
        else
            emit s_instance->popStateRequested(stateJson);
    } else {
        emit s_instance->popStateRequested(stateJson);
    }
}

void BrowserHistoryController::receiveHashChangeFromBrowser(const QString& hash,
                                                              const QString& stateJson)
{
    if (s_instance)
        emit s_instance->hashChangeRequested(hash, stateJson);
}

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void decenzaBrowserHistoryPopState(const char* json)
{
    BrowserHistoryController::receivePopStateFromBrowser(QString::fromUtf8(json ? json : "null"));
}

extern "C" EMSCRIPTEN_KEEPALIVE void decenzaBrowserHistoryHashChange(const char* hash,
                                                                      const char* stateJson)
{
    BrowserHistoryController::receiveHashChangeFromBrowser(
        QString::fromUtf8(hash ? hash : ""),
        QString::fromUtf8(stateJson ? stateJson : "null"));
}
#endif
