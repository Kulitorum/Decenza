#include "fileshare.h"

#include <QFileInfo>
#include <QtGlobal>

// A .cpp, not a .mm, matching the rest of the codebase: the iOS block below is
// inside #ifdef Q_OS_IOS, and on iOS CMakeLists.txt compiles ALL C++ as
// Objective-C++ (`-x objective-c++`, CMakeLists.txt:162) to work around a
// Qt/Foundation header conflict. macOS and every other platform never see the
// Objective-C. A .mm would instead fail to build on Android and Windows.

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QtCore/private/qandroidextras_p.h>
#endif

#ifdef Q_OS_IOS
#import <UIKit/UIKit.h>
#endif

namespace FileShare {

Result shareFile(const QString& filePath, const QString& title)
{
    if (filePath.isEmpty()) {
        return {false, QStringLiteral("No file to share")};
    }
    // Checked here rather than per platform. The iOS branch used to be the only one
    // that verified existence, so on Android a missing file produced an opaque
    // FileProvider failure instead of saying what was wrong.
    if (!QFileInfo::exists(filePath)) {
        return {false, QStringLiteral("File not found: %1").arg(filePath)};
    }

#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();

    // FileProvider content URI — Android 7+ refuses file:// URIs across apps.
    QJniObject fileObj = QJniObject::fromString(filePath);
    QJniObject file("java/io/File", "(Ljava/lang/String;)V", fileObj.object<jstring>());

    QJniObject packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
    QString authority = packageName.toString() + ".fileprovider";
    QJniObject authorityObj = QJniObject::fromString(authority);

    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider",
        "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        context.object(),
        authorityObj.object<jstring>(),
        file.object());

    if (!uri.isValid()) {
        // Not fatal for the user: the file is written and its path is actionable.
        return {false, QStringLiteral("Could not share; file is at %1").arg(filePath)};
    }

    QJniObject actionSend = QJniObject::fromString("android.intent.action.SEND");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", actionSend.object<jstring>());

    QJniObject mimeType = QJniObject::fromString("text/plain");
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;", mimeType.object<jstring>());

    QJniObject extraStream = QJniObject::getStaticObjectField<jstring>("android/content/Intent", "EXTRA_STREAM");
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                            extraStream.object<jstring>(), uri.object());

    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 1);  // FLAG_GRANT_READ_URI_PERMISSION

    QJniObject chooserTitle = QJniObject::fromString(title);
    QJniObject chooser = QJniObject::callStaticObjectMethod(
        "android/content/Intent",
        "createChooser",
        "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
        intent.object(),
        chooserTitle.object<jstring>());

    // createChooser can return a null/invalid Intent (e.g. a thrown
    // ActivityNotFoundException already cleared below would otherwise leave
    // `chooser` invalid and startActivity would crash on a null jobject).
    if (!chooser.isValid()) {
        QJniEnvironment().checkAndClearExceptions();
        return {false, QStringLiteral("Could not build the share chooser for %1")
                            .arg(filePath)};
    }

    chooser.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000);  // FLAG_ACTIVITY_NEW_TASK

    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());

    // startActivity is void, so its only failure signal is a pending Java
    // exception (ActivityNotFoundException: no app can handle ACTION_SEND;
    // SecurityException: URI permission refused). Qt's own JNI wrapper prints and
    // clears these on its own (qjnienvironment.cpp), which is why they were
    // reaching debug.log unmarked and invisible to the connections view while
    // this function still reported success. checkAndClearExceptions() here
    // reports the same failure through the channel this file's caller expects.
    if (QJniEnvironment().checkAndClearExceptions()) {
        return {false, QStringLiteral("Could not open the share dialog for %1 — no "
                                      "app handled it, or permission was refused")
                            .arg(filePath)};
    }

    return {true, QStringLiteral("Opening share dialog...")};

#elif defined(Q_OS_IOS)
    Q_UNUSED(title)  // The activity sheet titles itself from the item.
    NSString* nsPath = filePath.toNSString();
    NSURL* fileURL = [NSURL fileURLWithPath:nsPath];

    NSArray* activityItems = @[fileURL];
    UIActivityViewController* activityVC = [[UIActivityViewController alloc]
        initWithActivityItems:activityItems
        applicationActivities:nil];

    UIWindow* keyWindow = nil;
    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
        if ([scene isKindOfClass:[UIWindowScene class]]) {
            UIWindowScene* windowScene = (UIWindowScene*)scene;
            for (UIWindow* window in windowScene.windows) {
                if (window.isKeyWindow) {
                    keyWindow = window;
                    break;
                }
            }
        }
        if (keyWindow) break;
    }

    UIViewController* rootVC = keyWindow.rootViewController;
    if (!rootVC) {
        // Matches the Android branch's convention: not fatal for the user, the
        // file is written and its path is actionable even if the sheet can't open.
        return {false, QStringLiteral("Could not open the share dialog; file is at %1")
                            .arg(filePath)};
    }
    // iPad requires an anchor for the popover or the presentation throws.
    if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad) {
        activityVC.popoverPresentationController.sourceView = rootVC.view;
        activityVC.popoverPresentationController.sourceRect = CGRectMake(
            rootVC.view.bounds.size.width / 2,
            rootVC.view.bounds.size.height / 2,
            0, 0);
    }
    [rootVC presentViewController:activityVC animated:YES completion:nil];
    return {true, QStringLiteral("Opening share dialog...")};

#else
    Q_UNUSED(title)
    // Desktop: no share sheet. The path is the answer.
    return {true, QStringLiteral("Log saved to: %1").arg(filePath)};
#endif
}

} // namespace FileShare
