#include "videorecordpage.h"
#include "ui_videorecordpage.h"
#include "socketconnect.h"
#include "videoplayer.h"
#include <QDebug>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QVBoxLayout>

VideoRecordPage::VideoRecordPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VideoRecordPage),
    m_player(nullptr)
{
    ui->setupUi(this);

    // 连接服务器视频列表信号
    connect(SocketConnect::instance(), &SocketConnect::videoListItemReceived,
            this, &VideoRecordPage::onVideoListItemReceived);
}

VideoRecordPage::~VideoRecordPage()
{
    delete ui;
}

void VideoRecordPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 进入页面时请求视频列表
    refreshList();
}

void VideoRecordPage::refreshList()
{
    ui->listWidget->clear();
    // 向服务器请求视频列表 (userId=0 暂时，page=1, pageSize=20)
    // 向服务器请求视频列表
    if (SocketConnect::instance()->isConnected()) {
        uint32_t userId = SocketConnect::instance()->getUserId();
        SocketConnect::instance()->requestVideoList(userId, 1, 20);
    }
}

void VideoRecordPage::onVideoListItemReceived(const VIDEORECORDITEM& item)
{
    if (!isVisible()) return;
    const VIDEORECORD& record = item.record;

    QWidget *itemWidget = createVideoItemWidget(record);

    QListWidgetItem *listItem = new QListWidgetItem();
    listItem->setSizeHint(QSize(0, 160));
    ui->listWidget->addItem(listItem);
    ui->listWidget->setItemWidget(listItem, itemWidget);
}

QWidget* VideoRecordPage::createVideoItemWidget(const VIDEORECORD& record)
{
    QWidget *widget = new QWidget(this);
    widget->setStyleSheet("background-color: #2A2A3A; border-radius: 6px; padding: 8px;");

    QHBoxLayout *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(12);

    // 缩略图占位
    QLabel *thumbnail = new QLabel(widget);
    thumbnail->setFixedSize(140, 90);
    thumbnail->setStyleSheet("background-color: #444; border: 1px solid #555; border-radius: 4px;");
    thumbnail->setAlignment(Qt::AlignCenter);
    thumbnail->setText("🎬");
    thumbnail->setStyleSheet(thumbnail->styleSheet() + " font-size: 32px; color: #00BFFF;");
    layout->addWidget(thumbnail);

    // 信息区
    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);

    // 记录时间
    QLabel *lblTime = new QLabel(QString("记录于 %1").arg(QString(record.createTime)), widget);
    lblTime->setStyleSheet("color: #FFFFFF; font-weight: bold; font-size: 16px;");
    lblTime->setWordWrap(true);
    infoLayout->addWidget(lblTime);

    // 文件名
    QLabel *lblName = new QLabel(QString(record.fileName), widget);
    lblName->setStyleSheet("color: #CCCCCC; font-size: 13px;");
    lblName->setWordWrap(true);
    infoLayout->addWidget(lblName);

    // 观看进度条
    int percent = 0;
    if (record.durationSeconds > 0) {
        percent = (int)(record.progressSeconds * 100.0 / record.durationSeconds);
    }
    QString progressText;
    if (percent >= 100) {
        progressText = "已经看完";
    } else if (percent > 0) {
        progressText = QString("观看%1%").arg(percent);
    } else {
        progressText = "未观看";
    }

    QLabel *lblProgress = new QLabel(progressText, widget);
    lblProgress->setStyleSheet("color: #00E5FF; font-size: 14px; font-weight: bold;");
    infoLayout->addWidget(lblProgress);

    QProgressBar *progressBar = new QProgressBar(widget);
    progressBar->setRange(0, 100);
    progressBar->setValue(percent);
    progressBar->setMaximumHeight(8);
    progressBar->setTextVisible(false);
    progressBar->setStyleSheet(
        "QProgressBar { background-color: #333; border-radius: 4px; }"
        "QProgressBar::chunk { background-color: #00BFFF; border-radius: 4px; }"
    );
    infoLayout->addWidget(progressBar);

    layout->addLayout(infoLayout, 1);

    // 播放按钮
    QPushButton *btnPlay = new QPushButton("播放", widget);
    btnPlay->setFixedSize(80, 36);
    btnPlay->setStyleSheet(
        "QPushButton { background-color: #00BFFF; color: white; border-radius: 4px; font-weight: bold; font-size: 14px; }"
        "QPushButton:hover { background-color: #1E90FF; }"
    );

    // 保存视频信息到按钮属性中
    uint32_t videoId = record.video_id;
    QString filePath = QString(record.filePath);
    uint32_t progressSec = record.progressSeconds;
    uint32_t durationSec = record.durationSeconds;

    connect(btnPlay, &QPushButton::clicked, this, [this, videoId, filePath, progressSec, durationSec]() {
        onPlayVideo(videoId, filePath, progressSec, durationSec);
    });

    layout->addWidget(btnPlay);

    return widget;
}

void VideoRecordPage::onPlayVideo(uint32_t videoId, const QString& filePath,
                                  uint32_t progressSeconds, uint32_t durationSeconds)
{
    // 创建或复用播放器
    if (!m_player) {
        m_player = new VideoPlayer(nullptr); // 独立窗口
        connect(m_player, &VideoPlayer::playerClosed, this, &VideoRecordPage::refreshList);
    }

    m_player->setVideo(filePath, videoId, progressSeconds, durationSeconds);
    m_player->show();
    m_player->raise();
    m_player->activateWindow();
}
