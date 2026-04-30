#include "shothistorystorage_internal.h"

#include "ai/shotsummarizer.h"
#include "profile/profile.h"

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
    return info;
}

AnalysisInputs prepareAnalysisInputs(const QString& profileKbId,
                                     const QString& profileJson)
{
    AnalysisInputs inputs;
    inputs.analysisFlags = ShotSummarizer::getAnalysisFlags(profileKbId);
    const ProfileFrameInfo frameInfo = profileFrameInfoFromJson(profileJson);
    inputs.firstFrameSeconds = frameInfo.firstFrameSeconds;
    inputs.frameCount = frameInfo.frameCount;
    return inputs;
}

bool use12h()
{
    static const bool val = QLocale::system().timeFormat(QLocale::ShortFormat).contains("AP", Qt::CaseInsensitive);
    return val;
}

} // namespace decenza::storage::detail
