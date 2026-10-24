#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

struct SteamPitcherSnapshot {
    QVariantList presets;
    int selectedIndex = -1;

    // Stable optimistic-concurrency token for the ordered preset data. Selection
    // is deliberately excluded so two browser clients can select independently.
    QString revision() const;
    QJsonObject toJson() const;
    static bool parse(const QJsonObject& object, SteamPitcherSnapshot* snapshot,
                      QString* error = nullptr);

    // Project native settings data to the stable, read-only remote schema.
    // Invalid optional user-preset fields are omitted while the row itself is
    // retained, so this does not fabricate values or shift selection indexes.
    static SteamPitcherSnapshot project(const QVariantList& presets, int selectedIndex);

    QVariantMap presetAt(int index) const;
    int displayIndex() const;
};
