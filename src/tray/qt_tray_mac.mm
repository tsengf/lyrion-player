// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_tray.hpp"
#include "qt_tray_mac_p.hpp"
#include <QBuffer>
#include <QMenu>
#include <QPixmap>

@implementation LyrionTrayClickTarget {
    QMenu *menu;
    std::function<void()> activate;
}
- (instancetype)initWithMenu:(QMenu *)value activate:(std::function<void()>)callback {
    if ((self = [super init])) {
        menu = value;
        activate = std::move(callback);
    }
    return self;
}
- (void)clicked:(NSStatusBarButton *)button {
    // Keyboard/accessibility activation may have a non-mouse current event.
    // Inspect only its type; querying clickCount caused the previous Qt crash.
    auto type = NSApp.currentEvent.type;
    if (type == NSEventTypeRightMouseUp || type == NSEventTypeRightMouseDown) {
        NSMenu *native = menu->toNSMenu();
        [native popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, 0) inView:button];
    } else {
        activate();
    }
}
@end

namespace tray {
// Qt's Cocoa tray activation callback queries NSApp.currentEvent.clickCount.
// Menu-bar tracking can begin with a non-mouse event, which makes AppKit abort.
// Host Qt's native menu directly so no Qt tray activation observer is installed.
class MacTrayIcon final : public TrayIcon {
    NSStatusItem *item = nil;
    LyrionTrayClickTarget *target = nil;
    QMenu *menu;
    QIcon icon;
    QString tooltip;
    std::function<void()> activate;

  public:
    MacTrayIcon(QMenu *menu, const QIcon &icon, const QString &tooltip,
                std::function<void()> activate)
        : menu(menu), icon(icon), tooltip(tooltip), activate(std::move(activate)) {}
    ~MacTrayIcon() override { hide(); }
    void show() override {
        if (item)
            return;
        item = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
        // Assigning item.menu would make AppKit open it on both mouse buttons.
        target = [[LyrionTrayClickTarget alloc] initWithMenu:menu activate:activate];
        item.button.target = target;
        item.button.action = @selector(clicked:);
        [item.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
        auto utf8 = tooltip.toUtf8();
        item.button.toolTip = [[NSString alloc] initWithBytes:utf8.constData()
                                                       length:utf8.size()
                                                     encoding:NSUTF8StringEncoding];
        setIcon(icon);
    }
    void hide() override {
        if (!item)
            return;
        item.button.target = nil;
        item.button.action = nullptr;
        [NSStatusBar.systemStatusBar removeStatusItem:item];
        item = nil;
        target = nil;
    }
    void setIcon(const QIcon &image) override {
        icon = image;
        if (!item)
            return;
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.pixmap(36, 36).save(&buffer, "PNG");
        NSData *data = [NSData dataWithBytes:png.constData() length:png.size()];
        NSImage *native = [[NSImage alloc] initWithData:data];
        native.size = NSMakeSize(18, 18);
        [native setTemplate:YES];
        item.button.image = native;
    }
};

std::unique_ptr<TrayIcon> createTrayIcon(QMenu *menu, const QIcon &icon, const QString &tooltip,
                                         std::function<void()> activate) {
    return std::make_unique<MacTrayIcon>(menu, icon, tooltip, std::move(activate));
}
} // namespace tray
