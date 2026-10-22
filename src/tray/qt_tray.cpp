// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_tray.hpp"
#include <QMenu>
#include <QSystemTrayIcon>

namespace tray {
class QtTrayIcon final : public TrayIcon {
    QSystemTrayIcon icon;

  public:
    QtTrayIcon(QMenu *menu, const QIcon &image, const QString &tooltip,
               std::function<void()> doubleClicked)
        : icon(image) {
        icon.setContextMenu(menu);
        icon.setToolTip(tooltip);
        QObject::connect(&icon, &QSystemTrayIcon::activated, &icon,
                         [callback = std::move(doubleClicked)](auto reason) {
                             if (reason == QSystemTrayIcon::DoubleClick)
                                 callback();
                         });
    }
    void show() override { icon.show(); }
    void hide() override { icon.hide(); }
    void setIcon(const QIcon &image) override { icon.setIcon(image); }
};

std::unique_ptr<TrayIcon> createTrayIcon(QMenu *menu, const QIcon &icon, const QString &tooltip,
                                         std::function<void()> doubleClicked) {
    return std::make_unique<QtTrayIcon>(menu, icon, tooltip, std::move(doubleClicked));
}
} // namespace tray
