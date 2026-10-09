#include "videoplayer.h"
#include "socketconnect.h"
#include <QDebug>
#include <QCloseEvent>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QStyle>
#include <QApplication> // Added for screenshot
#include <QDateTime>    // Added for screenshot
#include <QtConcurrent> // Added for background upload

#include "socketconnect.h" // Already present, but listed in instruction

VideoPlayer::VideoPlayer(QWidget *parent)
    : QWidget(parent)
    , m_videoId(0)
    , m_lastSeekSeconds(0)
    , m_totalDuration(0)
    , m_fps(30.0)
    , m_totalFrames(0)
    , m_currentFrame(0)
    , m_isPlaying(false)
    , m_sliderPressed(false)
    , m_speedFactor(1.0)
    , m_isUploading(false)
{
    initUI();

    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &VideoPlayer::onTimerTick);
}

VideoPlayer::~VideoPlayer()
{
    if (m_cap.isOpened()) {
        m_cap.release();
    }
}

void VideoPlayer::initUI()
{
    setWindowTitle("视频播放器");
    setMinimumSize(800, 600);
    setStyleSheet("background-color: #1A1A2E; color: white;");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // 视频显示区
    m_videoLabel = new QLabel(this);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setStyleSheet("background-color: black; border: 1px solid #444; border-radius: 4px;");
    m_videoLabel->setMinimumHeight(400);
    m_videoLabel->setText("等待播放...");
    mainLayout->addWidget(m_videoLabel, 1);

    // 进度条 (支持点击跳转)
    m_progressSlider = new QSlider(Qt::Horizontal, this);
    m_progressSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #333; height: 8px; border-radius: 4px; }"
        "QSlider::handle:horizontal { background: #00BFFF; width: 16px; margin: -4px 0; border-radius: 8px; }"
        "QSlider::sub-page:horizontal { background: #00BFFF; border-radius: 4px; }"
    );
    connect(m_progressSlider, &QSlider::sliderPressed, this, &VideoPlayer::onSliderPressed);
    connect(m_progressSlider, &QSlider::sliderReleased, this, &VideoPlayer::onSliderReleased);
    // 安装事件过滤器，实现点击跳转
    m_progressSlider->installEventFilter(this);
    mainLayout->addWidget(m_progressSlider);

    // 时间 & 帧数信息行
    QHBoxLayout *infoLayout = new QHBoxLayout();
    m_lblCurrentTime = new QLabel("已播放 00:00", this);
    m_lblCurrentTime->setStyleSheet("color: #00BFFF; font-size: 13px;");
    m_lblCurrentFrame = new QLabel("", this);
    m_lblCurrentFrame->setStyleSheet("color: #888; font-size: 12px;");
    m_lblTotalTime = new QLabel("总时长 00:00", this);
    m_lblTotalTime->setStyleSheet("color: #AAA; font-size: 13px;");
    infoLayout->addWidget(m_lblCurrentTime);
    infoLayout->addStretch();
    infoLayout->addWidget(m_lblCurrentFrame);
    infoLayout->addStretch();
    infoLayout->addWidget(m_lblTotalTime);
    mainLayout->addLayout(infoLayout);

    // 控制栏
    QHBoxLayout *controlLayout = new QHBoxLayout();
    controlLayout->setSpacing(12);

    // 单个播放/暂停切换按钮
    m_btnPlayPause = new QPushButton(this);
    m_btnPlayPause->setFixedSize(48, 48);
    m_btnPlayPause->setIcon(QIcon(":/images/视频回放/播放.png"));
    m_btnPlayPause->setIconSize(QSize(32, 32));
    m_btnPlayPause->setStyleSheet(
        "QPushButton { background-color: #00BFFF; border-radius: 24px; border: none; }"
        "QPushButton:hover { background-color: #1E90FF; }"
    );
    connect(m_btnPlayPause, &QPushButton::clicked, this, &VideoPlayer::onPlayPauseClicked);

    // 倍速选择
    QLabel *lblSpeed = new QLabel("倍速:", this);
    lblSpeed->setStyleSheet("color: #AAA; font-size: 13px;");
    m_cmbSpeed = new QComboBox(this);
    m_cmbSpeed->addItem("0.5x", 0.5);
    m_cmbSpeed->addItem("1.0x", 1.0);
    m_cmbSpeed->addItem("1.5x", 1.5);
    m_cmbSpeed->addItem("2.0x", 2.0);
    m_cmbSpeed->setCurrentIndex(1); // 默认 1.0x
    m_cmbSpeed->setStyleSheet(
        "QComboBox { background-color: #333; color: white; border: 1px solid #555; border-radius: 4px; padding: 4px 8px; font-size: 13px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #333; color: white; selection-background-color: #00BFFF; }"
    );
    connect(m_cmbSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VideoPlayer::onSpeedChanged);

    controlLayout->addWidget(m_btnPlayPause);

    // 截图按钮
    m_btnScreenshot = new QPushButton("📷 截图上传", this);
    m_btnScreenshot->setFixedHeight(36);
    m_btnScreenshot->setStyleSheet(
        "QPushButton { background-color: #FF6347; color: white; border-radius: 6px; padding: 0 12px; font-size: 13px; border: none; }"
        "QPushButton:hover { background-color: #FF4500; }"
        "QPushButton:disabled { background-color: #555; }"
    );
    connect(m_btnScreenshot, &QPushButton::clicked, this, &VideoPlayer::onScreenshotClicked);
    controlLayout->addWidget(m_btnScreenshot);

    controlLayout->addStretch();
    controlLayout->addWidget(lblSpeed);
    controlLayout->addWidget(m_cmbSpeed);
    mainLayout->addLayout(controlLayout);
}

void VideoPlayer::setVideo(const QString& filePath, uint32_t videoId,
                           uint32_t lastSeekSeconds, uint32_t totalDuration)
{
    m_filePath = filePath;
    m_videoId = videoId;
    m_lastSeekSeconds = lastSeekSeconds;
    m_totalDuration = totalDuration;

    // 打开视频 (如果是相对路径，先根据应用程序目录拼接绝对路径)
    if (m_cap.isOpened()) m_cap.release();
    
    QString absPath = filePath;
    if (!QFileInfo(filePath).isAbsolute()) {
        absPath = QCoreApplication::applicationDirPath() + "/" + filePath;
    }
    
    // 检查文件是否存在
    QFileInfo fileInfo(absPath);
    if (!fileInfo.exists()) {
        QMessageBox::warning(this, "错误", "视频文件不存在:\n" + absPath);
        return;
    }
    qDebug() << "[VideoPlayer] 文件存在, 大小:" << fileInfo.size() << "字节, 路径:" << absPath;
    
    // 尝试多种后端打开
    bool opened = m_cap.open(absPath.toStdString(), cv::CAP_FFMPEG);
    if (!opened) {
        qDebug() << "[VideoPlayer] FFMPEG后端失败, 尝试默认后端...";
        opened = m_cap.open(absPath.toStdString(), cv::CAP_ANY);
    }
    if (!opened) {
        QMessageBox::warning(this, "错误", "无法打开视频文件:\n" + absPath + 
                             "\n\n文件大小: " + QString::number(fileInfo.size()) + " 字节");
        return;
    }

    m_fps = m_cap.get(cv::CAP_PROP_FPS);
    if (m_fps <= 0) m_fps = 30.0;
    m_totalFrames = (int)m_cap.get(cv::CAP_PROP_FRAME_COUNT);

    // 设置进度条范围
    m_progressSlider->setRange(0, m_totalFrames);

    // 计算总时长
    int totalSec = m_totalFrames / (int)m_fps;
    m_lblTotalTime->setText("总时长 " + formatTime(totalSec));

    // 断点续播：跳转到上次位置
    if (m_lastSeekSeconds > 0) {
        int seekFrame = (int)(m_lastSeekSeconds * m_fps);
        if (seekFrame < m_totalFrames) {
            m_cap.set(cv::CAP_PROP_POS_FRAMES, seekFrame);
            m_currentFrame = seekFrame;
            m_progressSlider->setValue(seekFrame);
            m_lblCurrentTime->setText("已播放 " + formatTime(m_lastSeekSeconds));
            qDebug() << "[VideoPlayer] 断点续播: 跳转到" << m_lastSeekSeconds << "秒 (帧" << seekFrame << ")";
        }
    }

    setWindowTitle("视频播放器 - " + QFileInfo(filePath).fileName());

    // 自动开始播放
    onPlayPauseClicked();
}

void VideoPlayer::onPlayPauseClicked()
{
    if (!m_cap.isOpened()) return;

    if (m_isPlaying) {
        // 暂停
        m_playTimer->stop();
        m_isPlaying = false;
        m_btnPlayPause->setIcon(QIcon(":/images/视频回放/播放.png"));
        uploadProgress();
    } else {
        // 如果已播放到末尾，重置到开头
        if (m_currentFrame >= m_totalFrames - 1) {
            m_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            m_currentFrame = 0;
            m_progressSlider->setValue(0);
            m_lblCurrentTime->setText("已播放 00:00");
        }
        // 播放
        int interval = (int)(1000.0 / (m_fps * m_speedFactor));
        m_playTimer->start(interval);
        m_isPlaying = true;
        m_btnPlayPause->setIcon(QIcon(":/images/视频回放/暂停.png"));
    }
}

// 事件过滤: 让 slider 支持点击跳转
bool VideoPlayer::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_progressSlider && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            int value = QStyle::sliderValueFromPosition(
                m_progressSlider->minimum(), m_progressSlider->maximum(),
                mouseEvent->pos().x(), m_progressSlider->width());
            m_progressSlider->setValue(value);
            // 跳转视频
            if (m_cap.isOpened()) {
                m_cap.set(cv::CAP_PROP_POS_FRAMES, value);
                m_currentFrame = value;
                int currentSec = m_currentFrame / (int)m_fps;
                m_lblCurrentTime->setText("已播放 " + formatTime(currentSec));
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void VideoPlayer::onTimerTick()
{
    if (!m_cap.isOpened()) return;

    cv::Mat frame;
    m_cap >> frame;

    if (frame.empty()) {
        // 播放完毕
        m_playTimer->stop();
        m_isPlaying = false;
        m_btnPlayPause->setIcon(QIcon(":/images/视频回放/播放.png"));
        uploadProgress();
        return;
    }

    m_currentFrame = (int)m_cap.get(cv::CAP_PROP_POS_FRAMES);
    m_currentMat = frame.clone();  // 保存当前帧用于截图

    // 转换并显示
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    m_videoLabel->setPixmap(QPixmap::fromImage(img).scaled(
        m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // 更新进度条 (仅在用户未拖拽时)
    if (!m_sliderPressed) {
        m_progressSlider->setValue(m_currentFrame);
    }

    // 更新时间和帧数显示
    int currentSec = m_currentFrame / (int)m_fps;
    m_lblCurrentTime->setText("已播放 " + formatTime(currentSec));
    m_lblCurrentFrame->setText(QString("%1/%2帧").arg(m_currentFrame).arg(m_totalFrames));
}

void VideoPlayer::onSliderPressed()
{
    m_sliderPressed = true;
}

void VideoPlayer::onSliderReleased()
{
    m_sliderPressed = false;
    int targetFrame = m_progressSlider->value();
    m_cap.set(cv::CAP_PROP_POS_FRAMES, targetFrame);
    m_currentFrame = targetFrame;
}

void VideoPlayer::onSpeedChanged(int index)
{
    m_speedFactor = m_cmbSpeed->itemData(index).toDouble();
    if (m_isPlaying) {
        int interval = (int)(1000.0 / (m_fps * m_speedFactor));
        m_playTimer->setInterval(interval);
    }
    qDebug() << "[VideoPlayer] 倍速切换:" << m_speedFactor << "x";
}

void VideoPlayer::closeEvent(QCloseEvent *event)
{
    m_playTimer->stop();
    m_isPlaying = false;
    uploadProgress();
    emit playerClosed();
    event->accept();
}

void VideoPlayer::uploadProgress()
{
    if (m_videoId == 0) return;
    
    int currentSec = m_currentFrame / (int)m_fps;
    int totalSec = m_totalFrames / (int)m_fps;
    
    // 总时长不足1秒的视频，至少报1秒
    if (m_totalFrames > 0 && totalSec == 0) {
        totalSec = 1;
    }
    // 如果已播放到末尾（基于帧数判断），标记为看完
    if (m_totalFrames > 0 && m_currentFrame >= m_totalFrames - 1) {
        currentSec = totalSec;
    }
    
    SocketConnect::instance()->uploadVideoRecord(
        SocketConnect::instance()->getUserId(),
        m_videoId,
        currentSec,
        totalSec
    );
    qDebug() << "[VideoPlayer] 上传播放进度: 当前" << currentSec << "秒 / 总" << totalSec << "秒";
}

QString VideoPlayer::formatTime(int seconds)
{
    int min = seconds / 60;
    int sec = seconds % 60;
    return QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

void VideoPlayer::onScreenshotClicked()
{
    // 检查是否有当前帧
    if (m_currentMat.empty()) {
        QMessageBox::warning(this, "提示", "当前没有可截取的画面\n请先播放视频");
        return;
    }

    // 防止重入
    if (m_isUploading) {
        qDebug() << "[VideoPlayer] 上一张图片正在上传中，请稍候...";
        return;
    }
    
    // 创建截图保存目录
    QString screenshotsDir = QCoreApplication::applicationDirPath() + "/screenshots";
    QDir dir(screenshotsDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // 生成文件名: feature_时间戳.jpg
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QString("feature_%1.jpg").arg(timestamp);
    QString fullPath = screenshotsDir + "/" + fileName;
    
    // 保存截图
    bool saved = cv::imwrite(fullPath.toStdString(), m_currentMat);
    if (!saved) {
        QMessageBox::critical(this, "失败", "截图保存失败");
        return;
    }
    
    qDebug() << "[VideoPlayer] 截图已保存:" << fullPath;
    
    // 上传到服务器
    if (!SocketConnect::instance()->isConnected()) {
        QMessageBox::warning(this, "提示", "截图已保存到本地\n但未连接服务器，无法上传");
        return;
    }

    m_isUploading = true;
    m_btnScreenshot->setText("上传中...");
    m_btnScreenshot->setEnabled(false);

    // 获取必要参数，供后台线程使用
    uint32_t userId = SocketConnect::instance()->getUserId();
    
    // 使用 QtConcurrent 在后台线程执行耗时上传任务
    QtConcurrent::run([=](){
        // 读取文件数据用于切片上传
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly)) {
            // 恢复 UI 状态 (需通过 invokeMethod 或信号，这里简化处理，直接在 lambda 结束时不处理 UI 异常)
            qDebug() << "[VideoPlayer] 后台线程读取文件失败:" << fullPath;
            return;
        }
        QByteArray fileData = file.readAll();
        file.close();
        
        // 计算切片数
        int sliceSize = 1024;
        int totalSlices = (fileData.size() + sliceSize - 1) / sliceSize;
        uint32_t uploadId = (uint32_t)QDateTime::currentMSecsSinceEpoch();  // 唯一上传ID
        
        qDebug() << "[VideoPlayer] 开始切片上传: 文件=" << fileName 
                 << ", 大小=" << fileData.size() 
                 << ", 切片数=" << totalSlices;
        
        // ===== 第1步: 发送 IMGREQ 头部包 (FID_IMG_UPLOAD = 2001) =====
        IMGREQ req;
        memset(&req, 0, sizeof(req));
        req.user_id = userId;
        strncpy(req.fileName, fileName.toUtf8().constData(), sizeof(req.fileName) - 1);
        strncpy(req.filePath, fullPath.toUtf8().constData(), sizeof(req.filePath) - 1);
        QByteArray timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toUtf8();
        strncpy(req.createTime, timeStr.constData(), sizeof(req.createTime) - 1);
        req.photoSlices = totalSlices;
        req.uploadId = uploadId;
        
        SocketConnect::instance()->sendPacket(FID_IMG_UPLOAD, &req, sizeof(req));
    
    // 缓存文件数据，供服务器要求重传时使用
    SocketConnect::instance()->cacheUploadData(uploadId, fileData, totalSlices);
    
    // 【关键新增】发送头包后延时，确保服务器有时间创建任务，防止“找不到上传任务ID”
    QThread::msleep(200);

    // ===== 第2步: 逐片发送 PHOTOSLICE (FID_IMG_UPLOAD_SLICE = 2002) =====
        for (int i = 0; i < totalSlices; i++) {
            PHOTOSLICE slice;
            memset(&slice, 0, sizeof(slice));
            
            int offset = i * sliceSize;
            int remaining = fileData.size() - offset;
            int currentSize = qMin(remaining, sliceSize);
            
            slice.isLastSlice = (i == totalSlices - 1) ? 1 : 0;
            slice.sliceIndex = i;
            slice.sliceSize = currentSize;
            slice.uploadId = uploadId;
            memcpy(slice.buf, fileData.constData() + offset, currentSize);
            
            // 模拟丢包：跳过第 2 和 第 5 个切片 (如果存在的话)
            if (i == 1 || i == 4) {
                qDebug() << "[VideoPlayer] 模拟丢包: 故意不发送 sliceIndex=" << i;
            } else {
                SocketConnect::instance()->sendPacket(FID_IMG_UPLOAD_SLICE, &slice, sizeof(slice));
            }

            // 【关键修复】添加延时，防止发送过快导致服务器粘包
            // 从 50ms 改为 5ms，解决大文件(1080p图片)上传超时被截断的问题
            QThread::msleep(5);
        }
        
        qDebug() << "[VideoPlayer] 切片上传完成: " << totalSlices << "片";
        
        // 恢复 UI 状态 (使用 QTimer::singleShot 跨线程更新 UI，兼容 Qt 5.9)
        QTimer::singleShot(0, this, [=](){
            m_isUploading = false;
            m_btnScreenshot->setText("📷 截图上传");
            m_btnScreenshot->setEnabled(true);
            // 提示成功 (可选，用 log 代替弹窗以免打扰播放)
            qDebug() << "[VideoPlayer] 截图上传成功提示已触发";
        });
    });
}
