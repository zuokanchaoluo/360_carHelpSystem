#include "camerathread.h"
#include "socketconnect.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>

CameraThread* CameraThread::s_instance = nullptr;
QMutex CameraThread::s_mutex;

CameraThread* CameraThread::instance()
{
    if (!s_instance) {
        QMutexLocker locker(&s_mutex);
        if (!s_instance) {
            s_instance = new CameraThread();
        }
    }
    return s_instance;
}

CameraThread::CameraThread(QObject *parent)
    : QThread(parent)
    , m_running(false)
    , m_recording(false)
    , m_frameCount(0)
    , m_segmentRecording(false)
    , m_segmentFrameCount(0)
{
    // 注册 cv::Mat 类型
    qRegisterMetaType<cv::Mat>("cv::Mat");
}

CameraThread::~CameraThread()
{
    closeCamera();
}

void CameraThread::openCamera()
{
    if (isRunning()) return;
    m_running = true;
    start();
}

void CameraThread::closeCamera()
{
    m_running = false;
    if (m_recording) {
        stopRecording();
    }
    m_segmentRecording = false;
    
    if (isRunning()) {
        wait(3000);  // 最多等3秒
    }
    
    if (m_cap.isOpened()) {
        m_cap.release();
    }
}

void CameraThread::startRecording()
{
    m_recording = true;
}

void CameraThread::stopRecording()
{
    m_recording = false;
    if (m_writer.isOpened()) {
        m_writer.release();
    }
}

void CameraThread::setVideoSource(const QString& path)
{
    m_videoPath = path;
}

void CameraThread::startSegmentRecording(int frameCount)
{
    if (m_segmentRecording) return; // 正在录制中，忽略
    m_segmentFrameCount = frameCount;
    m_segmentRecording = true;
    qDebug() << "[CameraThread] 开始分段录制:" << frameCount << "帧";
}

void CameraThread::stopSegmentRecording()
{
    m_segmentRecording = false;
    if (m_segmentWriter.isOpened()) {
        m_segmentWriter.release();
        
        // 录制完成，通知服务器
        if (!m_segmentFileName.isEmpty()) {
            QString absPath = QDir::currentPath() + "/" + m_segmentFileName;
            SocketConnect::instance()->uploadVideoInfo(
                m_segmentFileName,  // 文件名
                absPath,            // 绝对路径
                ""                  // 封面路径 (暂无)
            );
            qDebug() << "[CameraThread] 已上传视频信息:" << m_segmentFileName;
            m_segmentFileName.clear();
        }
    }
    qDebug() << "[CameraThread] 停止分段录制";
}

void CameraThread::run()
{
    qDebug() << "[CameraThread] Thread started";

    // 打开摄像头或视频文件
    bool success = false;
    if (!m_videoPath.isEmpty()) {
        success = m_cap.open(m_videoPath.toStdString());
        if (!success) {
             qDebug() << "[CameraThread] Failed to open video file:" << m_videoPath;
        } else {
             qDebug() << "[CameraThread] Video file opened successfully:" << m_videoPath;
        }
    } 
    
    if (!success) {
        // 尝试打开摄像头
        if (!m_cap.open(0)) {
            emit errorOccurred("Failed to open camera 0");
            qDebug() << "[CameraThread] Failed to open camera";
            return;
        }
        qDebug() << "[CameraThread] Camera opened successfully";
    }

    cv::Mat frame;
    cv::Mat rgbFrame;

    while (m_running) {
        m_cap >> frame;
        
        // 视频播放结束，循环播放
        if (frame.empty() && !m_videoPath.isEmpty()) {
            m_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            continue;
        }
        
        if (frame.empty()) {
            qDebug() << "[CameraThread] Empty frame";
            continue;
        }

        // 录像逻辑
        if (m_recording) {
            if (!m_writer.isOpened()) {
                QString filename = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".avi";
                m_writer.open(filename.toStdString(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30, frame.size());
            }
            if (m_writer.isOpened()) {
                m_writer.write(frame);
            }
        } else {
             if (m_writer.isOpened()) m_writer.release();
        }

        // 分段录像逻辑 (鸟瞰模式自动录制)
        if (m_segmentRecording) {
            if (!m_segmentWriter.isOpened()) {
                // 使用日期+时间作为文件名
                QString filename = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".avi";
                m_segmentFileName = filename; // 保存文件名，供上传使用
                m_segmentWriter.open(filename.toStdString(),
                    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30, frame.size());
                qDebug() << "[CameraThread] 分段录像文件:" << filename;
            }
            if (m_segmentWriter.isOpened()) {
                m_segmentWriter.write(frame);
                m_segmentFrameCount--;
                if (m_segmentFrameCount <= 0) {
                    // 在工作线程中只关闭 writer, 不调用 uploadVideoInfo
                    m_segmentRecording = false;
                    m_segmentWriter.release();
                    qDebug() << "[CameraThread] 分段录制完成:" << m_segmentFileName;
                    // 通过信号通知主线程上传
                    if (!m_segmentFileName.isEmpty()) {
                        QString absPath = QDir::currentPath() + "/" + m_segmentFileName;
                        emit segmentRecordingFinished(m_segmentFileName, absPath);
                        m_segmentFileName.clear();
                    }
                }
            }
        }

        // 发送原始 BGR 帧给行车模式做后处理（车辆检测、水印等）
        emit rawFrameReady(frame.clone());

        // 转换为 QImage 用于显示
        cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
        
        QImage image((const uchar*)rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
        
        // 发送信号 (深拷贝)
        emit imageReady(image.copy());
        
        // 控制帧率 (~30 FPS)
        QThread::msleep(30);
    }
    
    m_cap.release();
    qDebug() << "[CameraThread] Thread stopped";
}
