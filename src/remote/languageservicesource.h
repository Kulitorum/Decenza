#pragma once

#include "rep_languageservice_source.h"
#include "serviceoperation.h"
#include "opaquefilebundlestore.h"

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class TranslationManager;
class NativeImportServiceSource;
class QThread;

// Host-only adapter. All storage, network and provider policy remains in
// TranslationManager; the replica receives status and user-requested results,
// never settings or credentials.
class NativeLanguageServiceSource final : public LanguageStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeLanguageServiceSource(TranslationManager* manager,
                                         QObject* parent = nullptr);
    void setImportService(NativeImportServiceSource* imports);
    void setFileOperationAllowed(std::function<bool()> allowed);
    void refresh() override;
    void execute(QString requestId, QString operation,
                 QVariantMap arguments) override;

private:
    void completeFileOperation(const QString& requestId, const QString& operation, bool success,
                               const QVariantMap& result, const QString& reason);
    void publishSnapshot();
    void queueSnapshotPublication();
    void rebuildLanguageRows(const QString& refreshPercentFor = {});
    void finish(const QString& requestId, const QString& operation,
                bool success, const QVariantMap& result = {},
                const QString& reason = {});
    bool exactKeys(const QVariantMap& arguments, const QStringList& keys) const;
    bool boundedString(const QVariantMap& arguments, const QString& key,
                       int maximum, QString* result) const;
    QString stringContextError(const QVariantMap& arguments) const;
    QVariantList boundedStringPage(int offset, int limit, QString* error) const;
    QVariantMap catalogPage(qint64 revision, int offset, QString* error);
    void completeAsync(const QString& event, const QString& identity,
                       bool success, const QString& reason = {},
                       const QVariantMap& result = {});

    QPointer<TranslationManager> m_manager;
    QPointer<NativeImportServiceSource> m_imports;
    OpaqueFileBundleStore m_bundles;
    std::function<bool()> m_fileOperationAllowed;
    QPointer<QThread> m_fileWorker;
    QString m_activeFileRequest;
    QString m_activeFileOperation;
    bool m_fileCancelled = false;
    bool m_applyingPreparedFile = false;
#ifdef DECENZA_TESTING
    friend class LanguageServiceSourceTest;
    qint64 m_lastFileCallbackNanos = 0;
#endif
    const QString m_sourceId;
    QVariantList m_catalogRows;
    QString m_catalogLanguage;
    qint64 m_catalogRevision = -1;
    ServiceRequestIds m_requests;
    QVariantList m_languageRows;
    QString m_rowCurrentLanguage;
    int m_uniqueStringCount = 0;
    int m_uniqueUntranslatedCount = 0;
    bool m_uniqueCountsCached = false;
    bool m_snapshotPublishQueued = false;
    bool m_batchTranslateUploadActive = false;
    bool m_cancelRequested = false;
    QHash<QString, QString> m_asyncRequestByCode;
    QHash<QString, QString> m_asyncOperationByRequest;
};
