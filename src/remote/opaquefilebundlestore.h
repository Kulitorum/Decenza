#pragma once

#include <QObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <memory>

// Host-owned temporary output. Only its opaque identity and bounded chunks
// cross the service boundary; the output path remains private to the host.
class OpaqueFileBundleStore final : public QObject
{
public:
    explicit OpaqueFileBundleStore(QObject* parent = nullptr);

    QVariantMap adopt(std::shared_ptr<QTemporaryDir> directory, const QString& opaqueId,
                      const QString& fileName, const QString& displayName,
                      qint64 maximumBytes, const QString& mimeType, const QString& filter);
    QVariantMap readChunk(const QVariantMap& arguments, QString* reason);
    bool release(const QVariantMap& arguments, QString* reason);
    bool hasBundle() const { return bool(m_directory); }

private:
    friend struct OpaqueFileBundleStoreTestAccess;
    void clear();
    bool identifiesBundle(const QVariantMap& arguments) const;

    std::shared_ptr<QTemporaryDir> m_directory;
    QString m_id;
    QString m_fileName;
    qint64 m_size = 0;
    qint64 m_offset = 0;
    QTimer m_expiryTimer;
};
