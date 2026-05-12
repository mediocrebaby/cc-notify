// macOS notification via Objective-C++ + UserNotifications framework.
// No osascript, no scripts — direct framework calls.

#include "notifier.h"

#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>

// UNUserNotificationCenter requires:
//   - macOS 10.14+
//   - An active NSRunLoop so the permission dialog can be presented
//     and completion handlers can fire on the main thread.
//   - Ad-hoc code signature (see CMakeLists.txt POST_BUILD step)
//   - UNAuthorizationOptionProvisional for silent grant without a dialog

bool sendNotification(const std::string& title, const std::string& message) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];

        UNUserNotificationCenter* center =
            [UNUserNotificationCenter currentNotificationCenter];

        // ── 1. Request permission ─────────────────────────────────────────
        // Provisional: system grants immediately without a dialog.
        // Notifications appear quietly in Notification Centre; the user can
        // promote them to banners by clicking "Keep" on the first one.
        __block BOOL permDone = NO;
        __block BOOL granted  = NO;

        [center requestAuthorizationWithOptions:
                    UNAuthorizationOptionAlert | UNAuthorizationOptionProvisional
                             completionHandler:^(BOOL g, NSError*) {
            granted  = g;
            permDone = YES;
        }];

        NSDate* permDeadline = [NSDate dateWithTimeIntervalSinceNow:30.0];
        while (!permDone && [permDeadline timeIntervalSinceNow] > 0) {
            [[NSRunLoop currentRunLoop]
                runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        }

        if (!granted) return false;

        // ── 2. Build the notification content ────────────────────────────
        UNMutableNotificationContent* content =
            [[UNMutableNotificationContent alloc] init];
        content.title = [NSString stringWithUTF8String:title.c_str()];
        content.body  = [NSString stringWithUTF8String:message.c_str()];

        // ── 3. Post the notification ──────────────────────────────────────
        UNNotificationRequest* req =
            [UNNotificationRequest requestWithIdentifier:@"cc-notify"
                                                content:content
                                                trigger:nil];

        __block BOOL postDone = NO;
        __block BOOL posted   = NO;

        [center addNotificationRequest:req
               withCompletionHandler:^(NSError* err) {
            posted   = (err == nil);
            postDone = YES;
        }];

        NSDate* postDeadline = [NSDate dateWithTimeIntervalSinceNow:5.0];
        while (!postDone && [postDeadline timeIntervalSinceNow] > 0) {
            [[NSRunLoop currentRunLoop]
                runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        }

        return posted;
    }
}
