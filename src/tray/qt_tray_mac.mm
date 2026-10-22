// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_tray.hpp"
#import <AppKit/AppKit.h>
#include <QBuffer>
#include <QMenu>
#include <QPixmap>

namespace tray {
// Qt's Cocoa tray activation callback queries NSApp.currentEvent.clickCount.
// Menu-bar tracking can begin with a non-mouse event, which makes AppKit abort.
// Host Qt's native menu directly so no Qt tray activation observer is installed.
class MacTrayIcon final : public TrayIcon {
    NSStatusItem *item = nil;
    QMenu *menu;
    QIcon icon;
    QString tooltip;

  public:
    MacTrayIcon(QMenu *menu, const QIcon &icon, const QString &tooltip)
        : menu(menu), icon(icon), tooltip(tooltip) {}
    ~MacTrayIcon() override { hide(); }
    void show() override {
        if (item)
            return;
        item = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
        item.menu = menu->toNSMenu();
        auto utf8 = tooltip.toUtf8();
        item.button.toolTip = [[NSString alloc] initWithBytes:utf8.constData()
                                                       length:utf8.size()
                                                     encoding:NSUTF8StringEncoding];
        setIcon(icon);
    }
    void hide() override {
        if (!item)
            return;
        item.menu = nil;
        [NSStatusBar.systemStatusBar removeStatusItem:item];
        item = nil;
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
                                         std::function<void()>) {
    return std::make_unique<MacTrayIcon>(menu, icon, tooltip);
}
} // namespace tray
