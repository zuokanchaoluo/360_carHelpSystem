#ifndef MAINMANAGER_H
#define MAINMANAGER_H

#include <QWidget>

class VideoRecordPage;
class DrivingModePage;
class ReverseModePage;
class BirdViewPage;
class FeatureMemoryPage;
class SettingsPage;

namespace Ui {
class MainManager;
}

class MainManager : public QWidget
{
    Q_OBJECT

public:
    explicit MainManager(QWidget *parent = nullptr);
    ~MainManager();

    // 暴露切换页面的接口
    void onBtnVideoRecordClicked();
    void onBtnDrivingModeClicked();
    void onBtnSystemSettingsClicked();
    
    // 设置登录状态
    void setLoginState(bool isLoggedIn, const QString& username = "");

signals:
    void requestLogin();

private slots:
    void onLogoutClicked();
    void onAvatarClicked();
    void onToggleNavPanel();
    
    // 导航按钮
    void onNavButtonClicked();
    
    // 网络状态
    void onConnected();
    void onDisconnected();
    void onNetworkError(const QString& error);

private:
    Ui::MainManager *ui;
    bool m_navVisible;
    
    // 页面实例
    VideoRecordPage* m_videoRecordPage;
    DrivingModePage* m_drivingModePage;
    ReverseModePage* m_reverseModePage;
    BirdViewPage* m_birdViewPage;
    FeatureMemoryPage* m_featureMemoryPage;
    SettingsPage* m_settingsPage;

    void tryAutoConnect();
};

#endif // MAINMANAGER_H
