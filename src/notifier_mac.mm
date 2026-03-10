// macOS notification via Objective-C++ + UserNotifications framework.
// No osascript, no scripts — direct framework calls.

#include "notifier.h"

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

// UNUserNotificationCenter requires:
//   - macOS 10.14+
//   - A run-loop to receive async callbacks (we use dispatch semaphores)
//
// For CLI tools without a proper app bundle the system may show the
// notification under "Terminal" (or whatever launched us), which is fine.

bool sendNotification(const std::string& title, const std::string& message) {
    @autoreleasepool {
        UNUserNotificationCenter* center =
            [UNUserNotificationCenter currentNotificationCenter];

        // ── 1. Request permission (if not already granted) ───────────────
        dispatch_semaphore_t permSem = dispatch_semaphore_create(0);
        __block BOOL granted = NO;

        [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                             completionHandler:^(BOOL g, NSError*) {
            granted = g;
            dispatch_semaphore_signal(permSem);
        }];

        // Wait up to 5 s for the user to respond to the permission dialog.
        dispatch_semaphore_wait(permSem,
            dispatch_time(DISPATCH_TIME_NOW, 5 * (int64_t)NSEC_PER_SEC));

        if (!granted) return false;

        // ── 2. Build the notification content ────────────────────────────
        UNMutableNotificationContent* content =
            [[UNMutableNotificationContent alloc] init];
        content.title = [NSString stringWithUTF8String:title.c_str()];
        content.body  = [NSString stringWithUTF8String:message.c_str()];

        // ── 3. Post the notification ──────────────────────────────────────
        // trigger = nil means "deliver immediately"
        UNNotificationRequest* req =
            [UNNotificationRequest requestWithIdentifier:@"cc-notify"
                                                content:content
                                                trigger:nil];

        dispatch_semaphore_t postSem = dispatch_semaphore_create(0);
        __block BOOL posted = NO;

        [center addNotificationRequest:req
               withCompletionHandler:^(NSError* err) {
            posted = (err == nil);
            dispatch_semaphore_signal(postSem);
        }];

        dispatch_semaphore_wait(postSem,
            dispatch_time(DISPATCH_TIME_NOW, 5 * (int64_t)NSEC_PER_SEC));

        return posted;
    }
}
