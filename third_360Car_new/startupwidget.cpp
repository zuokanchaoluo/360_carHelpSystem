#include "startupwidget.h"
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QDebug>

StartupWidget::StartupWidget(const QString &videoPath, QWidget *parent)
    : QWidget(parent)
    , m_frameTimer(new QTimer(this))
    , m_fadeAnimation(nullptr)
    , m_isFinishing(false)
{
    // 设置窗口属性：全屏、无边框、置顶
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_DeleteOnClose);
    
    // 获取主屏幕尺寸并设置全屏
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        setGeometry(screen->geometry());
    }
    showFullScreen();
    
    // 设置背景色为黑色（视频加载前）
    setStyleSheet("background-color: black;");
    
    // 初始化 OpenCV 视频捕获
    std::string path = videoPath.toStdString();
    if (!m_videoCapture.open(path)) {
        qWarning() << "Failed to open video file:" << videoPath;
        // 如果视频打开失败，直接触发完成
        QTimer::singleShot(100, this, &StartupWidget::onFadeOutFinished);
        return;
    }
    
    // 获取视频帧率，如果无法获取则使用默认 30 FPS
    double fps = m_videoCapture.get(cv::CAP_PROP_FPS);
    if (fps <= 0 || fps > 120) {
        fps = 30.0;
    }
    int interval = static_cast<int>(1000.0 / fps);
    
    // 连接定时器信号
    connect(m_frameTimer, &QTimer::timeout, this, &StartupWidget::updateFrame);
    
    // 启动帧刷新定时器
    m_frameTimer->start(interval);
    
    qDebug() << "StartupWidget initialized with video:" << videoPath 
             << "FPS:" << fps << "Interval:" << interval << "ms";
}

StartupWidget::~StartupWidget()
{
    if (m_frameTimer) {
        m_frameTimer->stop();
    }
    if (m_videoCapture.isOpened()) {
        m_videoCapture.release();
    }
}

void StartupWidget::updateFrame()
{
    if (m_isFinishing) {
        return;
    }
    
    cv::Mat frame;
    if (!m_videoCapture.read(frame)) {
        // 视频播放结束，启动淡出动画
        m_frameTimer->stop();
        startFadeOut();
        return;
    }
    
    // 转换并保存当前帧
    m_currentFrame = matToQImage(frame);
    
    // 触发重绘
    update();
}

void StartupWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    if (m_currentFrame.isNull()) {
        // 如果没有帧，绘制黑色背景
        painter.fillRect(rect(), Qt::black);
        return;
    }
    
    // 计算绘制区域：使用 KeepAspectRatioByExpanding 模式铺满屏幕
    QSize widgetSize = size();
    QSize imageSize = m_currentFrame.size();
    
    // 计算缩放比例（取较大值以确保铺满）
    qreal scaleX = static_cast<qreal>(widgetSize.width()) / imageSize.width();
    qreal scaleY = static_cast<qreal>(widgetSize.height()) / imageSize.height();
    qreal scale = qMax(scaleX, scaleY);
    
    // 计算缩放后的尺寸
    int scaledWidth = static_cast<int>(imageSize.width() * scale);
    int scaledHeight = static_cast<int>(imageSize.height() * scale);
    
    // 计算居中偏移（超出部分会被裁剪）
    int x = (widgetSize.width() - scaledWidth) / 2;
    int y = (widgetSize.height() - scaledHeight) / 2;
    
    // 绘制缩放后的图像
    QRect targetRect(x, y, scaledWidth, scaledHeight);
    painter.drawImage(targetRect, m_currentFrame);
}

void StartupWidget::keyPressEvent(QKeyEvent *event)
{
    // 按 ESC 键跳过动画
    if (event->key() == Qt::Key_Escape) {
        if (!m_isFinishing) {
            m_frameTimer->stop();
            startFadeOut();
        }
    }
    QWidget::keyPressEvent(event);
}

void StartupWidget::startFadeOut()
{
    if (m_isFinishing) {
        return;
    }
    m_isFinishing = true;
    
    qDebug() << "Starting fade out animation...";
    
    // 创建淡出动画
    m_fadeAnimation = new QPropertyAnimation(this, "windowOpacity", this);
    m_fadeAnimation->setDuration(1000);  // 1秒淡出
    m_fadeAnimation->setStartValue(1.0);
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutQuad);  // 缓出效果
    
    // 连接动画结束信号
    connect(m_fadeAnimation, &QPropertyAnimation::finished, 
            this, &StartupWidget::onFadeOutFinished);
    
    // 启动动画
    m_fadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void StartupWidget::onFadeOutFinished()
{
    qDebug() << "Fade out animation finished, emitting signal...";
    
    // 发射完成信号
    emit animationFinished();
    
    // 关闭窗口
    close();
}

QImage StartupWidget::matToQImage(const cv::Mat &mat)
{
    if (mat.empty()) {
        return QImage();
    }
    
    // OpenCV 默认是 BGR 格式，需要转换为 RGB
    cv::Mat rgbMat;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, rgbMat, cv::COLOR_BGR2RGB);
    } else if (mat.channels() == 4) {
        cv::cvtColor(mat, rgbMat, cv::COLOR_BGRA2RGBA);
    } else {
        // 灰度图
        rgbMat = mat.clone();
    }
    
    // 创建 QImage（深拷贝数据）
    QImage image(rgbMat.data, 
                 rgbMat.cols, 
                 rgbMat.rows, 
                 static_cast<int>(rgbMat.step),
                 rgbMat.channels() == 1 ? QImage::Format_Grayscale8 :
                 rgbMat.channels() == 3 ? QImage::Format_RGB888 :
                                          QImage::Format_RGBA8888);
    
    // 返回深拷贝，避免 Mat 销毁后数据失效
    return image.copy();
}
