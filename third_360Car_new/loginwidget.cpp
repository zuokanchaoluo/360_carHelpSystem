#include "loginwidget.h"
#include "ui_loginwidget.h"
#include "socketconnect.h"
#include "protocol.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTextStream>

#include <QSettings>
#include <QDebug>

LoginWidget::LoginWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginWidget)
    , m_isConnecting(false)
    , m_loginTimer(new QTimer(this))
{
    ui->setupUi(this);
    
    // 连接UI信号
    connect(ui->btnLogin, &QPushButton::clicked, this, &LoginWidget::onLoginClicked);
    connect(ui->btnToRegister, &QPushButton::clicked, this, &LoginWidget::onToRegisterClicked);
    connect(ui->btnSettings, &QPushButton::clicked, this, &LoginWidget::requestSettings);
    
    // 设置密码框小眼睛
    setupPasswordToggle(ui->lineEditPassword);
    
    // 连接网络信号
    SocketConnect* socket = SocketConnect::instance();
    connect(socket, &SocketConnect::packetReceived, this, &LoginWidget::onPacketReceived);
    connect(socket, &SocketConnect::errorOccurred, this, &LoginWidget::onNetworkError);
    connect(socket, &SocketConnect::connected, this, &LoginWidget::onConnected);

    connect(socket, &SocketConnect::disconnected, this, &LoginWidget::onDisconnected);

    // 登录超时定时器 (5秒)
    m_loginTimer->setSingleShot(true);
    m_loginTimer->setInterval(5000);
    connect(m_loginTimer, &QTimer::timeout, this, &LoginWidget::onLoginTimeout);

    // 加载凭据
    loadCredentials();
}

LoginWidget::~LoginWidget()
{
    delete ui;
}

void LoginWidget::connectToServer()
{
    // 读取配置文件获取服务器地址
    QString ip = "";
    int port = 0;
    bool configExists = false;
    
    QFile configFile("config.ini");
    if (!configFile.exists()) {
        configFile.setFileName("config.txt");
    }
    if (configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            QStringList parts = line.split("=");
            if (parts.size() != 2) continue;
            
            QString key = parts[0].trimmed();
            QString value = parts[1].trimmed();
            
            if (key == "IP") {
                ip = value;
                configExists = true;
            } else if (key == "PORT") {
                port = value.toInt();
            }
        }
        configFile.close();
    }
    
    // 如果配置文件不存在、IP为空或端口无效，提示用户先设置
    if (!configExists || ip.isEmpty() || port <= 0) {
        ui->labelStatus->setText("首次运行，请先进行网络设置");
        ui->labelStatus->setStyleSheet("color: #FFD700; font-size: 12px;");
        return;
    }
    
    ui->labelStatus->setText("正在连接服务器...");
    m_isConnecting = true;
    
    // 使用异步连接，避免阻塞 UI
    SocketConnect::instance()->connectToServerAsync(ip, port);
}

QString LoginWidget::md5Hash(const QString& text)
{
    QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5);
    return hash.toHex();
}

void LoginWidget::onLoginClicked()
{
    QString username = ui->lineEditUsername->text().trimmed();
    QString password = ui->lineEditPassword->text();
    
    if (username.isEmpty()) {
        ui->labelStatus->setText("请输入账号");
        return;
    }
    if (password.isEmpty()) {
        ui->labelStatus->setText("请输入密码");
        return;
    }
    
    if (!SocketConnect::instance()->isConnected()) {
        ui->labelStatus->setText("未连接服务器，正在重连...");
        connectToServer();
        return;
    }
    
    // 构造登录请求
    LOGINREQ req;
    memset(&req, 0, sizeof(req));
    strncpy(req.username, username.toUtf8().constData(), sizeof(req.username) - 1);
    QString pwdMd5 = md5Hash(password);
    strncpy(req.pwd, pwdMd5.toUtf8().constData(), sizeof(req.pwd) - 1);
    
    ui->labelStatus->setText("正在登录...");
    ui->btnLogin->setEnabled(false);
    
    // 启动登录超时定时器
    m_loginTimer->start();
    
    SocketConnect::instance()->sendPacket(FID_LOGIN, &req, sizeof(req));
}

void LoginWidget::onToRegisterClicked()
{
    emit goToRegister();
    ui->labelStatus->clear();
}

void LoginWidget::onPacketReceived(uint16_t funcId, const QByteArray& data)
{
    if (funcId == FID_LOGIN) {
        m_loginTimer->stop();  // 收到响应，取消超时
        ui->btnLogin->setEnabled(true);
        if (data.size() >= (int)sizeof(LOGINRES)) {
            const LOGINRES* res = (const LOGINRES*)data.constData();
            if (res->resCode == SUCCESS) {
                ui->labelStatus->setStyleSheet("color: #00FF00; font-size: 12px;");
                ui->labelStatus->setText("登录成功！");
                
                // 保存用户ID到 SocketConnect
                SocketConnect::instance()->setUserId(res->userId);
                

                
                // 保存/清除凭据
                saveCredentials(ui->lineEditUsername->text(), ui->lineEditPassword->text(), ui->checkBoxRememberPwd->isChecked());

                emit loginSuccess(res->userId, QString::fromUtf8(res->nickname));
            } else if (res->resCode == ERR_NAME_OR_PWD_ERR) {
                ui->labelStatus->setText("用户名或密码错误");
            } else {
                ui->labelStatus->setText(QString("登录失败，错误码: %1").arg(res->resCode));
            }
        }
    }
}

void LoginWidget::onNetworkError(const QString& error)
{
    m_loginTimer->stop();  // 网络错误也取消超时
    ui->labelStatus->setText("网络错误: " + error);
    ui->btnLogin->setEnabled(true);
}

void LoginWidget::onLoginTimeout()
{
    ui->labelStatus->setStyleSheet("color: #FF6B6B; font-size: 12px;");
    ui->labelStatus->setText("登录超时，服务器无响应，请重试");
    ui->btnLogin->setEnabled(true);
    qDebug() << "[LoginWidget] 登录超时 (5秒无响应)";
}

void LoginWidget::onConnected()
{
    m_isConnecting = false;
    ui->labelStatus->setStyleSheet("color: #00FF00; font-size: 12px;");
    ui->labelStatus->setText("服务器连接成功");
}

void LoginWidget::onDisconnected()
{
    ui->labelStatus->setStyleSheet("color: #FF6B6B; font-size: 12px;");
    ui->labelStatus->setText("服务器连接断开");
}

void LoginWidget::setupPasswordToggle(QLineEdit* lineEdit)
{
    // 使用 radio_unchecked (空心) 表示隐藏/闭眼
    // 使用 radio_checked (实心) 表示显示/睁眼
    QAction* action = lineEdit->addAction(QIcon(":/images/radio_unchecked.png"), QLineEdit::TrailingPosition);
    
    // 连接触发信号
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

void LoginWidget::saveCredentials(const QString& username, const QString& password, bool remember)
{
    QSettings settings("config.ini", QSettings::IniFormat);
    if (remember) {
        settings.setValue("Login/username", username);
        // 简单Base64编码，避免明文存储
        settings.setValue("Login/password", password.toUtf8().toBase64());
        settings.setValue("Login/remember", true);
    } else {
        settings.remove("Login/username");
        settings.remove("Login/password");
        settings.setValue("Login/remember", false);
    }
}

void LoginWidget::loadCredentials()
{
    QSettings settings("config.ini", QSettings::IniFormat);
    bool remember = settings.value("Login/remember", false).toBool();
    ui->checkBoxRememberPwd->setChecked(remember);
    
    if (remember) {
        QString username = settings.value("Login/username").toString();
        // Base64解码
        QByteArray pwdData = settings.value("Login/password").toByteArray();
        QString password = QString::fromUtf8(QByteArray::fromBase64(pwdData));
        
        ui->lineEditUsername->setText(username);
        ui->lineEditPassword->setText(password);
    }
}
