#ifndef VIDEORECORDPAGE_H
#define VIDEORECORDPAGE_H

#include <QWidget>
#include <QListWidget>
#include "protocol.h"

class VideoPlayer;

namespace Ui {
class VideoRecordPage;
}

class VideoRecordPage : public QWidget
{
    Q_OBJECT

public:
    explicit VideoRecordPage(QWidget *parent = nullptr);
    ~VideoRecordPage();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onVideoListItemReceived(const VIDEORECORDITEM& item);
    void onPlayVideo(uint32_t videoId, const QString& filePath,
                     uint32_t progressSeconds, uint32_t durationSeconds);

private:
    Ui::VideoRecordPage *ui;
    VideoPlayer *m_player;
    
    void refreshList();
    QWidget* createVideoItemWidget(const VIDEORECORD& record);
};

#endif // VIDEORECORDPAGE_H
