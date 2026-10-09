#ifndef BIRDVIEWPAGE_H
#define BIRDVIEWPAGE_H

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <opencv2/opencv.hpp>

namespace Ui {
class BirdViewPage;
}

class BirdViewPage : public QWidget
{
    Q_OBJECT

public:
    explicit BirdViewPage(QWidget *parent = nullptr);
    ~BirdViewPage();

    // 供 MainManager 调用的启动/停止接口
    void startBirdView();
    void stopBirdView();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onUpdateFrames();
    void onCameraFrame(const cv::Mat& frame);
    void onStartAutoRecord();

private:
    Ui::BirdViewPage *ui;
    bool m_isRunning;

    QTimer *m_timer;
    cv::VideoCapture m_capSide;
    cv::VideoCapture m_capRear;
    
    cv::Mat m_latestCameraFrame;
    QImage matToQImage(const cv::Mat& mat);
    
    void drawTrapezoid(cv::Mat& dest, const cv::Mat& src, const std::vector<cv::Point2f>& srcPts, const std::vector<cv::Point2f>& dstPts);

    // 自动录制相关
    QTimer *m_recordTimer;
    cv::VideoWriter m_recWriter;
    bool m_isRecording;
    int m_recFrameCount;
    QString m_currentVideoFile;
    cv::Mat m_latestBirdView;  // 缓存最新的鸟瞰合成画面用于录制
};

#endif // BIRDVIEWPAGE_H
