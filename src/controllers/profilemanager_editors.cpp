#include "profilemanager.h"
#include "../profile/profileeditmodel.h"
#include "../core/profilestorage.h"
#include "../core/settings.h"
#include "../core/settings_app.h"
#include "../core/settings_brew.h"
#include "../machine/machinestate.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
bool safeFilename(const QString& filename)
{
    if (filename.isEmpty() || filename.size() > 256 || filename.startsWith(QLatin1Char('_'))
        || filename.contains(QLatin1Char('/')) || filename.contains(QLatin1Char('\\'))
        || filename == QLatin1String(".") || filename == QLatin1String("..")) return false;
    for (QChar ch : filename) if (ch.isNull() || ch.category() == QChar::Other_Control) return false;
    return true;
}
}

bool ProfileManager::persistUserProfile(const QString& filename, const Profile& profile)
{
    if (m_profileStorage && m_profileStorage->isConfigured()
        && m_profileStorage->writeProfile(filename, profile.toJsonString())) return true;
    return profile.saveToFile(userProfilesPath() + QLatin1Char('/') + filename + QStringLiteral(".json"));
}

QVariantMap ProfileManager::editorProfileDetail(const QString& sourceFilename) const
{
    if (!sourceFilename.isEmpty() && !safeFilename(sourceFilename)) return {};
    QVariantMap detail = sourceFilename.isEmpty() ? getCurrentProfile() : getProfileByFilename(sourceFilename);
    if (detail.isEmpty()) return {};
    // A current-profile draft is bound to both its active document and the file
    // it would overwrite. A named draft is independent of unrelated catalogue edits.
    QVariantMap version{{QStringLiteral("document"), detail},
                        {QStringLiteral("source"), sourceFilename}};
    if (sourceFilename.isEmpty()) {
        version.insert(QStringLiteral("base"), m_baseProfileName);
        version.insert(QStringLiteral("stored"), m_baseProfileName.isEmpty()
            ? QVariantMap{} : getProfileByFilename(m_baseProfileName));
    }
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(version)).toJson(QJsonDocument::Compact);
    detail.insert(QStringLiteral("_sourceRevision"), QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    detail.insert(QStringLiteral("_sourceFilename"), sourceFilename.isEmpty() ? m_baseProfileName : sourceFilename);
    return detail;
}

QVariantMap ProfileManager::commitEditorDraft(const QString& sourceFilename, const QVariantMap& draft,
                                             const QString& expectedRevision, const QString& saveAsTitle,
                                             bool tryOnly, QString* reason, const QString& overwriteRevision)
{
    auto fail = [reason](const char* error) -> QVariantMap {
        if (reason) *reason = QString::fromLatin1(error);
        return {};
    };
    if (reason) reason->clear();
    if (m_machineState) {
        const auto phase = m_machineState->phase();
        if (phase != MachineState::Phase::Disconnected && phase != MachineState::Phase::Sleep
            && phase != MachineState::Phase::Idle && phase != MachineState::Phase::Heating
            && phase != MachineState::Phase::Ready) return fail("machineBusy");
    }
    const bool newDraft = sourceFilename.isEmpty() && expectedRevision.isEmpty();
    const QVariantMap source = newDraft ? QVariantMap{} : editorProfileDetail(sourceFilename);
    if (!newDraft && source.isEmpty()) return fail("profileNotFound");
    if (!newDraft && (expectedRevision.isEmpty()
        || source.value(QStringLiteral("_sourceRevision")).toString() != expectedRevision))
        return fail("conflict");
    const QString originalFilename = newDraft ? QString()
        : source.value(QStringLiteral("_sourceFilename")).toString();
    bool parsed = false;
    Profile candidate = ProfileEditModel::fromVariantMap(draft, &parsed);
    if (!parsed || !candidate.isValid() || candidate.title().trimmed().isEmpty()
        || candidate.title().size() > 512) return fail("invalidProfile");
    const bool saveAs = !saveAsTitle.isEmpty();
    if (newDraft && !tryOnly && !saveAs) return fail("saveAsRequired");
    if (!tryOnly && !saveAs && source.value(QStringLiteral("read_only")).toInt() == 1)
        return fail("readOnly");
    QString target = originalFilename;
    if (saveAs) {
        if (saveAsTitle.trimmed().isEmpty() || saveAsTitle.size() > 512) return fail("invalidTitle");
        target = titleToFilename(saveAsTitle);
        if (!safeFilename(target)) return fail("invalidTitle");
        if (isBuiltInFilename(target)) return fail("builtInFilename");
        const bool exists = profileExists(target)
            || (m_profileStorage && m_profileStorage->isConfigured() && m_profileStorage->profileExists(target))
            || QFile::exists(userProfilesPath() + QLatin1Char('/') + target + QStringLiteral(".json"))
            || QFile::exists(downloadedProfilesPath() + QLatin1Char('/') + target + QStringLiteral(".json"));
        if (exists) {
            const QVariantMap destination = editorProfileDetail(target);
            if (destination.value(QStringLiteral("read_only")).toInt() == 1) return fail("readOnly");
            const QString revision = destination.value(QStringLiteral("_sourceRevision")).toString();
            if (overwriteRevision.isEmpty()) {
                if (reason) *reason = QStringLiteral("alreadyExists");
                return {{QStringLiteral("filename"), target},
                        {QStringLiteral("overwriteRevision"), revision}};
            }
            if (revision.isEmpty() || revision != overwriteRevision) return fail("conflict");
        } else if (!overwriteRevision.isEmpty()) return fail("conflict");
        candidate.setTitle(saveAsTitle);
        candidate.setReadOnly(0);
    }
    if (!tryOnly && !safeFilename(target)) return fail("saveAsRequired");
    // Persist before touching the active profile or its overrides. Failed writes
    // leave both the editor's draft and the machine's current profile intact.
    if (tryOnly) {
        if (!candidate.saveToFile(profilesPath() + QStringLiteral("/_current.json")))
            return fail("saveFailed");
    } else if (!persistUserProfile(target, candidate)) return fail("saveFailed");

    const QString oldFilename = m_baseProfileName;
    if (oldFilename != target && !oldFilename.isEmpty()) m_previousProfileName = oldFilename;
    setCurrentProfile(std::move(candidate), QStringLiteral("commitEditorDraft"));
    m_baseProfileName = tryOnly ? originalFilename : target;
    if (tryOnly) {
        if (!m_profileModified) { m_profileModified = true; emit profileModifiedChanged(); }
    } else {
        markProfileClean();
        QFile::remove(profilesPath() + QStringLiteral("/_current.json"));
    }
    if (m_settings) {
        m_settings->brew()->clearTemperatureOverride();
        m_settings->brew()->setBrewYieldOverride(0);
        // Startup restores _current.json against this persisted base, including
        // a Try of a named profile that was not the previously active profile.
        m_settings->app()->setCurrentProfile(m_baseProfileName);
        if (!tryOnly) {
            if (saveAs && originalFilename != target)
                m_settings->app()->addFavoriteProfile(m_currentProfile.title(), target);
            else if (!originalFilename.isEmpty())
                m_settings->app()->updateFavoriteProfile(originalFilename, target, m_currentProfile.title());
            m_settings->app()->setSelectedFavoriteProfile(m_settings->app()->findFavoriteIndexByFilename(target));
        }
    }
    syncMachineStateToCurrentProfile();
    if (!tryOnly) refreshProfiles();
    if (m_currentProfile.mode() == Profile::Mode::FrameBased) uploadCurrentProfile();
    emit currentProfileChanged();
    const QString resultSource = tryOnly ? QString() : target;
    const QVariantMap detail = editorProfileDetail(resultSource);
    return {{QStringLiteral("filename"), resultSource},
            {QStringLiteral("sourceRevision"), detail.value(QStringLiteral("_sourceRevision"))},
            {QStringLiteral("detail"), detail}};
}
