// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_ui.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>

namespace tray {
SettingsWindow::SettingsWindow(Manager &owner) : manager(owner) {
    setObjectName("settingsWindow");
    setWindowTitle(tr("Lyrion Player — Settings"));
    setWindowIcon(applicationIcon());
    setMinimumSize(820, 340);
    resize(minimumSize());
    auto layout = new QVBoxLayout(this);
    table = new QTableWidget(0, 5, this);
    table->setObjectName("playersTable");
    table->setHorizontalHeaderLabels(
        {tr("Player"), tr("Auto discovery"), tr("Server"), tr("Start with app"), tr("Status")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setMinimumSectionSize(100);
    table->verticalHeader()->hide();
    layout->addWidget(table, 1);
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    connect(table, &QTableWidget::cellDoubleClicked, this, [this] {
        if (auto value = manager.find(selectedId()))
            openEditor(*value);
    });
    auto controls = new QHBoxLayout;
    layout->addLayout(controls);
    auto button = [&](const QString &label, const char *id, bool selection,
                      const std::function<void()> &operation, QHBoxLayout *row) {
        auto result = new QPushButton(label, this);
        result->setObjectName(id);
        row->addWidget(result);
        if (selection)
            selectionButtons.push_back(result);
        connect(result, &QPushButton::clicked, this, [this, operation] { action(operation); });
        return result;
    };
    add = button(tr("Add…"), "addPlayer", false, [this] { openEditor(manager.draft()); }, controls);
    button(
        tr("Edit…"), "editPlayer", true,
        [this] {
            if (auto value = manager.find(selectedId()))
                openEditor(*value);
        },
        controls);
    button(
        tr("Remove"), "removePlayer", true,
        [this] {
            auto id = selectedId();
            if (manager.find(id) &&
                QMessageBox::question(
                    this, tr("Remove player?"),
                    tr("Remove this player and stop it if running? Its logs will be retained."),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel) == QMessageBox::Yes)
                manager.remove(id);
        },
        controls);
    button(
        tr("Start / Stop"), "togglePlayer", true,
        [this] {
            auto id = selectedId();
            if (!manager.find(id))
                return;
            if (manager.runtime[id].desired)
                manager.stop(id);
            else
                manager.start(id);
        },
        controls);
    controls->addStretch();
    auto files = new QHBoxLayout;
    layout->addLayout(files);
    button(
        tr("Open configuration folder"), "openConfiguration", false,
        [] {
            fs::create_directories(platform::configDir());
            platform::openFile(platform::configDir());
        },
        files);
    button(
        tr("Open Lyrion"), "openLyrion", true,
        [this] {
            auto url = manager.url(selectedId());
            if (url.empty())
                throw std::runtime_error(
                    "Start the player and wait for it to connect, or configure a Web URL.");
            platform::openURL(url);
        },
        files);
    button(
        tr("View Log"), "viewLog", true,
        [this] {
            auto path = manager.logPath(selectedId());
            if (!fs::exists(path))
                throw std::runtime_error("No log exists yet. Start the player first.");
            platform::openFile(path);
        },
        files);
    files->addStretch();
    button(
        tr("Quit"), "quitApp", false,
        [this] {
            if (quitRequested)
                quitRequested();
        },
        files);
    summary = new QLabel(this);
    summary->setObjectName("settingsSummary");
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary);
    refresh();
}

std::string SettingsWindow::selectedId() const {
    int row = table->currentRow();
    auto item = row >= 0 ? table->item(row, 0) : nullptr;
    return item ? item->data(Qt::UserRole).toString().toUtf8().toStdString() : "";
}
void SettingsWindow::updateButtons() {
    bool enabled = !manager.quitting && manager.find(selectedId());
    for (auto button : selectionButtons)
        button->setEnabled(enabled);
    add->setEnabled(!manager.quitting);
}
void SettingsWindow::action(const std::function<void()> &operation) {
    try {
        operation();
        refresh();
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Lyrion Player"), text(e.what()));
    }
}
void SettingsWindow::refresh() {
    QString next;
    for (const auto &value : manager.instances)
        next += text(encodeConfig({value}) + manager.status(value.id) +
                     manager.serverAddress(value.id));
    if (snapshot != next) {
        auto selected = selectedId();
        table->setRowCount(int(manager.instances.size()));
        for (int row = 0; row < int(manager.instances.size()); ++row) {
            const auto &value = manager.instances[size_t(row)];
            QStringList cells{text(value.name), value.host.empty() ? tr("Yes") : tr("No"),
                              text(manager.serverAddress(value.id)),
                              value.autoStart ? tr("Yes") : tr("No"),
                              text(manager.status(value.id))};
            for (int column = 0; column < cells.size(); ++column) {
                auto item = new QTableWidgetItem(cells[column]);
                if (!column)
                    item->setData(Qt::UserRole, text(value.id));
                item->setToolTip(cells[column]);
                table->setItem(row, column, item);
            }
            if (value.id == selected)
                table->selectRow(row);
        }
        snapshot = next;
    }
    updateButtons();
    summary->setText(!manager.error.empty() ? text(manager.error)
                     : manager.quitting     ? tr("Stopping players…")
                     : manager.scanning     ? tr("Scanning servers…")
                                            : tr("%1 players · %2 servers found")
                                              .arg(qulonglong(manager.instances.size()))
                                              .arg(qulonglong(manager.servers.size())));
    if (editor) {
        editor->setEnabled(!manager.quitting);
        editor->refreshServers();
    }
}
void SettingsWindow::openEditor(const Instance &instance) {
    if (manager.quitting)
        return;
    if (editor) {
        editor->raise();
        editor->activateWindow();
        return;
    }
    editor = new PlayerEditor(manager, instance, this);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    connect(editor, &QDialog::finished, this, [this] {
        editor = nullptr;
        refresh();
    });
    editor->open();
}
void SettingsWindow::openSettings(QScreen *screen) {
    if (!screen)
        screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (isMinimized())
        showNormal();
    if (screen) {
        winId();
        bool moved = windowHandle()->screen() != screen;
        windowHandle()->setScreen(screen);
        auto available = screen->availableGeometry();
        auto area = frameGeometry();
        if (!isVisible() || moved)
            area.moveCenter(available.center());
        int x = std::clamp(area.x(), available.left(),
                           std::max(available.left(), available.right() - area.width() + 1));
        int y = std::clamp(area.y(), available.top(),
                           std::max(available.top(), available.bottom() - area.height() + 1));
        move(x, y);
    }
    show();
    raise();
    activateWindow();
    if (editor) {
        editor->raise();
        editor->activateWindow();
    }
}
void SettingsWindow::closeEvent(QCloseEvent *event) {
    if (!QSystemTrayIcon::isSystemTrayAvailable() && quitRequested) {
        event->ignore();
        quitRequested();
    } else
        QWidget::closeEvent(event); // Hide only; the tray owns playback lifetime.
}
} // namespace tray
