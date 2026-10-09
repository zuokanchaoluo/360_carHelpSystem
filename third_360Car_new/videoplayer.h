#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QWidget>
#include <QTimer>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QMessageBox>
#include "opencv2/opencv.hpp"

/**
 * @brief 独立视频播放窗口
 * 
 * 支持：断点续播、进度条、倍速播放 (0.5x/1.0x/1.5x/2.0x)
 */
class VideoPlayer : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPlayer(QWidget *parent = nullptr);
    ~VideoPlayer();

    void setVideo(const QString& filePath, uint32_t videoId,
                  uint32_t lastSeekSeconds, uint32_t totalDuration);

signals:
    void playerClosed();

private slots:
    void onPlayPauseClicked();
    void onTimerTick();
    void onSliderPressed();
    void onSliderReleased();
    void onSpeedChanged(int index);
    void onScreenshotClicked();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void initUI();
    void uploadProgress();
    QString formatTime(int seconds);

    // UI 控件
    QLabel *m_videoLabel;
    QLabel *m_lblCurrentTime;
    QLabel *m_lblTotalTime;
    QLabel *m_lblCurrentFrame;
    QSlider *m_progressSlider;
    QPushButton *m_btnPlayPause;
    QPushButton *m_btnScreenshot;
    QComboBox *m_cmbSpeed;

    // 播放逻辑
    QTimer *m_playTimer;
    cv::VideoCapture m_cap;
    
    // 视频信息
    QString m_filePath;
    uint32_t m_videoId;
    uint32_t m_lastSeekSeconds;
    uint32_t m_totalDuration;
    double m_fps;
    int m_totalFrames;
    int m_currentFrame;
    bool m_isPlaying;
    bool m_sliderPressed;
    double m_speedFactor;
    cv::Mat m_currentMat;  // 当前显示帧（用于截图）
    bool m_isUploading;    // 上传状态标志
};

#endif // VIDEOPLAYER_H
