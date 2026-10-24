#include "profileeditordraft.h"

#include "profileeditmodel.h"

#include <utility>

ProfileEditorDraft::ProfileEditorDraft(QObject* parent) : QObject(parent) {}

QVariantMap ProfileEditorDraft::profile() const
{
    return m_hasProfile ? ProfileEditModel::toVariantMap(m_profile) : QVariantMap{};
}

bool ProfileEditorDraft::load(const QVariantMap& value, const QString& filename,
                              const QString& revision)
{
    bool ok = false;
    Profile parsed = ProfileEditModel::fromVariantMap(value, &ok);
    if (!ok || value.value(QStringLiteral("title")).toString().trimmed().isEmpty())
        return false;
    m_sourceFilename = filename;
    m_sourceRevision = revision;
    emit sourceChanged();
    const bool wasModified = m_modified;
    m_profile = std::move(parsed);
    m_hasProfile = true;
    m_modified = false;
    emit profileChanged();
    if (wasModified) emit modifiedChanged();
    return true;
}

bool ProfileEditorDraft::createNewProfile(const QString& editorType, const QString& title)
{
    if (title.trimmed().isEmpty()) return false;
    m_sourceFilename.clear();
    m_sourceRevision.clear();
    emit sourceChanged();
    Profile profile = ProfileEditModel::newEditorProfile(editorType, title.trimmed());
    const bool wasModified = m_modified;
    m_profile = std::move(profile);
    m_hasProfile = true;
    m_modified = true;
    emit profileChanged();
    if (!wasModified) emit modifiedChanged();
    return true;
}

bool ProfileEditorDraft::paramsBased() const
{
    if (!m_hasProfile) return false;
    const QString editorType = m_profile.editorType();
    return editorType == QLatin1String("dflow") || editorType == QLatin1String("aflow")
        || m_profile.profileType() == QLatin1String("settings_2a")
        || m_profile.profileType() == QLatin1String("settings_2b");
}

bool ProfileEditorDraft::setProfile(const QVariantMap& value)
{
    if (!m_hasProfile) return false;
    bool ok = false;
    Profile parsed = ProfileEditModel::fromVariantMap(value, &ok);
    if (!ok) return false;
    assign(std::move(parsed));
    return true;
}

bool ProfileEditorDraft::applyParams(const QVariantMap& params)
{
    if (!m_hasProfile) return false;
    Profile edited = m_profile;
    ProfileEditModel::applyParams(edited, params);
    assign(std::move(edited));
    return true;
}

bool ProfileEditorDraft::setRecommendedDose(double doseG)
{
    if (!m_hasProfile) return false;
    Profile edited = m_profile;
    if (doseG <= 0.0) {
        edited.setHasRecommendedDose(false);
    } else {
        edited.setRecommendedDose(qMin(doseG, 100.0));
        edited.setHasRecommendedDose(true);
    }
    assign(std::move(edited));
    return true;
}

bool ProfileEditorDraft::addFrame(int afterIndex)
{
    if (!m_hasProfile) return false;
    Profile edited = m_profile;
    if (!ProfileEditModel::addFrame(edited, afterIndex)) return false;
    assign(std::move(edited));
    return true;
}

bool ProfileEditorDraft::deleteFrame(int index)
{
    if (!m_hasProfile) return false;
    Profile edited = m_profile;
    if (!ProfileEditModel::deleteFrame(edited, index)) return false;
    assign(std::move(edited));
    return true;
}

bool ProfileEditorDraft::moveFrameUp(int index)
{
    if (!m_hasProfile) return false;
    Profile edited = m_profile;
    if (!ProfileEditModel::moveFrame(edited, index, -1)) return false;
    assign(std::move(edited));
    return true;
}

bool ProfileEditorDraft::moveFrameDown(int index)
{
    if (!m_hasProfile) return false;
    Profile edited = m_profile;
    if (!ProfileEditModel::moveFrame(edited, index, 1)) return false;
    assign(std::move(edited));
    return true;
}

bool ProfileEditorDraft::duplicateFrame(int index)
{
    if (!m_hasProfile) return false;
    Profile edited = m_profile;
    if (!ProfileEditModel::duplicateFrame(edited, index)) return false;
    assign(std::move(edited));
    return true;
}

bool ProfileEditorDraft::setFrameProperty(int index, const QString& property,
                                          const QVariant& value)
{
    if (!m_hasProfile) return false;
    Profile edited = m_profile;
    if (!ProfileEditModel::setFrameProperty(edited, index, property, value)) return false;
    assign(std::move(edited));
    return true;
}

QVariantMap ProfileEditorDraft::getFrameAt(int index) const
{
    return m_hasProfile ? ProfileEditModel::frameAt(m_profile, index) : QVariantMap{};
}

int ProfileEditorDraft::frameCount() const
{
    return m_hasProfile ? static_cast<int>(m_profile.steps().size()) : 0;
}

QVariantMap ProfileEditorDraft::getOrConvertProfileParams() const
{
    return m_hasProfile ? ProfileEditModel::paramsForProfile(m_profile) : QVariantMap{};
}

void ProfileEditorDraft::markClean()
{
    if (!m_modified) return;
    m_modified = false;
    emit modifiedChanged();
}

void ProfileEditorDraft::assign(Profile profile)
{
    const bool changed = !m_hasProfile || m_profile.toJsonString() != profile.toJsonString();
    if (!changed) return;
    m_profile = std::move(profile);
    m_hasProfile = true;
    if (!m_modified) {
        m_modified = true;
        emit modifiedChanged();
    }
    emit profileChanged();
}
