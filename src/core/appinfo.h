#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include "qmlsingletonpublish.h"

// The four startup values QML needs but that are not properties of any object: the app version,
// the build number, and the crash log and debug tail left behind by a previous run.
//
// They were four bare context properties — "AppVersion", "AppVersionCode", "PreviousCrashLog",
// "PreviousDebugLogTail" — which is the shape this whole migration exists to remove: a context
// property resolves at runtime and at no other time, so qmllint, qmlcachegen and the language
// server cannot tell one from a typo. Four QStrings and an int have no natural owner to hang off,
// so they get a holder rather than staying loose.
//
// Everything here is CONSTANT because every value is known before QQmlEngine::load() and none of
// them can change afterwards: the version is baked at compile time, and the crash log describes a
// run that has already ended. QML clears the crash log through CrashReporter, which does not write
// back here — the banner's dismissal is UI state, not a change to what the previous run did.
//
// Only the Decenza target compiles this header, so its QtQml include reaches nothing that links
// without Qt6::Qml.
class AppInfo : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(int versionCode READ versionCode CONSTANT)
    Q_PROPERTY(QString previousCrashLog READ previousCrashLog CONSTANT)
    Q_PROPERTY(QString previousDebugLogTail READ previousDebugLogTail CONSTANT)

public:
    AppInfo(QString version, int versionCode, QString previousCrashLog,
            QString previousDebugLogTail, QObject* parent = nullptr)
        : QObject(parent)
        , m_version(std::move(version))
        , m_versionCode(versionCode)
        , m_previousCrashLog(std::move(previousCrashLog))
        , m_previousDebugLogTail(std::move(previousDebugLogTail))
    {
    }

    QString version() const { return m_version; }
    int versionCode() const { return m_versionCode; }
    QString previousCrashLog() const { return m_previousCrashLog; }
    QString previousDebugLogTail() const { return m_previousDebugLogTail; }

    // Same publish-the-instance shape as contextsingletons_qml.h; see the lifetime rule at the
    // top of that file. main() owns the object and must declare it before `engine`.
    inline static AppInfo* s_singletonInstance = nullptr;
    static AppInfo* create(QQmlEngine*, QJSEngine* engine)
    {
        return decenzaPublishedSingleton(s_singletonInstance, engine, "AppInfo");
    }

private:
    QString m_version;
    int m_versionCode = 0;
    QString m_previousCrashLog;
    QString m_previousDebugLogTail;
};
