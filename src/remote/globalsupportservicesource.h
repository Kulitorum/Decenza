#pragma once

#include "rep_globalsupportservice_source.h"
#include "globalsupportconfirmationstate_p.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantMap>

class CrashReporter;
class MainController;
class McpServer;
class ProfileManager;

class NativeGlobalSupportServiceSource final : public GlobalSupportStateSimpleSource
{
    Q_OBJECT
public:
    NativeGlobalSupportServiceSource(CrashReporter* crashReporter,
                                     MainController* mainController,
                                     ProfileManager* profileManager,
                                     McpServer* mcpServer,
                                     QObject* parent = nullptr);

    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;
    QVariantMap requestCrashReport(QString requestId) override;

private:
    void refreshSnapshot();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void onProfileRefusedUnreadable(const QString& filename, const QString& title,
                           const QStringList& unsupportedKeys,
                           const QStringList& malformedValues);
    void clearProfileRefusal();
    void clearMcpConfirmation(const QString& confirmationId);
    void timeoutMcpConfirmation();

    QPointer<CrashReporter> m_crashReporter;
    QPointer<MainController> m_mainController;
    QPointer<ProfileManager> m_profileManager;
    QPointer<McpServer> m_mcpServer;
    ServiceRequestIds m_requests;

    bool m_crashAvailable = false;
    QString m_crashSubmissionStatus = QStringLiteral("prompt");
    QString m_crashIssueUrl;
    QString m_crashError;
    QString m_crashSubmitRequestId;

    QString m_de1CommunicationEventId;
    QString m_profileRefusalEventId;
    QVariantMap m_profileRefusal;

    GlobalSupportConfirmationState m_mcpConfirmation;
    QElapsedTimer m_mcpConfirmationClock;
    QTimer* m_mcpTimeoutTimer = nullptr;
};
