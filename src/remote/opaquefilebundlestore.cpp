#include "opaquefilebundlestore.h"

#include "filebundlepolicy.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QStringList>
#include <utility>

namespace {
bool exactKeys(const QVariantMap& arguments, const QStringList& keys)
{
    if (arguments.size() != keys.size()) return false;
    for (const QString& key : keys)
        if (!arguments.contains(key)) return false;
    return true;
}
}

OpaqueFileBundleStore::OpaqueFileBundleStore(QObject* parent) : QObject(parent)
{
    m_expiryTimer.setSingleShot(true);
    connect(&m_expiryTimer, &QTimer::timeout, this, &OpaqueFileBundleStore::clear);
}

QVariantMap OpaqueFileBundleStore::adopt(std::shared_ptr<QTemporaryDir> directory,
                                        const QString& opaqueId, const QString& fileName,
                                        const QString& displayName, qint64 maximumBytes,
                                        const QString& mimeType, const QString& filter)
{
    const bool json = mimeType == QLatin1String("application/json")
        && filter == QLatin1String("JSON files (*.json)");
    const bool zip = mimeType == QLatin1String("application/zip")
        && filter == QLatin1String("ZIP archives (*.zip)");
    const bool log = mimeType == QLatin1String("text/plain")
        && filter == QLatin1String("Log files (*.log)");
    const bool text = mimeType == QLatin1String("text/plain")
        && filter == QLatin1String("Text files (*.txt)");
    const QString suffix = json ? QStringLiteral(".json")
        : log ? QStringLiteral(".log") : text ? QStringLiteral(".txt") : QStringLiteral(".zip");
    if (hasBundle() || !directory || !directory->isValid()
        || !FileBundlePolicy::validOpaqueId(opaqueId) || (!json && !zip && !log && !text)
        || fileName != opaqueId + suffix || displayName.isEmpty() || displayName.size() > 128
        || QFileInfo(displayName).fileName() != displayName || displayName.contains(QLatin1Char('\\'))
        || displayName.contains(QChar::Null) || !displayName.endsWith(suffix, Qt::CaseInsensitive)
        || maximumBytes <= 0 || maximumBytes > ServiceValue::MaximumExactInteger
        || (log && maximumBytes > FileBundlePolicy::MaximumLogBytes))
        return {};
    const QFileInfo info(directory->filePath(fileName));
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > maximumBytes)
        return {};
    m_directory = std::move(directory);
    m_id = opaqueId;
    m_fileName = fileName;
    m_size = info.size();
    m_offset = 0;
    m_expiryTimer.start(FileBundlePolicy::IdleTimeoutMs);
    return {{QStringLiteral("bundleId"), opaqueId}, {QStringLiteral("displayName"), displayName},
            {QStringLiteral("size"), m_size}, {QStringLiteral("mimeType"), mimeType},
            {QStringLiteral("filter"), filter}};
}

bool OpaqueFileBundleStore::identifiesBundle(const QVariantMap& arguments) const
{
    return hasBundle() && arguments.value(QStringLiteral("bundleId")).metaType().id() == QMetaType::QString
        && FileBundlePolicy::validOpaqueId(arguments.value(QStringLiteral("bundleId")).toString())
        && arguments.value(QStringLiteral("bundleId")).toString() == m_id;
}

QVariantMap OpaqueFileBundleStore::readChunk(const QVariantMap& arguments, QString* reason)
{
    reason->clear();
    if (!exactKeys(arguments, {QStringLiteral("bundleId"), QStringLiteral("offset")})
        || arguments.value(QStringLiteral("bundleId")).metaType().id() != QMetaType::QString) {
        *reason = QStringLiteral("invalidArguments");
        return {};
    }
    if (!identifiesBundle(arguments)) {
        *reason = QStringLiteral("invalidBundle");
        return {};
    }
    QFile file(m_directory->filePath(m_fileName));
    qint64 offset = -1;
    if (file.size() != m_size
        || !FileBundlePolicy::validOffset(arguments.value(QStringLiteral("offset")),
                                          m_offset, m_size, &offset)
        || offset >= m_size || !file.open(QIODevice::ReadOnly) || !file.seek(offset)) {
        *reason = QStringLiteral("invalidOffset");
        return {};
    }
    const QByteArray bytes = file.read(FileBundlePolicy::MaximumChunkBytes);
    if (!FileBundlePolicy::validChunk(offset, bytes.size(), m_size)) {
        *reason = QStringLiteral("readFailed");
        return {};
    }
    m_offset = offset + bytes.size();
    m_expiryTimer.start(FileBundlePolicy::IdleTimeoutMs);
    return {{QStringLiteral("data"), bytes}, {QStringLiteral("nextOffset"), m_offset},
            {QStringLiteral("complete"), m_offset == m_size}};
}

bool OpaqueFileBundleStore::release(const QVariantMap& arguments, QString* reason)
{
    reason->clear();
    if (!exactKeys(arguments, {QStringLiteral("bundleId")})
        || arguments.value(QStringLiteral("bundleId")).metaType().id() != QMetaType::QString) {
        *reason = QStringLiteral("invalidArguments");
        return false;
    }
    if (!identifiesBundle(arguments)
        || !QFile::remove(m_directory->filePath(m_fileName))) {
        *reason = QStringLiteral("invalidBundle");
        return false;
    }
    clear();
    return true;
}

void OpaqueFileBundleStore::clear()
{
    m_expiryTimer.stop();
    m_directory.reset();
    m_id.clear();
    m_fileName.clear();
    m_size = 0;
    m_offset = 0;
}
