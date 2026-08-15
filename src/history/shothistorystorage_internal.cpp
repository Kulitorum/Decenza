#include "shothistorystorage_internal.h"

#include "ai/shotsummarizer.h"
#include "profile/profile.h"
#include "ai/profileshapeindex.h"

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
    info.profile = profile;
    return info;
}

AnalysisInputs prepareAnalysisInputs(const QString& profileKbId,
                                     const QString& profileJson)
{
    AnalysisInputs inputs;

    const ProfileFrameInfo frameInfo = profileFrameInfoFromJson(profileJson);
    inputs.firstFrameSeconds = frameInfo.firstFrameSeconds;
    inputs.frameCount = frameInfo.frameCount;

    // Resolve the KB from the shot's OWN stored profile, not from the
    // persisted id. Two reasons, one old and one new:
    //
    //  - Old (#1160/#1175): a shot saved before a KB reorganization carries a
    //    stale profileKbId, so re-resolving now restores the retroactive
    //    recompute promise.
    //  - New (resolve-profile-kb-by-shape): a profile whose TITLE resolves to
    //    nothing may still be a re-tuned copy of a documented one, recognisable
    //    by its frame structure. Its persisted id is empty and always will be,
    //    so anything reading the id sees "no context" for a profile we can in
    //    fact say a great deal about.
    //
    // Title steps run first inside resolveProfileKb and always win, so a
    // title-resolvable profile is unaffected by any of this.
    //
    // Gated on the profile having actually parsed, and this gate is
    // load-bearing rather than defensive.
    //
    // `Profile` default-constructs with the title "Default" — not an empty
    // string — and "Default" is a REAL shipped profile (resources/profiles/
    // default.json) with a real KB entry carrying flow_trend_ok and a UGS of
    // 0.75. So a shot whose profileJson is absent or unparseable (older rows,
    // imports, a failed parse) would resolve to that entry and silently
    // inherit its suppression: a genuine flow-trend finding on an unrelated
    // shot, discarded because a default-constructed object happened to share
    // a name with a documented profile.
    //
    // That is precisely the failure this change exists to prevent — wrong
    // facts reaching a shot — arriving through the change's own code. A
    // profile with no frames has no identity to resolve and no shape to match,
    // so it resolves to nothing.
    const KbResolution resolution = frameInfo.profile.steps().isEmpty()
        ? KbResolution{}
        : resolveProfileKb(frameInfo.profile);

    // Same observability as before for the stale-id fallback: a non-empty
    // title that fails to re-resolve while a stale stored id survives means we
    // fall back to the STALE id, silently reinstating the bug D14a targets.
    if (resolution.isEmpty() && !frameInfo.profileTitle.isEmpty()
        && !profileKbId.isEmpty()) {
        qDebug() << "prepareAnalysisInputs: fresh re-resolve missed for title="
                 << frameInfo.profileTitle
                 << "— falling back to stored kbId=" << profileKbId;
    }

    // Fall back to the stored id when nothing re-resolves, preserving the
    // pre-existing behaviour for rows whose profileJson is absent or
    // unparseable (older shots, imports).
    const QStringList ids = resolution.isEmpty()
        ? (profileKbId.isEmpty() ? QStringList{} : QStringList{ profileKbId })
        : resolution.ids;

    // Per-fact transfer rules live in the accessors, never here — see the
    // block above ShotSummarizer::getAnalysisFlags(QStringList).
    inputs.analysisFlags = ShotSummarizer::getAnalysisFlags(ids);
    inputs.expertBand    = ShotSummarizer::expertBandForKbIds(ids);

    // Arm 1's gate asks "is this flow goal a real target or a safety limiter" —
    // a structural question, which a shape match answers as well as a title
    // match does. Any non-empty candidate set means we have that context.
    inputs.profileKbResolved = !ids.isEmpty();

    // Identity is a stricter claim than analysis: it needs exactly one
    // candidate. An ambiguous shape still suppresses false positives; it just
    // does not get to say WHICH profile it came from.
    if (resolution.hasIdentity()) {
        inputs.identityKbId = resolution.ids.first();
        inputs.identityFromShape = (resolution.origin == KbResolution::Origin::Shape);
    } else if (resolution.isEmpty() && ids.size() == 1) {
        inputs.identityKbId = ids.first();   // stored-id fallback path
    }
    return inputs;
}

bool use12h()
{
    static const bool val = QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive);
    return val;
}

} // namespace decenza::storage::detail
