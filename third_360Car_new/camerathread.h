#ifndef CAMERATHREAD_H
#define CAMERATHREAD_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include "opencv2/opencv.hpp"

// 注册 cv::Mat 类型以支持跨线程信号传递
Q_DECLARE_METATYPE(cv::Mat)

class CameraThread : public QThread
{
    Q_OBJECT

public:
    static CameraThread* instance();
    
    // 打开/关闭摄像头
    void openCamera();

public slots:
    void closeCamera();

    // 录像控制
    void startRecording();
    void stopRecording();

    // 设置视频源 (为空则使用摄像头)
    void setVideoSource(const QString& path);

    // 分段录制控制 (鸟瞰模式专用)
    void startSegmentRecording(int frameCount);
    void stopSegmentRecording();

signals:
    void imageReady(const QImage& image);
    void rawFrameReady(const cv::Mat& frame);
    void segmentRecordingFinished(const QString& fileName, const QString& absPath);
    void errorOccurred(const QString& error);

protected:
    void run() override;


    
private:
    explicit CameraThread(QObject *parent = nullptr);
    ~CameraThread();

    static CameraThread* s_instance;
    static QMutex s_mutex;

    bool m_running;
    bool m_recording;
    cv::VideoCapture m_cap;
    QString m_videoPath;
    
    // 录像相关
    cv::VideoWriter m_writer;
    int m_frameCount;

    // 分段录像相关 (鸟瞰模式)
    bool m_segmentRecording;
    int m_segmentFrameCount;
    cv::VideoWriter m_segmentWriter;
    QString m_segmentFileName; // 当前录制文件名，用于上传服务器
};

#endif // CAMERATHREAD_H
