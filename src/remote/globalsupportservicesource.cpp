#include "globalsupportservicesource.h"

#include "controllers/maincontroller.h"
#include "controllers/profilemanager.h"
#include "core/crashhandler.h"
#include "mcp/mcpserver.h"
#include "network/crashreporter.h"

#include <QDateTime>
#include <QFileInfo>
#include <QMetaType>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>
#include <utility>

namespace {
constexpr qsizetype kMaxNotesLength = 4000;
constexpr qsizetype kMaxProfileKeys = 64;
constexpr qsizetype kMaxProfileKeyLength = 160;
constexpr qsizetype kMaxMcpDescriptionLength = 1200;
constexpr qsizetype kMaxSafeErrorLength = 512;
constexpr int kMcpConfirmationTimeoutMs = 15000;

bool exactKeys(const QVariantMap& arguments,
               std::initializer_list<QLatin1StringView> allowed)
{
    if (arguments.size() != qsizetype(allowed.size()))
        return false;
    for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
        bool matched = false;
        for (QLatin1StringView key : allowed) {
            if (it.key() == key) {
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }
    return true;
}

bool exactString(const QVariantMap& arguments, QLatin1StringView key,
                 qsizetype maxLength, QString* value, bool allowEmpty = false,
                 bool allowLineBreaks = false)
{
    const auto it = arguments.constFind(key);
    if (it == arguments.cend() || it->metaType().id() != QMetaType::QString)
        return false;
    const QString candidate = it->toString();
    const QString controlPattern = allowLineBreaks
        ? QStringLiteral("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f\\x7f]")
        : QStringLiteral("[\\x00-\\x1f\\x7f]");
    if ((!allowEmpty && candidate.isEmpty()) || candidate.size() > maxLength
        || candidate.contains(QRegularExpression(controlPattern))) {
        return false;
    }
    *value = candidate;
    return true;
}

bool exactBool(const QVariantMap& arguments, QLatin1StringView key, bool* value)
{
    const auto it = arguments.constFind(key);
    if (it == arguments.cend() || it->metaType().id() != QMetaType::Bool)
        return false;
    *value = it->toBool();
    return true;
}

QString safeText(QString text, qsizetype maxLength)
{
    text.replace(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f\\x7f]")),
                 QStringLiteral(" "));
    return text.left(maxLength);
}

QStringList safeKeyList(const QStringList& input)
{
    QStringList result;
    const qsizetype limit = qMin(input.size(), kMaxProfileKeys);
    result.reserve(limit);
    for (qsizetype i = 0; i < limit; ++i) {
        const QString item = safeText(input.at(i), kMaxProfileKeyLength).trimmed();
        if (!item.isEmpty())
            result.append(item);
    }
    return result;
}

QString requestStartReason(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::Accepted: return {};
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequestId");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequestId");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("tooManyRequests");
    }
    return QStringLiteral("invalidRequestId");
}
} // namespace

NativeGlobalSupportServiceSource::NativeGlobalSupportServiceSource(
    CrashReporter* crashReporter, MainController* mainController,
    ProfileManager* profileManager, McpServer* mcpServer, QObject* parent)
    : GlobalSupportStateSimpleSource(parent)
    , m_crashReporter(crashReporter)
    , m_mainController(mainController)
    , m_profileManager(profileManager)
    , m_mcpServer(mcpServer)
{
    m_mcpTimeoutTimer = new QTimer(this);
    m_mcpTimeoutTimer->setSingleShot(true);
    m_mcpTimeoutTimer->setTimerType(Qt::PreciseTimer);
    connect(m_mcpTimeoutTimer, &QTimer::timeout,
            this, &NativeGlobalSupportServiceSource::timeoutMcpConfirmation);

    if (crashReporter) {
        m_crashAvailable = !crashReporter->previousCrashLog().isEmpty();
        connect(crashReporter, &CrashReporter::submittingChanged,
                this, &NativeGlobalSupportServiceSource::refreshSnapshot);
        connect(crashReporter, &CrashReporter::lastErrorChanged, this, [this] {
            if (m_crashReporter)
                m_crashError = safeText(m_crashReporter->lastError(), kMaxSafeErrorLength);
            refreshSnapshot();
        });
        connect(crashReporter, &CrashReporter::submitted, this, [this](const QString& url) {
            const QString requestId = std::exchange(m_crashSubmitRequestId, {});
            m_crashSubmissionStatus = QStringLiteral("success");
            m_crashIssueUrl = safeText(url, 2048);
            m_crashError.clear();
            refreshSnapshot();
            if (!requestId.isEmpty())
                finish(requestId, QStringLiteral("submitCrashReport"), true,
                       {{QStringLiteral("issueUrl"), m_crashIssueUrl}});
        });
        connect(crashReporter, &CrashReporter::failed, this, [this](const QString& error) {
            const QString requestId = std::exchange(m_crashSubmitRequestId, {});
            m_crashSubmissionStatus = QStringLiteral("error");
            m_crashError = safeText(error, kMaxSafeErrorLength);
            m_crashIssueUrl.clear();
            refreshSnapshot();
            if (!requestId.isEmpty())
                finish(requestId, QStringLiteral("submitCrashReport"), false, {},
                       QStringLiteral("submissionFailed"));
        });
    }

    if (profileManager) {
        connect(profileManager, &ProfileManager::de1CommunicationFailureChanged,
                this, &NativeGlobalSupportServiceSource::refreshSnapshot);
        connect(profileManager, &ProfileManager::profileRefusedUnreadable,
                this, &NativeGlobalSupportServiceSource::onProfileRefusedUnreadable);
    }

    if (mcpServer) {
        connect(mcpServer, &McpServer::confirmationRequested, this,
                [this](const QString&, const QString& description, const QString& confirmationId) {
            if (confirmationId.isEmpty() || confirmationId.size() > 64)
                return;
            m_mcpTimeoutTimer->stop();
            const qint64 deadlineEpochMs = QDateTime::currentMSecsSinceEpoch()
                + kMcpConfirmationTimeoutMs;
            m_mcpConfirmationClock.restart();
            if (!m_mcpConfirmation.begin(confirmationId,
                                         safeText(description, kMaxMcpDescriptionLength),
                                         deadlineEpochMs,
                                         m_mcpConfirmationClock.elapsed()
                                             + kMcpConfirmationTimeoutMs)) {
                return;
            }
            m_mcpTimeoutTimer->start(kMcpConfirmationTimeoutMs);
            refreshSnapshot();
        });
        connect(mcpServer, &McpServer::confirmationCancelled, this,
                [this](const QString& confirmationId, const QString&) {
            clearMcpConfirmation(confirmationId);
        });
    }

    refreshSnapshot();
}

void NativeGlobalSupportServiceSource::refresh()
{
    refreshSnapshot();
}

void NativeGlobalSupportServiceSource::refreshSnapshot()
{
    setReady(true);
    setCrashAvailable(m_crashAvailable);
    setCrashSubmitting(m_crashReporter && m_crashReporter->isSubmitting());
    setCrashSubmissionStatus(m_crashSubmissionStatus);
    setCrashIssueUrl(m_crashIssueUrl);
    setCrashError(m_crashError);
    const bool de1CommunicationFailure = m_profileManager
        && m_profileManager->de1CommunicationFailure();
    if (de1CommunicationFailure && m_de1CommunicationEventId.isEmpty())
        m_de1CommunicationEventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    else if (!de1CommunicationFailure)
        m_de1CommunicationEventId.clear();
    setDe1CommunicationEventId(m_de1CommunicationEventId);
    setDe1CommunicationFailure(de1CommunicationFailure);
    setProfileRefusal(m_profileRefusal);
    setProfileRefusalEventId(m_profileRefusalEventId);
    setMcpToolDescription(m_mcpConfirmation.description());
    setMcpConfirmationDeadlineMs(m_mcpConfirmation.deadlineEpochMs());
    setMcpConfirmationId(m_mcpConfirmation.id());
}

QVariantMap NativeGlobalSupportServiceSource::requestCrashReport(QString requestId)
{
    const auto start = m_requests.begin(requestId);
    const QString startError = requestStartReason(start);
    if (!startError.isEmpty())
        return {{QStringLiteral("success"), false},
                {QStringLiteral("reason"), startError}};

    QVariantMap response;
    if (!m_crashReporter || !m_crashAvailable) {
        response = {{QStringLiteral("success"), false},
                    {QStringLiteral("reason"), QStringLiteral("crashReportUnavailable")}};
    } else {
        response = {{QStringLiteral("success"), true},
                    {QStringLiteral("result"),
                     QVariantMap{
                         {QStringLiteral("crashLog"),
                          m_crashReporter->previousCrashLog().left(CrashHandler::kCrashLogBudget)},
                         {QStringLiteral("debugLogTail"),
                          m_crashReporter->previousDebugLogTail().left(
                              CrashHandler::kDebugLogTailBudget)}}}};
    }
    m_requests.finish(requestId);
    return response;
}

void NativeGlobalSupportServiceSource::finish(const QString& requestId,
                                               const QString& operation,
                                               bool success,
                                               const QVariantMap& result,
                                               const QString& reason)
{
    if (requestId.isEmpty() || !m_requests.finish(requestId))
        return;
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeGlobalSupportServiceSource::onProfileRefusedUnreadable(
    const QString& filename, const QString& title,
    const QStringList& unsupportedKeys, const QStringList& malformedValues)
{
    m_profileRefusalEventId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_profileRefusal = {
        {QStringLiteral("filename"), safeText(QFileInfo(filename).fileName(), 180)},
        {QStringLiteral("title"), safeText(title, 180)},
        {QStringLiteral("unsupportedKeys"), safeKeyList(unsupportedKeys)},
        {QStringLiteral("malformedValues"), safeKeyList(malformedValues)}};
    refreshSnapshot();
}

void NativeGlobalSupportServiceSource::clearProfileRefusal()
{
    m_profileRefusalEventId.clear();
    m_profileRefusal.clear();
    refreshSnapshot();
}

void NativeGlobalSupportServiceSource::clearMcpConfirmation(const QString& confirmationId)
{
    if (!m_mcpConfirmation.clearIfCurrent(confirmationId))
        return;
    if (m_mcpTimeoutTimer)
        m_mcpTimeoutTimer->stop();
    refreshSnapshot();
}

void NativeGlobalSupportServiceSource::timeoutMcpConfirmation()
{
    const QString confirmationId = m_mcpConfirmation.takeExpired(
        m_mcpConfirmationClock.elapsed());
    if (confirmationId.isEmpty())
        return;
    m_mcpTimeoutTimer->stop();
    refreshSnapshot();
    if (m_mcpServer)
        m_mcpServer->confirmationResolved(confirmationId, false, true);
}

void NativeGlobalSupportServiceSource::execute(QString requestId, QString operation,
                                                 QVariantMap arguments)
{
    const auto start = m_requests.begin(requestId);
    const QString startError = requestStartReason(start);
    if (!startError.isEmpty()) {
        if (start != ServiceRequestIds::Start::DuplicateId)
            emit operationCompleted(requestId, operation, false, {}, startError);
        return;
    }

    const auto reject = [this, &requestId, &operation](const QString& reason) {
        finish(requestId, operation, false, {}, reason);
    };

    if (operation == QLatin1String("dismissCrashReport")) {
        if (!exactKeys(arguments, {}) || !m_mainController || !m_crashAvailable) {
            reject(!m_mainController ? QStringLiteral("unavailable")
                                     : QStringLiteral("crashReportUnavailable"));
            return;
        }
        if (m_crashReporter && m_crashReporter->isSubmitting()) {
            reject(QStringLiteral("busy"));
            return;
        }
        m_mainController->clearCrashLog();
        m_crashAvailable = false;
        m_crashSubmissionStatus = QStringLiteral("prompt");
        m_crashIssueUrl.clear();
        m_crashError.clear();
        refreshSnapshot();
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("submitCrashReport")) {
        QString userNotes;
        if (!exactKeys(arguments, {QLatin1StringView("userNotes")})
            || !exactString(arguments, QLatin1StringView("userNotes"),
                            kMaxNotesLength, &userNotes, true, true)) {
            reject(QStringLiteral("invalidArguments"));
            return;
        }
        if (!m_crashReporter || !m_crashAvailable) {
            reject(QStringLiteral("crashReportUnavailable"));
            return;
        }
        if (m_crashReporter->isSubmitting() || !m_crashSubmitRequestId.isEmpty()) {
            reject(QStringLiteral("busy"));
            return;
        }
        if (m_crashSubmissionStatus == QLatin1String("success")) {
            reject(QStringLiteral("alreadySubmitted"));
            return;
        }
        m_crashSubmitRequestId = requestId;
        m_crashSubmissionStatus = QStringLiteral("submitting");
        m_crashIssueUrl.clear();
        m_crashError.clear();
        refreshSnapshot();
        m_crashReporter->submitReport(
            m_crashReporter->previousCrashLog().left(CrashHandler::kCrashLogBudget),
            userNotes,
            m_crashReporter->previousDebugLogTail().left(CrashHandler::kDebugLogTailBudget));
        return;
    }

    if (operation == QLatin1String("acknowledgeDe1CommunicationFailure")) {
        QString eventId;
        if (!exactKeys(arguments, {QLatin1StringView("eventId")})
            || !exactString(arguments, QLatin1StringView("eventId"), 64, &eventId)) {
            reject(QStringLiteral("invalidArguments"));
            return;
        }
        if (!m_profileManager || !m_profileManager->de1CommunicationFailure()) {
            reject(!m_profileManager ? QStringLiteral("unavailable")
                                     : QStringLiteral("notPending"));
            return;
        }
        if (eventId != m_de1CommunicationEventId) {
            reject(QStringLiteral("staleEvent"));
            return;
        }
        m_profileManager->acknowledgeDe1CommunicationFailure();
        refreshSnapshot();
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("dismissProfileRefusal")) {
        QString eventId;
        if (!exactKeys(arguments, {QLatin1StringView("eventId")})
            || !exactString(arguments, QLatin1StringView("eventId"), 64, &eventId)) {
            reject(QStringLiteral("invalidArguments"));
            return;
        }
        if (eventId != m_profileRefusalEventId) {
            reject(QStringLiteral("staleEvent"));
            return;
        }
        clearProfileRefusal();
        finish(requestId, operation, true, {{QStringLiteral("eventId"), eventId}});
        return;
    }

    if (operation == QLatin1String("resolveMcpConfirmation")) {
        QString confirmationId;
        bool accepted = false;
        bool timedOut = false;
        if (!exactKeys(arguments, {QLatin1StringView("confirmationId"),
                                   QLatin1StringView("accepted"),
                                   QLatin1StringView("timedOut")})
            || !exactString(arguments, QLatin1StringView("confirmationId"),
                            64, &confirmationId)
            || !exactBool(arguments, QLatin1StringView("accepted"), &accepted)
            || !exactBool(arguments, QLatin1StringView("timedOut"), &timedOut)) {
            reject(QStringLiteral("invalidArguments"));
            return;
        }
        if (!m_mcpServer || !m_mcpConfirmation.isCurrent(confirmationId)) {
            reject(!m_mcpServer ? QStringLiteral("unavailable")
                                : QStringLiteral("staleConfirmation"));
            return;
        }
        if (timedOut) {
            reject(QStringLiteral("invalidArguments"));
            return;
        }
        const auto resolution = m_mcpConfirmation.resolve(
            confirmationId, m_mcpConfirmationClock.elapsed());
        if (resolution == GlobalSupportConfirmationState::Resolution::TimedOut) {
            m_mcpTimeoutTimer->stop();
            refreshSnapshot();
            m_mcpServer->confirmationResolved(confirmationId, false, true);
            reject(QStringLiteral("confirmationTimedOut"));
            return;
        }
        if (resolution == GlobalSupportConfirmationState::Resolution::Stale) {
            reject(QStringLiteral("staleConfirmation"));
            return;
        }
        m_mcpTimeoutTimer->stop();
        refreshSnapshot();
        m_mcpServer->confirmationResolved(confirmationId, accepted, timedOut);
        finish(requestId, operation, true,
               {{QStringLiteral("confirmationId"), confirmationId}});
        return;
    }

    reject(QStringLiteral("unknownOperation"));
}
