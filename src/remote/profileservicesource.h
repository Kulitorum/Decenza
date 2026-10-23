#pragma once

#include "rep_profileservice_source.h"
#include "serviceoperation.h"

#include <QPointer>

class ProfileManager;
class SettingsApp;
class SettingsCalibration;

class NativeProfileServiceSource final : public ProfileStateSimpleSource
{
    Q_OBJECT
public:
    NativeProfileServiceSource(ProfileManager* profiles, SettingsApp* app,
                               SettingsCalibration* calibration = nullptr,
                               QObject* parent = nullptr);

    void refresh() override;
    void requestProfileDetail(QString requestId, QString filename) override;
    void requestKnowledge(QString requestId, QString title, QString profileJson) override;
    void execute(QString requestId, QString operation, QString filename,
                 QVariantMap fields, qint64 expectedRevision) override;

private:
    void publish(bool revisionChanged = false, bool rebuildCatalog = false);
    void finishRead(const QString& requestId);
    void finishOperation(const QString& requestId, const QString& operation,
                         const QString& filename, bool success, const QString& reason,
                         const QVariantMap& result = {});
    bool knownFilename(const QString& filename) const;
    QVariantMap captureProfileSummaries() const;
    QVariantMap captureFlowCalibration() const;
    QVariantMap captureRoastAffinities() const;
    QVariantMap captureGrindDirections() const;

    QPointer<ProfileManager> m_profiles;
    QPointer<SettingsApp> m_app;
    QPointer<SettingsCalibration> m_calibration;
    ServiceRequestIds m_reads;
    ServiceRequestIds m_operations;
    qint64 m_catalogRevision = 0;
    QVariantList m_allProfiles;
    QVariantList m_availableProfiles;
    QStringList m_installedTitles;
    QVariantMap m_summaries;
    QVariantMap m_roastAffinities;
    QVariantMap m_grindDirections;
};
