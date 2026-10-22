// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_info.hpp"
#include "qt_ui.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <csignal>
#include <cstdio>

int main(int argc, char **argv) {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    QApplication app(argc, argv);
    app.setApplicationName("Lyrion Player");
    app.setOrganizationName("lyrion-player");
    app.setApplicationVersion(tray::text(tray::appInfo::version));
    app.setQuitOnLastWindowClosed(false);
    bool smoke = app.arguments().contains("--smoke-test");
    try {
        auto environment = tray::platform::initializeApplication();
        QTemporaryDir temporary;
        if (smoke && !temporary.isValid())
            throw std::runtime_error("Cannot create smoke-test profile");
        auto directory = smoke ? tray::fs::u8path(temporary.path().toUtf8().toStdString())
                               : tray::platform::configDir();
        QString endpoint =
            "lyrion-player-" +
            QString::fromLatin1(QCryptographicHash::hash(tray::pathText(directory).toUtf8(),
                                                         QCryptographicHash::Sha256)
                                    .toHex()
                                    .left(24));
        std::unique_ptr<tray::AppLock> lock;
        if (!smoke) {
            lock = tray::platform::lockApplication();
            if (!lock) {
                QLocalSocket existing;
                existing.connectToServer(endpoint);
                if (existing.waitForConnected(1000)) {
                    existing.write("settings\n");
                    existing.flush();
                    existing.waitForBytesWritten(1000);
                } else
                    QMessageBox::information(nullptr, "Lyrion Player",
                                             "Another tray app is running. Open its Settings or "
                                             "quit it before starting this version.");
                return 0;
            }
        }
        tray::Manager manager(directory);
        tray::TrayApplication controller(manager, smoke);
        if (smoke)
            return controller.smokeTest() ? 0 : 1;
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        QLocalServer::removeServer(endpoint); // Safe while the native, process-owned lock is held.
        if (!server.listen(endpoint))
            throw std::runtime_error("Cannot listen for app activation requests");
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (auto socket = server.nextPendingConnection()) {
                auto activate = [&, socket] {
                    if (socket->bytesAvailable() > 32) {
                        socket->abort();
                        return;
                    }
                    if (!socket->canReadLine())
                        return;
                    if (socket->readLine() == "settings\n")
                        controller.openSettings();
                    socket->disconnectFromServer();
                };
                QObject::connect(socket, &QLocalSocket::disconnected, socket,
                                 &QObject::deleteLater);
                QTimer::singleShot(1000, socket, [socket] {
                    socket->abort();
                    socket->deleteLater();
                });
                QObject::connect(socket, &QLocalSocket::readyRead, &app, activate);
                activate();
            }
        });
        return app.exec();
    } catch (const std::exception &e) {
        if (smoke)
            std::fprintf(stderr, "%s\n", e.what());
        else
            QMessageBox::critical(nullptr, "Lyrion Player", tray::text(e.what()));
        return 1;
    }
}
