#pragma once

#include <QVariantList>
#include <QVariantMap>

#include <limits>

// Stored selections use a positionless sentinel for the synthetic Heater off
// preset. Display lists append that preset as their final row. Keep the two
// address spaces aligned here so native settings and remote snapshots share the
// exact lookup rule.
namespace SteamPitcherSelection {

inline constexpr int HeaterOffIndex = -1;

inline QVariantMap presetAt(const QVariantList& presets, int index)
{
    if (presets.isEmpty())
        return {};

    const qsizetype heaterOffDisplayIndex = presets.size() - 1;
    if (index == HeaterOffIndex
        || (index >= 0 && static_cast<qsizetype>(index) == heaterOffDisplayIndex))
        return presets.constLast().toMap();

    if (index < 0 || static_cast<qsizetype>(index) >= heaterOffDisplayIndex)
        return {};

    return presets.at(index).toMap();
}

inline int displayIndex(const QVariantList& presets, int selectedIndex)
{
    if (presets.isEmpty())
        return selectedIndex;

    const qsizetype heaterOffDisplayIndex = presets.size() - 1;
    if (selectedIndex == HeaterOffIndex
        || (selectedIndex >= 0
            && static_cast<qsizetype>(selectedIndex) == heaterOffDisplayIndex)) {
        return heaterOffDisplayIndex <= std::numeric_limits<int>::max()
            ? static_cast<int>(heaterOffDisplayIndex) : -1;
    }
    return selectedIndex;
}

} // namespace SteamPitcherSelection
