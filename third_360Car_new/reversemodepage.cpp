#include "reversemodepage.h"
#include "ui_reversemodepage.h"
#include "camerathread.h"
#include "socketconnect.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

ReverseModePage::ReverseModePage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ReverseModePage),
    m_isReversing(false),
    m_isRecording(false),
    m_frameCount(0)
{
    ui->setupUi(this);

    // lblDistance 悬浮在视频之上
    ui->lblDistance->raise();
    ui->lblDistance->setVisible(false);

    // 自动录制定时器 (每10秒)
    m_recordTimer = new QTimer(this);
    m_recordTimer->setInterval(10000);
    connect(m_recordTimer, &QTimer::timeout, this, &ReverseModePage::onStartAutoRecord);
}

ReverseModePage::~ReverseModePage()
{
    stopReverse();
    delete ui;
}

void ReverseModePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // lblDisplay 铺满整个页面
    ui->lblDisplay->setGeometry(0, 0, width(), height());

    // lblDistance 居中悬浮
    int labelW = 350;
    int labelH = 60;
    ui->lblDistance->setGeometry(
        (width() - labelW) / 2,
        (height() - labelH) / 2,
        labelW, labelH
    );
}

void ReverseModePage::startReverse()
{
    if (m_isReversing) return;
    m_isReversing = true;

    qDebug() << "[ReverseMode] 进入倒车模式";

    // 先关闭当前可能正在运行的摄像头/视频（其他页面可能在用）
    CameraThread::instance()->closeCamera();

    // 使用车库视频2作为视频源（相对路径，与其他页面一致）
    CameraThread::instance()->setVideoSource("assets/车库视频2.mp4");

    // 连接信号
    connect(CameraThread::instance(), &CameraThread::rawFrameReady,
            this, &ReverseModePage::onRawFrameReady, Qt::QueuedConnection);

    // 启动摄像头/视频
    CameraThread::instance()->openCamera();

    // 启动自动录制
    m_recordTimer->start();
    onStartAutoRecord();
}

void ReverseModePage::stopReverse()
{
    if (!m_isReversing) return;
    m_isReversing = false;

    qDebug() << "[ReverseMode] 退出倒车模式";

    // 停止录制定时器
    m_recordTimer->stop();

    // 停止当前录制
    if (m_writer.isOpened()) {
        m_writer.release();
        qDebug() << "[ReverseMode] 录制停止，已录" << m_frameCount << "帧";
    }
    m_isRecording = false;
    m_frameCount = 0;

    // 断开信号
    disconnect(CameraThread::instance(), &CameraThread::rawFrameReady,
               this, &ReverseModePage::onRawFrameReady);

    // 关闭视频
    CameraThread::instance()->closeCamera();
}

void ReverseModePage::onStartAutoRecord()
{
    if (!m_isReversing) return;
    if (m_isRecording) return;  // 上一段还没录完

    // 创建视频保存目录
    QString videosDir = QCoreApplication::applicationDirPath() + "/videos";
    QDir dir(videosDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 以日期+时间命名
    QString filename = videosDir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".avi";
    m_currentVideoFile = filename;

    qDebug() << "[ReverseMode] 开始录制:" << filename;

    m_writer.open(filename.toStdString(), cv::CAP_FFMPEG,
                  cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                  30, cv::Size(640, 480));

    if (m_writer.isOpened()) {
        m_isRecording = true;
        m_frameCount = 0;
    } else {
        qDebug() << "[ReverseMode] 无法打开视频写入器:" << filename;
    }
}

void ReverseModePage::onRawFrameReady(const cv::Mat& frame)
{
    if (!m_isReversing || frame.empty()) return;

    cv::Mat displayFrame = frame.clone();
    int W = displayFrame.cols;
    int H = displayFrame.rows;

    // =========================================================
    // 1. 定义三色区域的 Y 坐标分界线
    // =========================================================
    int yRedTop    = (int)(H * 0.85);
    int yYellowTop = (int)(H * 0.65);
    int yGreenTop  = (int)(H * 0.45);

    auto calcX = [&](int y, bool isLeft) -> int {
        double ratio = (double)(H - y) / (double)(H - yGreenTop);
        double halfW = W * 0.40 * (1.0 - ratio * 0.6);
        int centerX = W / 2;
        return isLeft ? (int)(centerX - halfW) : (int)(centerX + halfW);
    };

    // =========================================================
    // 2. 绘制三色辅助线（三个梯形轮廓）
    // =========================================================
    cv::line(displayFrame, cv::Point(calcX(H-1, true), H-1), cv::Point(calcX(yRedTop, true), yRedTop), cv::Scalar(0, 0, 255), 2);
    cv::line(displayFrame, cv::Point(calcX(H-1, false), H-1), cv::Point(calcX(yRedTop, false), yRedTop), cv::Scalar(0, 0, 255), 2);
    cv::line(displayFrame, cv::Point(calcX(H-1, true), H-1), cv::Point(calcX(H-1, false), H-1), cv::Scalar(0, 0, 255), 2);
    cv::line(displayFrame, cv::Point(calcX(yRedTop, true), yRedTop), cv::Point(calcX(yRedTop, false), yRedTop), cv::Scalar(0, 0, 255), 2);

    cv::line(displayFrame, cv::Point(calcX(yRedTop, true), yRedTop), cv::Point(calcX(yYellowTop, true), yYellowTop), cv::Scalar(0, 255, 255), 2);
    cv::line(displayFrame, cv::Point(calcX(yRedTop, false), yRedTop), cv::Point(calcX(yYellowTop, false), yYellowTop), cv::Scalar(0, 255, 255), 2);
    cv::line(displayFrame, cv::Point(calcX(yYellowTop, true), yYellowTop), cv::Point(calcX(yYellowTop, false), yYellowTop), cv::Scalar(0, 255, 255), 2);

    cv::line(displayFrame, cv::Point(calcX(yYellowTop, true), yYellowTop), cv::Point(calcX(yGreenTop, true), yGreenTop), cv::Scalar(0, 255, 0), 2);
    cv::line(displayFrame, cv::Point(calcX(yYellowTop, false), yYellowTop), cv::Point(calcX(yGreenTop, false), yGreenTop), cv::Scalar(0, 255, 0), 2);
    cv::line(displayFrame, cv::Point(calcX(yGreenTop, true), yGreenTop), cv::Point(calcX(yGreenTop, false), yGreenTop), cv::Scalar(0, 255, 0), 2);

    // =========================================================
    // 3. 障碍物检测
    // =========================================================
    cv::Mat gray, blurred, edges;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);
    cv::Canny(blurred, edges, 50, 150);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::dilate(edges, edges, kernel);

    cv::Mat guideMask = cv::Mat::zeros(H, W, CV_8UC1);
    std::vector<cv::Point> guideArea;
    guideArea.push_back(cv::Point(calcX(H - 1, true), H - 1));
    guideArea.push_back(cv::Point(calcX(H - 1, false), H - 1));
    guideArea.push_back(cv::Point(calcX(yGreenTop, false), yGreenTop));
    guideArea.push_back(cv::Point(calcX(yGreenTop, true), yGreenTop));
    std::vector<std::vector<cv::Point>> guidePoly = {guideArea};
    cv::fillPoly(guideMask, guidePoly, cv::Scalar(255));

    cv::Mat maskedEdges;
    cv::bitwise_and(edges, guideMask, maskedEdges);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(maskedEdges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // =========================================================
    // 4. 判断障碍物所在区域
    // =========================================================
    int closestY = 0;
    bool obstacleDetected = false;

    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < 800) continue;

        cv::Rect rect = cv::boundingRect(contour);
        int cx = rect.x + rect.width / 2;
        int bottomY = rect.y + rect.height;
        int leftBound = calcX(bottomY, true);
        int rightBound = calcX(bottomY, false);

        if (cx < leftBound || cx > rightBound) continue;
        if (bottomY < yGreenTop) continue;

        obstacleDetected = true;
        if (bottomY > closestY) closestY = bottomY;
    }

    // =========================================================
    // 5. 自动录制（每段 300 帧）
    // =========================================================
    if (m_isRecording && m_writer.isOpened()) {
        cv::Mat writeFrame;
        if (displayFrame.cols != 640 || displayFrame.rows != 480) {
            cv::resize(displayFrame, writeFrame, cv::Size(640, 480));
        } else {
            writeFrame = displayFrame;
        }
        m_writer.write(writeFrame);
        m_frameCount++;

        if (m_frameCount >= 300) {
            m_writer.release();
            m_isRecording = false;
            m_frameCount = 0;
            QFileInfo fi(m_currentVideoFile);
            qDebug() << "[ReverseMode] 录制完成 (300帧), 文件:" << fi.fileName() << "大小:" << fi.size() << "字节";

            // 上传视频信息到服务器
            SocketConnect::instance()->uploadVideoInfo(
                fi.fileName(),
                m_currentVideoFile,
                ""
            );
        }
    }

    // =========================================================
    // 6. 显示到 UI
    // =========================================================
    cv::Mat rgb;
    cv::cvtColor(displayFrame, rgb, cv::COLOR_BGR2RGB);
    QImage result((const uchar*)rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);

    ui->lblDisplay->setPixmap(QPixmap::fromImage(result).scaled(
        ui->lblDisplay->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // =========================================================
    // 7. 根据障碍物区域更新警告
    // =========================================================
    if (obstacleDetected && closestY >= yYellowTop) {
        if (closestY >= yRedTop) {
            ui->lblDistance->setText("⚠ WARNING ⚠");
            ui->lblDistance->setStyleSheet(
                "color: #FF0000; font-size: 36px; font-weight: bold; "
                "background-color: rgba(0, 0, 0, 200); border-radius: 10px; padding: 10px;");
        } else {
            ui->lblDistance->setText("WARNING");
            ui->lblDistance->setStyleSheet(
                "color: #FFD700; font-size: 32px; font-weight: bold; "
                "background-color: rgba(0, 0, 0, 180); border-radius: 10px; padding: 10px;");
        }
        ui->lblDistance->setVisible(true);
    } else {
        ui->lblDistance->setVisible(false);
    }
}
