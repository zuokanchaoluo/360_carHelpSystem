#include "registerwidget.h"
#include "ui_registerwidget.h"
#include "socketconnect.h"
#include "protocol.h"
#include <QCryptographicHash>
#include <QTimer>
#include <QDebug>

RegisterWidget::RegisterWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RegisterWidget)
{
    ui->setupUi(this);
    
    connect(ui->btnRegister, &QPushButton::clicked, this, &RegisterWidget::onRegisterClicked);
    connect(ui->btnGetSms, &QPushButton::clicked, this, &RegisterWidget::onGetSmsClicked);
    connect(ui->btnToLogin, &QPushButton::clicked, this, &RegisterWidget::onToLoginClicked);
    
    // 设置密码框小眼睛
    setupPasswordToggle(ui->lineEditPassword);
    setupPasswordToggle(ui->lineEditConfirmPwd);
    
    // 连接网络信号
    SocketConnect* socket = SocketConnect::instance();
    connect(socket, &SocketConnect::packetReceived, this, &RegisterWidget::onPacketReceived);
    connect(socket, &SocketConnect::errorOccurred, this, &RegisterWidget::onNetworkError);
    
    // 初始化短信倒计时定时器
    m_smsTimer = new QTimer(this);
    m_smsTimer->setInterval(1000); // 1秒
    connect(m_smsTimer, &QTimer::timeout, this, [this]() {
        m_smsCountDown--;
        if (m_smsCountDown <= 0) {
            m_smsTimer->stop();
            ui->btnGetSms->setEnabled(true);
            ui->btnGetSms->setText("获取验证码");
        } else {
            ui->btnGetSms->setText(QString("%1s").arg(m_smsCountDown));
        }
    });
}

RegisterWidget::~RegisterWidget()
{
    delete ui;
}

QString RegisterWidget::md5Hash(const QString& text)
{
    QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5);
    return hash.toHex();
}

void RegisterWidget::onRegisterClicked()
{
    QString phone = ui->lineEditPhone->text().trimmed();
    QString smsCode = ui->lineEditSmsCode->text().trimmed();
    QString nickname = ui->lineEditNickname->text().trimmed();
    QString password = ui->lineEditPassword->text();
    QString confirmPwd = ui->lineEditConfirmPwd->text();
    
    if (phone.length() != 11) {
        ui->labelStatus->setText("请输入正确的手机号");
        return;
    }
    if (smsCode.isEmpty()) {
        ui->labelStatus->setText("请输入验证码");
        return;
    }
    if (nickname.isEmpty()) {
        ui->labelStatus->setText("请输入昵称");
        return;
    }
    if (password.length() < 6) {
        ui->labelStatus->setText("密码长度至少6位");
        return;
    }
    if (password != confirmPwd) {
        ui->labelStatus->setText("两次密码输入不一致");
        return;
    }
    
    if (!SocketConnect::instance()->isConnected()) {
        ui->labelStatus->setText("未连接服务器");
        return;
    }
    
    // 构造注册请求
    REGISTERREQ req;
    memset(&req, 0, sizeof(req));
    strncpy(req.sms_code, smsCode.toUtf8().constData(), sizeof(req.sms_code) - 1);
    strncpy(req.username, phone.toUtf8().constData(), sizeof(req.username) - 1);
    strncpy(req.nickname, nickname.toUtf8().constData(), sizeof(req.nickname) - 1);
    QString pwdMd5 = md5Hash(password);
    strncpy(req.pwd, pwdMd5.toUtf8().constData(), sizeof(req.pwd) - 1);
    
    ui->labelStatus->setText("正在注册...");
    ui->btnRegister->setEnabled(false);
    
    SocketConnect::instance()->sendPacket(FID_REGISTER, &req, sizeof(req));
}

void RegisterWidget::onGetSmsClicked()
{
    QString phone = ui->lineEditPhone->text().trimmed();
    
    if (phone.length() != 11) {
        ui->labelStatus->setText("请输入正确的手机号");
        return;
    }
    
    if (!SocketConnect::instance()->isConnected()) {
        ui->labelStatus->setText("未连接服务器");
        return;
    }
    
    // 构造验证码请求
    SMSREQ req;
    memset(&req, 0, sizeof(req));
    strncpy(req.phone, phone.toUtf8().constData(), sizeof(req.phone) - 1);
    
    // 启动倒计时 (改为直接启动，不等待服务器响应，防止网络延迟导致无法冻结)
    m_smsCountDown = 60;
    m_smsTimer->start();
    ui->btnGetSms->setEnabled(false);
    ui->btnGetSms->setText(QString("%1s").arg(m_smsCountDown));
    
    SocketConnect::instance()->sendPacket(FID_GET_SMS, &req, sizeof(req));
}

void RegisterWidget::onToLoginClicked()
{
    emit goToLogin();
    ui->labelStatus->clear();
}

void RegisterWidget::onPacketReceived(uint16_t funcId, const QByteArray& data)
{
    switch (funcId) {
        case FID_REGISTER: {
            ui->btnRegister->setEnabled(true);
            if (data.size() >= (int)sizeof(BASERES)) {
                const BASERES* res = (const BASERES*)data.constData();
                if (res->resCode == SUCCESS) {
                    ui->labelStatus->setStyleSheet("color: #00FF00; font-size: 12px;");
                    ui->labelStatus->setText("注册成功，请登录");
                    // 自动切换到登录页
                    QTimer::singleShot(1500, this, &RegisterWidget::onToLoginClicked);
                    emit registerSuccess();
                } else if (res->resCode == ERR_USER_EXIST) {
                    ui->labelStatus->setText("该手机号已注册");
                } else if (res->resCode == ERR_CODE_ERR) {
                    ui->labelStatus->setText("验证码错误");
                } else {
                    ui->labelStatus->setText(QString("注册失败，错误码: %1").arg(res->resCode));
                }
            }
            break;
        }
        
        case FID_GET_SMS: {
            if (data.size() >= (int)sizeof(BASERES)) {
                // ...
                const BASERES* res = (const BASERES*)data.constData();
                if (res->resCode == SUCCESS) {
                    ui->labelStatus->setStyleSheet("color: #00FF00; font-size: 12px;");
                    ui->labelStatus->setText("验证码已发送");
                } else {
                    ui->labelStatus->setText("验证码发送失败");
                }
            }
            break;
        }
    }
}

void RegisterWidget::onNetworkError(const QString& error)
{
    ui->labelStatus->setText("网络错误: " + error);
    ui->btnRegister->setEnabled(true);
    // 网络错误时，停止倒计时并恢复按钮
    if (m_smsTimer->isActive()) {
        m_smsTimer->stop();
        ui->btnGetSms->setEnabled(true);
        ui->btnGetSms->setText("获取验证码");
    }
}

void RegisterWidget::setupPasswordToggle(QLineEdit* lineEdit)
{
    QAction* action = lineEdit->addAction(QIcon(":/images/radio_unchecked.png"), QLineEdit::TrailingPosition);
    
    connect(action, &QAction::triggered, [lineEdit, action](bool checked) {
        Q_UNUSED(checked);
        if (lineEdit->echoMode() == QLineEdit::Password) {
            lineEdit->setEchoMode(QLineEdit::Normal);
            action->setIcon(QIcon(":/images/eye.png"));
        } else {
            lineEdit->setEchoMode(QLineEdit::Password);
            action->setIcon(QIcon(":/images/radio_unchecked.png"));
        }
    });
}
