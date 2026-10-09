#ifndef DRIVINGMODEPAGE_H
#define DRIVINGMODEPAGE_H

#include <QWidget>
#include <QTimer>
#include "opencv2/opencv.hpp"
#include <atomic>

namespace Ui {
class DrivingModePage;
}

class DrivingModePage : public QWidget
{
    Q_OBJECT

public:
    explicit DrivingModePage(QWidget *parent = nullptr);
    ~DrivingModePage();

    // 进入/退出行车模式
    void startDriving();
    void stopDriving();

private slots:
    void onRawFrameReady(const cv::Mat& frame);
    void onStartAutoRecord();

private:
    Ui::DrivingModePage *ui;
    
    // 车辆检测
    cv::CascadeClassifier m_carClassifier;
    int m_detectFrameCount;            // 检测帧计数器
    std::vector<cv::Rect> m_detectedRects; // 缓存检测结果 (车辆)
    
    struct TrafficLight {
        cv::Rect rect;
        QString color; // "Red" or "Green"
    };
    std::vector<TrafficLight> m_trafficLights; // 缓存检测结果 (红绿灯)
    
    // 自动录制
    QTimer* m_recordTimer;
    cv::VideoWriter m_writer;
    int m_frameCount;
    bool m_isRecording;
    bool m_isDriving;
    QString m_currentVideoFile;
    
    // 特征图片自动上传
    cv::Mat m_latestFrame;
    std::atomic<bool> m_isUploading;
    void uploadFeatureImage(const cv::Mat& frame);
};

#endif // DRIVINGMODEPAGE_H
