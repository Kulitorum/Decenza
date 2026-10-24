#pragma once

#include "rep_shotuploadservice_source.h"
#include "serviceoperation.h"

#include <QHash>
#include <QPointer>
#include <QQueue>

class DecentAccount;
class Settings;
class ShotUploads;
class VisualizerImporter;
class VisualizerUploader;

class NativeShotUploadServiceSource final : public ShotUploadStateSimpleSource
{
    Q_OBJECT
public:
    NativeShotUploadServiceSource(Settings* settings, VisualizerUploader* visualizer,
                                  DecentAccount* decent, VisualizerImporter* importer,
                                  ShotUploads* uploads, QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;
    QVariantMap takePrivateResult(QString receipt) override;
private:
    QVariantMap makeSnapshot() const;
    void refreshSnapshot();
    void complete(const QString& requestId, const QString& operation, bool success,
                  const QVariantMap& result = {}, const QString& reason = {});

    QPointer<Settings> m_settings;
    QPointer<VisualizerUploader> m_visualizer;
    QPointer<DecentAccount> m_decent;
    QPointer<VisualizerImporter> m_importer;
    QPointer<ShotUploads> m_uploads;
    ServiceRequestIds m_requests;
    QString m_visualizerLinkId;
    QString m_decentLinkId;
    QString m_decentChoiceOwnerId;
    QHash<QString, QString> m_decentAccountUrlRequests;
    QHash<QString, QVariantMap> m_privateResults;
    QQueue<QString> m_privateResultOrder;
    QString m_recoveryId;
};
