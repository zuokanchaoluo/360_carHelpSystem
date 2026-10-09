#include "syssetting.h"
#include "ui_syssetting.h"
#include "socketconnect.h"
#include <QMessageBox>
#include <QDebug>
#include <QFile>
#include <QTextStream>

SysSetting::SysSetting(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SysSetting)
{
    ui->setupUi(this);
    
    // 连接信号槽
    connect(ui->btnLogout, &QPushButton::clicked, this, &SysSetting::onLogoutClicked);
    connect(ui->btnSave, &QPushButton::clicked, this, &SysSetting::onSaveSettings);
    
    // 导航按钮连接
    connect(ui->btnVideoRecord, &QPushButton::clicked, this, &SysSetting::onBtnVideoRecordClicked);
    connect(ui->btnDrivingMode, &QPushButton::clicked, this, &SysSetting::onBtnDrivingModeClicked);
    connect(ui->btnReverseMode, &QPushButton::clicked, this, &SysSetting::onBtnReverseModeClicked);
    connect(ui->btnBirdView, &QPushButton::clicked, this, &SysSetting::onBtnBirdViewClicked);
    connect(ui->btnFeatureMemory, &QPushButton::clicked, this, &SysSetting::onBtnFeatureMemoryClicked);
    connect(ui->btnSystemSettings, &QPushButton::clicked, this, &SysSetting::onBtnSystemSettingsClicked);
    
    // 连接网络状态信号
    SocketConnect* socket = SocketConnect::instance();
    connect(socket, &SocketConnect::connected, this, &SysSetting::onConnected);
    connect(socket, &SocketConnect::disconnected, this, &SysSetting::onDisconnected);
    connect(socket, &SocketConnect::errorOccurred, this, &SysSetting::onNetworkError);
    
    // 加载配置
    loadConfig();
}

SysSetting::~SysSetting()
{
    delete ui;
}

// Navigation button handlers
void SysSetting::onBtnVideoRecordClicked()
{
    ui->stackedWidget->setCurrentIndex(0);
}

void SysSetting::onBtnDrivingModeClicked()
{
    ui->stackedWidget->setCurrentIndex(1);
}

void SysSetting::onBtnReverseModeClicked()
{
    ui->stackedWidget->setCurrentIndex(2);
}

void SysSetting::onBtnBirdViewClicked()
{
    ui->stackedWidget->setCurrentIndex(3);
}

void SysSetting::onBtnFeatureMemoryClicked()
{
    ui->stackedWidget->setCurrentIndex(4);
}

void SysSetting::onBtnSystemSettingsClicked()
{
    ui->stackedWidget->setCurrentIndex(5);
}

void SysSetting::onLogoutClicked()
{
    // 点击头像/退出按钮，跳转到登录页面
    emit requestLogin();
}

void SysSetting::onSaveSettings()
{
    saveConfig();
}

void SysSetting::loadConfig()
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

void SysSetting::saveConfig()
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

    //写到文件中
    QTextStream out(&file);
    out << "IP=" << ip << "\n";
    out << "PORT=" << port << "\n";
    out << "STORAGE=" << storage << "\n";
    out << "AUTODELETE=" << autoDelete << "\n";
    file.close();

    QMessageBox::information(this, "保存成功", "系统设置已保存！\n配置文件：" + m_configFile);
    
    // 发射信号通知其他组件
    emit settingsSaved();
}

void SysSetting::tryAutoConnect()
{
    // 读取配置文件，如果有效则自动连接
    QString ip = "";
    int port = 0;
    
    QFile file(m_configFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "No config file, waiting for user to configure.";
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
            ip = value;
        } else if (key == "PORT") {
            port = value.toInt();
        }
    }
    file.close();
    
    // 如果配置有效，异步连接（不阻塞 UI）
    if (!ip.isEmpty() && port > 0) {
        qDebug() << "Auto-connecting to" << ip << ":" << port;
        SocketConnect::instance()->connectToServerAsync(ip, port);
    }
}

void SysSetting::onConnected()
{
    QMessageBox::information(this, "连接成功", "服务器连接成功！\n点击头像进入登录/注册页面");
}

void SysSetting::onDisconnected()
{
    QMessageBox::warning(this, "连接断开", "服务器连接已断开");
}

void SysSetting::onNetworkError(const QString& error)
{
    QMessageBox::warning(this, "网络错误", "连接失败：" + error);
}

