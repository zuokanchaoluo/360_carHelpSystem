#include "settingspage.h"
#include "ui_settingspage.h"
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QDebug>
#include "socketconnect.h"

SettingsPage::SettingsPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsPage)
{
    ui->setupUi(this);
    
    connect(ui->btnSave, &QPushButton::clicked, this, &SettingsPage::onSaveClicked);
    
    // 连接 Socket 信号
    connect(SocketConnect::instance(), &SocketConnect::connected, this, &SettingsPage::onConnected);
    connect(SocketConnect::instance(), &SocketConnect::errorOccurred, this, &SettingsPage::onConnectionError);
    
    loadConfig();
}

SettingsPage::~SettingsPage()
{
    delete ui;
}

void SettingsPage::onSaveClicked()
{
    saveConfig();
}

void SettingsPage::loadConfig()
{
    QFile file(m_configFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Config file not found, using defaults.";
        return;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList parts = line.split("=");
        if (parts.size() != 2) continue;

        QString key = parts[0].trimmed();
        QString value = parts[1].trimmed();

        if (key == "IP") {
            QStringList ipParts = value.split(".");
            if (ipParts.size() == 4) {
                ui->editIP1->setText(ipParts[0]);
                ui->editIP2->setText(ipParts[1]);
                ui->editIP3->setText(ipParts[2]);
                ui->editIP4->setText(ipParts[3]);
            }
        } else if (key == "PORT") {
            ui->editPort->setText(value);
        } else if (key == "STORAGE") {
            ui->editStorageSize->setText(value);
        } else if (key == "AUTODELETE") {
            ui->chkAutoDelete->setChecked(value == "1");
        }
    }
    file.close();
}

void SettingsPage::saveConfig()
{
    QString ip = QString("%1.%2.%3.%4")
        .arg(ui->editIP1->text())
        .arg(ui->editIP2->text())
        .arg(ui->editIP3->text())
        .arg(ui->editIP4->text());
    QString port = ui->editPort->text();
    QString storage = ui->editStorageSize->text();
    QString autoDelete = ui->chkAutoDelete->isChecked() ? "1" : "0";

    QFile file(m_configFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法保存配置文件！");
        return;
    }

    QTextStream out(&file);
    out << "IP=" << ip << "\n";
    out << "PORT=" << port << "\n";
    out << "STORAGE=" << storage << "\n";
    out << "AUTODELETE=" << autoDelete << "\n";
    file.close();

    // 禁用保存按钮，防止重复点击
    ui->btnSave->setEnabled(false);
    ui->btnSave->setText("正在测试连接...");
    
    // 断开旧连接并尝试连接新地址
    SocketConnect::instance()->disconnectFromServer();
    SocketConnect::instance()->connectToServerAsync(ip, port.toUShort());
}

void SettingsPage::onConnected()
{
    // 如果页面不可见，忽略（可能是登录页的连接请求）
    if (!this->isVisible()) return;
    
    ui->btnSave->setEnabled(true);
    ui->btnSave->setText("保存设置");
    
    QMessageBox::information(this, "连接成功", "配置保存成功，且服务器连接测试通过！");
    emit settingsSaved();
}

void SettingsPage::onConnectionError(const QString& error)
{
    if (!this->isVisible()) return;

    ui->btnSave->setEnabled(true);
    ui->btnSave->setText("保存设置");
    
    QMessageBox::warning(this, "连接失败", "配置已保存，但无法连接服务器。\n错误信息: " + error);
    // 即使连接失败，也发送保存成功信号，因为配置确实保存了
    emit settingsSaved();
}
