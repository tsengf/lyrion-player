// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QIcon>
#include <QString>
#include <functional>
#include <memory>

class QMenu;

namespace tray {
class TrayIcon {
  public:
    virtual ~TrayIcon() = default;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void setIcon(const QIcon &icon) = 0;
};

std::unique_ptr<TrayIcon> createTrayIcon(QMenu *menu, const QIcon &icon, const QString &tooltip,
                                         std::function<void()> doubleClicked);
} // namespace tray
