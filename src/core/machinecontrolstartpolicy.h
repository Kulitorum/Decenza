#pragma once

#include <QString>

// Pure guard used at request time and again at deferred dispatch time. The
// second check is essential: a recipe may finish applying after machine state,
// safety mode, or the user's selection has changed.
namespace MachineControlStartPolicy {

inline bool contextMatches(const QString& expectedContextId,
                           const QString& currentContextId)
{
    return !expectedContextId.isEmpty() && expectedContextId == currentContextId;
}

inline bool deviceCanStart(bool ready, bool headless, bool simulation)
{
    return ready && (headless || simulation);
}

inline bool machineCanStart(const QString& expectedContextId,
                            const QString& currentContextId,
                            bool ready, bool headless, bool simulation)
{
    return contextMatches(expectedContextId, currentContextId)
        && deviceCanStart(ready, headless, simulation);
}

inline bool recipeCanStart(const QString& expectedContextId,
                           const QString& currentContextId,
                           qint64 expectedRecipeId,
                           qint64 selectedRecipeId,
                           qint64 activeRecipeId,
                           bool ready, bool headless, bool simulation)
{
    return expectedRecipeId > 0
        && expectedRecipeId == selectedRecipeId
        && expectedRecipeId == activeRecipeId
        && machineCanStart(expectedContextId, currentContextId,
                           ready, headless, simulation);
}

inline bool simulatedShotCanRun(bool simulationMode, bool operating, bool flowing,
                                bool savePending, bool historyReady, bool hasModel,
                                bool hasSettings, bool hasProfile)
{
    return simulationMode && !operating && !flowing && !savePending
        && historyReady && hasModel && hasSettings && hasProfile;
}

} // namespace MachineControlStartPolicy
