// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core.hpp"
#include <QDialog>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QTableWidget;
class QPushButton;
class QMenu;
class QAction;
class QScreen;
class QCloseEvent;

namespace tray {
QString text(const std::string &value);
QString pathText(const fs::path &path);
QIcon applicationIcon();

class PlayerEditor : public QDialog {
  public:
    PlayerEditor(Manager &manager, Instance instance, QWidget *parent = nullptr);
    Instance configuration() const;
    void refreshServers();
    void accept() override;

  private:
    Manager &manager;
    Instance editing;
    QLineEdit *name, *host, *port, *mac, *web;
    QComboBox *server;
    QCheckBox *autoStart;
    std::string manualHost;
    QString manualPort;
    unsigned manualWebPort;
    int previousMode = 0;
    QString serverSnapshot;
    void chooseServer();
};

class SettingsWindow : public QWidget {
  public:
    explicit SettingsWindow(Manager &manager);
    void refresh();
    void openEditor(const Instance &instance);
    void openSettings(QScreen *screen = nullptr);
    std::string selectedId() const;
    std::function<void()> quitRequested;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    Manager &manager;
    QTableWidget *table;
    QLabel *summary;
    QPushButton *add;
    std::vector<QPushButton *> selectionButtons;
    QPointer<PlayerEditor> editor;
    QString snapshot;
    void updateButtons();
    void action(const std::function<void()> &operation);
};

class TrayApplication : public QObject {
  public:
    TrayApplication(Manager &manager, bool smoke = false);
    ~TrayApplication() override;
    void openSettings();
    void requestQuit();
    void showAbout();
    void tick();
    SettingsWindow &settingsWindow() { return settings; }
    bool smokeTest();

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    Manager &manager;
    bool smoke, finishing = false;
    SettingsWindow settings;
    QSystemTrayIcon tray;
    QMenu *menu;
    QAction *login;
    std::vector<QAction *> workActions;
    QPointer<QScreen> menuScreen;
    QPointer<QDialog> about;
    void updateLogin();
};
} // namespace tray
