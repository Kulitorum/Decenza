#include "remoteprofilemanager.h"
#include <QQmlEngine>
#include <QJSEngine>
#include "../profile/temperaturedisplay.h"

RemoteProfileManager::RemoteProfileManager(QObject* parent) : RemoteMachineUiSource(parent) {}

QString RemoteProfileManager::temperatureDisplayForSteps(const QVariantList& stepTempsC,
                                                          double anchorTemp, bool hasOverride,
                                                          double overrideTemp,
                                                          double baselineShiftC) const
{
    const bool fahrenheit = sessionIsFresh()
        && session()->appearance().value(QStringLiteral("fahrenheit")).toBool();
    return TemperatureDisplay::formatForSteps(stepTempsC, anchorTemp, hasOverride,
                                              overrideTemp, fahrenheit, baselineShiftC);
}

RemoteProfileManager* RemoteProfileManager::create(QQmlEngine* engine, QJSEngine*)
{
    auto* instance = new RemoteProfileManager(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}
