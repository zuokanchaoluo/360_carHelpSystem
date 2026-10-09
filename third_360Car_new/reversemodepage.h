#ifndef REVERSEMODEPAGE_H
#define REVERSEMODEPAGE_H

#include <QWidget>
#include <QResizeEvent>
#include <QTimer>
#include "opencv2/opencv.hpp"

namespace Ui {
class ReverseModePage;
}

class ReverseModePage : public QWidget
{
    Q_OBJECT

public:
    explicit ReverseModePage(QWidget *parent = nullptr);
    ~ReverseModePage();

    // 进入/退出页面时调用
    void startReverse();
    void stopReverse();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onRawFrameReady(const cv::Mat& frame);
    void onStartAutoRecord();

private:
    Ui::ReverseModePage *ui;
    bool m_isReversing;

    // 自动录制相关
    QTimer *m_recordTimer;
    cv::VideoWriter m_writer;
    bool m_isRecording;
    int m_frameCount;
    QString m_currentVideoFile;
};

#endif // REVERSEMODEPAGE_H
