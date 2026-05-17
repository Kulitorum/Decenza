#include "shothistorystorage_internal.h"

#include "ai/shotsummarizer.h"
#include "profile/profile.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>

namespace decenza::storage::detail {

ProfileFrameInfo profileFrameInfoFromJson(const QString& profileJson)
{
    if (profileJson.isEmpty())
        return {};

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(profileJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    const Profile profile = Profile::fromJson(doc);
    ProfileFrameInfo info;
    info.frameCount = static_cast<int>(profile.steps().size());
    if (!profile.steps().isEmpty())
        info.firstFrameSeconds = profile.steps().first().seconds;
    info.profileTitle = profile.title();
    info.editorType = profile.editorType();
    return info;
}

AnalysisInputs prepareAnalysisInputs(const QString& profileKbId,
                                     const QString& profileJson)
{
    AnalysisInputs inputs;
    // analysisFlags / UGS keep using the persisted profileKbId — unchanged
    // pre-existing behavior, out of this change's scope.
    inputs.analysisFlags = ShotSummarizer::getAnalysisFlags(profileKbId);

    const ProfileFrameInfo frameInfo = profileFrameInfoFromJson(profileJson);
    inputs.firstFrameSeconds = frameInfo.firstFrameSeconds;
    inputs.frameCount = frameInfo.frameCount;

    // Expert band: re-resolve canonical identity from the CURRENT KB by
    // title+editorType — mirrors the save-time computeProfileKbId() call
    // but run now, so a shot saved before a KB reorganization
    // (#1160/#1175) whose persisted profileKbId is stale (e.g. a
    // "D-Flow / Q" shot stored as "d-flow / default") still resolves to
    // the correct band. Restores the D7/D14 retroactive-recompute
    // promise. Falls back to the stored kbId if the title doesn't
    // resolve; an absent band stays a strict no-op as before.
    const QString freshKbId = ShotSummarizer::computeProfileKbId(
        frameInfo.profileTitle, frameInfo.editorType);
    // Observability for the silent-degrade case: a non-empty title that
    // fails to re-resolve while a stale stored kbId survives means we fall
    // back to the *stale* id — silently reinstating the very bug D14a
    // targets. That path must be visible (the A6 shadow-validation run is
    // how a stale-band slip would otherwise go undetected).
    if (freshKbId.isEmpty() && !frameInfo.profileTitle.isEmpty()
        && !profileKbId.isEmpty()) {
        qDebug() << "prepareAnalysisInputs: expert-band fresh re-resolve "
                    "missed for title=" << frameInfo.profileTitle
                 << "— falling back to stored kbId=" << profileKbId;
    }
    inputs.expertBand = ShotSummarizer::expertBandForKbId(
        !freshKbId.isEmpty() ? freshKbId : profileKbId);
    return inputs;
}

bool use12h()
{
    static const bool val = QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive);
    return val;
}

} // namespace decenza::storage::detail
