// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_ui.hpp"
#include "setting_help.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVariantMap>
#include <algorithm>

namespace tray {
QString text(const std::string &value) {
    return QString::fromUtf8(value.data(), int(value.size()));
}
QString pathText(const fs::path &path) { return text(path.u8string()); }
static std::string utf8(const QString &value) { return value.toUtf8().toStdString(); }

PlayerEditor::PlayerEditor(Manager &owner, Instance instance, QWidget *parent)
    : QDialog(parent), manager(owner), editing(std::move(instance)), manualHost(editing.host),
      manualPort(QString::number(editing.port)), manualWebPort(editing.webPort) {
    setObjectName("playerEditor");
    setWindowTitle(tr("Configure player"));
    setWindowModality(Qt::WindowModal);
    auto layout = new QVBoxLayout(this);
    auto form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    auto row = [&](const QString &label, QWidget *field, const char *helpText) {
        auto container = new QWidget(this);
        auto line = new QHBoxLayout(container);
        line->setContentsMargins(0, 0, 0, 0);
        auto info = new QToolButton(container);
        info->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
        info->setAutoRaise(true);
        info->setToolTip(text(helpText));
        info->setAccessibleName(label + tr(" information"));
        info->setAccessibleDescription(text(helpText));
        connect(info, &QToolButton::clicked, info, [info] {
            QToolTip::showText(info->mapToGlobal(QPoint(0, info->height())), info->toolTip(), info);
        });
        field->setToolTip(text(helpText));
        field->setAccessibleName(label);
        line->addWidget(field, 1);
        line->addWidget(info);
        form->addRow(label, container);
    };
    auto field = [&](const char *id, const char *label, const std::string &value,
                     const char *helpText) {
        auto input = new QLineEdit(text(value), this);
        input->setObjectName(id);
        row(tr(label), input, helpText);
        return input;
    };
    name = field("playerName", "Player name", editing.name, help::playerName);
    server = new QComboBox(this);
    server->setObjectName("serverChoice");
    row(tr("Server"), server, help::server);
    host = field("serverHost", "Host / IPv4 address", editing.host, help::host);
    port = field("serverPort", "SlimProto port", std::to_string(editing.port), help::port);
    mac = field("playerId", "Player ID (advanced)", editing.mac, help::playerId);
    web = field("webUrl", "Web URL (optional)", editing.webURL, help::webUrl);
    web->setPlaceholderText("http://server:9000");
    autoStart = new QCheckBox(tr("Start this instance with the app"), this);
    autoStart->setObjectName("startWithApp");
    autoStart->setChecked(editing.autoStart);
    row(tr("Startup"), autoStart, help::startWithApp);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName("savePlayer");
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &PlayerEditor::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refreshServers();
    previousMode = 0;
    server->setCurrentIndex(editing.host.empty() ? 0 : 1);
    connect(server, &QComboBox::currentIndexChanged, this, [this] { chooseServer(); });
    chooseServer();
    resize(580, sizeHint().height());
}

void PlayerEditor::refreshServers() {
    QStringList values;
    for (const auto &found : manager.servers)
        values << text(found.name) << text(found.host) << QString::number(found.port)
               << QString::number(found.webPort);
    auto snapshot = values.join(QChar(0));
    if (server->count() && snapshot == serverSnapshot) {
        if (server->currentIndex() == 0)
            chooseServer();
        return;
    }
    serverSnapshot = snapshot;
    const QSignalBlocker blocked(server);
    int selected = server->currentIndex();
    QVariantMap chosen = server->currentData().toMap();
    server->clear();
    server->addItem(tr("Automatic — first server to reply"));
    server->addItem(tr("Manual address"));
    int restored = selected < 2 ? std::max(0, selected) : 1;
    for (const auto &found : manager.servers) {
        QVariantMap data{
            {"host", text(found.host)}, {"port", found.port}, {"webPort", found.webPort}};
        server->addItem(
            text(found.name + " (" + found.host + ":" + std::to_string(found.port) + ")"), data);
        if (selected >= 2 && data.value("host") == chosen.value("host") &&
            data.value("port") == chosen.value("port"))
            restored = server->count() - 1;
    }
    server->setCurrentIndex(restored);
    // A disappearing discovery result becomes a manual address without losing the selection.
    if (selected >= 2 && restored == 1) {
        manualHost = utf8(chosen.value("host").toString());
        manualPort = chosen.value("port").toString();
        manualWebPort = chosen.value("webPort").toUInt();
    }
    chooseServer();
}

void PlayerEditor::chooseServer() {
    int choice = server->currentIndex();
    if (previousMode == 1 && choice != 1) {
        manualHost = utf8(host->text());
        manualPort = port->text();
        manualWebPort = editing.webPort;
    }
    if (choice == 0) {
        auto found = manager.automaticServer(editing.id);
        host->setText(text(found.host));
        host->setPlaceholderText(tr("Discovering server…"));
        port->setText(QString::number(found.port));
        editing.webPort = found.webPort;
    } else if (choice == 1) {
        if (previousMode != 1) {
            host->setText(text(manualHost));
            port->setText(manualPort);
            editing.webPort = manualWebPort;
        }
        host->setPlaceholderText(tr("Hostname or IPv4 address"));
    } else {
        auto found = server->currentData().toMap();
        host->setText(found.value("host").toString());
        port->setText(found.value("port").toString());
        editing.webPort = found.value("webPort").toUInt();
    }
    host->setReadOnly(choice != 1);
    port->setReadOnly(choice != 1);
    previousMode = choice;
}

Instance PlayerEditor::configuration() const {
    auto value = editing;
    value.name = utf8(name->text());
    value.host = server->currentIndex() == 0 ? "" : utf8(host->text());
    value.port = parsePort(utf8(port->text()));
    value.mac = utf8(mac->text());
    value.webURL = utf8(web->text());
    value.autoStart = autoStart->isChecked();
    validate(value);
    return value;
}

void PlayerEditor::accept() {
    try {
        auto value = configuration();
        if (manager.find(value.id) && value.mac != editing.mac &&
            QMessageBox::question(
                this, tr("Change player identity?"),
                tr("Changing the Player ID creates a different player in Lyrion. Continue?"),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        manager.save(value);
        QDialog::accept();
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Cannot save player"), text(e.what()));
    }
}
} // namespace tray
