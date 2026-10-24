#include "libraryservicesource.h"

#include "core/settings.h"
#include "core/settings_network.h"
#include "core/settings_theme.h"
#include "core/widgetlibrary.h"
#include "network/librarysharing.h"
#include "servicepayload.h"
#include "servicevalue.h"

#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {
constexpr qint64 kMaxRequestBytes = 2 * 1024 * 1024;

}

NativeLibraryServiceSource::NativeLibraryServiceSource(WidgetLibrary* library,
    LibrarySharing* sharing, Settings* settings, QObject* parent)
    : LibraryStateSimpleSource(parent), m_library(library), m_sharing(sharing), m_settings(settings)
{
    if (m_library) {
        connect(m_library, &WidgetLibrary::entriesChanged, this,
                [this] { publishLocalEntries(true); });
        connect(m_library, &WidgetLibrary::thumbnailSaved, this,
                [this] { publishLocalEntries(false); });
    }
    if (m_sharing) {
        connect(m_sharing, &LibrarySharing::browseRequestFinished, this,
                [this](const QString& id, bool success, const QVariantList& entries,
                       int total, const QString& reason) {
            completeSharing(id, success, {{QStringLiteral("entries"), annotateOwnership(entries)},
                                          {QStringLiteral("total"), total}}, reason);
        });
        connect(m_sharing, &LibrarySharing::featuredRequestFinished, this,
                [this](const QString& id, bool success, const QVariantList& entries,
                       const QString& reason) {
            completeSharing(id, success,
                            {{QStringLiteral("entries"), annotateOwnership(entries)}}, reason);
        });
        connect(m_sharing, &LibrarySharing::deleteRequestFinished, this,
                [this](const QString& id, const QString& serverId, bool success,
                       const QString& reason) {
            completeSharing(id, success, {{QStringLiteral("serverId"), serverId}}, reason);
        });
        connect(m_sharing, &LibrarySharing::uploadRequestFinished, this,
                [this](const QString& id, const QString&, bool success, const QString& serverId,
                       bool alreadyExists, const QString& reason) {
            completeSharing(id, success, {{QStringLiteral("serverId"), serverId},
                                           {QStringLiteral("alreadyExists"), alreadyExists}}, reason);
        });
        connect(m_sharing, &LibrarySharing::downloadRequestFinished, this,
                [this](const QString& id, const QString&, bool success, const QString& localId,
                       bool alreadyExists, const QString& reason) {
            completeSharing(id, success, {{QStringLiteral("localEntryId"), localId},
                                           {QStringLiteral("alreadyExists"), alreadyExists}}, reason);
        });
        connect(m_sharing, &LibrarySharing::flagRequestFinished, this,
                [this](const QString& id, const QString& serverId, bool success,
                       const QString& reason) {
            completeSharing(id, success, {{QStringLiteral("serverId"), serverId}}, reason);
        });
    }
    publishLocalEntries(false);
}

void NativeLibraryServiceSource::refresh()
{
    if (!m_library) {
        setReady(false);
        setErrorMessage(QStringLiteral("Library is unavailable"));
        return;
    }
    setErrorMessage(QString());
    publishLocalEntries(false);
}

void NativeLibraryServiceSource::publishLocalEntries(bool advanceRevision)
{
    if (!m_library) return;
    QVariantList entries = m_library->entriesForRemote();
    if (entries != localEntries()) {
        setLocalEntries(entries);
        if (advanceRevision && m_revision < 9007199254740991ULL) ++m_revision;
        setRevision(m_revision);
    }
    if (!ready()) setReady(true);
    if (!errorMessage().isEmpty()) setErrorMessage(QString());
}

void NativeLibraryServiceSource::finish(const QString& requestId, bool success,
                                         const QVariantMap& result, const QString& reason)
{
    if (!m_requests.finish(requestId)) return;
    const QString operation = m_operations.take(requestId);
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeLibraryServiceSource::completeSharing(const QString& requestId, bool success,
                                                  const QVariantMap& result,
                                                  const QString& reason)
{
    if (requestId.isEmpty() || !m_operations.contains(requestId)) return;
    publishLocalEntries(false);
    finish(requestId, success, result, reason);
}

QVariantList NativeLibraryServiceSource::annotateOwnership(const QVariantList& entries) const
{
    const QString localDeviceId = m_settings && m_settings->app()
        ? m_settings->app()->deviceId() : QString();
    QVariantList result;
    result.reserve(entries.size());
    for (const QVariant& value : entries) {
        QVariantMap entry = value.toMap();
        entry.insert(QStringLiteral("isOwn"), !localDeviceId.isEmpty()
            && entry.value(QStringLiteral("deviceId")).toString() == localDeviceId);
        result.append(entry);
    }
    return result;
}

bool NativeLibraryServiceSource::validArguments(const QString& operation,
                                                 const QVariantMap& args) const
{
    if (args.size() > 32 || ServicePayload::serializedSize(args, kMaxRequestBytes) < 0)
        return false;
    QSet<QString> allowed;
    if (operation == QLatin1String("getEntry") || operation == QLatin1String("getEntryData")
        || operation == QLatin1String("removeLocal") || operation == QLatin1String("exportEntry"))
        allowed = {QStringLiteral("entryId")};
    else if (operation == QLatin1String("download") || operation == QLatin1String("deleteRemote"))
        allowed = {QStringLiteral("serverId")};
    else if (operation == QLatin1String("addItem")) allowed = {QStringLiteral("itemId")};
    else if (operation == QLatin1String("addZone")) allowed = {QStringLiteral("zoneName")};
    else if (operation == QLatin1String("addLayout")) allowed = {QStringLiteral("includeTheme")};
    else if (operation == QLatin1String("addTheme")) allowed = {QStringLiteral("name")};
    else if (operation == QLatin1String("renameTheme"))
        allowed = {QStringLiteral("entryId"), QStringLiteral("name")};
    else if (operation == QLatin1String("applyItem") || operation == QLatin1String("applyZone"))
        allowed = {QStringLiteral("entryId"), QStringLiteral("targetZone"),
                   QStringLiteral("expectedLayout")};
    else if (operation == QLatin1String("applyLayout"))
        allowed = {QStringLiteral("entryId"), QStringLiteral("applyTheme"),
                   QStringLiteral("expectedLayout"), QStringLiteral("expectedTheme")};
    else if (operation == QLatin1String("applyTheme"))
        allowed = {QStringLiteral("entryId"), QStringLiteral("expectedTheme")};
    else if (operation == QLatin1String("importEntry")) allowed = {QStringLiteral("json")};
    else if (operation == QLatin1String("browseCommunity"))
        allowed = {QStringLiteral("type"), QStringLiteral("variable"), QStringLiteral("action"),
                   QStringLiteral("search"), QStringLiteral("sort"), QStringLiteral("page")};
    else if (operation == QLatin1String("browseMyUploads")) allowed = {QStringLiteral("page")};
    else if (operation == QLatin1String("featured")) allowed = {};
    else if (operation == QLatin1String("upload"))
        allowed = {QStringLiteral("entryId"), QStringLiteral("thumbnailFullPng"),
                   QStringLiteral("thumbnailCompactPng")};
    else if (operation == QLatin1String("flag"))
        allowed = {QStringLiteral("serverId"), QStringLiteral("reason")};
    else return false;
    for (auto it = args.cbegin(); it != args.cend(); ++it)
        if (!allowed.contains(it.key())) return false;

    auto isText = [&args](const QString& key, qsizetype maximum, bool required = true) {
        if (!args.contains(key)) return !required;
        return args.value(key).metaType().id() == QMetaType::QString
            && args.value(key).toString().size() <= maximum;
    };
    auto isSafeEntryId = [&args](const QString& key) {
        if (args.value(key).metaType().id() != QMetaType::QString) return false;
        const QString value = args.value(key).toString();
        return !value.isEmpty() && value != QLatin1String(".")
            && value != QLatin1String("..") && !value.contains(QLatin1Char('/'))
            && !value.contains(QLatin1Char('\\')) && !value.contains(QChar::Null)
            && value.size() <= 128;
    };
    auto validPage = [&args] {
        if (!args.contains(QStringLiteral("page"))) return true;
        int page = 0;
        return ServiceValue::boundedInteger(args.value(QStringLiteral("page")), 1, 100000, &page);
    };
    auto validImage = [&args](const QString& key) {
        if (!args.contains(key)) return true;
        if (args.value(key).metaType().id() != QMetaType::QImage) return false;
        const QImage image = args.value(key).value<QImage>();
        return image.isNull() || (image.width() <= 1024 && image.height() <= 1024
                                  && qint64(image.width()) * image.height() <= 1000000);
    };
    if (operation == QLatin1String("getEntry") || operation == QLatin1String("getEntryData")
        || operation == QLatin1String("removeLocal") || operation == QLatin1String("exportEntry"))
        return isSafeEntryId(QStringLiteral("entryId"));
    if (operation == QLatin1String("download") || operation == QLatin1String("deleteRemote"))
        return isSafeEntryId(QStringLiteral("serverId"));
    if (operation == QLatin1String("addItem")) return isText(QStringLiteral("itemId"), 128);
    if (operation == QLatin1String("addZone")) return isText(QStringLiteral("zoneName"), 64);
    if (operation == QLatin1String("addLayout"))
        return !args.contains(QStringLiteral("includeTheme"))
            || args.value(QStringLiteral("includeTheme")).metaType().id() == QMetaType::Bool;
    if (operation == QLatin1String("addTheme")) return isText(QStringLiteral("name"), 128, false);
    if (operation == QLatin1String("renameTheme"))
        return isSafeEntryId(QStringLiteral("entryId")) && isText(QStringLiteral("name"), 128);
    if (operation == QLatin1String("applyItem") || operation == QLatin1String("applyZone"))
        return isSafeEntryId(QStringLiteral("entryId")) && isText(QStringLiteral("targetZone"), 64)
            && isText(QStringLiteral("expectedLayout"), 768 * 1024);
    if (operation == QLatin1String("applyLayout"))
        return isSafeEntryId(QStringLiteral("entryId"))
            && isText(QStringLiteral("expectedLayout"), 768 * 1024)
            && (!args.contains(QStringLiteral("applyTheme"))
                || args.value(QStringLiteral("applyTheme")).metaType().id() == QMetaType::Bool)
            && (!args.value(QStringLiteral("applyTheme")).toBool()
                || args.value(QStringLiteral("expectedTheme")).metaType().id() == QMetaType::QVariantMap);
    if (operation == QLatin1String("applyTheme"))
        return isSafeEntryId(QStringLiteral("entryId"))
            && args.value(QStringLiteral("expectedTheme")).metaType().id() == QMetaType::QVariantMap;
    if (operation == QLatin1String("importEntry")) return isText(QStringLiteral("json"), 1024 * 1024);
    if (operation == QLatin1String("browseCommunity"))
        return isText(QStringLiteral("type"), 32, false)
            && isText(QStringLiteral("variable"), 64, false)
            && isText(QStringLiteral("action"), 128, false)
            && isText(QStringLiteral("search"), 512, false)
            && isText(QStringLiteral("sort"), 32, false) && validPage();
    if (operation == QLatin1String("browseMyUploads") || operation == QLatin1String("featured"))
        return validPage();
    if (operation == QLatin1String("upload"))
        return isSafeEntryId(QStringLiteral("entryId"))
            && validImage(QStringLiteral("thumbnailFullPng"))
            && validImage(QStringLiteral("thumbnailCompactPng"));
    if (operation == QLatin1String("flag"))
        return isSafeEntryId(QStringLiteral("serverId")) && isText(QStringLiteral("reason"), 512);
    return false;
}

bool NativeLibraryServiceSource::expectedLayoutMatches(const QVariantMap& args) const
{
    return m_settings && m_settings->network()
        && args.value(QStringLiteral("expectedLayout")).toString()
        == m_settings->network()->layoutConfiguration();
}

void NativeLibraryServiceSource::execute(QString requestId, QString operation,
                                           QVariantMap args)
{
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId
        || start == ServiceRequestIds::Start::InvalidId)
        return;
    if (start == ServiceRequestIds::Start::LimitReached) {
        emit operationCompleted(requestId, operation, false, {}, QStringLiteral("tooManyRequests"));
        return;
    }
    m_operations.insert(requestId, operation);
    if (!validArguments(operation, args)) {
        finish(requestId, false, {}, QStringLiteral("invalidRequest"));
        return;
    }
    if (!m_library) {
        finish(requestId, false, {}, QStringLiteral("disconnected"));
        return;
    }

    const QString id = args.value(QStringLiteral("entryId")).toString();
    if (operation == QLatin1String("getEntry")) {
        const QVariantMap entry = m_library->entryForRemote(id);
        finish(requestId, !entry.isEmpty(), {{QStringLiteral("entry"), entry}},
               entry.isEmpty() ? QStringLiteral("notFound") : QString());
    } else if (operation == QLatin1String("getEntryData")) {
        const QVariantMap entry = m_library->getEntryData(id);
        finish(requestId, !entry.isEmpty(), {{QStringLiteral("entry"), entry}},
               entry.isEmpty() ? QStringLiteral("notFound") : QString());
    } else if (operation == QLatin1String("addItem")) {
        const QString entryId = m_library->addItemFromLayout(args.value(QStringLiteral("itemId")).toString());
        finish(requestId, !entryId.isEmpty(), {{QStringLiteral("entryId"), entryId}},
               entryId.isEmpty() ? QStringLiteral("operationFailed") : QString());
    } else if (operation == QLatin1String("addZone")) {
        const QString entryId = m_library->addZoneFromLayout(args.value(QStringLiteral("zoneName")).toString());
        finish(requestId, !entryId.isEmpty(), {{QStringLiteral("entryId"), entryId}},
               entryId.isEmpty() ? QStringLiteral("operationFailed") : QString());
    } else if (operation == QLatin1String("addLayout")) {
        const QString entryId = m_library->addCurrentLayout(args.value(QStringLiteral("includeTheme")).toBool());
        finish(requestId, !entryId.isEmpty(), {{QStringLiteral("entryId"), entryId}},
               entryId.isEmpty() ? QStringLiteral("operationFailed") : QString());
    } else if (operation == QLatin1String("addTheme")) {
        const QString entryId = m_library->addCurrentTheme(args.value(QStringLiteral("name")).toString());
        finish(requestId, !entryId.isEmpty(), {{QStringLiteral("entryId"), entryId}},
               entryId.isEmpty() ? QStringLiteral("operationFailed") : QString());
    } else if (operation == QLatin1String("removeLocal")) {
        const bool ok = m_library->removeEntry(id);
        finish(requestId, ok, {{QStringLiteral("entryId"), id}}, ok ? QString() : QStringLiteral("notFound"));
    } else if (operation == QLatin1String("renameTheme")) {
        const bool ok = m_library->updateThemeName(id, args.value(QStringLiteral("name")).toString());
        finish(requestId, ok, {{QStringLiteral("entryId"), id}}, ok ? QString() : QStringLiteral("operationFailed"));
    } else if (operation == QLatin1String("applyItem") || operation == QLatin1String("applyZone")
               || operation == QLatin1String("applyLayout")) {
        if (!expectedLayoutMatches(args)) {
            finish(requestId, false, {}, QStringLiteral("conflict"));
            return;
        }
        if (operation == QLatin1String("applyLayout")
            && args.value(QStringLiteral("applyTheme")).toBool()
            && (!m_settings || args.value(QStringLiteral("expectedTheme")).toMap()
                != m_settings->theme()->customThemeColors())) {
            finish(requestId, false, {}, QStringLiteral("conflict"));
            return;
        }
        bool ok = false;
        if (operation == QLatin1String("applyItem"))
            ok = m_library->applyItem(id, args.value(QStringLiteral("targetZone")).toString());
        else if (operation == QLatin1String("applyZone"))
            ok = m_library->applyZone(id, args.value(QStringLiteral("targetZone")).toString());
        else
            ok = m_library->applyLayout(id, args.value(QStringLiteral("applyTheme")).toBool());
        finish(requestId, ok, {{QStringLiteral("entryId"), id}}, ok ? QString() : QStringLiteral("operationFailed"));
    } else if (operation == QLatin1String("applyTheme")) {
        const QVariantMap expected = args.value(QStringLiteral("expectedTheme")).toMap();
        if (!m_settings || expected != m_settings->theme()->customThemeColors()) {
            finish(requestId, false, {}, QStringLiteral("conflict"));
            return;
        }
        const bool ok = m_library->applyThemeEntry(id);
        finish(requestId, ok, {{QStringLiteral("entryId"), id}}, ok ? QString() : QStringLiteral("operationFailed"));
    } else if (operation == QLatin1String("importEntry")) {
        const QString entryId = m_library->importEntry(args.value(QStringLiteral("json")).toString().toUtf8());
        finish(requestId, !entryId.isEmpty(), {{QStringLiteral("entryId"), entryId}},
               entryId.isEmpty() ? QStringLiteral("invalidEntry") : QString());
    } else if (operation == QLatin1String("exportEntry")) {
        const QByteArray json = m_library->exportEntry(id);
        finish(requestId, !json.isEmpty(), {{QStringLiteral("json"), QString::fromUtf8(json)}},
               json.isEmpty() ? QStringLiteral("notFound") : QString());
    } else if (operation == QLatin1String("browseCommunity")) {
        if (!m_sharing || !m_sharing->browseCommunityForRequest(requestId,
            args.value(QStringLiteral("type")).toString(), args.value(QStringLiteral("variable")).toString(),
            args.value(QStringLiteral("action")).toString(), args.value(QStringLiteral("search")).toString(),
            args.value(QStringLiteral("sort"), QStringLiteral("newest")).toString(),
            qMax(1, args.value(QStringLiteral("page"), 1).toInt())))
            finish(requestId, false, {}, QStringLiteral("busy"));
    } else if (operation == QLatin1String("browseMyUploads")) {
        if (!m_sharing || !m_sharing->browseMyUploadsForRequest(requestId,
                qMax(1, args.value(QStringLiteral("page"), 1).toInt())))
            finish(requestId, false, {}, QStringLiteral("busy"));
    } else if (operation == QLatin1String("featured")) {
        if (!m_sharing || !m_sharing->loadFeaturedForRequest(requestId))
            finish(requestId, false, {}, QStringLiteral("disconnected"));
    } else if (operation == QLatin1String("download")) {
        if (!m_sharing || !m_sharing->downloadEntryForRequest(
                requestId, args.value(QStringLiteral("serverId")).toString()))
            finish(requestId, false, {}, QStringLiteral("busy"));
    } else if (operation == QLatin1String("upload")) {
        const QImage full = args.value(QStringLiteral("thumbnailFullPng")).value<QImage>();
        const QImage compact = args.value(QStringLiteral("thumbnailCompactPng")).value<QImage>();
        if (!full.isNull()) m_library->saveThumbnail(id, full);
        if (!compact.isNull()) m_library->saveThumbnailCompact(id, compact);
        if (!m_sharing || !m_sharing->uploadEntryForRequest(requestId, id, full, compact))
            finish(requestId, false, {}, QStringLiteral("operationFailed"));
    } else if (operation == QLatin1String("deleteRemote")) {
        if (!m_sharing || !m_sharing->deleteFromServerForRequest(
                requestId, args.value(QStringLiteral("serverId")).toString()))
            finish(requestId, false, {}, QStringLiteral("operationFailed"));
    } else if (operation == QLatin1String("flag")) {
        if (!m_sharing || !m_sharing->flagEntryForRequest(requestId,
                args.value(QStringLiteral("serverId")).toString(),
                args.value(QStringLiteral("reason")).toString()))
            finish(requestId, false, {}, QStringLiteral("operationFailed"));
    }
}
