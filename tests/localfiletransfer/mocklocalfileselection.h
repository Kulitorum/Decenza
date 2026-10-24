#pragma once

#include <QObject>
#include <QByteArray>
#include <QJSValue>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <QtQml/qqmlregistration.h>

class MockLocalFileSelection final : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(LocalFileSelection)

    Q_PROPERTY(int nextId MEMBER nextId)
    Q_PROPERTY(QString lastSelectionId MEMBER lastSelectionId)
    Q_PROPERTY(QString lastClosedSelectionId MEMBER lastClosedSelectionId)
    Q_PROPERTY(bool acceptSave MEMBER acceptSave)
    Q_PROPERTY(bool acceptChunk MEMBER acceptChunk)
    Q_PROPERTY(bool acceptFinish MEMBER acceptFinish)
    Q_PROPERTY(QString savedMimeType MEMBER savedMimeType)
    Q_PROPERTY(QString savedFilter MEMBER savedFilter)
    Q_PROPERTY(QString cancelledSaveId MEMBER cancelledSaveId)
    Q_PROPERTY(int savedOffset MEMBER savedOffset)

public:
    Q_INVOKABLE QString newRequestId()
    {
        return QStringLiteral("selection-%1").arg(++nextId);
    }

    Q_INVOKABLE QVariantMap roundTrip(const QVariantMap& value) const { return value; }

    Q_INVOKABLE void selectDirectory(const QString& requestId, const QString& kind)
    {
        Q_UNUSED(kind)
        lastSelectionId = requestId;
    }

    Q_INVOKABLE void inspectFiles(const QString& requestId, const QVariantList& urls,
                                  const QString& kind)
    {
        Q_UNUSED(urls)
        Q_UNUSED(kind)
        lastSelectionId = requestId;
    }

    Q_INVOKABLE void closeSelection(const QString& requestId)
    {
        lastClosedSelectionId = requestId;
    }

    Q_INVOKABLE void closeFile(const QString& requestId) { Q_UNUSED(requestId) }
    Q_INVOKABLE void openSelectedFile(const QString& selectionId, int index,
                                      const QString& requestId)
    {
        Q_UNUSED(selectionId)
        Q_UNUSED(index)
        Q_UNUSED(requestId)
    }
    Q_INVOKABLE void readNextChunk(const QString& requestId) { Q_UNUSED(requestId) }
    Q_INVOKABLE void acknowledgeChunk(const QString& requestId) { Q_UNUSED(requestId) }

    Q_INVOKABLE bool beginSaveFile(const QString& requestId, const QString& displayName,
                                   double totalBytes, const QString& mimeType,
                                   const QString& filter)
    {
        Q_UNUSED(requestId)
        Q_UNUSED(displayName)
        Q_UNUSED(totalBytes)
        savedMimeType = mimeType;
        savedFilter = filter;
        savedOffset = 0;
        return acceptSave;
    }

    Q_INVOKABLE bool appendSaveFileChunk(const QString& requestId, double offset,
                                         const QJSValue& bytes)
    {
        Q_UNUSED(requestId)
        if (!acceptChunk || offset != savedOffset)
            return false;
        const QJSValue length = bytes.property(QStringLiteral("length"));
        const int byteCount = length.isUndefined()
            ? bytes.property(QStringLiteral("byteLength")).toInt() : length.toInt();
        savedOffset += byteCount;
        return true;
    }

    Q_INVOKABLE bool finishSaveFile(const QString& requestId)
    {
        Q_UNUSED(requestId)
        return acceptFinish;
    }

    Q_INVOKABLE void cancelSaveFile(const QString& requestId)
    {
        cancelledSaveId = requestId;
    }

    Q_INVOKABLE void emitByteArrayChunk(const QString& requestId)
    {
        emit chunkReady(requestId, 0, QByteArray::fromHex("0041ff"), true, {});
    }

signals:
    void inspectionCompleted(const QString& requestId, bool success,
                             const QVariantList& files, const QString& reason);
    void fileOpened(const QString& requestId, bool success, const QString& fileName,
                    qint64 fileSize, const QString& reason);
    void chunkReady(const QString& requestId, qint64 offset, const QByteArray& data,
                    bool lastChunk, const QString& reason);
    void saveFileReady(const QString& requestId, bool success, const QString& reason);

public:
    int nextId = 0;
    QString lastSelectionId;
    QString lastClosedSelectionId;
    bool acceptSave = true;
    bool acceptChunk = true;
    bool acceptFinish = true;
    QString savedMimeType;
    QString savedFilter;
    QString cancelledSaveId;
    int savedOffset = 0;
};
