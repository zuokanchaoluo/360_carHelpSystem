#include "birdviewpage.h"
#include "ui_birdviewpage.h"
#include "camerathread.h"
#include "socketconnect.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

BirdViewPage::BirdViewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::BirdViewPage),
    m_isRunning(false),
    m_timer(new QTimer(this)),
    m_isRecording(false),
    m_recFrameCount(0)
{
    ui->setupUi(this);
    
    m_capSide.open("assets/cartest.mp4");
    if (!m_capSide.isOpened()) {
        qWarning() << "[BirdViewPage] Failed to open assets/cartest.mp4";
    }
    
    m_capRear.open("assets/车库视频.mp4");
    if (!m_capRear.isOpened()) {
        qWarning() << "[BirdViewPage] Failed to open assets/车库视频.mp4";
    }
    
    connect(m_timer, &QTimer::timeout, this, &BirdViewPage::onUpdateFrames);

    // 自动录制定时器 (每10秒)
    m_recordTimer = new QTimer(this);
    m_recordTimer->setInterval(10000);
    connect(m_recordTimer, &QTimer::timeout, this, &BirdViewPage::onStartAutoRecord);
}

BirdViewPage::~BirdViewPage()
{
    stopBirdView();
    if (m_capSide.isOpened()) m_capSide.release();
    if (m_capRear.isOpened()) m_capRear.release();
    delete ui;
}

void BirdViewPage::startBirdView()
{
    if (m_isRunning) return;
    qDebug() << "[BirdViewPage] 鸟瞰模式: 启动";
    m_isRunning = true;
    
    connect(CameraThread::instance(), &CameraThread::rawFrameReady, this, &BirdViewPage::onCameraFrame);
    CameraThread::instance()->openCamera();
    
    m_timer->start(33); // ~30 fps

    // 启动自动录制
    m_recordTimer->start();
    onStartAutoRecord();
}

void BirdViewPage::stopBirdView()
{
    if (!m_isRunning) return;
    qDebug() << "[BirdViewPage] 鸟瞰模式: 停止";
    m_isRunning = false;
    m_timer->stop();

    // 停止录制
    m_recordTimer->stop();
    if (m_recWriter.isOpened()) {
        m_recWriter.release();
        qDebug() << "[BirdViewPage] 录制停止，已录" << m_recFrameCount << "帧";
    }
    m_isRecording = false;
    m_recFrameCount = 0;
    
    disconnect(CameraThread::instance(), &CameraThread::rawFrameReady, this, &BirdViewPage::onCameraFrame);
}

void BirdViewPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    startBirdView();
}

void BirdViewPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    stopBirdView();
}

void BirdViewPage::onCameraFrame(const cv::Mat& frame)
{
    if (frame.empty()) return;
    frame.copyTo(m_latestCameraFrame);
}

void BirdViewPage::onStartAutoRecord()
{
    if (!m_isRunning) return;
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

    qDebug() << "[BirdViewPage] 开始录制:" << filename;

    m_recWriter.open(filename.toStdString(), cv::CAP_FFMPEG,
                     cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                     30, cv::Size(600, 600));

    if (m_recWriter.isOpened()) {
        m_isRecording = true;
        m_recFrameCount = 0;
    } else {
        qDebug() << "[BirdViewPage] 无法打开视频写入器:" << filename;
    }
}

void BirdViewPage::onUpdateFrames()
{
    if (!m_isRunning) return;
    
    cv::Mat frameSide, frameRear;
    if (m_capSide.isOpened()) {
        m_capSide >> frameSide;
        if (frameSide.empty()) {
            m_capSide.set(cv::CAP_PROP_POS_FRAMES, 0);
            m_capSide >> frameSide;
        }
    }
    
    if (m_capRear.isOpened()) {
        m_capRear >> frameRear;
        if (frameRear.empty()) {
            m_capRear.set(cv::CAP_PROP_POS_FRAMES, 0);
            m_capRear >> frameRear;
        }
    }
    
    cv::Mat birdView = cv::Mat::zeros(600, 600, CV_8UC3);
    
    std::vector<cv::Point2f> tl_o = {{0, 0}};
    std::vector<cv::Point2f> bl_o = {{0, 600}};
    std::vector<cv::Point2f> tr_o = {{600, 0}};
    std::vector<cv::Point2f> br_o = {{600, 600}};
    
    std::vector<cv::Point2f> tl_i = {{200, 150}};
    std::vector<cv::Point2f> bl_i = {{200, 450}};
    std::vector<cv::Point2f> tr_i = {{400, 150}};
    std::vector<cv::Point2f> br_i = {{400, 450}};
    
    // 临时使用 cartest.mp4 (frameSide) 代替前摄像头的实时画面
    if (!frameSide.empty()) {
        cv::Mat camFrame;
        frameSide.copyTo(camFrame); // 将 frameSide 复制为前向画面使用
        
        QImage camImg = matToQImage(camFrame);
        ui->lblRealTime->setPixmap(QPixmap::fromImage(camImg).scaled(ui->lblRealTime->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        
        float W = camFrame.cols, H = camFrame.rows;
        std::vector<cv::Point2f> srcCorners = {{0, 0}, {W, 0}, {W, H}, {0, H}};
        std::vector<cv::Point2f> dstCorners = {tr_o[0], tl_o[0], tl_i[0], tr_i[0]}; // 前方（上方）梯形区域
        drawTrapezoid(birdView, camFrame, srcCorners, dstCorners);
    }
    
    if (!frameRear.empty()) {
        float W = frameRear.cols, H = frameRear.rows;
        std::vector<cv::Point2f> srcCorners = {{0, 0}, {W, 0}, {W, H}, {0, H}};
        std::vector<cv::Point2f> dstCorners = {br_o[0], bl_o[0], bl_i[0], br_i[0]};
        drawTrapezoid(birdView, frameRear, srcCorners, dstCorners);
    }
    
    if (!frameSide.empty()) {
        float W = frameSide.cols, H = frameSide.rows;
        std::vector<cv::Point2f> srcCorners = {{0, 0}, {W, 0}, {W, H}, {0, H}};
        std::vector<cv::Point2f> dstCorners1 = {tr_o[0], br_o[0], br_i[0], tr_i[0]};
        drawTrapezoid(birdView, frameSide, srcCorners, dstCorners1);
        
        std::vector<cv::Point2f> dstCorners2 = {tl_o[0], bl_o[0], bl_i[0], tl_i[0]};
        drawTrapezoid(birdView, frameSide, srcCorners, dstCorners2);
    }
    
    cv::Mat carImg = cv::imread("images/car.jpg");
    if (!carImg.empty()) {
        cv::resize(carImg, carImg, cv::Size(200, 300));
        cv::Rect roi(200, 150, 200, 300);
        carImg.copyTo(birdView(roi));
    } else {
        // 回退逻辑，找不到图片就画方块
        cv::rectangle(birdView, cv::Rect(200, 150, 200, 300), cv::Scalar(100,100,100), cv::FILLED);
        cv::putText(birdView, "CAR", cv::Point(245, 315), cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(255,255,255), 3);
    }

    // 自动录制（每段 300 帧）
    if (m_isRecording && m_recWriter.isOpened()) {
        cv::Mat writeFrame;
        if (birdView.cols != 600 || birdView.rows != 600) {
            cv::resize(birdView, writeFrame, cv::Size(600, 600));
        } else {
            writeFrame = birdView;
        }
        m_recWriter.write(writeFrame);
        m_recFrameCount++;

        if (m_recFrameCount >= 300) {
            m_recWriter.release();
            m_isRecording = false;
            m_recFrameCount = 0;
            QFileInfo fi(m_currentVideoFile);
            qDebug() << "[BirdViewPage] 录制完成 (300帧), 文件:" << fi.fileName() << "大小:" << fi.size() << "字节";

            // 上传视频信息到服务器
            SocketConnect::instance()->uploadVideoInfo(
                fi.fileName(),
                m_currentVideoFile,
                ""
            );
        }
    }
    
    // 缓存鸟瞰画面
    m_latestBirdView = birdView.clone();

    QImage birdImg = matToQImage(birdView);
    ui->lblBirdView->setPixmap(QPixmap::fromImage(birdImg).scaled(ui->lblBirdView->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

void BirdViewPage::drawTrapezoid(cv::Mat& dest, const cv::Mat& src, const std::vector<cv::Point2f>& srcPts, const std::vector<cv::Point2f>& dstPts)
{
    cv::Mat M = cv::getPerspectiveTransform(srcPts, dstPts);
    cv::Mat warped;
    cv::warpPerspective(src, warped, M, dest.size());
    
    cv::Mat mask = cv::Mat::zeros(dest.size(), CV_8UC1);
    std::vector<cv::Point> ptsMask;
    for (const auto& p : dstPts) {
        ptsMask.push_back(cv::Point(p.x, p.y));
    }
    cv::fillConvexPoly(mask, ptsMask, cv::Scalar(255));
    
    warped.copyTo(dest, mask);
}

QImage BirdViewPage::matToQImage(const cv::Mat& mat)
{
    if(mat.type() == CV_8UC3) {
        const uchar *pSrc = (const uchar*)mat.data;
        QImage image(pSrc, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return image.rgbSwapped();
    } else if(mat.type() == CV_8UC1) {
        const uchar *pSrc = (const uchar*)mat.data;
        QImage image(pSrc, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
        return image;
    } else if(mat.type() == CV_8UC4) {
        const uchar *pSrc = (const uchar*)mat.data;
        QImage image(pSrc, mat.cols, mat.rows, mat.step, QImage::Format_ARGB32);
        return image;
    }
    return QImage();
}
