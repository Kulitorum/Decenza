#include "debugconversionservicesource.h"

#include "profile/profileconverter.h"
#include "importservicesource.h"
#include "debugconversionpolicy.h"
#include "servicevalue.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <QUuid>
#include <utility>

#include "core/ziparchive.h"

namespace {
struct BundleResult {
    bool success = false;
    QVariantMap result;
    QString reason;
};

BundleResult writeBundle(const QString& inputPath, const QString& archivePath,
                         const QString& id, int converted, int errors, int skipped)
{
    BundleResult outcome;
    QuaZip zip(archivePath);
    if (!zip.open(QuaZip::mdCreate)) {
        outcome.reason = QStringLiteral("bundleWriteFailed");
        return outcome;
    }
    qint64 totalInputBytes = 0;
    QDirIterator it(inputPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const qint64 fileSize = QFileInfo(filePath).size();
        QFile file(filePath);
        const QString relative = QDir(inputPath).relativeFilePath(filePath);
        totalInputBytes += fileSize;
        if (fileSize <= 0 || fileSize > 64LL * 1024 * 1024
            || totalInputBytes > DebugConversionPolicy::MaximumBundleBytes
            || !file.open(QIODevice::ReadOnly) || relative.startsWith(QLatin1String("../"))) {
            zip.close();
            QFile::remove(archivePath);
            outcome.reason = QStringLiteral("bundleReadFailed");
            return outcome;
        }
        if (!ZipArchive::addFile(zip, relative, file)
            || QFileInfo(archivePath).size() > DebugConversionPolicy::MaximumBundleBytes) {
            zip.close();
            QFile::remove(archivePath);
            outcome.reason = QStringLiteral("bundleWriteFailed");
            return outcome;
        }
    }
    zip.close();
    const qint64 size = QFileInfo(archivePath).size();
    if (zip.getZipError() != UNZ_OK || size <= 0
        || size > DebugConversionPolicy::MaximumBundleBytes) {
        QFile::remove(archivePath);
        outcome.reason = QStringLiteral("bundleSizeLimit");
        return outcome;
    }
    outcome.success = true;
    outcome.result = {{QStringLiteral("bundleId"), id},
                      {QStringLiteral("displayName"), QStringLiteral("converted-profiles.zip")},
                      {QStringLiteral("size"), size},
                      {QStringLiteral("converted"), converted},
                      {QStringLiteral("errors"), errors},
                      {QStringLiteral("skipped"), skipped}};
    return outcome;
}
} // namespace

NativeDebugConversionServiceSource::NativeDebugConversionServiceSource(
    ProfileConverter* converter, NativeImportServiceSource* imports, QObject* parent)
    : DebugConversionStateSimpleSource(parent), m_converter(converter), m_imports(imports)
{
    if (m_converter) {
        connect(m_converter, &ProfileConverter::conversionComplete,
                this, &NativeDebugConversionServiceSource::conversionFinished);
        connect(m_converter, &ProfileConverter::conversionError,
                this, &NativeDebugConversionServiceSource::conversionFailed);
        connect(m_converter, &ProfileConverter::progressChanged,
                this, &NativeDebugConversionServiceSource::publishProgress);
        connect(m_converter, &ProfileConverter::currentFileChanged,
                this, &NativeDebugConversionServiceSource::publishProgress);
        connect(m_converter, &ProfileConverter::statusMessageChanged,
                this, &NativeDebugConversionServiceSource::publishProgress);
        connect(m_converter, &QObject::destroyed, this, [this] {
            m_converter = nullptr;
            setReady(false);
            conversionFailed(QStringLiteral("serviceUnavailable"));
        });
    }
    if (m_imports) {
        connect(m_imports, &QObject::destroyed, this, [this] {
            m_imports = nullptr;
            setReady(false);
        });
    }
    refresh();
}

void NativeDebugConversionServiceSource::refresh()
{
    setReady(m_converter && m_imports);
}

void NativeDebugConversionServiceSource::publishProgress()
{
    if (m_activeId.isEmpty() || !m_converter) return;
    emit operationProgress(m_activeId,
        {{QStringLiteral("statusMessage"), m_converter->statusMessage().left(160)},
         {QStringLiteral("currentFile"), QFileInfo(m_converter->currentFile()).fileName().left(128)},
         {QStringLiteral("totalFiles"), m_converter->totalFiles()},
         {QStringLiteral("processedFiles"), m_converter->processedFiles()},
         {QStringLiteral("successCount"), m_converter->successCount()},
         {QStringLiteral("errorCount"), m_converter->errorCount()},
         {QStringLiteral("skippedCount"), m_converter->skippedCount()}});
}


void NativeDebugConversionServiceSource::finish(const QString& requestId,
                                                const QString& operation,
                                                bool success, const QVariantMap& result,
                                                const QString& reason)
{
    m_requests.finish(requestId);
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeDebugConversionServiceSource::execute(QString requestId, QString operation,
                                                 QVariantMap arguments)
{
    const auto begun = m_requests.begin(requestId);
    if (begun == ServiceRequestIds::Start::DuplicateId) return;
    if (begun != ServiceRequestIds::Start::Accepted) {
        const QString reason = begun == ServiceRequestIds::Start::InvalidId
            ? QStringLiteral("invalidRequest")
            : begun == ServiceRequestIds::Start::DuplicateId
                ? QStringLiteral("duplicateRequest") : QStringLiteral("busy");
        emit operationCompleted(requestId, operation, false, {}, reason);
        return;
    }

    if (operation == QLatin1String("convertProfiles")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("transferId"), QStringLiteral("overwrite")})
            || arguments.value(QStringLiteral("transferId")).metaType().id() != QMetaType::QString
            || arguments.value(QStringLiteral("overwrite")).metaType().id() != QMetaType::Bool) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments"));
            return;
        }
        const QString transferId = arguments.value(QStringLiteral("transferId")).toString();
        if (!DebugConversionPolicy::validOpaqueId(transferId) || !m_converter || !m_imports
            || m_converter->isConverting() || !m_activeId.isEmpty()
            || m_bundles.hasBundle()) {
            finish(requestId, operation, false, {}, QStringLiteral("busyOrUnavailable"));
            return;
        }
        auto source = m_imports->takeCompletedTransfer(transferId, QStringLiteral("tclFiles"));
        if (!source || !source->isValid()) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidTransfer"));
            return;
        }
        auto bundle = std::make_shared<QTemporaryDir>();
        if (!bundle->isValid()) {
            finish(requestId, operation, false, {}, QStringLiteral("storageUnavailable"));
            return;
        }
        m_sourceDirectory = std::shared_ptr<QTemporaryDir>(std::move(source));
        m_bundleDirectory = std::move(bundle);
        m_activeId = requestId;
        m_activeOperation = operation;
        if (!m_converter->convertProfilesToBundle(m_sourceDirectory->path(),
                                                   m_bundleDirectory->path(),
                                                   arguments.value(QStringLiteral("overwrite")).toBool())) {
            conversionFailed(QStringLiteral("conversionFailed"));
        }
        return;
    }

    if (operation == QLatin1String("readBundleChunk")) {
        QString reason;
        const QVariantMap result = m_bundles.readChunk(arguments, &reason);
        finish(requestId, operation, reason.isEmpty(), result, reason);
        return;
    }

    if (operation == QLatin1String("releaseBundle")) {
        QString reason;
        const bool released = m_bundles.release(arguments, &reason);
        finish(requestId, operation, released, {}, reason);
        return;
    }

    finish(requestId, operation, false, {}, QStringLiteral("unsupportedOperation"));
}

void NativeDebugConversionServiceSource::packageBundleAsync(const QString& requestId,
                                                             int converted, int errors,
                                                             int skipped)
{
    if (!m_bundleDirectory || !m_bundleDirectory->isValid()) {
        packageBundleFinished(requestId, {}, false, {}, QStringLiteral("conversionFailed"));
        return;
    }
    if (!m_archiveDirectory) {
        m_archiveDirectory = std::make_shared<QTemporaryDir>();
        if (!m_archiveDirectory->isValid()) {
            m_archiveDirectory.reset();
            packageBundleFinished(requestId, {}, false, {}, QStringLiteral("storageUnavailable"));
            return;
        }
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    const QString archivePath = m_archiveDirectory->filePath(id + QStringLiteral(".zip"));
    const auto inputDirectory = m_bundleDirectory;
    const auto archiveDirectory = m_archiveDirectory;
    const QString inputPath = inputDirectory->path();
    QPointer<NativeDebugConversionServiceSource> self(this);
    auto* completionContext = QCoreApplication::instance();
    QThreadPool::globalInstance()->start(QRunnable::create(
        [self, inputDirectory, archiveDirectory, inputPath, requestId, archivePath, id,
         converted, errors, skipped, completionContext] {
            Q_UNUSED(inputDirectory); // Keeps the converter output alive until packaging ends.
            Q_UNUSED(archiveDirectory); // Keeps the archive's temporary root alive in the worker.
            const BundleResult result = writeBundle(inputPath, archivePath, id, converted,
                                                    errors, skipped);
            QMetaObject::invokeMethod(completionContext,
                [self, archiveDirectory, requestId, archivePath, result] {
                Q_UNUSED(archiveDirectory); // Retain the archive until delivery or abandoned cleanup.
                if (self) {
                    self->packageBundleFinished(requestId, archivePath, result.success,
                                                result.result, result.reason);
                } else {
                    QFile::remove(archivePath);
                }
            }, Qt::QueuedConnection);
        }));
}

void NativeDebugConversionServiceSource::packageBundleFinished(
    const QString& requestId, const QString& archivePath, bool success,
    const QVariantMap& result, const QString& reason)
{
    const QString operation = QStringLiteral("convertProfiles");
    if (m_activeId != requestId || m_activeOperation != operation) {
        if (!archivePath.isEmpty()) QFile::remove(archivePath);
        return;
    }
    QVariantMap descriptor = result;
    QString finalReason = reason;
    if (success) {
        const QString bundleId = result.value(QStringLiteral("bundleId")).toString();
        const QVariantMap bundle = m_bundles.adopt(m_archiveDirectory, bundleId,
            bundleId + QStringLiteral(".zip"), QStringLiteral("converted-profiles.zip"),
            DebugConversionPolicy::MaximumBundleBytes, QStringLiteral("application/zip"),
            QStringLiteral("ZIP archives (*.zip)"));
        success = !bundle.isEmpty();
        if (success) {
            for (auto it = bundle.cbegin(); it != bundle.cend(); ++it)
                descriptor.insert(it.key(), it.value());
        } else {
            descriptor.clear();
            finalReason = QStringLiteral("bundleWriteFailed");
        }
    }
    m_archiveDirectory.reset();
    const QString id = std::exchange(m_activeId, {});
    const QString activeOperation = std::exchange(m_activeOperation, {});
    m_sourceDirectory.reset();
    m_bundleDirectory.reset();
    finish(id, activeOperation, success, descriptor, finalReason);
}

void NativeDebugConversionServiceSource::conversionFinished(int converted, int errors)
{
    if (m_activeId.isEmpty() || m_activeOperation != QLatin1String("convertProfiles")) return;
    packageBundleAsync(m_activeId, converted, errors,
                       m_converter ? m_converter->skippedCount() : 0);
}

void NativeDebugConversionServiceSource::conversionFailed(const QString&)
{
    if (m_activeId.isEmpty() || m_activeOperation != QLatin1String("convertProfiles")) return;
    const QString id = std::exchange(m_activeId, {});
    const QString operation = std::exchange(m_activeOperation, {});
    m_sourceDirectory.reset();
    m_bundleDirectory.reset();
    finish(id, operation, false, {}, QStringLiteral("conversionFailed"));
}
