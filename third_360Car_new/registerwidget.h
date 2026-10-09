#ifndef REGISTERWIDGET_H
#define REGISTERWIDGET_H

#include <QWidget>

namespace Ui {
class RegisterWidget;
}

class QLineEdit;
class QTimer;

/**
 * @brief 注册界面
 */
class RegisterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RegisterWidget(QWidget *parent = nullptr);
    ~RegisterWidget();

signals:
    /// 注册成功
    void registerSuccess();
    
    /// 请求切换到登录页面
    void goToLogin();

private slots:
    void onRegisterClicked();
    void onGetSmsClicked();
    void onToLoginClicked();
    
    // 网络响应
    void onPacketReceived(uint16_t funcId, const QByteArray& data);
    void onNetworkError(const QString& error);

private:
    QString md5Hash(const QString& text);
    
    void setupPasswordToggle(QLineEdit* lineEdit);

private:
    Ui::RegisterWidget *ui;
    
    QTimer* m_smsTimer;
    int m_smsCountDown;
};

#endif // REGISTERWIDGET_H
