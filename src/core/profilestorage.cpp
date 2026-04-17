#include "profilestorage.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QSettings>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#endif

ProfileStorage::ProfileStorage(QObject* parent)
    : QObject(parent)
{
    // Check if user previously skipped setup
    QSettings settings;
    m_setupSkipped = settings.value("storage/setupSkipped", false).toBool();

    qDebug() << "[ProfileStorage] Initialized. isConfigured:" << isConfigured()
             << "needsSetup:" << needsSetup()
             << "setupSkipped:" << m_setupSkipped;

    // Migrate any existing profiles to external storage if permission is granted
    if (isConfigured()) {
        migrateProfilesToExternal();
    }
}

bool ProfileStorage::isConfigured() const {
#ifdef Q_OS_ANDROID
    // Check if we have the MANAGE_EXTERNAL_STORAGE permission (Android 11+)
    // or if we're on Android 10 or below (where WRITE_EXTERNAL_STORAGE is enough)
    return QJniObject::callStaticMethod<jboolean>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "hasStoragePermission",
        "()Z");
#else
    return true; // Desktop always uses regular files
#endif
}

bool ProfileStorage::needsSetup() const {
#ifdef Q_OS_ANDROID
    if (m_setupSkipped) {
        return false; // User already skipped
    }
    // Need setup on Android 6+ if permission not granted
    bool needsPermission = QJniObject::callStaticMethod<jboolean>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "needsStoragePermission",
        "()Z");
    if (!needsPermission) {
        return false; // Android 5 or below, permission granted at install
    }
    return !isConfigured();
#else
    return false; // Desktop doesn't need setup
#endif
}

void ProfileStorage::selectFolder() {
#ifdef Q_OS_ANDROID
    // Open settings to grant permission
    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "requestStoragePermission",
        "()V");
    qDebug() << "[ProfileStorage] Opened storage permission settings";
#else
    emit folderSelected(true);
    emit configuredChanged();
#endif
}

void ProfileStorage::skipSetup() {
    m_setupSkipped = true;
    QSettings settings;
    settings.setValue("storage/setupSkipped", true);
    emit configuredChanged();
    qDebug() << "[ProfileStorage] Setup skipped by user";
}

QString ProfileStorage::externalProfilesPath() const {
#ifdef Q_OS_ANDROID
    QJniObject path = QJniObject::callStaticObjectMethod(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "getProfilesPath",
        "()Ljava/lang/String;");
    if (path.isValid()) {
        return path.toString();
    }
#endif
    // Fallback for desktop or if JNI fails
    return QString();
}

QString ProfileStorage::userProfilesPath() const {
    QString basePath = isConfigured() ? externalProfilesPath() : fallbackPath();
    if (basePath.isEmpty()) {
        basePath = fallbackPath();
    }
    QString path = basePath + "/user";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QString ProfileStorage::downloadedProfilesPath() const {
    QString basePath = isConfigured() ? externalProfilesPath() : fallbackPath();
    if (basePath.isEmpty()) {
        basePath = fallbackPath();
    }
    QString path = basePath + "/downloaded";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QString ProfileStorage::userHistoryPath() const {
    QString basePath = isConfigured() ? externalProfilesPath() : fallbackPath();
    if (basePath.isEmpty()) {
        basePath = fallbackPath();
    }
    QString path = basePath + "/history";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QStringList ProfileStorage::listProfiles() const {
    QStringList profiles;
    QStringList filters;
    filters << "*.json";

    // Check external storage (Documents/Decenza) if configured
    if (isConfigured()) {
        QString extPath = externalProfilesPath();
        if (!extPath.isEmpty()) {
            QDir extDir(extPath);
            if (extDir.exists()) {
                QStringList files = extDir.entryList(filters, QDir::Files);
                for (const QString& file : files) {
                    if (!file.startsWith("_")) {
                        QString name = file.left(file.length() - 5);
                        if (!profiles.contains(name)) {
                            profiles.append(name);
                        }
                    }
                }
            }
        }
    }

    // Also check fallback path
    QDir fallbackDir(fallbackPath());
    if (fallbackDir.exists()) {
        QStringList files = fallbackDir.entryList(filters, QDir::Files);
        for (const QString& file : files) {
            if (!file.startsWith("_")) {
                QString name = file.left(file.length() - 5);
                if (!profiles.contains(name)) {
                    profiles.append(name);
                }
            }
        }
    }

    return profiles;
}

QString ProfileStorage::readProfile(const QString& filename) const {
    // Try external storage first
    if (isConfigured()) {
        QString extPath = externalProfilesPath();
        if (!extPath.isEmpty()) {
            QString path = extPath + "/" + filename + ".json";
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QString::fromUtf8(file.readAll());
                qDebug() << "[ProfileStorage] Read from external:" << path;
                return content;
            }
        }
    }

    // Try fallback path
    QString path = fallbackPath() + "/" + filename + ".json";
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll());
    }

    return QString();
}

bool ProfileStorage::writeProfile(const QString& filename, const QString& content) {
    // Write to external storage if configured
    if (isConfigured()) {
        QString extPath = externalProfilesPath();
        if (!extPath.isEmpty()) {
            QDir dir(extPath);
            if (!dir.exists()) {
                dir.mkpath(".");
            }

            QString path = extPath + "/" + filename + ".json";
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(content.toUtf8());
                file.close();
                qDebug() << "[ProfileStorage] Wrote to external:" << path;
                return true;
            } else {
                qWarning() << "[ProfileStorage] Failed to write to external:" << path;
            }
        }
    }

    // Fall back to app-internal storage
    QString path = fallbackPath() + "/" + filename + ".json";
    QDir dir(fallbackPath());
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(content.toUtf8());
        file.close();
        qDebug() << "[ProfileStorage] Wrote to fallback:" << path;
        return true;
    }

    qWarning() << "[ProfileStorage] Failed to write profile:" << filename;
    return false;
}

bool ProfileStorage::deleteProfile(const QString& filename) {
    bool deleted = false;

    // Try external storage
    if (isConfigured()) {
        QString extPath = externalProfilesPath();
        if (!extPath.isEmpty()) {
            QString path = extPath + "/" + filename + ".json";
            if (QFile::exists(path) && QFile::remove(path)) {
                qDebug() << "[ProfileStorage] Deleted from external:" << path;
                deleted = true;
            }
        }
    }

    // Also try fallback path
    QString path = fallbackPath() + "/" + filename + ".json";
    if (QFile::exists(path) && QFile::remove(path)) {
        qDebug() << "[ProfileStorage] Deleted from fallback:" << path;
        deleted = true;
    }

    return deleted;
}

bool ProfileStorage::profileExists(const QString& filename) const {
    // Check external storage
    if (isConfigured()) {
        QString extPath = externalProfilesPath();
        if (!extPath.isEmpty()) {
            QString path = extPath + "/" + filename + ".json";
            if (QFile::exists(path)) {
                return true;
            }
        }
    }

    // Check fallback path
    QString path = fallbackPath() + "/" + filename + ".json";
    return QFile::exists(path);
}

QString ProfileStorage::fallbackPath() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    path += "/profiles";

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return path;
}

void ProfileStorage::checkPermissionAndNotify() {
#ifdef Q_OS_ANDROID
    bool configured = isConfigured();
    qDebug() << "[ProfileStorage] Permission check - configured:" << configured;

    // If permission was just granted, migrate existing profiles
    if (configured) {
        migrateProfilesToExternal();
    }

    emit configuredChanged();
    emit folderSelected(configured);
#endif
}

void ProfileStorage::migrateProfilesToExternal() {
    if (!isConfigured()) {
        qDebug() << "[ProfileStorage] Cannot migrate - not configured";
        return;
    }

    QString extPath = externalProfilesPath();
    if (extPath.isEmpty()) {
        qDebug() << "[ProfileStorage] Cannot migrate - no external path";
        return;
    }

    // Create external directory if needed
    QDir extDir(extPath);
    if (!extDir.exists()) {
        extDir.mkpath(".");
    }

    // Find profiles in fallback (internal) storage
    QDir fallbackDir(fallbackPath());
    if (!fallbackDir.exists()) {
        qDebug() << "[ProfileStorage] No fallback profiles to migrate";
        return;
    }

    QStringList filters;
    filters << "*.json";
    QStringList files = fallbackDir.entryList(filters, QDir::Files);

    int migrated = 0;
    for (const QString& file : files) {
        if (file.startsWith("_")) {
            continue;  // Skip temp files like _current.json
        }

        QString srcPath = fallbackDir.filePath(file);
        QString destPath = extDir.filePath(file);

        // Only migrate if not already in external storage
        if (QFile::exists(destPath)) {
            qDebug() << "[ProfileStorage] Profile already in external, skipping:" << file;
            continue;
        }

        // Copy to external storage
        if (QFile::copy(srcPath, destPath)) {
            qDebug() << "[ProfileStorage] Migrated profile:" << file;
            // Remove from internal storage after successful copy
            QFile::remove(srcPath);
            migrated++;
        } else {
            qWarning() << "[ProfileStorage] Failed to migrate:" << file;
        }
    }

    qDebug() << "[ProfileStorage] Migration complete. Migrated" << migrated << "profiles";
}
