// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#import <AppKit/AppKit.h>
#include <functional>

class QMenu;

// Internal action target, shared with the native click regression test.
@interface LyrionTrayClickTarget : NSObject
- (instancetype)initWithMenu:(QMenu *)menu activate:(std::function<void()>)activate;
- (void)clicked:(NSStatusBarButton *)button;
@end
