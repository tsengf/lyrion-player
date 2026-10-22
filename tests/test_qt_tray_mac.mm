// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_tray.hpp"
#include "qt_tray_mac_p.hpp"
#import <AppKit/AppKit.h>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#import <objc/runtime.h>

namespace {
// Menu tracking on recent macOS can start with an application event. Recreate
// that condition without depending on a physical click or accessibility access.
class ApplicationEvent {
    Method method;
    IMP original, replacement;

  public:
    explicit ApplicationEvent(NSEventType type = NSEventTypeApplicationDefined) {
        NSEvent *event = type == NSEventTypeApplicationDefined
                             ? [NSEvent otherEventWithType:type
                                                  location:NSZeroPoint
                                             modifierFlags:0
                                                 timestamp:0
                                              windowNumber:0
                                                   context:nil
                                                   subtype:0
                                                     data1:0
                                                     data2:0]
                             : [NSEvent mouseEventWithType:type
                                                  location:NSZeroPoint
                                             modifierFlags:0
                                                 timestamp:0
                                              windowNumber:0
                                                   context:nil
                                               eventNumber:1
                                                clickCount:1
                                                  pressure:0];
        method = class_getInstanceMethod([NSApp class], @selector(currentEvent));
        replacement = imp_implementationWithBlock(^NSEvent *(id) {
          return event;
        });
        original = method_setImplementation(method, replacement);
    }
    ~ApplicationEvent() {
        method_setImplementation(method, original);
        imp_removeBlock(replacement);
    }
};
} // namespace

void verifyNativeTrayMenu(QMenu *menu) {
    auto icon = tray::createTrayIcon(menu, QIcon(), "Tray regression test", [] {});
    icon->show();
    QApplication::processEvents();
    NSMenu *native = menu->toNSMenu();
    QVERIFY(native);
    QStringList titles;
    for (NSMenuItem *item in native.itemArray) {
        if (item.separatorItem)
            continue;
        QVERIFY(!item.hidden);
        titles << QString::fromUtf8(item.title.UTF8String);
    }
    QCOMPARE(titles,
             QStringList({"Settings…", "Start app at login", "Start all players",
                          "Stop all players", "About Lyrion Player", "Quit Lyrion Player"}));
    QSignalSpy opening(menu, &QMenu::aboutToShow);
    for (int attempt = 0; attempt < 3; ++attempt) {
        ApplicationEvent applicationEvent;
        QCOMPARE(NSApp.currentEvent.type, NSEventTypeApplicationDefined);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 150 * NSEC_PER_MSEC),
                       dispatch_get_main_queue(), ^{
                         [native cancelTracking];
                       });
        [native popUpMenuPositioningItem:nil atLocation:NSMakePoint(100, 100) inView:nil];
    }
    QCOMPARE(opening.count(), 3);
    // Native menu selection must still reach the shared Qt action.
    auto settings = menu->actions().front();
    QSignalSpy selected(settings, &QAction::triggered);
    NSMenuItem *item = [native itemAtIndex:0];
    QVERIFY(item.action);
    QVERIFY([NSApp sendAction:item.action to:item.target from:item]);
    QTRY_COMPARE(selected.count(), 1);
    int activations = 0;
    LyrionTrayClickTarget *target = [[LyrionTrayClickTarget alloc] initWithMenu:menu
                                                                       activate:[&] {
                                                                           ++activations;
                                                                           settings->trigger();
                                                                       }];
    {
        ApplicationEvent left(NSEventTypeLeftMouseUp);
        [target clicked:nil];
    }
    QCOMPARE(activations, 1);
    QCOMPARE(opening.count(), 3); // A left click opens Settings without a menu.
    {
        ApplicationEvent right(NSEventTypeRightMouseUp);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 150 * NSEC_PER_MSEC),
                       dispatch_get_main_queue(), ^{
                         [native cancelTracking];
                       });
        [target clicked:nil];
    }
    QCOMPARE(activations, 1);
    QCOMPARE(opening.count(), 4); // A right click keeps the context menu available.
    {
        ApplicationEvent keyboard;
        [target clicked:nil];
    }
    QCOMPARE(activations, 2); // Accessibility/non-mouse activation is also safe.
    icon->hide();
}
