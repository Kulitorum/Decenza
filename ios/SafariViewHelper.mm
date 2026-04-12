#import <SafariServices/SafariServices.h>
#import <UIKit/UIKit.h>

#include "SafariViewHelper.h"

static UIViewController* topViewController() {
    UIWindow *window = nil;
    for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
        if ([scene isKindOfClass:[UIWindowScene class]]) {
            UIWindowScene *ws = (UIWindowScene *)scene;
            window = ws.windows.firstObject;
            break;
        }
    }
    if (!window) return nil;

    UIViewController *vc = window.rootViewController;
    while (vc.presentedViewController)
        vc = vc.presentedViewController;
    return vc;
}

bool openInSafariView(const QString& urlString) {
    @autoreleasepool {
        NSURL *url = [NSURL URLWithString:urlString.toNSString()];
        if (!url) return false;

        UIViewController *rootVC = topViewController();
        if (!rootVC) return false;

        SFSafariViewController *sfvc = [[SFSafariViewController alloc] initWithURL:url];
        [rootVC presentViewController:sfvc animated:YES completion:nil];
        return true;
    }
}

void dismissSafariView() {
    @autoreleasepool {
        UIWindow *window = nil;
        for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
            if ([scene isKindOfClass:[UIWindowScene class]]) {
                UIWindowScene *ws = (UIWindowScene *)scene;
                window = ws.windows.firstObject;
                break;
            }
        }
        if (!window) return;

        UIViewController *vc = window.rootViewController;
        while (vc.presentedViewController) {
            if ([vc.presentedViewController isKindOfClass:[SFSafariViewController class]]) {
                [vc.presentedViewController dismissViewControllerAnimated:NO completion:nil];
                return;
            }
            vc = vc.presentedViewController;
        }
    }
}
