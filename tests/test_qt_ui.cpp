// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_ui.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <utility>

#ifdef Q_OS_MACOS
void verifyNativeTrayMenu(QMenu *menu);
#endif

namespace {
struct ChildState {
    bool running = true, stopped = false, killed = false;
    std::string output;
};
class TestChild final : public tray::Child {
    std::shared_ptr<ChildState> state;

  public:
    explicit TestChild(std::shared_ptr<ChildState> value) : state(std::move(value)) {}
    bool alive() override { return state->running; }
    int exitCode() override { return 0; }
    std::string read() override { return std::exchange(state->output, {}); }
    void stop() override { state->stopped = true; }
    void kill() override {
        state->killed = true;
        state->running = false;
    }
};
} // namespace

class QtUiTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QApplication::setQuitOnLastWindowClosed(false); }
    void editorPreservesAutomaticDiscovery() {
        QTemporaryDir profile;
        tray::Manager manager(tray::fs::u8path(profile.path().toStdString()));
        manager.servers.push_back({"Test server", "192.0.2.10", "one", 3483, 9001});
        auto draft = manager.draft();
        tray::PlayerEditor editor(manager, draft);
        auto host = editor.findChild<QLineEdit *>("serverHost");
        auto port = editor.findChild<QLineEdit *>("serverPort");
        auto choice = editor.findChild<QComboBox *>("serverChoice");
        QVERIFY(host && port && choice);
        QCOMPARE(host->text(), QString("192.0.2.10"));
        QVERIFY(host->isReadOnly());
        QVERIFY(editor.configuration().host.empty());
        QCOMPARE(editor.configuration().mac, draft.mac);
        QCOMPARE(editor.configuration().webPort, 9001u);
        manager.servers[0].host = "192.0.2.20";
        editor.refreshServers();
        QCOMPARE(host->text(), QString("192.0.2.20"));
        QVERIFY(editor.configuration().host.empty());
        editor.accept();
        QVERIFY(manager.find(draft.id));
        QVERIFY(manager.find(draft.id)->host.empty());
        tray::Manager restored(tray::fs::u8path(profile.path().toStdString()));
        QCOMPARE(restored.instances.size(), size_t(1));
        QCOMPARE(restored.instances[0].mac, draft.mac);
    }
    void manualAndDiscoveredServers() {
        QTemporaryDir profile;
        tray::Manager manager(tray::fs::u8path(profile.path().toStdString()));
        manager.servers.push_back({"Discovered", "192.0.2.10", "one", 3484, 9010});
        auto draft = manager.draft();
        draft.host = "music.example";
        draft.port = 3490;
        draft.webPort = 9020;
        tray::PlayerEditor editor(manager, draft);
        auto choice = editor.findChild<QComboBox *>("serverChoice");
        auto host = editor.findChild<QLineEdit *>("serverHost");
        auto port = editor.findChild<QLineEdit *>("serverPort");
        QCOMPARE(choice->currentIndex(), 1);
        QCOMPARE(host->text(), QString("music.example"));
        QCOMPARE(port->text(), QString("3490"));
        QVERIFY(!host->isReadOnly());
        host->setText("typed.example");
        port->setText("3500");
        choice->setCurrentIndex(0);
        editor.refreshServers();
        choice->setCurrentIndex(1);
        QCOMPARE(host->text(), QString("typed.example"));
        QCOMPARE(port->text(), QString("3500"));
        choice->setCurrentIndex(2);
        QCOMPARE(editor.configuration().host, std::string("192.0.2.10"));
        QCOMPARE(editor.configuration().port, 3484u);
        QCOMPARE(editor.configuration().webPort, 9010u);
        manager.servers.insert(manager.servers.begin(), {"Other", "192.0.2.30", "two", 3483, 9000});
        editor.refreshServers();
        QCOMPARE(choice->currentIndex(), 3);
        QCOMPARE(editor.configuration().host, std::string("192.0.2.10"));
        manager.servers.pop_back();
        editor.refreshServers();
        QCOMPARE(choice->currentIndex(), 1);
        QCOMPARE(editor.configuration().host, std::string("192.0.2.10"));
        QCOMPARE(editor.configuration().port, 3484u);
    }
    void validationAndCancel() {
        QTemporaryDir profile;
        tray::Manager manager(tray::fs::u8path(profile.path().toStdString()));
        auto draft = manager.draft();
        draft.host = "music.example";
        tray::PlayerEditor editor(manager, draft);
        auto port = editor.findChild<QLineEdit *>("serverPort");
        port->setText("70000");
        QVERIFY_THROWS_EXCEPTION(std::exception, editor.configuration());
        port->setText("3483");
        editor.findChild<QLineEdit *>("playerName")->setText(QString::fromUtf8("Music 🎵"));
        QCOMPARE(editor.configuration().name, std::string("Music 🎵"));
        editor.findChild<QLineEdit *>("playerId")->setText("01:00:00:00:00:01");
        QVERIFY_THROWS_EXCEPTION(std::exception, editor.configuration());
        editor.reject();
        QVERIFY(manager.instances.empty());
        QVERIFY(!QFile::exists(profile.path() + "/config.json"));
    }
    void settingsSelectionRefreshAndLifetime() {
        QTemporaryDir profile;
        tray::Manager manager(tray::fs::u8path(profile.path().toStdString()));
        tray::SettingsWindow settings(manager);
        auto edit = settings.findChild<QPushButton *>("editPlayer");
        auto table = settings.findChild<QTableWidget *>("playersTable");
        QVERIFY(edit && table);
        QVERIFY(!edit->isEnabled());
        auto first = manager.draft(), second = manager.draft();
        first.name = "First";
        second.name = "Second";
        first.autoStart = second.autoStart = false;
        manager.save(first);
        manager.save(second);
        settings.refresh();
        QCOMPARE(table->rowCount(), 2);
        table->selectRow(1);
        QVERIFY(edit->isEnabled());
        QCOMPARE(settings.selectedId(), second.id);
        first.name = "Changed";
        manager.save(first);
        settings.refresh();
        QCOMPARE(settings.selectedId(), second.id);
        QCOMPARE(table->item(0, 0)->text(), QString("Changed"));
        settings.show();
        QVERIFY(QTest::qWaitForWindowExposed(&settings));
        int initial = table->columnWidth(0);
        settings.resize(settings.width() + 200, settings.height() + 100);
        QTRY_VERIFY(table->columnWidth(0) > initial);
        settings.close();
        QCOMPARE(manager.instances.size(), size_t(2));
        QVERIFY(!manager.quitting);
        settings.openEditor(second);
        QApplication::processEvents();
        auto dialog = settings.findChild<QDialog *>("playerEditor");
        QVERIFY(dialog);
        dialog->reject();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!settings.findChild<QDialog *>("playerEditor"));
    }
    void controllerSmokeAndAbout() {
        QTemporaryDir profile;
        tray::Manager manager(tray::fs::u8path(profile.path().toStdString()));
        tray::TrayApplication controller(manager, true);
        QVERIFY(controller.smokeTest());
        QVERIFY(!manager.scanning);
        QVERIFY(manager.instances.empty());
        controller.showAbout();
        QApplication::processEvents();
        auto about = controller.settingsWindow().findChild<QDialog *>("aboutDialog");
        QVERIFY(about);
        QVERIFY(about->isVisible());
        bool found = false;
        for (auto label : about->findChildren<QLabel *>())
            found |= label->text().contains("github.com/tsengf/lyrion-player");
        QVERIFY(found);
        about->close();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void trayMenu() {
        QTemporaryDir profile;
        tray::Manager manager(tray::fs::u8path(profile.path().toStdString()));
        tray::TrayApplication controller(manager, true);
        auto menu = controller.settingsWindow().findChild<QMenu *>("trayMenu");
        QVERIFY(menu);
        QStringList titles;
        for (auto action : menu->actions()) {
            QCOMPARE(action->menuRole(), QAction::NoRole);
            if (action->isSeparator())
                continue;
            QVERIFY(action->isVisible());
            titles << action->text();
        }
        QCOMPARE(titles,
                 QStringList({"Settings…", "Start app at login", "Start all players",
                              "Stop all players", "About Lyrion Player", "Quit Lyrion Player"}));
#ifdef Q_OS_MACOS
        if (QApplication::platformName() == "cocoa")
            verifyNativeTrayMenu(menu);
#endif
        menu->actions().front()->trigger();
        QVERIFY(controller.settingsWindow().isVisible());
        QVERIFY(!manager.quitting);
    }
    void playbackUpdatesAndGracefulQuit() {
        QTemporaryDir profile;
        tray::Manager manager(tray::fs::u8path(profile.path().toStdString()));
        auto value = manager.draft();
        manager.save(value);
        auto state = std::make_shared<ChildState>();
        state->output =
            R"({"version":1,"type":"connection","state":"connected","host":"192.0.2.10","port":3483})"
            "\n";
        manager.runtime[value.id].child = std::make_unique<TestChild>(state);
        manager.runtime[value.id].desired = true;
        tray::TrayApplication controller(manager, true);
        auto &settings = controller.settingsWindow();
        auto table = settings.findChild<QTableWidget *>("playersTable");
        table->selectRow(0);
        controller.tick();
        QCOMPARE(table->item(0, 2)->text(), QString("192.0.2.10:3483"));
        QCOMPARE(table->item(0, 4)->text(), QString("Connected"));
        QCOMPARE(settings.selectedId(), value.id);
        // An OS/application quit request must use the manager's bounded shutdown path.
        QEvent quit(QEvent::Quit);
        QApplication::sendEvent(QApplication::instance(), &quit);
        QVERIFY(manager.quitting);
        QVERIFY(state->stopped);
        QVERIFY(!state->killed);
        QVERIFY(!manager.stopped());
        QVERIFY(!settings.findChild<QPushButton *>("addPlayer")->isEnabled());
        manager.runtime[value.id].deadline =
            std::chrono::steady_clock::now() - std::chrono::seconds(1);
        controller.tick();
        QVERIFY(state->killed);
        QVERIFY(manager.stopped());
    }
    void screenshots() {
        const auto output = qEnvironmentVariable("PLAYER_TEST_SCREENSHOT_DIR");
        if (output.isEmpty())
            QSKIP("Screenshot export not requested");
        QTemporaryDir profile;
        tray::Manager manager(tray::fs::u8path(profile.path().toStdString()));
        auto first = manager.draft(), second = manager.draft();
        first.name = "Living Room";
        first.host = "192.0.2.10";
        second.name = "Office";
        second.host = "192.0.2.20";
        second.autoStart = false;
        manager.save(first);
        manager.save(second);
        tray::SettingsWindow settings(manager);
        settings.show();
        QVERIFY(QTest::qWaitForWindowExposed(&settings));
        for (bool dark : {false, true}) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            QApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark
                                                            : Qt::ColorScheme::Light);
#endif
            QTest::qWait(200);
            QVERIFY(settings.grab().save(
                output + (dark ? "/settings-macos-dark.png" : "/settings-macos.png")));
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        QApplication::styleHints()->unsetColorScheme();
#endif
    }
};
QTEST_MAIN(QtUiTests)
#include "test_qt_ui.moc"
