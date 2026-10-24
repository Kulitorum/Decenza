#include "fullbackuprestore.h"

#include "ai/aiconversation.h"
#include "ai/aimanager.h"
#include "appsettings.h"
#include "core/profilestorage.h"
#include "profile/profile.h"
#include "core/settings.h"
#include "core/settingsserializer.h"
#include "history/shothistorystorage.h"
#include "screensaver/screensavervideomanager.h"
#include "translationmanager.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QThreadPool>
#include <QRunnable>
#include <QtEndian>
#include <utility>
#include <limits>

namespace {
constexpr quint32 MaxEntries = 100000;
constexpr quint32 MaxNameBytes = 10000;
constexpr quint64 CopyChunkBytes = 48 * 1024;

struct ArchiveFile {
    QString name;
    QString path;
};

struct StagingCleanup {
    QStringList paths;
    ~StagingCleanup() { for (const QString& path : paths) QFile::remove(path); }
};

}

struct FullBackupRestore::PreparedArchive {
    std::shared_ptr<QTemporaryDir> directory;
    ArchiveFile settings;
    ArchiveFile extraSettings;
    ArchiveFile shots;
    ArchiveFile conversations;
    ArchiveFile mediaCatalog;
    QHash<QString, QJsonObject> mediaMetadata;
    QList<ArchiveFile> profiles;
    QList<ArchiveFile> media;
    QJsonObject settingsObject;
    QJsonObject extraSettingsObject;
    QJsonArray conversationsArray;
};

struct FullBackupRestore::ActiveRestore {
    QString requestId;
    Options options;
    std::shared_ptr<QTemporaryDir> directory;
    std::shared_ptr<PreparedArchive> archive;
    bool settingsRestored = false;
    bool shotsImported = false;
    int profilesImported = 0;
    int profilesSkipped = 0;
    int mediaImported = 0;
    int mediaSkipped = 0;
    QJsonArray conversations;
};

namespace {

void clearStoredConversations(AppSettings& settings)
{
    const QJsonArray existing = QJsonDocument::fromJson(
        settings.value(QStringLiteral("ai/conversations/index")).toByteArray()).array();
    for (const QJsonValue& value : existing) {
        const QString key = value.toObject().value(QStringLiteral("key")).toString();
        if (!key.isEmpty()) settings.remove(QStringLiteral("ai/conversations/") + key);
    }
    settings.remove(QStringLiteral("ai/conversations/index"));
}

bool readExact(QFile& file, char* output, qint64 bytes)
{
    while (bytes > 0) {
        const qint64 count = file.read(output, bytes);
        if (count <= 0) return false;
        output += count;
        bytes -= count;
    }
    return true;
}

bool readU32(QFile& file, quint32* value)
{
    uchar bytes[4];
    if (!readExact(file, reinterpret_cast<char*>(bytes), sizeof(bytes))) return false;
    *value = qFromLittleEndian<quint32>(bytes);
    return true;
}

bool readU64(QFile& file, quint64* value)
{
    uchar bytes[8];
    if (!readExact(file, reinterpret_cast<char*>(bytes), sizeof(bytes))) return false;
    *value = qFromLittleEndian<quint64>(bytes);
    return true;
}

bool safeRelativeName(const QString& name)
{
    if (name.isEmpty() || name.size() > 1000 || name.contains(QChar::Null)
        || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return false;
    const QStringList parts = name.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString& part : parts)
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    return true;
}

bool recognizedName(const QString& name)
{
    return name == QLatin1String("settings.json")
        || name == QLatin1String("extra_settings.json")
        || name == QLatin1String("shots.db")
        || name == QLatin1String("ai_conversations.json")
        || name.startsWith(QLatin1String("profiles/"))
        || name.startsWith(QLatin1String("media/"));
}

bool safeRecognizedName(const QString& name)
{
    if (!safeRelativeName(name)) return false;
    if (name.startsWith(QLatin1String("profiles/"))) {
        const QString relative = name.mid(9);
        const QStringList parts = relative.split(QLatin1Char('/'));
        const bool basenameOnly = parts.size() == 1;
        const bool knownFolder = parts.size() == 2
            && (parts.first() == QLatin1String("user")
                || parts.first() == QLatin1String("downloaded"));
        return (basenameOnly || knownFolder)
            && parts.last().endsWith(QLatin1String(".json"), Qt::CaseInsensitive);
    }
    if (name.startsWith(QLatin1String("media/"))) {
        const QStringList parts = name.mid(6).split(QLatin1Char('/'));
        return parts.size() == 1 && !parts.first().isEmpty();
    }
    return name == QLatin1String("settings.json")
        || name == QLatin1String("extra_settings.json")
        || name == QLatin1String("shots.db")
        || name == QLatin1String("ai_conversations.json");
}

bool validateStagedPayloads(const std::shared_ptr<FullBackupRestore::PreparedArchive>& archive,
                            QString* error);

std::shared_ptr<FullBackupRestore::PreparedArchive> parseArchive(
    const QString& inputPath, const std::shared_ptr<QTemporaryDir>& directory,
    QString* error, int* httpStatus)
{
    auto fail = [&](const QString& message, int status) {
        *error = message;
        *httpStatus = status;
        return std::shared_ptr<FullBackupRestore::PreparedArchive>();
    };
    QFile input(inputPath);
    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.isFile() || inputInfo.isSymLink() || !input.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Failed to open uploaded file"), 500);
    if (input.size() < 12)
        return fail(QStringLiteral("Invalid backup file size"), 400);
    char magic[4];
    quint32 version = 0;
    quint32 count = 0;
    if (!readExact(input, magic, sizeof(magic)) || QByteArray(magic, 4) != QByteArrayLiteral("DCBK"))
        return fail(QStringLiteral("Invalid backup file. Expected a .dcbackup file."), 400);
    if (!readU32(input, &version) || !readU32(input, &count))
        return fail(QStringLiteral("Backup file is truncated"), 400);
    if (version != 1)
        return fail(QStringLiteral("Unsupported backup version: %1").arg(version), 400);
    if (count > MaxEntries)
        return fail(QStringLiteral("Backup file appears corrupt (too many entries)"), 400);

    auto prepared = std::make_shared<FullBackupRestore::PreparedArchive>();
    prepared->directory = directory;
    QSet<QString> singletonEntries;
    QSet<QString> fileEntries;
    quint64 totalEntryBytes = 0;
    for (quint32 i = 0; i < count; ++i) {
        quint32 nameLength = 0;
        quint64 dataLength = 0;
        if (!readU32(input, &nameLength) || nameLength == 0 || nameLength > MaxNameBytes)
            return fail(QStringLiteral("Backup file is corrupt (entry name)"), 400);
        QByteArray nameBytes(static_cast<qsizetype>(nameLength), Qt::Uninitialized);
        if (!readExact(input, nameBytes.data(), nameBytes.size()))
            return fail(QStringLiteral("Backup file is corrupt (entry name)"), 400);
        const QString name = QString::fromUtf8(nameBytes.constData(), nameBytes.size());
        if (name.toUtf8() != nameBytes)
            return fail(QStringLiteral("Backup file contains an invalid entry name"), 400);
        if (!readU64(input, &dataLength) || dataLength > static_cast<quint64>(input.size() - input.pos()))
            return fail(QStringLiteral("Backup file is corrupt (entry data)"), 400);
        constexpr quint64 MaxSignedFileBytes = static_cast<quint64>(std::numeric_limits<qint64>::max());
        if (dataLength > MaxSignedFileBytes || totalEntryBytes > MaxSignedFileBytes - dataLength)
            return fail(QStringLiteral("Backup file entry sizes overflow the supported file range"), 400);
        totalEntryBytes += dataLength;

        const bool recognized = recognizedName(name);
        if (recognized && !safeRecognizedName(name))
            return fail(QStringLiteral("Backup file contains an unsafe entry path"), 400);
        if (recognized && (name == QLatin1String("settings.json")
            || name == QLatin1String("extra_settings.json") || name == QLatin1String("shots.db")
            || name == QLatin1String("ai_conversations.json"))) {
            if (singletonEntries.contains(name))
                return fail(QStringLiteral("Backup file contains duplicate entries"), 400);
            singletonEntries.insert(name);
        }
        if (recognized && (name.startsWith(QLatin1String("profiles/"))
            || name.startsWith(QLatin1String("media/")))) {
            if (fileEntries.contains(name.toCaseFolded()))
                return fail(QStringLiteral("Backup file contains duplicate entries"), 400);
            fileEntries.insert(name.toCaseFolded());
        }
        QString stagedName = QStringLiteral("entry_%1").arg(i);
        if (name.startsWith(QLatin1String("media/"))) {
            const QString suffix = QFileInfo(name.mid(6)).suffix();
            if (!suffix.isEmpty()) stagedName += QLatin1Char('.') + suffix;
        }
        const QString stagedPath = directory->filePath(stagedName);
        if (recognized) {
            QFile output(stagedPath);
            if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                return fail(QStringLiteral("Failed to stage backup entry"), 500);
            quint64 remaining = dataLength;
            QByteArray buffer(static_cast<qsizetype>(CopyChunkBytes), Qt::Uninitialized);
            while (remaining > 0) {
                const qint64 amount = static_cast<qint64>(qMin<quint64>(remaining, CopyChunkBytes));
                if (!readExact(input, buffer.data(), amount)
                    || output.write(buffer.constData(), amount) != amount)
                    return fail(QStringLiteral("Backup file is truncated while reading an entry"), 400);
                remaining -= static_cast<quint64>(amount);
            }
            output.close();
            const ArchiveFile file{name, stagedPath};
            if (name == QLatin1String("settings.json")) prepared->settings = file;
            else if (name == QLatin1String("extra_settings.json")) prepared->extraSettings = file;
            else if (name == QLatin1String("shots.db")) prepared->shots = file;
            else if (name == QLatin1String("ai_conversations.json")) prepared->conversations = file;
            else if (name.startsWith(QLatin1String("profiles/"))) prepared->profiles.append(file);
            else if (name == QLatin1String("media/catalog.json")) prepared->mediaCatalog = file;
            else if (name.startsWith(QLatin1String("media/"))
                && name != QLatin1String("media/catalog.json")
                && name != QLatin1String("media/index.json")) prepared->media.append(file);
        } else if (!input.seek(input.pos() + static_cast<qint64>(dataLength))) {
            return fail(QStringLiteral("Backup file is truncated while skipping an entry"), 400);
        }
    }
    if (input.pos() != input.size())
        return fail(QStringLiteral("Backup file contains trailing data"), 400);
    QString payloadError;
    if (!validateStagedPayloads(prepared, &payloadError))
        return fail(payloadError, 400);
    return prepared;
}

bool validateStagedPayloads(const std::shared_ptr<FullBackupRestore::PreparedArchive>& archive,
                            QString* error)
{
    for (const ArchiveFile* file : {&archive->settings, &archive->extraSettings,
                                    &archive->conversations}) {
        if (file->path.isEmpty()) continue;
        QFile input(file->path);
        if (!input.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Backup contains unreadable JSON");
            return false;
        }
        const QJsonDocument document = QJsonDocument::fromJson(input.readAll());
        const bool expectedArray = file->name == QLatin1String("ai_conversations.json");
        if ((expectedArray && !document.isArray()) || (!expectedArray && !document.isObject())) {
            *error = QStringLiteral("Backup contains invalid JSON");
            return false;
        }
        if (file->name == QLatin1String("settings.json")) archive->settingsObject = document.object();
        else if (file->name == QLatin1String("extra_settings.json")) archive->extraSettingsObject = document.object();
        else if (expectedArray) {
            archive->conversationsArray = document.array();
            for (const QJsonValue& value : archive->conversationsArray) {
                const QJsonObject conversation = value.toObject();
                // Match the malformed-entry checks in importConversationsStatic
                // before Replace can clear the existing conversation store.
                if (conversation.value(QStringLiteral("key")).toString().isEmpty()
                    || !conversation.value(QStringLiteral("messages")).isArray()) {
                    *error = QStringLiteral("Backup contains an invalid AI conversation");
                    return false;
                }
            }
        }
    }
    if (!archive->shots.path.isEmpty()) {
        QFile database(archive->shots.path);
        if (!database.open(QIODevice::ReadOnly) || database.size() < 100
            || !database.read(16).startsWith("SQLite format 3")) {
            *error = QStringLiteral("Backup contains an invalid shot database");
            return false;
        }
    }
    for (const ArchiveFile& profile : archive->profiles) {
        QFile input(profile.path);
        if (!input.open(QIODevice::ReadOnly)
            || !Profile::fromJson(QJsonDocument::fromJson(input.readAll())).isValid()) {
            *error = QStringLiteral("Backup contains an invalid profile");
            return false;
        }
    }
    QHash<QString, QJsonObject> catalogByName;
    if (!archive->mediaCatalog.path.isEmpty()) {
        QFile catalog(archive->mediaCatalog.path);
        if (!catalog.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Backup contains an unreadable media catalog");
            return false;
        }
        for (const QJsonValue& value : QJsonDocument::fromJson(catalog.readAll()).array()) {
            const QJsonObject item = value.toObject();
            catalogByName.insert(QFileInfo(item.value(QStringLiteral("path")).toString()).fileName(), item);
        }
    }
    for (const ArchiveFile& file : archive->media) {
        const QString name = file.name.mid(6);
        archive->mediaMetadata.insert(file.path, catalogByName.value(name));
        if (!ScreensaverVideoManager::personalMediaTypeForFile(name)) {
            *error = QStringLiteral("Backup contains unsupported media");
            return false;
        }
    }
    return true;
}
}

FullBackupRestore::FullBackupRestore(Settings* settings, ShotHistoryStorage* storage,
                                     ProfileStorage* profiles, ScreensaverVideoManager* media,
                                     AIManager* aiManager, TranslationManager* translations,
                                     QObject* parent)
    : QObject(parent), m_settings(settings), m_storage(storage), m_profiles(profiles),
      m_media(media), m_aiManager(aiManager), m_translations(translations)
{
}

FullBackupRestore::~FullBackupRestore() = default;

bool FullBackupRestore::busy() const
{
    return bool(m_active);
}

bool FullBackupRestore::restoreFile(const QString& requestId, const QString& path,
                                    const Options& options,
                                    std::shared_ptr<QTemporaryDir> inputLease)
{
    if (m_active || (m_restoreAllowed && !m_restoreAllowed())) return false;
    QCoreApplication* const application = QCoreApplication::instance();
    if (!application) return false;
    auto active = std::make_unique<ActiveRestore>();
    active->requestId = requestId;
    active->options = options;
    active->directory = std::make_shared<QTemporaryDir>();
    if (!active->directory->isValid()) return false;
    auto stageDirectory = active->directory;
    m_active = std::move(active);
    const QPointer<FullBackupRestore> guard(this);
    emit busyChanged();
    if (!guard || !m_active || m_active->requestId != requestId) return true;
    QThreadPool::globalInstance()->start(QRunnable::create(
        [guard, application, requestId, path, stageDirectory, inputLease] {
            Q_UNUSED(inputLease)
            QString error;
            int status = 400;
            auto archive = parseArchive(path, stageDirectory, &error, &status);
            QMetaObject::invokeMethod(application, [guard, requestId, archive, error, status] {
                if (guard) guard->archivePrepared(requestId, archive, error, status);
            }, Qt::QueuedConnection);
        }));
    return true;
}

void FullBackupRestore::applySettingsData(const std::shared_ptr<PreparedArchive>& archive,
                                          const Options& options)
{
    const QPointer<FullBackupRestore> guard(this);
    if (options.settings && !archive->settings.path.isEmpty() && m_settings) {
        const QJsonObject settingsJson = archive->settingsObject;
        if (!settingsJson.isEmpty()) {
            const bool imported = SettingsSerializer::importFromJson(
                m_settings, settingsJson, SettingsSerializer::sensitiveKeys());
            if (!guard) return;
            if (m_active) m_active->settingsRestored = imported;
        }
        if (!guard) return;
    }
    if (options.settings && !archive->extraSettings.path.isEmpty()) {
        const QJsonObject extra = archive->extraSettingsObject;
        if (!extra.isEmpty()) {
            AppSettings settings;
            const QJsonObject shotMap = extra.value(QStringLiteral("shotMap")).toObject();
            if (!shotMap.isEmpty()) {
                settings.setValue(QStringLiteral("shotMap/manualCity"), shotMap.value(QStringLiteral("manualCity")).toString());
                settings.setValue(QStringLiteral("shotMap/manualLat"), shotMap.value(QStringLiteral("manualLat")).toDouble());
                settings.setValue(QStringLiteral("shotMap/manualLon"), shotMap.value(QStringLiteral("manualLon")).toDouble());
                settings.setValue(QStringLiteral("shotMap/manualCountryCode"), shotMap.value(QStringLiteral("manualCountryCode")).toString());
                settings.setValue(QStringLiteral("shotMap/manualGeocoded"), shotMap.value(QStringLiteral("manualGeocoded")).toBool());
            }
            const QJsonObject accessibility = extra.value(QStringLiteral("accessibility")).toObject();
            if (!accessibility.isEmpty()) {
                const QStringList fields = {QStringLiteral("enabled"), QStringLiteral("ttsEnabled"),
                    QStringLiteral("tickEnabled"), QStringLiteral("tickSoundIndex"),
                    QStringLiteral("tickVolume"), QStringLiteral("extractionAnnouncementsEnabled"),
                    QStringLiteral("extractionAnnouncementInterval"), QStringLiteral("extractionAnnouncementMode")};
                for (const QString& field : fields)
                    if (accessibility.contains(field))
                        settings.setValue(QStringLiteral("accessibility/") + field,
                                          accessibility.value(field).toVariant());
            }
            if (extra.contains(QStringLiteral("language")))
                settings.setValue(QStringLiteral("localization/language"),
                                  extra.value(QStringLiteral("language")).toString());
            settings.setValue(QStringLiteral("accessibility/_migratedFromLegacyV1"), true);
            settings.sync();
        }
    }
}

void FullBackupRestore::applyNonShotData(const std::shared_ptr<PreparedArchive>& archive,
                                         const Options& options,
                                         std::function<void(bool)> completed)
{
    if (!m_active || !archive) return;
    auto stagedProfiles = std::make_shared<QList<StagedFile>>();
    auto stagedMedia = std::make_shared<QList<StagedFile>>();
    const QString profileUserDir = options.profiles && m_profiles
        ? m_profiles->userProfilesPath() : QString();
    const QString profileDownloadedDir = options.profiles && m_profiles
        ? m_profiles->downloadedProfilesPath() : QString();
    const QString profileFallbackDir = options.profiles && m_profiles
        ? m_profiles->fallbackPath() : QString();
    const QString mediaDir = options.media && m_media ? m_media->personalMediaDirectory() : QString();

    for (const ArchiveFile& file : archive->profiles) {
        if (!options.profiles || !m_profiles) break;
        QString relative = file.name.mid(9);
        QString destinationDir = profileFallbackDir;
        if (relative.startsWith(QLatin1String("user/"))) {
            relative.remove(0, 5);
            destinationDir = profileUserDir;
        } else if (relative.startsWith(QLatin1String("downloaded/"))) {
            relative.remove(0, 11);
            destinationDir = profileDownloadedDir;
        }
        const QString basename = QFileInfo(relative).fileName();
        QString profileName = basename;
        if (profileName.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) profileName.chop(5);
        const QString destination = QDir(destinationDir).filePath(basename);
        if (options.merge && (QFileInfo::exists(destination) || m_profiles->profileExists(profileName))) {
            ++m_active->profilesSkipped;
            continue;
        }
        const QString temporary = QDir(destinationDir).filePath(
            QStringLiteral(".restore-%1-%2").arg(m_active->requestId, QUuid::createUuid().toString(QUuid::WithoutBraces)));
        stagedProfiles->append({file.path, temporary, destination, profileName});
    }
    for (const ArchiveFile& file : archive->media) {
        if (!options.media || !m_media) break;
        const QString filename = file.name.mid(6);
        const QJsonObject metadata = archive->mediaMetadata.value(file.path);
        const QString originalName = metadata.value(QStringLiteral("original_name")).toString(filename);
        if (options.merge && m_media->hasPersonalMediaWithName(originalName)) {
            ++m_active->mediaSkipped;
            continue;
        }
        const QString temporary = QDir(mediaDir).filePath(
            QStringLiteral(".restore-%1-%2.%3").arg(m_active->requestId,
                QUuid::createUuid().toString(QUuid::WithoutBraces), QFileInfo(filename).suffix()));
        stagedMedia->append({file.path, temporary, {}, originalName});
    }

    const QString requestId = m_active->requestId;
    auto cleanup = std::make_shared<StagingCleanup>();
    for (const StagedFile& file : *stagedProfiles) cleanup->paths.append(file.temporary);
    for (const StagedFile& file : *stagedMedia) cleanup->paths.append(file.temporary);
    const QPointer<FullBackupRestore> guard(this);
    QCoreApplication* const application = QCoreApplication::instance();
    if (!application) {
        if (completed) completed(false);
        return;
    }
    QThreadPool::globalInstance()->start(QRunnable::create(
        [guard, application, requestId, archive, options, stagedProfiles, stagedMedia, cleanup,
         profilePairs = *stagedProfiles, mediaPairs = *stagedMedia, completed]() mutable {
            bool success = true;
            for (const StagedFile& profile : profilePairs) {
                if (!success) break;
                QFile source(profile.source);
                if (!source.open(QIODevice::ReadOnly)) { success = false; break; }
                const QByteArray bytes = source.readAll();
                success = ProfileStorage::writeProfileContentToPath(profile.temporary, bytes);
            }
            // Media paths preserve the validated extension so catalog type detection is reliable.
            for (const StagedFile& mediaFile : mediaPairs) {
                if (!success) break;
                if (!QDir().mkpath(QFileInfo(mediaFile.temporary).absolutePath())
                    || !QFile::copy(mediaFile.source, mediaFile.temporary)) {
                    success = false;
                }
            }
            QMetaObject::invokeMethod(application, [guard, requestId, success, archive, options, cleanup,
                                              stagedProfiles, stagedMedia, completed] {
                if (guard) guard->nonShotDataStaged(requestId, success, archive, options,
                                                     stagedProfiles, stagedMedia, completed);
                Q_UNUSED(cleanup)
            }, Qt::QueuedConnection);
        }));
}

void FullBackupRestore::nonShotDataStaged(
    const QString& requestId, bool staged,
    const std::shared_ptr<PreparedArchive>& archive, const Options& options,
    const std::shared_ptr<QList<StagedFile>>& profiles,
    const std::shared_ptr<QList<StagedFile>>& media,
    std::function<void(bool)> completed)
{
    if (!m_active || m_active->requestId != requestId) return;
    if (!staged || (m_restoreAllowed && !m_restoreAllowed())) {
        for (const StagedFile& file : *profiles) QFile::remove(file.temporary);
        for (const StagedFile& file : *media) QFile::remove(file.temporary);
        if (completed) completed(false);
        return;
    }
    const QPointer<FullBackupRestore> guard(this);
    applySettingsData(archive, options);
    if (!guard || !m_active || m_active->requestId != requestId) return;
    if (options.profiles && m_profiles) {
        if (!options.merge) {
            QSet<QString> stagedPaths;
            for (const StagedFile& file : *profiles)
                stagedPaths.insert(QDir::cleanPath(QFileInfo(file.temporary).absoluteFilePath()));
            const QString fallbackRoot = m_profiles->fallbackPath();
            const QString externalRoot = m_profiles->externalProfilesPath();
            for (const QString& dir : {m_profiles->userProfilesPath(), m_profiles->downloadedProfilesPath(),
                                       fallbackRoot, externalRoot}) {
                if (dir.isEmpty()) continue;
                const bool root = dir == fallbackRoot || dir == externalRoot;
                for (const QFileInfo& file : QDir(dir).entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
                    if (root && (file.fileName().startsWith(QLatin1Char('_'))
                        || file.suffix().compare(QLatin1String("json"), Qt::CaseInsensitive) != 0))
                        continue;
                    if (!stagedPaths.contains(QDir::cleanPath(file.absoluteFilePath())))
                        QFile::remove(file.absoluteFilePath());
                }
            }
        }
        for (const StagedFile& file : *profiles) {
            if (!options.merge) QFile::remove(file.destination);
            if (QFile::rename(file.temporary, file.destination)) ++m_active->profilesImported;
            else {
                if (completed) completed(false);
                return;
            }
        }
    }
    if (options.media && m_media) {
        if (!options.merge) m_media->clearPersonalMedia();
        if (!guard || !m_active || m_active->requestId != requestId) return;
        for (const StagedFile& file : *media) {
            const QJsonObject metadata = archive->mediaMetadata.value(file.source);
            const bool adopted = m_media->adoptPreparedPersonalMedia(file.temporary, file.displayName,
                QDateTime::fromString(metadata.value(QStringLiteral("date")).toString(), Qt::ISODate),
                metadata.value(QStringLiteral("duration_s")).toInt(-1));
            if (!guard || !m_active || m_active->requestId != requestId) return;
            if (!adopted) {
                if (completed) completed(false);
                return;
            }
            ++m_active->mediaImported;
        }
    }
    if (completed) completed(true);
}

void FullBackupRestore::archivePrepared(const QString& requestId,
                                        const std::shared_ptr<PreparedArchive>& archive,
                                        const QString& parseError, int httpStatus)
{
    if (!m_active || m_active->requestId != requestId) return;
    if (!archive) {
        finishRestore(requestId, false,
                      {{QStringLiteral("error"), parseError}}, parseError, httpStatus);
        return;
    }
    m_active->archive = archive;
    const Options options = m_active->options;
    if (m_restoreAllowed && !m_restoreAllowed()) {
        const QString error = QStringLiteral("Restore is no longer allowed");
        finishRestore(requestId, false, {{QStringLiteral("error"), error}},
                      QStringLiteral("operationNotAllowed"), 409);
        return;
    }
    if (options.shots && !options.merge && archive->shots.path.isEmpty()) {
        const QString error = QStringLiteral("Backup has no shot database to replace the current history");
        finishRestore(requestId, false, {{QStringLiteral("error"), error}}, error, 400);
        return;
    }
    if (options.settings && !archive->conversations.path.isEmpty()) {
        m_active->conversations = archive->conversationsArray;
    }

    // In replace mode, keep every existing category intact until the shot database
    // has passed its transactional import. Merge mode retains the legacy HTTP
    // behavior of restoring non-shot categories even if shot import is refused.
    const bool hasShotDatabase = options.shots && !archive->shots.path.isEmpty();
    if (options.merge || !hasShotDatabase) {
        const QPointer<FullBackupRestore> guard(this);
        applyNonShotData(archive, options, [guard, requestId, hasShotDatabase](bool applied) {
            if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
            if (!applied) {
                const QString error = QStringLiteral("Restore is no longer allowed or files could not be staged");
                guard->finishRestore(requestId, false, {{QStringLiteral("error"), error}},
                                     QStringLiteral("restoreApplyFailed"), 409);
                return;
            }
            if (hasShotDatabase) {
                guard->importShots(requestId);
                return;
            }
            auto* self = guard.data();
            QVariantMap result{
                {QStringLiteral("success"), true},
                {QStringLiteral("settings"), self->m_active->settingsRestored},
                {QStringLiteral("shotsImported"), false},
                {QStringLiteral("profilesImported"), self->m_active->profilesImported},
                {QStringLiteral("profilesSkipped"), self->m_active->profilesSkipped},
                {QStringLiteral("mediaImported"), self->m_active->mediaImported},
                {QStringLiteral("mediaSkipped"), self->m_active->mediaSkipped},
                {QStringLiteral("aiConversationsImported"), 0}};
            if (self->m_active->options.settings
                && (!self->m_active->options.merge || !self->m_active->archive->conversations.path.isEmpty())
                && self->m_aiManager) {
                AppSettings settings;
                if (!self->m_active->options.merge) clearStoredConversations(settings);
                const auto tally = AIConversation::importConversationsStatic(
                    settings, self->m_active->conversations, nullptr, nullptr);
                result.insert(QStringLiteral("aiConversationsImported"), tally.conversationsImported);
                const QString note = AIConversation::importRefusalNote(tally, self->m_translations);
                if (!note.isEmpty()) result.insert(QStringLiteral("aiConversationsNote"), note);
                if (tally.conversationsImported > 0 || !self->m_active->options.merge) {
                    settings.sync();
                    self->m_aiManager->reloadConversations();
                    if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
                }
            }
            if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
            self->finishRestore(requestId, true, result);
        });
        return;
    }
    if (!m_storage) {
        finishRestore(requestId, false, {
            {QStringLiteral("success"), false},
            {QStringLiteral("settings"), m_active->settingsRestored},
            {QStringLiteral("shotsImported"), false},
            {QStringLiteral("error"), QStringLiteral(
                "The shot history in this backup could not be imported — shot storage is not "
                "available. Your existing shots were not changed.")},
            {QStringLiteral("profilesImported"), m_active->profilesImported},
            {QStringLiteral("profilesSkipped"), m_active->profilesSkipped},
            {QStringLiteral("mediaImported"), m_active->mediaImported},
            {QStringLiteral("mediaSkipped"), m_active->mediaSkipped},
            {QStringLiteral("aiConversationsImported"), 0}},
            QStringLiteral("storageUnavailable"), 500);
        return;
    }
    if (m_restoreAllowed && !m_restoreAllowed()) {
        const QString error = QStringLiteral("Restore is no longer allowed");
        finishRestore(requestId, false,
                      {{QStringLiteral("success"), false}, {QStringLiteral("error"), error}},
                      QStringLiteral("operationNotAllowed"), 409);
        return;
    }
    importShots(requestId);
}

void FullBackupRestore::importShots(const QString& requestId)
{
    if (!m_active || m_active->requestId != requestId || !m_active->archive) return;
    if (!m_storage) {
        const QString error = QStringLiteral("Shot storage is not available");
        finishRestore(requestId, false, {{QStringLiteral("error"), error}},
                      QStringLiteral("storageUnavailable"), 500);
        return;
    }
    if (m_restoreAllowed && !m_restoreAllowed()) {
        const QString error = QStringLiteral("Restore is no longer allowed");
        finishRestore(requestId, false, {{QStringLiteral("error"), error}},
                      QStringLiteral("operationNotAllowed"), 409);
        return;
    }
    const QString databasePath = m_storage->databasePath();
    const QString shotsPath = m_active->archive->shots.path;
    const bool merge = m_active->options.merge;
    const auto archive = m_active->archive;
    const QPointer<FullBackupRestore> guard(this);
    QCoreApplication* const application = QCoreApplication::instance();
    if (!application) {
        const QString error = QStringLiteral("Restore dispatcher is not available");
        finishRestore(requestId, false, {{QStringLiteral("error"), error}},
                      QStringLiteral("dispatcherUnavailable"), 500);
        return;
    }
    QThreadPool::globalInstance()->start(QRunnable::create(
        [guard, application, requestId, databasePath, shotsPath, merge, archive] {
            Q_UNUSED(archive)
            ShotHistoryStorage::ImportResult shotImport;
            const bool success = ShotHistoryStorage::importDatabaseStatic(
                databasePath, shotsPath, merge, &shotImport);
            QMetaObject::invokeMethod(application, [guard, requestId, success, shotImport, archive] {
                Q_UNUSED(archive)
                if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
                auto* self = guard.data();
                if (success && self->m_storage) self->m_storage->refreshTotalShots();
                if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
                self->m_active->shotsImported = success;
                auto finishShotImport = [guard, requestId, success, shotImport] {
                    if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
                    auto* restore = guard.data();
                    QVariantMap result{
                    {QStringLiteral("success"), success},
                    {QStringLiteral("settings"), restore->m_active->settingsRestored},
                    {QStringLiteral("shotsImported"), success},
                    {QStringLiteral("profilesImported"), restore->m_active->profilesImported},
                    {QStringLiteral("profilesSkipped"), restore->m_active->profilesSkipped},
                    {QStringLiteral("mediaImported"), restore->m_active->mediaImported},
                    {QStringLiteral("mediaSkipped"), restore->m_active->mediaSkipped},
                    {QStringLiteral("aiConversationsImported"), 0}};
                    QString conversationNote;
                    if (shotImport.refused()) {
                    conversationNote = AIConversation::importHeldBackNote(
                        restore->m_active->conversations.size(), restore->m_translations);
                    } else if ((success || restore->m_active->options.merge)
                        && restore->m_active->options.settings
                        && (!restore->m_active->options.merge || !restore->m_active->archive->conversations.path.isEmpty())
                        && restore->m_aiManager) {
                    AppSettings settings;
                    if (!restore->m_active->options.merge) clearStoredConversations(settings);
                    const AIConversation::ImportTally tally = AIConversation::importConversationsStatic(
                        settings, restore->m_active->conversations,
                        success ? shotImport.idMapOrNull() : nullptr,
                        success ? shotImport.equipmentIdMapOrNull() : nullptr);
                    result.insert(QStringLiteral("aiConversationsImported"), tally.conversationsImported);
                    if (tally.conversationsImported > 0 || !restore->m_active->options.merge) {
                        settings.sync();
                        restore->m_aiManager->reloadConversations();
                        if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
                    }
                    conversationNote = AIConversation::importRefusalNote(tally, restore->m_translations);
                    }
                    if (!conversationNote.isEmpty())
                        result.insert(QStringLiteral("aiConversationsNote"), conversationNote);
                    if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
                    QString error;
                    if (!shotImport.integrityFailure.isEmpty()) {
                    result.insert(QStringLiteral("shotsRefused"), shotImport.integrityFailure);
                    error = QStringLiteral("shotImportRefused");
                    } else if (!success) {
                    result.insert(QStringLiteral("error"), QStringLiteral(
                        "The shot history in this backup could not be imported. "
                        "Your existing shots were not changed."));
                    error = QStringLiteral("shotImportFailed");
                    }
                    restore->finishRestore(requestId, success, result, error);
                };
                if (success && !self->m_active->options.merge) {
                    self->applyNonShotData(self->m_active->archive, self->m_active->options,
                        [guard, requestId, finishShotImport](bool applied) {
                            if (!guard || !guard->m_active || guard->m_active->requestId != requestId) return;
                            if (applied) finishShotImport();
                            else guard->finishRestore(requestId, false,
                                {{QStringLiteral("error"), QStringLiteral("Restore is no longer allowed or files could not be staged")}},
                                QStringLiteral("restoreApplyFailed"), 409);
                        });
                } else {
                    finishShotImport();
                }
            }, Qt::QueuedConnection);
        }));
}

void FullBackupRestore::finishRestore(const QString& requestId, bool success,
                                      QVariantMap result, QString reason, int httpStatus)
{
    if (!m_active || m_active->requestId != requestId) return;
    const bool profilesChanged = m_active->options.profiles
        && (m_active->profilesImported > 0 || !m_active->options.merge);
    const bool mediaChanged = m_active->options.media
        && (m_active->mediaImported > 0 || !m_active->options.merge);
    m_active.reset();
    const QPointer<FullBackupRestore> guard(this);
    emit busyChanged();
    if (!guard) return;
    if (profilesChanged) emit profilesRestored();
    if (!guard) return;
    if (mediaChanged) emit mediaRestored();
    if (!guard) return;
    emit finished(requestId, success, result, reason, httpStatus);
}
