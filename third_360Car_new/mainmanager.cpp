#include "mainmanager.h"
#include "ui_mainmanager.h"
#include "socketconnect.h"
#include "videorecordpage.h"
#include "drivingmodepage.h"
#include "reversemodepage.h"
#include "birdviewpage.h"
#include "featurememorypage.h"
#include "settingspage.h"
#include <QMessageBox>
#include <QDebug>
#include <QFile>
#include <QTextStream>

MainManager::MainManager(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MainManager),
    m_navVisible(true)
{
    ui->setupUi(this);
    
    // 初始化子页面
    m_videoRecordPage = new VideoRecordPage(this);
    m_drivingModePage = new DrivingModePage(this);
    m_reverseModePage = new ReverseModePage(this);
    m_birdViewPage = new BirdViewPage(this);
    m_featureMemoryPage = new FeatureMemoryPage(this);
    m_settingsPage = new SettingsPage(this);
    
    // 将子页面添加到 QStackedWidget
    while(ui->stackedWidget->count() > 0) {
        QWidget* w = ui->stackedWidget->widget(0);
        ui->stackedWidget->removeWidget(w);
    }
    
    ui->stackedWidget->addWidget(m_videoRecordPage);      // Index 0
    ui->stackedWidget->addWidget(m_drivingModePage);      // Index 1
    ui->stackedWidget->addWidget(m_reverseModePage);      // Index 2
    ui->stackedWidget->addWidget(m_birdViewPage);         // Index 3
    ui->stackedWidget->addWidget(m_featureMemoryPage);    // Index 4
    ui->stackedWidget->addWidget(m_settingsPage);         // Index 5
    
    // 连接主框架信号槽
    connect(ui->btnLogout, &QPushButton::clicked, this, &MainManager::onLogoutClicked);
    connect(ui->btnAvatar, &QPushButton::clicked, this, &MainManager::onAvatarClicked);
    connect(ui->btnToggleNav, &QPushButton::clicked, this, &MainManager::onToggleNavPanel);
    
    // 导航按钮连接
    connect(ui->btnVideoRecord, &QPushButton::clicked, this, &MainManager::onNavButtonClicked);
    connect(ui->btnDrivingMode, &QPushButton::clicked, this, &MainManager::onNavButtonClicked);
    connect(ui->btnReverseMode, &QPushButton::clicked, this, &MainManager::onNavButtonClicked);
    connect(ui->btnBirdView, &QPushButton::clicked, this, &MainManager::onNavButtonClicked);
    connect(ui->btnFeatureMemory, &QPushButton::clicked, this, &MainManager::onNavButtonClicked);
    connect(ui->btnSystemSettings, &QPushButton::clicked, this, &MainManager::onNavButtonClicked);
    
    // 连接网络状态信号
    SocketConnect* socket = SocketConnect::instance();
    connect(socket, &SocketConnect::connected, this, &MainManager::onConnected);
    connect(socket, &SocketConnect::disconnected, this, &MainManager::onDisconnected);
    connect(socket, &SocketConnect::errorOccurred, this, &MainManager::onNetworkError);
    
    // 首次进来默认配置
    ui->btnSystemSettings->setChecked(true);
    ui->stackedWidget->setCurrentIndex(5);
    
    // 初始化为未登录状态
    setLoginState(false);
    
    // 隐藏侧边栏切换按钮 (仅在行车模式显示)
    ui->btnToggleNav->setVisible(false);
    
    // 去除按钮边框
    ui->btnToggleNav->setStyleSheet("border: none; background: transparent;");
}

MainManager::~MainManager()
{
    delete ui;
}

void MainManager::onNavButtonClicked()
{
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    
    // 离开行车模式时自动停止
    int oldIndex = ui->stackedWidget->currentIndex();
    if (oldIndex == 1) {
        m_drivingModePage->stopDriving();
    }
    // 离开倒车模式时自动停止
    if (oldIndex == 2) {
        m_reverseModePage->stopReverse();
    }
    // 离开鸟瞰模式时自动停止
    if (oldIndex == 3) {
        m_birdViewPage->stopBirdView();
    }
    
    int newIndex = -1;
    if (btn == ui->btnVideoRecord) newIndex = 0;
    else if (btn == ui->btnDrivingMode) newIndex = 1;
    else if (btn == ui->btnReverseMode) newIndex = 2;
    else if (btn == ui->btnBirdView) newIndex = 3;
    else if (btn == ui->btnFeatureMemory) newIndex = 4;
    else if (btn == ui->btnSystemSettings) newIndex = 5;
    
    if (newIndex >= 0 && newIndex != oldIndex) {
        ui->stackedWidget->setCurrentIndex(newIndex);
        
        // 仅在行车模式 (Index 1) 或鸟瞰模式 (Index 3) 显示折叠按钮
        ui->btnToggleNav->setVisible(newIndex == 1 || newIndex == 3);
        
        // 进入行车模式时自动启动
        if (newIndex == 1) {
            m_drivingModePage->startDriving();
        }
        // 进入倒车模式时自动启动
        else if (newIndex == 2) {
            m_reverseModePage->startReverse();
        }
        // 进入鸟瞰模式时自动启动
        else if (newIndex == 3) {
            m_birdViewPage->startBirdView();
        }
        else {
             // 非行车/鸟瞰模式，确保导航栏显示
             if (!m_navVisible) {
                 onToggleNavPanel();
             }
        }
    }
}

void MainManager::onBtnVideoRecordClicked()
{
    // 离开行车模式
    if (ui->stackedWidget->currentIndex() == 1) {
        m_drivingModePage->stopDriving();
    }
    ui->btnVideoRecord->setChecked(true);
    ui->stackedWidget->setCurrentIndex(0);
}

void MainManager::onBtnDrivingModeClicked()
{
    // 离开当前页面（如果是行车模式本身则忽略）
    int oldIndex = ui->stackedWidget->currentIndex();
    if (oldIndex == 1) return; // 已经在行车模式
    
    ui->btnDrivingMode->setChecked(true);
    ui->stackedWidget->setCurrentIndex(1);
    m_drivingModePage->startDriving();
}

void MainManager::onBtnSystemSettingsClicked()
{
    int oldIndex = ui->stackedWidget->currentIndex();
    if (oldIndex == 1) {
        m_drivingModePage->stopDriving();
    } else if (oldIndex == 2) {
        m_reverseModePage->stopReverse();
    } else if (oldIndex == 3) {
        m_birdViewPage->stopBirdView();
    }

    ui->btnSystemSettings->setChecked(true);
    ui->stackedWidget->setCurrentIndex(5);
    ui->btnToggleNav->setVisible(false);
    if (!m_navVisible) {
        onToggleNavPanel();
    }
    m_settingsPage->loadConfig();
}

void MainManager::onLogoutClicked()
{
    // 弹出确认退出对话框
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认退出", "您确定要退出登录吗？",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::No) {
        return; // 用户选择取消，留在当前界面
    }

    // 离开行车模式
    if (ui->stackedWidget->currentIndex() == 1) {
        m_drivingModePage->stopDriving();
    }
    // 离开倒车模式
    if (ui->stackedWidget->currentIndex() == 2) {
        m_reverseModePage->stopReverse();
    }
    // 离开鸟瞰模式
    if (ui->stackedWidget->currentIndex() == 3) {
        m_birdViewPage->stopBirdView();
    }
    // 清除用户会话数据
    SocketConnect::instance()->setUserId(0);
    
    // 切换为未登录状态
    setLoginState(false);
    QMessageBox::information(this, "提示", "已退出登录");
}

void MainManager::onAvatarClicked()
{
    // 只有未登录时点击头像才触发登录请求
    if (ui->lblUsername->text() == "点击登录") {
        emit requestLogin();
    }
}

void MainManager::onToggleNavPanel()
{
    m_navVisible = !m_navVisible;
    ui->navPanel->setVisible(m_navVisible);
    

    
    if (m_navVisible) {
        ui->btnToggleNav->setText(QString::fromUtf8("<")); // 简单箭头
    } else {
        ui->btnToggleNav->setText(QString::fromUtf8(">")); // 简单箭头
    }
}

void MainManager::setLoginState(bool isLoggedIn, const QString& username)
{
    if (isLoggedIn) {
        ui->lblUsername->setText(username.isEmpty() ? "用户" : username);
        ui->btnLogout->setVisible(true);
        // 使用 images/登录.png 作为头像
        QPixmap avatar("images/登录.png");
        if (!avatar.isNull()) {
            ui->btnAvatar->setIcon(QIcon(avatar));
            ui->btnAvatar->setIconSize(ui->btnAvatar->size());
        }
        ui->btnAvatar->setStyleSheet("QPushButton { border-radius: 32px; background-color: #00BFFF; border: 2px solid white; }");
    } else {
        ui->lblUsername->setText("点击登录");
        ui->btnLogout->setVisible(false);
        // 使用 images/登录.png 作为头像 (未登录也可以显示这个通用头像，或者显示默认灰色)
         QPixmap avatar("images/登录.png");
        if (!avatar.isNull()) {
            ui->btnAvatar->setIcon(QIcon(avatar));
            ui->btnAvatar->setIconSize(ui->btnAvatar->size());
        }
        ui->btnAvatar->setStyleSheet("QPushButton { border-radius: 32px; background-color: #555; border: 2px solid white; } QPushButton:hover { border-color: #00BFFF; }");
    }
}

void MainManager::onConnected()
{
    QMessageBox::information(this, "连接成功", "服务器连接成功！");
}

void MainManager::onDisconnected()
{
    QMessageBox::warning(this, "连接断开", "服务器连接已断开");
}

void MainManager::onNetworkError(const QString& error)
{
    QMessageBox::warning(this, "网络错误", "连接失败：" + error);
}

void MainManager::tryAutoConnect()
{
    QString ip = "";
    int port = 0;
    
    QFile file("config.ini");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList parts = line.split("=");
        if (parts.size() == 2) {
            if (parts[0].trimmed() == "IP") ip = parts[1].trimmed();
            else if (parts[0].trimmed() == "PORT") port = parts[1].trimmed().toInt();
        }
    }
    file.close();
    
    if (!ip.isEmpty() && port > 0) {
        SocketConnect::instance()->connectToServerAsync(ip, port);
    }
}
