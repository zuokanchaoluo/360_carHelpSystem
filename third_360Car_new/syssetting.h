#ifndef SYSSETTING_H
#define SYSSETTING_H

#include <QWidget>

namespace Ui {
class SysSetting;
}

/**
 * @brief 系统设置界面类
 * 
 * 左侧导航栏 + 右侧堆栈窗口布局
 * 包含：视频记录、行车模式、倒车模式、鸟瞰模式、特征记忆、系统设置 等页面
 */
class SysSetting : public QWidget
{
    Q_OBJECT

public:
    explicit SysSetting(QWidget *parent = nullptr);
    ~SysSetting();

signals:
    /// 设置已保存信号（用于通知其他组件重新连接）
    void settingsSaved();
    
    /// 请求跳转到登录页面（点击头像触发）
    void requestLogin();

public slots:
    /// 尝试自动连接服务器（启动时调用）
    void tryAutoConnect();

    /// 导航按钮点击
    void onBtnVideoRecordClicked();
    void onBtnDrivingModeClicked();
    void onBtnReverseModeClicked();
    void onBtnBirdViewClicked();
    void onBtnFeatureMemoryClicked();
    void onBtnSystemSettingsClicked();

private slots:
    /// 退出按钮点击
    void onLogoutClicked();
    
    /// 保存设置
    void onSaveSettings();
    
    /// 加载配置文件
    void loadConfig();
    
    /// 保存配置文件
    void saveConfig();
    
    /// 网络状态响应
    void onConnected();
    void onDisconnected();
    void onNetworkError(const QString& error);

private:
    Ui::SysSetting *ui;
    const QString m_configFile = "config.txt";
};

#endif // SYSSETTING_H
