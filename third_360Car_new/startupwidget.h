#ifndef STARTUPWIDGET_H
#define STARTUPWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QPropertyAnimation>
#include <opencv2/opencv.hpp>

/**
 * @brief 高颜值全屏开机动画类
 * 
 * 使用 OpenCV 播放本地视频，视频结束后执行淡出动画。
 * 特性：
 * - 全屏无边框置顶显示
 * - 30 FPS 流畅播放
 * - 视频铺满屏幕（KeepAspectRatioByExpanding）
 * - 1秒淡出转场特效
 */
class StartupWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param videoPath 视频文件路径，默认为 "assets/intro.mp4"
     * @param parent 父窗口指针
     */
    explicit StartupWidget(const QString &videoPath = "assets/intro.mp4", 
                           QWidget *parent = nullptr);
    ~StartupWidget();

signals:
    /**
     * @brief 动画完成信号
     * 当淡出动画结束后发射此信号，通知主程序可以显示主界面
     */
    void animationFinished();

protected:
    /**
     * @brief 重写绘制事件
     * 使用 QPainter 绘制当前视频帧，采用 KeepAspectRatioByExpanding 模式铺满屏幕
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 键盘事件处理
     * 按 ESC 键可跳过动画
     */
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    /**
     * @brief 更新视频帧
     * 由定时器触发，读取下一帧并刷新显示
     */
    void updateFrame();

    /**
     * @brief 淡出动画完成处理
     * 发射 animationFinished 信号并关闭窗口
     */
    void onFadeOutFinished();

private:
    /**
     * @brief 启动淡出动画
     * 使用 QPropertyAnimation 实现 1 秒淡出效果
     */
    void startFadeOut();

    /**
     * @brief 将 OpenCV Mat 转换为 QImage
     * @param mat OpenCV 图像矩阵
     * @return 转换后的 QImage
     */
    QImage matToQImage(const cv::Mat &mat);

private:
    cv::VideoCapture m_videoCapture;      ///< OpenCV 视频捕获器
    QTimer *m_frameTimer;                  ///< 帧刷新定时器 (30 FPS)
    QImage m_currentFrame;                 ///< 当前显示帧
    QPropertyAnimation *m_fadeAnimation;  ///< 淡出动画
    bool m_isFinishing;                    ///< 是否正在结束动画
};

#endif // STARTUPWIDGET_H
