#pragma once

#include "rep_debugconversionservice_source.h"
#include "serviceoperation.h"
#include "opaquefilebundlestore.h"

#include <QPointer>
#include <QTemporaryDir>
#include <memory>

class NativeImportServiceSource;
class ProfileConverter;

class NativeDebugConversionServiceSource final : public DebugConversionStateSimpleSource
{
    Q_OBJECT
public:
    NativeDebugConversionServiceSource(ProfileConverter* converter,
                                       NativeImportServiceSource* imports,
                                       QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;

private:
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void conversionFinished(int converted, int errors);
    void conversionFailed(const QString& reason);
    void publishProgress();
    void packageBundleAsync(const QString& requestId, int converted, int errors, int skipped);
    void packageBundleFinished(const QString& requestId, const QString& archivePath,
                               bool success, const QVariantMap& result, const QString& reason);

    QPointer<ProfileConverter> m_converter;
    QPointer<NativeImportServiceSource> m_imports;
    ServiceRequestIds m_requests;
    QString m_activeId;
    QString m_activeOperation;
    std::shared_ptr<QTemporaryDir> m_sourceDirectory;
    std::shared_ptr<QTemporaryDir> m_bundleDirectory;
    std::shared_ptr<QTemporaryDir> m_archiveDirectory;
    OpaqueFileBundleStore m_bundles;
};
