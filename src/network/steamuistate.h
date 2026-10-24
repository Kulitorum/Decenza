#pragma once

#include <QJsonObject>
#include <QString>

// Bounded session projection for the shared Steam page. Preference values and
// live coach output are kept together as one typed snapshot, separate from the
// machine telemetry and theme appearance.
struct SteamUiState {
    QString viewMode = QStringLiteral("timer");
    bool coachVisualEnabled = false;
    bool coachAudioEnabled = false;
    QString coachCueText;
    QString coachCueSeverity;
    bool coachCueActive = false;
    bool durationMilkDerived = false;
    bool extractionAnnouncementsEnabled = true;
    int extractionAnnouncementInterval = 5;
    QString extractionAnnouncementMode = QStringLiteral("both");

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& object, SteamUiState* state,
                      QString* error = nullptr);

    bool operator==(const SteamUiState& other) const;
    bool operator!=(const SteamUiState& other) const { return !(*this == other); }
};
