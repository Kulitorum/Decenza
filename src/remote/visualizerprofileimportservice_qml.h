#pragma once

#include "visualizerprofileimportservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct VisualizerProfileImportServiceForeign
{
    Q_GADGET
    QML_FOREIGN(VisualizerProfileImportService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(VisualizerProfileImportService)
public:
    static VisualizerProfileImportService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<VisualizerProfileImportService>,
                      "The QML factory must create the engine-owned service instance.");
        return new VisualizerProfileImportService(
            VisualizerProfileImportService::publishedNode(), engine);
    }
};
