#include "localfileselection.h"
#include "filebundlepolicy.h"

#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QUuid>
#include <QUrl>
#include <QStringList>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/val.h>
#include <private/qstdweb_p.h>
#include <private/qwasmlocalfileengine_p.h>
#else
#include <QApplication>
#include <QFileDialog>
#include <QSaveFile>
#include <QWindow>
#endif

namespace {
constexpr qint64 MaxFileBytes = 64LL * 1024 * 1024;
constexpr qint64 MaxSelectionBytes = 128LL * 1024 * 1024;
constexpr qsizetype MaxFilesPerSelection = 256;
constexpr qsizetype ChunkBytes = 48 * 1024;
constexpr qsizetype MaxOpenFiles = 2;
constexpr qsizetype MaxDownloadBytes = 128 * 1024 * 1024;
constexpr int MaxDirectoryEntriesVisited = 10000;
constexpr int MaxConcurrentNativeInspections = 2;
constexpr qint64 MaxLanguageJsonBytes = FileBundlePolicy::MaximumLanguageJsonBytes;

bool validSaveFormat(const QString& name, qint64 bytes, const QString& mimeType,
                     const QString& filter)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    return bytes > 0 && ((suffix == QLatin1String("zip") && bytes <= MaxDownloadBytes
            && mimeType == QLatin1String("application/zip") && filter == QLatin1String("ZIP archives (*.zip)"))
        || (suffix == QLatin1String("json") && bytes <= MaxLanguageJsonBytes
            && mimeType == QLatin1String("application/json") && filter == QLatin1String("JSON files (*.json)")));
}
}

#ifdef __EMSCRIPTEN__
class LocalFileSelection::DirectoryPicker
{
public:
    emscripten::val input = emscripten::val::undefined();
    std::unique_ptr<qstdweb::EventCallback> changed;
    std::unique_ptr<qstdweb::EventCallback> cancelled;
    ~DirectoryPicker()
    {
        if (!input.isUndefined() && !input.isNull()) {
            const emscripten::val parent = input["parentNode"];
            if (!parent.isUndefined() && !parent.isNull())
                parent.call<void>("removeChild", input);
        }
    }
};
#endif

LocalFileSelection::~LocalFileSelection()
{
#ifndef __EMSCRIPTEN__
    const auto dialogs = std::exchange(m_saveDialogs, {});
    for (const auto& dialog : dialogs) {
        if (!dialog) continue;
        disconnect(dialog, nullptr, this, nullptr);
        delete dialog.data();
    }
#endif
    m_saveGenerations.clear();
    const auto ids = m_openFiles.keys();
    for (const QString& id : ids) finishFile(id);
    const auto saveIds = m_saveFiles.keys();
    for (const QString& id : saveIds) cancelSaveFile(id);
    const auto selectionIds = m_selections.keys();
    for (const QString& id : selectionIds) closeSelection(id);
#ifdef __EMSCRIPTEN__
    const auto pickerIds = m_directoryPickers.keys();
    for (const QString& id : pickerIds) {
        auto* picker = m_directoryPickers.take(id);
        delete picker;
    }
#endif
}

QString LocalFileSelection::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString LocalFileSelection::pathForUrl(const QUrl& url)
{
    if (url.isLocalFile()) return url.toLocalFile();
    // Qt 6.12 WebAssembly's file dialog returns `weblocalfile:/<id>/<name>`.
    // Its file engine accepts that full URL as the QFile name; converting it
    // toLocalFile() would discard the selected browser File handle.
    if (url.scheme() == QLatin1String("weblocalfile"))
        return url.toString(QUrl::FullyEncoded);
    return {};
}

bool LocalFileSelection::allowedFile(const QString& kind, const QString& name)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (kind == QLatin1String("shotZip")) return suffix == QLatin1String("zip");
    if (kind == QLatin1String("shotFiles")) return suffix == QLatin1String("shot");
    if (kind == QLatin1String("database"))
        return suffix == QLatin1String("db") || suffix == QLatin1String("sqlite")
            || suffix == QLatin1String("sqlite3");
    if (kind == QLatin1String("tclFiles")) return suffix == QLatin1String("tcl");
    if (kind == QLatin1String("profileFiles"))
        return suffix == QLatin1String("json") || suffix == QLatin1String("tcl");
    if (kind == QLatin1String("languageJson")) return suffix == QLatin1String("json");
    return false;
}

void LocalFileSelection::inspectFiles(const QString& requestId, const QVariantList& urls,
                                      const QString& kind)
{
    closeSelection(requestId);
    if (requestId.isEmpty() || requestId.size() > 128 || urls.isEmpty()
        || urls.size() > MaxFilesPerSelection) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit inspectionCompleted(requestId, false, {}, QStringLiteral("invalidSelection"));
        }, Qt::QueuedConnection);
        return;
    }
    if (kind == QLatin1String("shotZip") && urls.size() != 1) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit inspectionCompleted(requestId, false, {}, QStringLiteral("selectOneArchive"));
        }, Qt::QueuedConnection);
        return;
    }
    if (kind == QLatin1String("languageJson") && urls.size() != 1) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit inspectionCompleted(requestId, false, {}, QStringLiteral("invalidSelection"));
        }, Qt::QueuedConnection);
        return;
    }
    if (kind != QLatin1String("shotZip") && kind != QLatin1String("shotFiles")
        && kind != QLatin1String("database") && kind != QLatin1String("tclFiles")
        && kind != QLatin1String("profileFiles") && kind != QLatin1String("languageJson")) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit inspectionCompleted(requestId, false, {}, QStringLiteral("unsupportedFileType"));
        }, Qt::QueuedConnection);
        return;
    }

#ifndef __EMSCRIPTEN__
    if (m_activeInspectionThreads >= MaxConcurrentNativeInspections) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit inspectionCompleted(requestId, false, {}, QStringLiteral("inspectionBusy"));
        }, Qt::QueuedConnection);
        return;
    }
#endif
    const quint64 generation = m_nextSelectionGeneration++;
    m_selectionGenerations.insert(requestId, generation);
#ifndef __EMSCRIPTEN__
    ++m_activeInspectionThreads;
    const QPointer<LocalFileSelection> guard(this);
    QCoreApplication* const app = QCoreApplication::instance();
    QThread* thread = QThread::create([guard, app, requestId, kind, urls, generation] {
        QString reason;
        QVector<SelectedFile> files = LocalFileSelection::inspectFileUrls(urls, kind, &reason);
        if (!app) return;
        QMetaObject::invokeMethod(app, [guard, requestId, kind, files, reason, generation] {
            if (guard) {
                --guard->m_activeInspectionThreads;
                guard->inspectResolvedFiles(requestId, kind, files, reason, generation);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
#else
    QString reason;
    const QVector<SelectedFile> files = inspectFileUrls(urls, kind, &reason);
    inspectResolvedFiles(requestId, kind, files, reason, generation);
#endif
}

QVector<LocalFileSelection::SelectedFile> LocalFileSelection::inspectFileUrls(
    const QVariantList& urls, const QString& kind, QString* reason)
{
    QVector<SelectedFile> files;
    const qint64 maximumFileBytes = kind == QLatin1String("languageJson") ? MaxLanguageJsonBytes : MaxFileBytes;
    qint64 totalBytes = 0;
    for (const QVariant& item : urls) {
        const QString path = pathForUrl(item.toUrl());
        if (path.isEmpty()) { *reason = QStringLiteral("unsupportedLocation"); break; }
        const QFileInfo info(path);
        QStringList candidates;
        if (info.isDir()) {
            if (kind != QLatin1String("shotFiles") && kind != QLatin1String("profileFiles")) {
                *reason = QStringLiteral("unsupportedFileType");
                break;
            }
            QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
            int visited = 0;
            while (it.hasNext()) {
                if (visited >= MaxDirectoryEntriesVisited) {
                    *reason = QStringLiteral("directoryEntryLimit");
                    break;
                }
                const QString candidate = it.next();
                ++visited;
                if (!allowedFile(kind, QFileInfo(candidate).fileName()))
                    continue;
                candidates.append(candidate);
                if (candidates.size() > MaxFilesPerSelection) {
                    *reason = QStringLiteral("selectionFileLimit");
                    break;
                }
            }
            if (!reason->isEmpty()) break;
            if (candidates.isEmpty()) {
                *reason = QStringLiteral("noSupportedFiles");
                break;
            }
        } else {
            candidates.append(path);
        }
        for (const QString& candidate : candidates) {
            QFile file(candidate);
            if (!file.open(QIODevice::ReadOnly)) { *reason = QStringLiteral("cannotReadFile"); break; }
            const qint64 size = file.size();
            const QString name = QFileInfo(candidate).fileName().left(255);
            if (size <= 0 || size > maximumFileBytes || !allowedFile(kind, name)) {
                *reason = size <= 0 || size > maximumFileBytes
                    ? QStringLiteral("fileSizeLimit") : QStringLiteral("unsupportedFileType");
                break;
            }
            totalBytes += size;
            if (totalBytes > MaxSelectionBytes) {
                *reason = QStringLiteral("selectionSizeLimit");
                break;
            }
            files.append(SelectedFile{candidate, name, size, false});
            if (files.size() > MaxFilesPerSelection) {
                *reason = QStringLiteral("selectionFileLimit");
                break;
            }
        }
        if (!reason->isEmpty()) break;
    }
    if (!reason->isEmpty()) files.clear();
    return files;
}

void LocalFileSelection::inspectResolvedFiles(const QString& requestId, const QString& kind,
                                               const QVector<SelectedFile>& files,
                                               const QString& initialReason, quint64 generation)
{
    if (m_selectionGenerations.value(requestId) != generation) {
#ifdef __EMSCRIPTEN__
        for (const SelectedFile& file : files)
            if (file.wasmRegistered) QWasmFileEngineHandler::removeFile(file.path);
#endif
        return;
    }
    QVariantList metadata;
    QString reason = initialReason;
    if (reason.isEmpty() && (requestId.isEmpty() || requestId.size() > 128 || files.isEmpty()))
        reason = QStringLiteral("invalidSelection");
    else if (reason.isEmpty() && files.size() > MaxFilesPerSelection)
        reason = QStringLiteral("selectionFileLimit");
    else if (reason.isEmpty() && kind == QLatin1String("shotZip") && files.size() != 1)
        reason = QStringLiteral("selectOneArchive");
    else if (reason.isEmpty() && kind == QLatin1String("languageJson") && files.size() != 1)
        reason = QStringLiteral("invalidSelection");
    const qint64 maximumFileBytes = kind == QLatin1String("languageJson") ? MaxLanguageJsonBytes : MaxFileBytes;
    qint64 total = 0;
    for (const SelectedFile& file : std::as_const(files)) {
        if (!reason.isEmpty()) break;
        if (file.path.isEmpty() || file.size <= 0 || file.size > maximumFileBytes
            || !allowedFile(kind, file.displayName)) {
            reason = QStringLiteral("invalidSelection");
            break;
        }
        total += file.size;
        if (total > MaxSelectionBytes) {
            reason = QStringLiteral("selectionSizeLimit");
            break;
        }
        metadata.append(QVariantMap{{QStringLiteral("name"), file.displayName},
                                    {QStringLiteral("size"), file.size}});
    }
    if (!reason.isEmpty()) {
#ifdef __EMSCRIPTEN__
        for (const SelectedFile& file : files)
            if (file.wasmRegistered) QWasmFileEngineHandler::removeFile(file.path);
#endif
    }
    if (reason.isEmpty()) m_selections.insert(requestId, files);
    else metadata.clear();
    QMetaObject::invokeMethod(this, [this, requestId, metadata, reason, generation] {
        if (m_selectionGenerations.value(requestId) != generation) return;
        m_selectionGenerations.remove(requestId);
        emit inspectionCompleted(requestId, reason.isEmpty(), metadata, reason);
    }, Qt::QueuedConnection);
}

void LocalFileSelection::selectDirectory(const QString& requestId, const QString& kind)
{
#ifdef __EMSCRIPTEN__
    if (requestId.isEmpty() || requestId.size() > 128
        || (kind != QLatin1String("shotFiles") && kind != QLatin1String("profileFiles"))
        || m_directoryPickers.contains(requestId)) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit inspectionCompleted(requestId, false, {}, QStringLiteral("invalidSelection"));
        }, Qt::QueuedConnection);
        return;
    }
    closeSelection(requestId);
    const quint64 generation = m_nextSelectionGeneration++;
    m_selectionGenerations.insert(requestId, generation);
    auto* picker = new DirectoryPicker;
    const emscripten::val document = emscripten::val::global("document");
    picker->input = document.call<emscripten::val>("createElement", std::string("input"));
    picker->input.set("type", std::string("file"));
    picker->input.set("multiple", true);
    picker->input.set("webkitdirectory", true);
    picker->input.set("directory", true);
    picker->input.set("accept", kind == QLatin1String("profileFiles")
        ? std::string(".json,.tcl") : std::string(".shot"));
    picker->input.set("style", std::string("display:none"));
    m_directoryPickers.insert(requestId, picker);
    const emscripten::val input = picker->input;
    picker->changed = std::make_unique<qstdweb::EventCallback>(input, "change",
        [this, requestId, kind, generation, input, picker](emscripten::val) {
            const auto list = qstdweb::FileList(input["files"]);
            QVector<SelectedFile> files;
            QString reason;
            qint64 total = 0;
            if (list.length() > MaxDirectoryEntriesVisited)
                reason = QStringLiteral("directoryEntryLimit");
            for (int i = 0; reason.isEmpty() && i < list.length(); ++i) {
                qstdweb::File file = list[i];
                const QString name = QString::fromStdString(file.name()).left(255);
                const qint64 size = qint64(file.size());
                if (!allowedFile(kind, name)) continue;
                if (files.size() >= MaxFilesPerSelection) {
                    reason = QStringLiteral("selectionFileLimit");
                    break;
                }
                if (size <= 0 || size > MaxFileBytes) {
                    reason = QStringLiteral("fileSizeLimit");
                    break;
                }
                total += size;
                if (total > MaxSelectionBytes) {
                    reason = QStringLiteral("selectionSizeLimit");
                    break;
                }
                const QString path = QWasmFileEngineHandler::addFile(std::move(file));
                files.append(SelectedFile{path, name, size, true});
            }
            if (files.isEmpty() && reason.isEmpty()) reason = QStringLiteral("noSupportedFiles");
            if (!reason.isEmpty()) {
                for (const SelectedFile& file : files)
                    if (file.wasmRegistered) QWasmFileEngineHandler::removeFile(file.path);
                files.clear();
            }
            inspectResolvedFiles(requestId, kind, files, reason, generation);
            QMetaObject::invokeMethod(this, [this, requestId, picker] {
                if (m_directoryPickers.value(requestId) == picker) {
                    m_directoryPickers.remove(requestId);
                    delete picker;
                }
            }, Qt::QueuedConnection);
        });
    picker->cancelled = std::make_unique<qstdweb::EventCallback>(input, "cancel",
        [this, requestId, generation, picker](emscripten::val) {
            QMetaObject::invokeMethod(this, [this, requestId, generation, picker] {
                if (m_selectionGenerations.value(requestId) == generation) {
                    m_selectionGenerations.remove(requestId);
                    emit inspectionCompleted(requestId, false, {}, QStringLiteral("cancelled"));
                }
                if (m_directoryPickers.value(requestId) == picker) {
                    m_directoryPickers.remove(requestId);
                    delete picker;
                }
            }, Qt::QueuedConnection);
        });
    const emscripten::val body = document["body"];
    body.call<void>("appendChild", input);
    input.call<void>("click");
#else
    Q_UNUSED(kind)
    QMetaObject::invokeMethod(this, [this, requestId] {
        emit inspectionCompleted(requestId, false, {}, QStringLiteral("directoryPickerUnavailable"));
    }, Qt::QueuedConnection);
#endif
}

void LocalFileSelection::openSelectedFile(const QString& selectionId, int fileIndex,
                                          const QString& readerId)
{
    const auto it = m_selections.constFind(selectionId);
    if (it == m_selections.cend() || fileIndex < 0 || fileIndex >= it->size()) {
        QMetaObject::invokeMethod(this, [this, readerId] {
            emit fileOpened(readerId, false, {}, 0, QStringLiteral("invalidSelection"));
        }, Qt::QueuedConnection);
        return;
    }
    const SelectedFile& selected = it->at(fileIndex);
    const QUrl selectedUrl = selected.wasmRegistered
        ? QUrl(selected.path) : QUrl::fromLocalFile(selected.path);
    openFile(readerId, selectedUrl);
}

void LocalFileSelection::closeSelection(const QString& selectionId)
{
    m_selectionGenerations.remove(selectionId);
    const auto files = m_selections.take(selectionId);
#ifdef __EMSCRIPTEN__
    for (const SelectedFile& file : files)
        if (file.wasmRegistered) QWasmFileEngineHandler::removeFile(file.path);
    auto* picker = m_directoryPickers.take(selectionId);
    delete picker;
#else
    Q_UNUSED(files)
#endif
}

void LocalFileSelection::openFile(const QString& requestId, const QUrl& url)
{
    if (requestId.isEmpty() || m_openFiles.contains(requestId)
        || m_openFiles.size() >= MaxOpenFiles) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit fileOpened(requestId, false, {}, 0, QStringLiteral("tooManyOpenFiles"));
        }, Qt::QueuedConnection);
        return;
    }
    const QString path = pathForUrl(url);
    auto* file = path.isEmpty() ? nullptr : new QFile(path, this);
    const bool ok = file && file->open(QIODevice::ReadOnly) && file->size() > 0
        && file->size() <= MaxFileBytes;
    if (!ok) {
        if (file) { file->close(); file->deleteLater(); }
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit fileOpened(requestId, false, {}, 0, QStringLiteral("cannotReadFile"));
        }, Qt::QueuedConnection);
        return;
    }
    m_openFiles.insert(requestId, OpenFile{file, 0, -1, 0});
    const QString name = QFileInfo(path).fileName().left(255);
    const qint64 size = file->size();
    QMetaObject::invokeMethod(this, [this, requestId, name, size] {
        emit fileOpened(requestId, true, name, size, {});
    }, Qt::QueuedConnection);
}

void LocalFileSelection::readNextChunk(const QString& requestId)
{
    auto it = m_openFiles.find(requestId);
    if (it == m_openFiles.end() || !it->file || it->pendingOffset >= 0) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit chunkReady(requestId, 0, {}, false, QStringLiteral("invalidReaderState"));
        }, Qt::QueuedConnection);
        return;
    }
    const qint64 offset = it->nextOffset;
#ifdef __EMSCRIPTEN__
    const QString path = it->file->fileName();
    if (QUrl(path).scheme() == QLatin1String("weblocalfile")) {
        const qint64 length = qMin<qint64>(ChunkBytes, it->file->size() - offset);
        if (length <= 0) {
            QMetaObject::invokeMethod(this, [this, requestId, offset] {
                emit chunkReady(requestId, offset, {}, false, QStringLiteral("readFailed"));
            }, Qt::QueuedConnection);
            return;
        }

        // Qt 6.12's QWasmFileEngine wraps the selected File in BlobIODevice;
        // qstdweb.cpp:695-703 reads it via arrayBuffer_sync(), which suspends
        // the main thread. Use the registered File and an asynchronous Blob
        // slice instead (qtbase/src/corelib/platform/wasm/qwasmlocalfileengine.cpp:109-111).
        QWasmFileEngineHandler fileEngineHandler;
        const qstdweb::File browserFile = fileEngineHandler.getFile(path);
        const emscripten::val browserFileValue = browserFile.val();
        if (browserFileValue.isUndefined() || browserFileValue.isNull()
            || browserFile.size() != static_cast<uint64_t>(it->file->size())) {
            QMetaObject::invokeMethod(this, [this, requestId, offset] {
                emit chunkReady(requestId, offset, {}, false, QStringLiteral("readFailed"));
            }, Qt::QueuedConnection);
            return;
        }

        it->pendingOffset = offset;
        it->pendingSize = 0;
        const bool last = offset + length >= it->file->size();
        const QPointer<LocalFileSelection> owner(this);
        const QPointer<QFile> file(it->file);
        const auto isCurrent = [owner, file, requestId, offset] {
            if (!owner || !file) return false;
            const auto current = owner->m_openFiles.constFind(requestId);
            return current != owner->m_openFiles.cend() && current->file == file
                && current->pendingOffset == offset;
        };
        const qstdweb::Blob slice = browserFile.slice(offset, offset + length);
        qstdweb::Promise::make(slice.val(), QStringLiteral("arrayBuffer"), {
            .thenFunc = [owner, file, requestId, offset, length, last, isCurrent]
                        (emscripten::val arrayBuffer) {
                if (!isCurrent()) return;
                const qstdweb::ArrayBuffer buffer(arrayBuffer);
                if (buffer.byteLength() != static_cast<uint32_t>(length)) {
                    emit owner->chunkReady(requestId, offset, {}, false,
                                           QStringLiteral("readFailed"));
                    return;
                }
                const QByteArray data = qstdweb::Uint8Array(buffer).copyToQByteArray();
                auto current = owner->m_openFiles.find(requestId);
                if (current == owner->m_openFiles.end() || current->file != file
                    || current->pendingOffset != offset) return;
                current->pendingSize = data.size();
                emit owner->chunkReady(requestId, offset, data, last, {});
            },
            .catchFunc = [owner, requestId, offset, isCurrent](emscripten::val) {
                if (!isCurrent()) return;
                emit owner->chunkReady(requestId, offset, {}, false,
                                       QStringLiteral("readFailed"));
            }
        });
        return;
    }
#endif
    const QByteArray data = it->file->read(ChunkBytes);
    const bool ok = !data.isEmpty() || it->file->atEnd();
    if (!ok || data.size() > ChunkBytes) {
        QMetaObject::invokeMethod(this, [this, requestId, offset] {
            emit chunkReady(requestId, offset, {}, false, QStringLiteral("readFailed"));
        }, Qt::QueuedConnection);
        return;
    }
    it->pendingOffset = offset;
    it->pendingSize = data.size();
    const bool last = it->file->atEnd();
    QMetaObject::invokeMethod(this, [this, requestId, offset, data, last] {
        emit chunkReady(requestId, offset, data, last, {});
    }, Qt::QueuedConnection);
}

void LocalFileSelection::acknowledgeChunk(const QString& requestId)
{
    auto it = m_openFiles.find(requestId);
    if (it == m_openFiles.end() || it->pendingOffset < 0 || it->pendingSize <= 0) return;
    it->nextOffset += it->pendingSize;
    it->pendingOffset = -1;
    it->pendingSize = 0;
}

void LocalFileSelection::closeFile(const QString& requestId) { finishFile(requestId); }

bool LocalFileSelection::beginSaveFile(const QString& requestId, const QString& displayName,
                                       qint64 totalBytes, const QString& mimeType, const QString& filter)
{
    const QString safeName = QFileInfo(displayName).fileName();
    if (requestId.trimmed().isEmpty() || requestId.size() > 128 || safeName.isEmpty()
        || safeName.size() > 128 || safeName.contains(QChar::Null)
        || !validSaveFormat(safeName, totalBytes, mimeType, filter)
        || totalBytes <= 0 || totalBytes > MaxDownloadBytes
        || !m_saveGenerations.isEmpty() || !m_saveFiles.isEmpty())
        return false;

    const quint64 generation = m_nextSaveGeneration++;
    m_saveGenerations.insert(requestId, generation);
#ifdef __EMSCRIPTEN__
    m_saveFiles.insert(requestId, SaveFile{safeName, mimeType, totalBytes, 0, {}, nullptr});
    queueSaveFileReady(requestId, generation, true, {});
    return true;
#else
    const QPointer<QWindow> ownerWindow = QGuiApplication::focusWindow();
    auto* dialog = new QFileDialog(QApplication::activeWindow(),
        mimeType == QLatin1String("application/json") ? tr("Save language translation") : tr("Save converted profiles"),
        safeName, mimeType == QLatin1String("application/json") ? tr("JSON files (*.json)") : tr("ZIP archives (*.zip)"));
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->setWindowModality(Qt::WindowModal);
    m_saveDialogs.insert(requestId, dialog);
    const auto complete = [this, requestId, generation, dialog, safeName, totalBytes, mimeType, filter](bool accepted) {
        if (m_saveGenerations.value(requestId) != generation || m_saveDialogs.value(requestId) != dialog)
            return;
        m_saveDialogs.remove(requestId);
        const QString path = accepted ? dialog->selectedFiles().value(0) : QString();
        dialog->deleteLater();
        const bool ready = accepted && !path.isEmpty()
            && beginSaveFileAtPath(requestId, safeName, totalBytes, path, mimeType, filter);
        queueSaveFileReady(requestId, generation, ready,
            ready ? QString() : accepted ? QStringLiteral("saveFailed") : QStringLiteral("saveCancelled"));
    };
    connect(dialog, &QDialog::accepted, this, [complete] { complete(true); });
    connect(dialog, &QDialog::rejected, this, [complete] { complete(false); });
    connect(dialog, &QObject::destroyed, this, [this, requestId, generation] {
        if (m_saveGenerations.value(requestId) != generation || !m_saveDialogs.contains(requestId)) return;
        m_saveDialogs.remove(requestId);
        queueSaveFileReady(requestId, generation, false, QStringLiteral("saveCancelled"));
    });
    // QDialog::open() shows asynchronously without exec()'s nested event loop
    // (Qt 6.12 qtbase/src/widgets/dialogs/qdialog.cpp:522-539,552-556).
    QMetaObject::invokeMethod(this, [this, requestId, generation, pendingDialog = QPointer<QFileDialog>(dialog), ownerWindow] {
        if (!pendingDialog || m_saveGenerations.value(requestId) != generation || m_saveDialogs.value(requestId) != pendingDialog)
            return;
        pendingDialog->winId();
        if (ownerWindow && pendingDialog->windowHandle() && pendingDialog->windowHandle() != ownerWindow.data())
            pendingDialog->windowHandle()->setTransientParent(ownerWindow.data());
        pendingDialog->open();
    }, Qt::QueuedConnection);
    return true;
#endif
}

void LocalFileSelection::queueSaveFileReady(const QString& requestId, quint64 generation,
                                            bool success, const QString& reason)
{
    QMetaObject::invokeMethod(this, [this, requestId, generation, success, reason] {
        if (m_saveGenerations.value(requestId) != generation) return;
        if (!success) m_saveGenerations.remove(requestId);
        emit saveFileReady(requestId, success, reason);
    }, Qt::QueuedConnection);
}

bool LocalFileSelection::beginSaveFileAtPath(const QString& requestId,
                                             const QString& safeName,
                                             qint64 totalBytes,
                                             const QString& path,
                                             const QString& mimeType, const QString& filter)
{
#ifdef __EMSCRIPTEN__
    Q_UNUSED(requestId);
    Q_UNUSED(safeName);
    Q_UNUSED(totalBytes);
    Q_UNUSED(path);
    Q_UNUSED(mimeType);
    Q_UNUSED(filter);
    return false;
#else
    if (requestId.trimmed().isEmpty() || requestId.size() > 128
        || safeName.isEmpty() || safeName.size() > 128
        || QFileInfo(safeName).fileName() != safeName
        || !validSaveFormat(safeName, totalBytes, mimeType, filter)
        || path.isEmpty() || totalBytes <= 0 || totalBytes > MaxDownloadBytes
        || m_saveFiles.size() >= 1 || m_saveFiles.contains(requestId))
        return false;
    auto* output = new QSaveFile(path, this);
    output->setDirectWriteFallback(false);
    if (!output->open(QIODevice::WriteOnly)) {
        output->deleteLater();
        return false;
    }
    m_saveFiles.insert(requestId, SaveFile{safeName, mimeType, totalBytes, 0, {}, output});
    return true;
#endif
}

bool LocalFileSelection::appendSaveFileChunk(const QString& requestId, qint64 offset,
                                             const QByteArray& bytes)
{
    auto it = m_saveFiles.find(requestId);
    if (it == m_saveFiles.end() || bytes.isEmpty() || bytes.size() > ChunkBytes
        || offset != it->offset || offset + bytes.size() > it->expectedSize)
        return false;
#ifdef __EMSCRIPTEN__
    it->wasmBytes.append(bytes);
#else
    if (!it->nativeFile || it->nativeFile->write(bytes) != bytes.size())
        return false;
#endif
    it->offset += bytes.size();
    return true;
}

bool LocalFileSelection::finishSaveFile(const QString& requestId)
{
    auto it = m_saveFiles.find(requestId);
    if (it == m_saveFiles.end() || it->offset != it->expectedSize)
        return false;
    const SaveFile save = it.value();
    m_saveFiles.erase(it);
    m_saveGenerations.remove(requestId);
#ifdef __EMSCRIPTEN__
    const QByteArray encodedName = save.displayName.toUtf8();
    const QByteArray encodedMime = save.mimeType.toUtf8();
    EM_ASM({
        const filename = UTF8ToString($0);
        const bytes = HEAPU8.slice($1, $1 + $2);
        const blob = new Blob([bytes], { type: UTF8ToString($3) });
        const link = document.createElement("a");
        link.href = URL.createObjectURL(blob);
        link.download = filename;
        link.style.display = "none";
        document.body.appendChild(link);
        link.click();
        link.remove();
        setTimeout(() => URL.revokeObjectURL(link.href), 1000);
    }, encodedName.constData(), save.wasmBytes.constData(), save.wasmBytes.size(), encodedMime.constData());
    return true;
#else
    if (!save.nativeFile) return false;
    const bool success = save.nativeFile->commit();
    save.nativeFile->deleteLater();
    return success;
#endif
}

void LocalFileSelection::cancelSaveFile(const QString& requestId)
{
    m_saveGenerations.remove(requestId);
#ifndef __EMSCRIPTEN__
    if (auto dialog = m_saveDialogs.take(requestId)) {
        disconnect(dialog, nullptr, this, nullptr);
        dialog->reject();
        dialog->deleteLater();
    }
#endif
    auto it = m_saveFiles.find(requestId);
    if (it == m_saveFiles.end()) return;
    if (it->nativeFile) {
        it->nativeFile->cancelWriting();
        it->nativeFile->deleteLater();
    }
    m_saveFiles.erase(it);
}

void LocalFileSelection::finishFile(const QString& requestId)
{
    auto it = m_openFiles.find(requestId);
    if (it == m_openFiles.end()) return;
    if (it->file) { it->file->close(); it->file->deleteLater(); }
    m_openFiles.erase(it);
}
