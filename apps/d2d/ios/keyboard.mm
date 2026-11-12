// SPDX-License-Identifier: GPL-3.0-or-later
// SDL's updateKeyboard assigns the view frame from UIKit_ComputeViewFrame.
// In landscape that frame is not the full phone. Pin it back to the window.
#import <UIKit/UIKit.h>

#include "ios/keyboard.hpp"

@interface D2DKeyboardPin : NSObject
- (void)show;
@end

@implementation D2DKeyboardPin {
    BOOL presenting;
}

+ (D2DKeyboardPin*)shared {
    static D2DKeyboardPin* pin;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        pin = [D2DKeyboardPin new];
    });
    return pin;
}

- (void)watch {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
        [center addObserver:self
                   selector:@selector(keyboardWillShow:)
                       name:UIKeyboardWillShowNotification
                     object:nil];
        [center addObserver:self
                   selector:@selector(keyboardWillHide:)
                       name:UIKeyboardWillHideNotification
                     object:nil];
    });
}

- (UIWindow*)gameWindow {
    UIWindow* fallback = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        for (UIWindow* window in ((UIWindowScene*)scene).windows) {
            if (!fallback) fallback = window;
            if (window.isKeyWindow) return window;
        }
    }
    return fallback;
}

- (UIView*)gameView {
    return [self gameWindow].rootViewController.view;
}

- (UITextField*)textField {
    for (UIView* subview in [self gameView].subviews)
        if ([subview isKindOfClass:[UITextField class]]) return (UITextField*)subview;
    return nil;
}

// SDL's observer runs first and sets the computed frame. Set the window
// bounds after that, including after becomeFirstResponder returns.
- (void)pinView {
    UIWindow* window = [self gameWindow];
    UIView* view = window.rootViewController.view;
    if (window && view) view.frame = window.bounds;
}

- (void)presentField {
    UITextField* field = [self textField];
    if (field) field.frame = CGRectMake(0, 0, 1, 1);
    if (field && !field.isFirstResponder && !presenting) {
        presenting = YES;
        [field becomeFirstResponder];
        presenting = NO;
    }
    [self pinView];
}

- (void)show {
    [self watch];
    [self presentField];
}

- (void)dismiss {
    UITextField* field = [self textField];
    if (field.isFirstResponder) [field resignFirstResponder];
    [self pinView];
}

- (void)keyboardWillShow:(NSNotification*)notification {
    (void)notification;
    [self presentField];
}

- (void)keyboardWillHide:(NSNotification*)notification {
    (void)notification;
    // Hide means the software keyboard went away. Resigning here drops the
    // field while text input is still wanted (a hardware keyboard sends hide
    // immediately), so only put the view back.
    [self pinView];
}

@end

namespace d2d::ios {

void present_text_keyboard() {
    [[D2DKeyboardPin shared] show];
}

void dismiss_text_keyboard() {
    [[D2DKeyboardPin shared] dismiss];
}

}  // namespace d2d::ios
