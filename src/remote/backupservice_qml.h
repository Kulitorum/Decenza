#pragma once
#include "backupservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQml/qqml.h>
#include <type_traits>

struct BackupServiceForeign
{
    Q_GADGET
    QML_FOREIGN(BackupService)
    QML_NAMED_ELEMENT(BackupService)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
public:
    static BackupService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<BackupService>);
        return BackupService::create(engine, nullptr);
    }
};
