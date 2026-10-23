#pragma once

#include <QObject>
#include <QFile>
#include <QHash>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

#ifndef __EMSCRIPTEN__
class QFileDialog;
#endif

// Local-only reader for files chosen by the user in the native or Wasm file
// dialog. It exposes metadata and one acknowledged chunk at a time; no local
// path is passed to the host service.
class LocalFileSelection : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    LocalFileSelection() = default;
    ~LocalFileSelection() override;

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void inspectFiles(const QString& requestId, const QVariantList& urls,
                                  const QString& kind);
    Q_INVOKABLE void selectDirectory(const QString& requestId, const QString& kind);
    Q_INVOKABLE void openSelectedFile(const QString& selectionId, int fileIndex,
                                      const QString& readerId);
    Q_INVOKABLE void closeSelection(const QString& selectionId);
    Q_INVOKABLE void openFile(const QString& requestId, const QUrl& url);
    Q_INVOKABLE void readNextChunk(const QString& requestId);
    Q_INVOKABLE void acknowledgeChunk(const QString& requestId);
    Q_INVOKABLE void closeFile(const QString& requestId);
    Q_INVOKABLE bool beginSaveFile(const QString& requestId, const QString& displayName,
                                   qint64 totalBytes,
                                   const QString& mimeType = QStringLiteral("application/zip"),
                                   const QString& filter = QStringLiteral("ZIP archives (*.zip)"));
    Q_INVOKABLE bool appendSaveFileChunk(const QString& requestId, qint64 offset,
                                         const QByteArray& bytes);
    Q_INVOKABLE bool finishSaveFile(const QString& requestId);
    Q_INVOKABLE void cancelSaveFile(const QString& requestId);

signals:
    void inspectionCompleted(const QString& requestId, bool success,
                             const QVariantList& files, const QString& reason);
    void fileOpened(const QString& requestId, bool success, const QString& fileName,
                    qint64 fileSize, const QString& reason);
    void chunkReady(const QString& requestId, qint64 offset, const QByteArray& data,
                    bool lastChunk, const QString& reason);
    void saveFileReady(const QString& requestId, bool success, const QString& reason);

private:
    friend struct LocalFileSelectionTestAccess;
    struct OpenFile {
        QFile* file = nullptr;
        qint64 nextOffset = 0;
        qint64 pendingOffset = -1;
        qint64 pendingSize = 0;
    };
    struct SaveFile {
        QString displayName;
        QString mimeType;
        qint64 expectedSize = 0;
        qint64 offset = 0;
        QByteArray wasmBytes;
        QSaveFile* nativeFile = nullptr;
    };
    struct SelectedFile {
        QString path;
        QString displayName;
        qint64 size = 0;
        bool wasmRegistered = false;
    };
    static QString pathForUrl(const QUrl& url);
    static bool allowedFile(const QString& kind, const QString& name);
    static QVector<SelectedFile> inspectFileUrls(const QVariantList& urls, const QString& kind,
                                                 QString* reason);
    bool beginSaveFileAtPath(const QString& requestId, const QString& safeName,
                             qint64 totalBytes, const QString& path,
                             const QString& mimeType = QStringLiteral("application/zip"),
                             const QString& filter = QStringLiteral("ZIP archives (*.zip)"));
    void finishFile(const QString& requestId);
    void queueSaveFileReady(const QString& requestId, quint64 generation,
                            bool success, const QString& reason);
    void inspectResolvedFiles(const QString& requestId, const QString& kind,
                              const QVector<SelectedFile>& files, const QString& reason,
                              quint64 generation);
    QHash<QString, OpenFile> m_openFiles;
    QHash<QString, SaveFile> m_saveFiles;
    QHash<QString, quint64> m_saveGenerations;
    quint64 m_nextSaveGeneration = 1;
#ifndef __EMSCRIPTEN__
    QHash<QString, QPointer<QFileDialog>> m_saveDialogs;
#endif
    QHash<QString, QVector<SelectedFile>> m_selections;
    QHash<QString, quint64> m_selectionGenerations;
    quint64 m_nextSelectionGeneration = 1;
    int m_activeInspectionThreads = 0;
#ifdef __EMSCRIPTEN__
    class DirectoryPicker;
    QHash<QString, DirectoryPicker*> m_directoryPickers;
#endif
};
