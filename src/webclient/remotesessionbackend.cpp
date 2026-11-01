#include "remotesessionbackend.h"
#include "core/packageidentity.h"
#include "core/backgroundpresets.h"
#include <QUrl>
#include <QUuid>
#include <cstdlib>
#if defined(__EMSCRIPTEN__)
#include "core/browserorigin.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace {
QString defaultOrigin()
{
#if defined(__EMSCRIPTEN__)
    return browserPageOrigin();
#else
    return qEnvironmentVariable("DECENZA_FEASIBILITY_ORIGIN",
                                QStringLiteral("http://127.0.0.1:8080"));
#endif
}

bool normalizeOrigin(const QString& value, QString* normalized)
{
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http")
                           && url.scheme() != QStringLiteral("https"))
        || url.host().isEmpty() || url.port(-1) == 0 || !url.userInfo().isEmpty()
        || !url.query().isEmpty() || !url.fragment().isEmpty()
        || (!url.path().isEmpty() && url.path() != QStringLiteral("/"))) {
        return false;
    }
#if defined(__EMSCRIPTEN__)
    // The packaged Wasm client and its native ShotServer are one distribution.
    // Keep it same-origin so the expected package identity cannot be used to
    // pair the page with an unrelated host. Browser fetch/WebSocket already
    // use same-origin credentials; this also rejects QML/API-origin overrides.
    const QUrl browserOrigin(defaultOrigin(), QUrl::StrictMode);
    const auto effectivePort = [](const QUrl& origin) {
        return origin.port(origin.scheme() == QStringLiteral("https") ? 443 : 80);
    };
    if (!browserOrigin.isValid()
        || url.scheme().compare(browserOrigin.scheme(), Qt::CaseInsensitive) != 0
        || url.host().compare(browserOrigin.host(), Qt::CaseInsensitive) != 0
        || effectivePort(url) != effectivePort(browserOrigin)) {
        return false;
    }
#endif
    QString result = url.toString(QUrl::RemovePath | QUrl::RemoveQuery
                                  | QUrl::RemoveFragment | QUrl::RemoveUserInfo);
    while (result.endsWith('/'))
        result.chop(1);
    *normalized = result;
    return true;
}


QString entryIdentity(const char* field)
{
#if defined(__EMSCRIPTEN__)
    auto* value = static_cast<char*>(EM_ASM_PTR({
        const name = UTF8ToString($0);
        const meta = document.querySelector('meta[name="' + name + '"]');
        return stringToNewUTF8(meta ? meta.content : "");
    }, field));
    const QString result = value ? QString::fromUtf8(value) : QString();
    std::free(value);
    return result;
#else
    return qEnvironmentVariable(field);
#endif
}
}

RemoteSessionBackend::RemoteSessionBackend(QObject* parent)
    : QObject(parent), m_apiOrigin(defaultOrigin())
{
#if defined(__EMSCRIPTEN__)
    m_expectedPackage = entryIdentity("decenza-session-package");
    m_expectedHost = entryIdentity("decenza-session-host");
    emscripten_set_visibilitychange_callback(this, false,
        [](int, const EmscriptenVisibilityChangeEvent* event, void* context) -> bool {
            static_cast<RemoteSessionBackend*>(context)->setPageVisible(!event->hidden);
            return false;
        });
    EmscriptenVisibilityChangeEvent visibility{};
    if (emscripten_get_visibility_status(&visibility) == EMSCRIPTEN_RESULT_SUCCESS)
        setPageVisible(!visibility.hidden);
#else
    m_expectedPackage = entryIdentity("DECENZA_SESSION_PACKAGE");
    m_expectedHost = entryIdentity("DECENZA_SESSION_HOST");
#endif
}

RemoteSessionBackend::~RemoteSessionBackend()
{
#if defined(__EMSCRIPTEN__)
    emscripten_set_visibilitychange_callback(nullptr, false, nullptr);
#endif
}

void RemoteSessionBackend::setPageVisible(bool visible)
{
    m_pageVisible = visible;
    if (!visible) {
        m_returningFromBackground = true;
        if (m_status == QStringLiteral("disconnected") && !m_error.isEmpty())
            m_resumeAfterLoss = true;
    } else if (m_resumeAfterLoss && !busy() && !reloadRequired()) {
        m_resumeAfterLoss = false;
        m_returningFromBackground = false;
        connectToHost();
    }
}

void RemoteSessionBackend::setService(HostSessionService* service)
{
    if (m_service == service) return;
    const quint64 generation = m_generation + 1;
    disconnectFromHost();
    if (generation != m_generation) return;
    disconnect(m_serviceChanged);
    disconnect(m_serviceDestroyed);
    m_service = service;
    if (service) {
        m_serviceChanged = connect(service, &HostSessionService::stateChanged, this, &RemoteSessionBackend::refresh);
        m_serviceDestroyed = connect(service, &QObject::destroyed, this, [this] {
            m_service = nullptr;
            transportFailed(QStringLiteral("The shared host session ended"));
            emit serviceChanged();
        });
    }
    emit serviceChanged();
}

void RemoteSessionBackend::setApiOrigin(const QString& origin)
{
    QString normalized;
    const bool valid = normalizeOrigin(origin, &normalized);
    if (valid && normalized == m_apiOrigin) return;
    const quint64 generation = m_generation + 1;
    disconnectFromHost();
    if (generation != m_generation) return;
    m_apiOrigin = valid ? normalized : origin;
    emit apiOriginChanged();
    if (!valid) { setError(QStringLiteral("Invalid API origin")); setStatus(QStringLiteral("error")); }
}

void RemoteSessionBackend::setBootstrapIdentity(const QString& packageId, const QString& hostId)
{
#if defined(__EMSCRIPTEN__)
    if (packageId != entryIdentity("decenza-session-package")
        || hostId != entryIdentity("decenza-session-host")) return;
#endif
    if (m_expectedPackage == packageId && m_expectedHost == hostId) return;
    const quint64 generation = m_generation + 1;
    disconnectFromHost();
    if (generation != m_generation) return;
    m_expectedPackage = packageId;
    m_expectedHost = hostId;
}

void RemoteSessionBackend::connectToHost()
{
    if (busy() || (m_connected && !m_stale)) return;
    QString normalized;
    if (!normalizeOrigin(m_apiOrigin, &normalized)) {
        setError(QStringLiteral("Invalid API origin")); setStatus(QStringLiteral("error")); return;
    }
    const QUuid host(m_expectedHost);
    if (!m_service || m_expectedPackage != QString::fromLatin1(decenzaPackageId())
        || host.isNull() || host.toString(QUuid::WithoutBraces) != m_expectedHost) {
        transportFailed(QStringLiteral("This page has no matching host identity. Reload the packaged interface."), true);
        return;
    }
    const quint64 generation = ++m_generation;
    m_wanted = true;
    invalidate();
    if (generation != m_generation) return;
    setError({});
    if (generation != m_generation) return;
    setStatus(QStringLiteral("connecting"));
    if (generation == m_generation) emit connectionRequested();
}

void RemoteSessionBackend::transportOpened()
{
    if (!m_wanted) return;
    const quint64 generation = m_generation;
    setStatus(QStringLiteral("synchronizing"));
    if (generation != m_generation || !m_wanted) return;
    refresh();
}

void RemoteSessionBackend::refresh()
{
    if (!m_wanted || !m_service) return;
    const quint64 generation = m_generation;
    const auto service = m_service;
    const auto current = [this, generation, service] {
        return generation == m_generation && m_wanted && service && service == m_service;
    };
    if (!m_service->ready()) {
        if (m_connected || !m_service->errorMessage().isEmpty())
            transportFailed(m_service->errorMessage().isEmpty()
                ? QStringLiteral("The shared host session disconnected. Reconnect to refresh its state.")
                : m_service->errorMessage(), !m_service->errorMessage().isEmpty());
        return;
    }
    if (m_service->packageId() != m_expectedPackage || m_service->hostId() != m_expectedHost) {
        transportFailed(QStringLiteral("The host restarted or the package changed. Reload the interface to pair again."), true);
        return;
    }
    const qint64 sequence = static_cast<qint64>(m_service->sequence());
    const bool telemetryChanged = sequence != m_sequence;
    if (telemetryChanged && m_sequence >= 0 && sequence != m_sequence + 1) {
        transportFailed(QStringLiteral("The host session sequence changed. Reconnect to refresh its state."));
        return;
    }
    m_sequence = sequence;
    if (!m_connected) { m_connected = true; emit connectedChanged(); }
    if (!current()) return;
    if (m_stale) { m_stale = false; emit staleChanged(); }
    if (!current()) return;
    const QVariantMap appearance = m_service->appearance();
    if (m_lastAppearance != appearance) { m_lastAppearance = appearance; emit appearanceChanged(); }
    if (!current()) return;
    setStatus(QStringLiteral("connected"));
    if (current() && telemetryChanged) emit snapshotChanged();
}

void RemoteSessionBackend::invalidate()
{
    const quint64 generation = m_generation;
    m_sequence = -1;
    if (m_connected) { m_connected = false; emit connectedChanged(); }
    if (generation != m_generation) return;
    if (!m_stale) { m_stale = true; emit staleChanged(); }
    if (generation != m_generation) return;
    if (!m_lastAppearance.isEmpty()) { m_lastAppearance.clear(); emit appearanceChanged(); }
    if (generation == m_generation) emit snapshotChanged();
}

void RemoteSessionBackend::disconnectFromHost()
{
    m_resumeAfterLoss = false;
    m_returningFromBackground = false;
    m_wanted = false;
    const quint64 generation = ++m_generation;
    invalidate();
    if (generation != m_generation) return;
    emit connectionRequested();
    if (generation != m_generation) return;
    setError({});
    if (generation == m_generation) setStatus(QStringLiteral("disconnected"));
}

void RemoteSessionBackend::transportFailed(const QString& error, bool reloadRequired)
{
    const bool resume = m_wanted && m_returningFromBackground && !reloadRequired;
    m_wanted = false;
    const quint64 generation = ++m_generation;
    invalidate();
    if (generation != m_generation) return;
    emit connectionRequested();
    if (generation != m_generation) return;
    setError(error);
    if (generation == m_generation)
        setStatus(reloadRequired ? QStringLiteral("reload-required") : QStringLiteral("disconnected"));
    if (generation != m_generation) return;
    m_resumeAfterLoss = resume;
    if (resume && m_pageVisible) {
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation == m_generation && m_resumeAfterLoss && m_pageVisible)
                setPageVisible(true);
        }, Qt::QueuedConnection);
    }
}

void RemoteSessionBackend::reloadInterface()
{
#if defined(__EMSCRIPTEN__)
    EM_ASM({ window.location.reload(); });
#endif
}

const MachineUiSnapshot& RemoteSessionBackend::machineUi() const
{
    static const MachineUiSnapshot empty;
    return m_connected && !m_stale && m_service ? m_service->machineUi() : empty;
}

QString RemoteSessionBackend::phase() const
{
    switch (machineUi().phase) {
#define PHASE_CASE(name) case MachinePhase::name: return QStringLiteral(#name);
        DECENZA_MACHINE_PHASES(PHASE_CASE)
#undef PHASE_CASE
    }
    return {};
}

QVariantMap RemoteSessionBackend::backgroundPattern() const
{
    const QString id = appearance().value(QStringLiteral("background")).toMap()
        .value(QStringLiteral("patternId")).toString();
    return BackgroundPresets::hasPattern(id)
        ? BackgroundPresets::patternToVariantMap(BackgroundPresets::patternById(id)) : QVariantMap();
}
void RemoteSessionBackend::setStatus(const QString& status)
{ if (m_status != status) { m_status = status; emit statusChanged(); } }
void RemoteSessionBackend::setError(const QString& error)
{ if (m_error != error) { m_error = error; emit errorMessageChanged(); } }
