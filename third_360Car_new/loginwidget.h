#ifndef LOGINWIDGET_H
#define LOGINWIDGET_H

#include <QWidget>
#include <QTimer>

namespace Ui {
class LoginWidget;
}

class QLineEdit;

/**
 * @brief 登录界面
 */
class LoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWidget(QWidget *parent = nullptr);
    ~LoginWidget();

signals:
    /// 登录成功，传递用户ID和昵称
    void loginSuccess(uint32_t userId, const QString& nickname);
    
    /// 请求切换到注册页面
    void goToRegister();
    
    /// 请求切换到设置页面
    void requestSettings();

private slots:
    void onLoginClicked();
    void onToRegisterClicked();
    
    // 网络响应
    void onPacketReceived(uint16_t funcId, const QByteArray& data);
    void onNetworkError(const QString& error);
    void onConnected();
    void onDisconnected();
    void onLoginTimeout();

public slots:
    /// 连接到服务器（可从外部调用以触发重连）
    void connectToServer();

private:
    QString md5Hash(const QString& text);

    void setupPasswordToggle(QLineEdit* lineEdit);
    
    // 凭据管理
    void saveCredentials(const QString& username, const QString& password, bool remember);
    void loadCredentials();

private:
    Ui::LoginWidget *ui;
    
    // 连接状态
    bool m_isConnecting;
    
    // 登录超时定时器
    QTimer *m_loginTimer;
};

#endif // LOGINWIDGET_H
