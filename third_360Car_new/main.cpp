#include "widget.h"
#include "startupwidget.h"
#include "mainmanager.h"
#include "loginwidget.h"
#include "registerwidget.h"
#include <QApplication>
#include <QFile>
#include <QStackedWidget>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // 创建主堆栈窗口
    QStackedWidget *mainStack = new QStackedWidget();
    mainStack->setWindowTitle("360智能行车辅助系统");
    mainStack->setMinimumSize(800, 500);

    // 创建各个组件
    LoginWidget *loginWidget = new LoginWidget();
    RegisterWidget *registerWidget = new RegisterWidget();
    MainManager *mainManager = new MainManager();

    // 添加到堆栈
    mainStack->addWidget(mainManager);    // Index 0: 主界面
    mainStack->addWidget(loginWidget);     // Index 1
    mainStack->addWidget(registerWidget);  // Index 2

    // 初始状态下不直接 show，由 StartupWidget 控制
    mainStack->setCurrentWidget(loginWidget);

    // 连接信号槽
    // 登录成功跳转主界面 -> 直接进入行车模式
    QObject::connect(loginWidget, &LoginWidget::loginSuccess, [mainStack, mainManager](uint32_t userId, const QString& nickname) {
        Q_UNUSED(userId);
        mainStack->setCurrentWidget(mainManager);
        mainManager->setLoginState(true, nickname);
        mainManager->onBtnDrivingModeClicked(); // 登录后进入行车模式
    });

    // 登录跳转注册
    QObject::connect(loginWidget, &LoginWidget::goToRegister, [mainStack, registerWidget]() {
        mainStack->setCurrentWidget(registerWidget);
    });

    // 注册返回登录
    QObject::connect(registerWidget, &RegisterWidget::goToLogin, [mainStack, loginWidget]() {
        mainStack->setCurrentWidget(loginWidget);
    });

    // 登录跳转网络设置
    QObject::connect(loginWidget, &LoginWidget::requestSettings, [mainStack, mainManager]() {
        mainStack->setCurrentWidget(mainManager);
        mainManager->onBtnSystemSettingsClicked();
    });

    // 主界面退出返回登录
    QObject::connect(mainManager, &MainManager::requestLogin, [mainStack, loginWidget]() {
        mainStack->setCurrentWidget(loginWidget);
    });

    // 创建并运行开机动画
    StartupWidget* startup = new StartupWidget("assets/intro.mp4");
    
    // 动画结束后显示窗口
    QObject::connect(startup, &StartupWidget::animationFinished, [mainStack, mainManager]() {
        mainStack->setCurrentWidget(mainManager);
        mainStack->show();
    });

    return a.exec();
}
