// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_info.hpp"
#include "qt_ui.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace tray {
QIcon applicationIcon() {
    QIcon icon;
    for (int size : {24, 32, 64}) {
        QPixmap image(size, size);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        painter.setPen(QPen(QApplication::palette().color(QPalette::WindowText), 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(5, 2, 14, 20), 2, 2);
        painter.drawEllipse(QPointF(12, 14), 4, 4);
        painter.drawEllipse(QPointF(12, 6), 1, 1);
        painter.end();
        icon.addPixmap(image);
    }
#ifdef Q_OS_MACOS
    icon.setIsMask(true);
#endif
    return icon;
}
TrayApplication::TrayApplication(Manager &owner, bool testing)
    : manager(owner), smoke(testing), settings(manager), tray(applicationIcon()), menu(new QMenu) {
    settings.quitRequested = [this] { requestQuit(); };
    auto configure = menu->addAction(tr("Settings…"), this, [this] { openSettings(); });
    configure->setMenuRole(QAction::PreferencesRole);
    login = menu->addAction(tr("Start app at login"));
    login->setCheckable(true);
    login->setObjectName("loginAction");
    connect(login, &QAction::triggered, this, [this] {
        try {
            platform::setLogin(platform::loginStatus() == "off");
            if (platform::loginStatus() == "approval") {
                QMessageBox::information(
                    &settings, tr("Login approval required"),
                    tr("Enable Lyrion Player in System Settings → General → Login Items."));
                platform::openURL(
                    "x-apple.systempreferences:com.apple.LoginItems-Settings.extension");
            }
        } catch (const std::exception &e) {
            QMessageBox::warning(&settings, tr("Login startup"), text(e.what()));
        }
        updateLogin();
    });
    menu->addSeparator();
    workActions.push_back(menu->addAction(tr("Start all players"), this, [this] {
        manager.startAll();
        settings.refresh();
    }));
    workActions.push_back(menu->addAction(tr("Stop all players"), this, [this] {
        manager.stopAll();
        settings.refresh();
    }));
    menu->addSeparator();
    auto aboutAction = menu->addAction(tr("About Lyrion Player"), this, [this] { showAbout(); });
    aboutAction->setMenuRole(QAction::AboutRole);
    auto quitAction = menu->addAction(tr("Quit Lyrion Player"), this, [this] { requestQuit(); });
    quitAction->setMenuRole(QAction::QuitRole);
    connect(menu, &QMenu::aboutToShow, this, [this] {
        menuScreen = QGuiApplication::screenAt(QCursor::pos());
        updateLogin();
    });
    tray.setToolTip(tr("Lyrion Player"));
    tray.setContextMenu(menu);
    connect(&tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                menuScreen = QGuiApplication::screenAt(QCursor::pos());
                if (reason == QSystemTrayIcon::DoubleClick)
                    openSettings();
            });
    QApplication::instance()->installEventFilter(this);
    auto timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this] { tick(); });
    timer->start();
    updateLogin();
    if (!smoke) {
        tray.show();
        manager.startConfigured();
        manager.scan();
        if (manager.instances.empty() || !manager.error.empty() ||
            !QSystemTrayIcon::isSystemTrayAvailable())
            openSettings();
    }
}
TrayApplication::~TrayApplication() {
    QApplication::instance()->removeEventFilter(this);
    tray.hide();
    tray.setContextMenu(nullptr);
    delete menu;
}
void TrayApplication::openSettings() { settings.openSettings(menuScreen); }
void TrayApplication::updateLogin() {
    auto state = smoke ? std::string("off") : platform::loginStatus();
    login->setChecked(state != "off");
    login->setText(state == "approval" ? tr("Start app at login (approval required)")
                                       : tr("Start app at login"));
}
void TrayApplication::showAbout() {
    if (about) {
        about->show();
        about->raise();
        about->activateWindow();
        return;
    }
    about = new QDialog(&settings);
    about->setObjectName("aboutDialog");
    about->setWindowTitle(tr("About Lyrion Player"));
    auto layout = new QVBoxLayout(about);
    auto title = new QLabel(tr("<h2>Lyrion Player %1</h2>").arg(text(appInfo::version)), about);
    layout->addWidget(title);
    for (auto value : {appInfo::description, appInfo::credits, appInfo::license}) {
        auto label = new QLabel(text(value), about);
        label->setWordWrap(true);
        layout->addWidget(label);
    }
    auto link = new QLabel(QString("<a href=\"%1\">%1</a>").arg(text(appInfo::website)), about);
    link->setOpenExternalLinks(true);
    layout->addWidget(link);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, about);
    connect(buttons, &QDialogButtonBox::rejected, about, &QDialog::close);
    layout->addWidget(buttons);
    about->resize(440, about->sizeHint().height());
    about->show();
    about->raise();
    about->activateWindow();
}
void TrayApplication::requestQuit() {
    if (manager.quitting)
        return;
    manager.quit();
    settings.refresh();
    login->setEnabled(false);
    for (auto action : workActions)
        action->setEnabled(false);
    tick();
}
void TrayApplication::tick() {
    try {
        manager.tick();
        settings.refresh();
        updateLogin();
        if (manager.quitting && manager.stopped()) {
            finishing = true;
            QCoreApplication::quit();
        }
    } catch (const std::exception &e) {
        settings.findChild<QLabel *>("settingsSummary")->setText(text(e.what()));
    }
}
bool TrayApplication::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::Quit && !finishing) {
        requestQuit();
        return true;
    }
    if (event->type() == QEvent::ApplicationPaletteChange) {
        tray.setIcon(applicationIcon());
        settings.setWindowIcon(applicationIcon());
    }
    return QObject::eventFilter(object, event);
}
bool TrayApplication::smokeTest() {
    openSettings();
    QApplication::processEvents();
    bool valid = settings.size() == settings.minimumSize() &&
                 settings.findChild<QTableWidget *>("playersTable") && !tray.icon().isNull();
    settings.openEditor(manager.draft());
    QApplication::processEvents();
    auto editor = settings.findChild<QDialog *>("playerEditor");
    valid = valid && editor && editor->findChild<QComboBox *>("serverChoice")->count() >= 2;
    if (editor) {
        editor->reject();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    showAbout();
    QApplication::processEvents();
    valid = valid && about && about->isVisible();
    if (about)
        about->close();
    return valid;
}
} // namespace tray
