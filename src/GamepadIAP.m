/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 * Copyright (C) 2026 Felix Tudoran (OpenTyrian2000-iOS port)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * GamepadIAP.m
 *
 * Objective-C implementation of the Gamepad Support in-app purchase gate.
 */

#include <TargetConditionals.h>
#if TARGET_OS_IOS

#import "GamepadIAP.h"
#import <UIKit/UIKit.h>
#import <StoreKit/StoreKit.h>

// StoreKit 1 APIs are deprecated since iOS 18 but remain functional; silence
// the warnings so we can build cleanly while targeting iOS 26+.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// ---------------------------------------------------------------------------
// Configuration — must match the product ID in GamepadUnlock.storekit and
// App Store Connect.
// ---------------------------------------------------------------------------
static NSString * const kGamepadIAPProductID  = @"tudoraneau.OpenTyrian2000-iOS.gamepad_unlock";
static NSString * const kGamepadIAPDefaultsKey = @"gamepad_iap_unlocked";

// ---------------------------------------------------------------------------
// GamepadIAPCoordinator — singleton that manages the StoreKit lifecycle.
// ---------------------------------------------------------------------------

@interface GamepadIAPCoordinator : NSObject <SKProductsRequestDelegate,
                                             SKPaymentTransactionObserver>

+ (instancetype)shared;

/// YES when the user has a valid purchase (persisted in NSUserDefaults).
@property (nonatomic, readonly) BOOL isUnlocked;

/// Present the unlock/restore/cancel alert.  on_result is called on the main
/// thread with YES (enabled) or NO (declined / failed).
- (void)showPromptWithCompletion:(void (^)(BOOL enabled))completion;

@end

@implementation GamepadIAPCoordinator {
    void (^_completion)(BOOL);   // pending callback; nil when no dialog is up
    SKProduct          *_product;
    SKProductsRequest  *_request;
}

+ (instancetype)shared {
    static GamepadIAPCoordinator *s_instance;
    static dispatch_once_t s_token;
    dispatch_once(&s_token, ^{ s_instance = [self new]; });
    return s_instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        // Register as transaction observer early so restored transactions
        // are never lost across app launches.
        [[SKPaymentQueue defaultQueue] addTransactionObserver:self];
        // Pre-fetch product info so price is ready when the dialog appears.
        [self _fetchProduct];
    }
    return self;
}

// ---------------------------------------------------------------------------
#pragma mark - Unlock state
// ---------------------------------------------------------------------------

- (BOOL)isUnlocked {
    return [[NSUserDefaults standardUserDefaults] boolForKey:kGamepadIAPDefaultsKey];
}

- (void)_setUnlocked:(BOOL)unlocked {
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    [ud setBool:unlocked forKey:kGamepadIAPDefaultsKey];
    [ud synchronize];
}

// ---------------------------------------------------------------------------
#pragma mark - Product fetch
// ---------------------------------------------------------------------------

- (void)_fetchProduct {
    if (_request) return;
    _request = [[SKProductsRequest alloc]
                    initWithProductIdentifiers:[NSSet setWithObject:kGamepadIAPProductID]];
    _request.delegate = self;
    [_request start];
}

// ---------------------------------------------------------------------------
#pragma mark - Alert presentation
// ---------------------------------------------------------------------------

- (void)showPromptWithCompletion:(void (^)(BOOL enabled))completion {
    NSAssert([NSThread isMainThread], @"showPromptWithCompletion must be called on the main thread");

    // If a dialog is already up, replace the stale callback (should not
    // normally happen because gpad_scan guards against double-prompting).
    _completion = [completion copy];

    UIViewController *vc = [self _topViewController];
    if (!vc) {
        NSLog(@"GamepadIAP: no root view controller — declining.");
        [self _deliverResult:NO];
        return;
    }

    // Build the price label: show the formatted price if we have it, otherwise
    // a generic "Purchase" label while the StoreKit fetch is in flight.
    NSString *priceLabel;
    if (_product) {
        NSNumberFormatter *fmt = [NSNumberFormatter new];
        fmt.formatterBehavior = NSNumberFormatterBehavior10_4;
        fmt.numberStyle       = NSNumberFormatterCurrencyStyle;
        fmt.locale            = _product.priceLocale;
        priceLabel = [fmt stringFromNumber:_product.price] ?: NSLocalizedString(@"Purchase", nil);
    } else {
        priceLabel = NSLocalizedString(@"Purchase", nil);
    }

    UIAlertController *alert = [UIAlertController
        alertControllerWithTitle:NSLocalizedString(@"Gamepad Support", nil)
        message:NSLocalizedString(
            @"A controller was connected.\n\n"
            @"Unlock Gamepad Support once to play with any Bluetooth controller, "
            @"or restore a previous purchase.",
            nil)
        preferredStyle:UIAlertControllerStyleAlert];

    // Unlock (purchase)
    NSString *unlockTitle = [NSString stringWithFormat:
        NSLocalizedString(@"Unlock — %@", nil), priceLabel];
    [alert addAction:
        [UIAlertAction actionWithTitle:unlockTitle
                                 style:UIAlertActionStyleDefault
                               handler:^(UIAlertAction *_) {
            [self _purchaseProduct];
        }]];

    // Restore
    [alert addAction:
        [UIAlertAction actionWithTitle:NSLocalizedString(@"Restore Purchase", nil)
                                 style:UIAlertActionStyleDefault
                               handler:^(UIAlertAction *_) {
            [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
        }]];

    // Not Now (cancel)
    [alert addAction:
        [UIAlertAction actionWithTitle:NSLocalizedString(@"Not Now", nil)
                                 style:UIAlertActionStyleCancel
                               handler:^(UIAlertAction *_) {
            [self _deliverResult:NO];
        }]];

    [vc presentViewController:alert animated:YES completion:nil];
}

// ---------------------------------------------------------------------------
#pragma mark - Purchase
// ---------------------------------------------------------------------------

- (void)_purchaseProduct {
    if (![SKPaymentQueue canMakePayments]) {
        [self _showError:NSLocalizedString(
            @"In-App Purchases are disabled on this device.", nil)];
        [self _deliverResult:NO];
        return;
    }

    if (!_product) {
        // Product info still loading; retry fetch then inform the user.
        [self _fetchProduct];
        [self _showError:NSLocalizedString(
            @"Unable to reach the App Store. Please check your connection and try again.", nil)];
        [self _deliverResult:NO];
        return;
    }

    [[SKPaymentQueue defaultQueue] addPayment:[SKPayment paymentWithProduct:_product]];
}

// ---------------------------------------------------------------------------
#pragma mark - Helpers
// ---------------------------------------------------------------------------

- (void)_deliverResult:(BOOL)enabled {
    if (_completion) {
        void (^cb)(BOOL) = _completion;
        _completion = nil;
        cb(enabled);
    }
}

- (void)_showError:(NSString *)message {
    UIViewController *vc = [self _topViewController];
    if (!vc) return;
    UIAlertController *err = [UIAlertController
        alertControllerWithTitle:NSLocalizedString(@"Gamepad Support", nil)
        message:message
        preferredStyle:UIAlertControllerStyleAlert];
    [err addAction:[UIAlertAction actionWithTitle:NSLocalizedString(@"OK", nil)
                                           style:UIAlertActionStyleDefault
                                         handler:nil]];
    [vc presentViewController:err animated:YES completion:nil];
}

/// Returns the topmost presented view controller, walking the presented-VC
/// chain from the key window's root.
- (UIViewController *)_topViewController {
    UIViewController *root = nil;

    for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        UIWindowScene *ws = (UIWindowScene *)scene;
        for (UIWindow *w in ws.windows) {
            if (w.isKeyWindow) { root = w.rootViewController; break; }
        }
        if (root) break;
    }

    // Walk the presentation stack to find the topmost VC.
    while (root.presentedViewController) root = root.presentedViewController;
    return root;
}

// ---------------------------------------------------------------------------
#pragma mark - SKProductsRequestDelegate
// ---------------------------------------------------------------------------

- (void)productsRequest:(SKProductsRequest *)request
     didReceiveResponse:(SKProductsResponse *)response {
    dispatch_async(dispatch_get_main_queue(), ^{
        self->_product = response.products.firstObject;
        self->_request = nil;
        if (!self->_product) {
            NSLog(@"GamepadIAP: product '%@' not found in App Store Connect. "
                  @"Check the product ID and that the IAP is approved.",
                  kGamepadIAPProductID);
        }
    });
}

- (void)request:(SKRequest *)request didFailWithError:(NSError *)error {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSLog(@"GamepadIAP: product fetch failed: %@", error.localizedDescription);
        self->_request = nil;
    });
}

// ---------------------------------------------------------------------------
#pragma mark - SKPaymentTransactionObserver
// ---------------------------------------------------------------------------

- (void)paymentQueue:(SKPaymentQueue *)queue
 updatedTransactions:(NSArray<SKPaymentTransaction *> *)transactions {
    for (SKPaymentTransaction *tx in transactions) {
        if (![tx.payment.productIdentifier isEqualToString:kGamepadIAPProductID])
            continue;

        switch (tx.transactionState) {

            case SKPaymentTransactionStatePurchased:
            case SKPaymentTransactionStateRestored: {
                [self _setUnlocked:YES];
                [[SKPaymentQueue defaultQueue] finishTransaction:tx];
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self _deliverResult:YES];
                });
                break;
            }

            case SKPaymentTransactionStateFailed: {
                [[SKPaymentQueue defaultQueue] finishTransaction:tx];
                NSError *err = tx.error;
                dispatch_async(dispatch_get_main_queue(), ^{
                    // SKErrorPaymentCancelled means the user backed out of
                    // the App Store sheet — no error message needed.
                    if (err && err.code != SKErrorPaymentCancelled) {
                        [self _showError:err.localizedDescription
                            ?: NSLocalizedString(@"The purchase could not be completed.", nil)];
                    }
                    [self _deliverResult:NO];
                });
                break;
            }

            case SKPaymentTransactionStatePurchasing:
            case SKPaymentTransactionStateDeferred:
            default:
                // Still in progress — wait for the next callback.
                break;
        }
    }
}

/// Called when restoreCompletedTransactions finishes and there were no
/// transactions matching our product (i.e. the user has not purchased it).
- (void)paymentQueueRestoreCompletedTransactionsFinished:(SKPaymentQueue *)queue {
    dispatch_async(dispatch_get_main_queue(), ^{
        // If _completion is nil, a Restored transaction already resolved it.
        if (self->_completion) {
            [self _showError:NSLocalizedString(
                @"No previous purchase found for Gamepad Support.", nil)];
            [self _deliverResult:NO];
        }
    });
}

- (void)paymentQueue:(SKPaymentQueue *)queue
    restoreCompletedTransactionsFailedWithError:(NSError *)error {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self _showError:error.localizedDescription
            ?: NSLocalizedString(@"Restore failed. Please try again.", nil)];
        [self _deliverResult:NO];
    });
}

@end

// ---------------------------------------------------------------------------
#pragma mark - C interface (called from gamepad.c)
// ---------------------------------------------------------------------------

bool gamepad_iap_is_unlocked(void) {
    return (bool)[[GamepadIAPCoordinator shared] isUnlocked];
}

void gamepad_iap_show_prompt(void (*on_result)(bool enabled)) {
    // Capture the C function pointer in a block so it survives the async hop.
    dispatch_async(dispatch_get_main_queue(), ^{
        [[GamepadIAPCoordinator shared]
            showPromptWithCompletion:^(BOOL enabled) {
                if (on_result) on_result((bool)enabled);
            }];
    });
}

#pragma clang diagnostic pop

#endif /* TARGET_OS_IOS */
