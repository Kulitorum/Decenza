#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

// Where the app writes files a USER may have to retrieve.
//
// Header-only on purpose. This used to live inside WebDebugLogger's constructor,
// which made it unreachable to anyone else without linking that class — and the
// first caller that wanted it (TranslationManager, dumping its unused-key list
// "beside debug.log") could not link it in test builds, because
// webdebuglogger.cpp is added per test target while translationmanager.cpp is in
// the shared test library. The choice at that point is a hand-copied second
// Android branch or one inline function; the centralize rule in CLAUDE.md says
// which, and inline costs no link dependency at all.
//
// THE ANDROID BRANCH IS THE WHOLE POINT. On Android this resolves to external
// storage via StorageHelper.getLogsPath(), so the file survives an APK update
// and — the part that matters — can be opened by the person reading the log. A
// second copy of this that quietly fell back to AppDataLocation would produce a
// file that exists, is named in the log, and is unreachable without adb.
namespace DecenzaPaths {

// Creates the directory if needed. Never empty: falls back to AppDataLocation
// when the Android call is unavailable, which is also the desktop answer.
inline QString logsDirectory()
{
    QString dir;
#ifdef Q_OS_ANDROID
    QJniObject javaPath = QJniObject::callStaticObjectMethod(
        "io/github/kulitorum/decenza_de1/StorageHelper",
        "getLogsPath",
        "()Ljava/lang/String;");
    if (javaPath.isValid())
        dir = javaPath.toString();
#endif
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

}  // namespace DecenzaPaths
