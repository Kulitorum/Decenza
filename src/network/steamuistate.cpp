#include "steamuistate.h"

#include <QJsonValue>

#include <cmath>

namespace {
bool hasExactKeys(const QJsonObject& object, std::initializer_list<const char*> keys)
{
    if (object.size() != static_cast<int>(keys.size()))
        return false;
    for (const char* key : keys) {
        if (!object.contains(QLatin1String(key)))
            return false;
    }
    return true;
}

bool jsonInt(const QJsonValue& value, int minimum, int maximum, int* output)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < minimum || number > maximum)
        return false;
    *output = static_cast<int>(number);
    return true;
}

bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}
} // namespace

QJsonObject SteamUiState::toJson() const
{
    return {
        {QStringLiteral("viewMode"), viewMode},
        {QStringLiteral("coach"), QJsonObject{
            {QStringLiteral("visualEnabled"), coachVisualEnabled},
            {QStringLiteral("audioEnabled"), coachAudioEnabled},
            {QStringLiteral("cueText"), coachCueText},
            {QStringLiteral("cueSeverity"), coachCueSeverity},
            {QStringLiteral("cueActive"), coachCueActive},
            {QStringLiteral("durationMilkDerived"), durationMilkDerived}
        }},
        {QStringLiteral("accessibility"), QJsonObject{
            {QStringLiteral("extractionAnnouncementsEnabled"), extractionAnnouncementsEnabled},
            {QStringLiteral("extractionAnnouncementInterval"), extractionAnnouncementInterval},
            {QStringLiteral("extractionAnnouncementMode"), extractionAnnouncementMode}
        }}
    };
}

bool SteamUiState::parse(const QJsonObject& object, SteamUiState* state, QString* error)
{
    if (!state)
        return fail(error, QStringLiteral("Steam UI state output is missing"));
    if (!hasExactKeys(object, {"viewMode", "coach", "accessibility"}))
        return fail(error, QStringLiteral("Steam UI state has missing or unknown fields"));

    const QJsonValue viewModeValue = object.value(QStringLiteral("viewMode"));
    const QJsonValue coachValue = object.value(QStringLiteral("coach"));
    const QJsonValue accessibilityValue = object.value(QStringLiteral("accessibility"));
    if (!viewModeValue.isString() || !coachValue.isObject() || !accessibilityValue.isObject())
        return fail(error, QStringLiteral("Steam UI state fields have invalid types"));
    const QString viewMode = viewModeValue.toString();
    if (viewMode != QStringLiteral("timer") && viewMode != QStringLiteral("chart"))
        return fail(error, QStringLiteral("Steam view mode is invalid"));

    const QJsonObject coach = coachValue.toObject();
    if (!hasExactKeys(coach, {"visualEnabled", "audioEnabled", "cueText", "cueSeverity",
                              "cueActive", "durationMilkDerived"}))
        return fail(error, QStringLiteral("Steam coach state has missing or unknown fields"));
    const QJsonValue visualValue = coach.value(QStringLiteral("visualEnabled"));
    const QJsonValue audioValue = coach.value(QStringLiteral("audioEnabled"));
    const QJsonValue cueTextValue = coach.value(QStringLiteral("cueText"));
    const QJsonValue cueSeverityValue = coach.value(QStringLiteral("cueSeverity"));
    const QJsonValue cueActiveValue = coach.value(QStringLiteral("cueActive"));
    const QJsonValue durationDerivedValue = coach.value(QStringLiteral("durationMilkDerived"));
    if (!visualValue.isBool() || !audioValue.isBool() || !cueTextValue.isString()
        || !cueSeverityValue.isString() || !cueActiveValue.isBool()
        || !durationDerivedValue.isBool()) {
        return fail(error, QStringLiteral("Steam coach state fields have invalid types"));
    }
    const QString cueText = cueTextValue.toString();
    const QString cueSeverity = cueSeverityValue.toString();
    const bool cueActive = cueActiveValue.toBool();
    if (cueText.size() > 512 || cueText.toUtf8().size() > 2048)
        return fail(error, QStringLiteral("Steam coach cue text exceeds the size limit"));
    if (cueActive) {
        if (cueText.trimmed().isEmpty()
            || (cueSeverity != QStringLiteral("positive")
                && cueSeverity != QStringLiteral("info")
                && cueSeverity != QStringLiteral("caution"))) {
            return fail(error, QStringLiteral("Active Steam coach cue is invalid"));
        }
    } else if (!cueText.isEmpty() || !cueSeverity.isEmpty()) {
        return fail(error, QStringLiteral("Inactive Steam coach cue must be empty"));
    }

    const QJsonObject accessibility = accessibilityValue.toObject();
    if (!hasExactKeys(accessibility, {"extractionAnnouncementsEnabled",
                                      "extractionAnnouncementInterval",
                                      "extractionAnnouncementMode"})) {
        return fail(error, QStringLiteral("Steam accessibility settings have missing or unknown fields"));
    }
    const QJsonValue announcementsValue = accessibility.value(
        QStringLiteral("extractionAnnouncementsEnabled"));
    const QJsonValue intervalValue = accessibility.value(
        QStringLiteral("extractionAnnouncementInterval"));
    const QJsonValue modeValue = accessibility.value(
        QStringLiteral("extractionAnnouncementMode"));
    int interval = 0;
    if (!announcementsValue.isBool() || !jsonInt(intervalValue, 5, 30, &interval)
        || !modeValue.isString()) {
        return fail(error, QStringLiteral("Steam accessibility settings have invalid types or range"));
    }
    const QString mode = modeValue.toString();
    if (mode != QStringLiteral("timed") && mode != QStringLiteral("milestones_only")
        && mode != QStringLiteral("both")) {
        return fail(error, QStringLiteral("Steam extraction announcement mode is invalid"));
    }

    SteamUiState parsed;
    parsed.viewMode = viewMode;
    parsed.coachVisualEnabled = visualValue.toBool();
    parsed.coachAudioEnabled = audioValue.toBool();
    parsed.coachCueText = cueText;
    parsed.coachCueSeverity = cueSeverity;
    parsed.coachCueActive = cueActive;
    parsed.durationMilkDerived = durationDerivedValue.toBool();
    parsed.extractionAnnouncementsEnabled = announcementsValue.toBool();
    parsed.extractionAnnouncementInterval = interval;
    parsed.extractionAnnouncementMode = mode;
    *state = parsed;
    if (error)
        error->clear();
    return true;
}

bool SteamUiState::operator==(const SteamUiState& other) const
{
    return viewMode == other.viewMode
        && coachVisualEnabled == other.coachVisualEnabled
        && coachAudioEnabled == other.coachAudioEnabled
        && coachCueText == other.coachCueText
        && coachCueSeverity == other.coachCueSeverity
        && coachCueActive == other.coachCueActive
        && durationMilkDerived == other.durationMilkDerived
        && extractionAnnouncementsEnabled == other.extractionAnnouncementsEnabled
        && extractionAnnouncementInterval == other.extractionAnnouncementInterval
        && extractionAnnouncementMode == other.extractionAnnouncementMode;
}
