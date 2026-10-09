#include "featurememorypage.h"
#include "ui_featurememorypage.h"
#include "socketconnect.h"
#include <QDebug>
#include <QPixmap>
#include <QScrollBar>
#include <QFileInfo>
#include <QCoreApplication>
#include <QMouseEvent>

FeatureMemoryPage::FeatureMemoryPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FeatureMemoryPage),
    m_currentPage(1),
    m_pageSize(9),
    m_itemCount(0),
    m_totalRecords(0),
    m_requestId(0)
{
    ui->setupUi(this);
    initUI();

    // 连接服务器图片列表信号
    connect(SocketConnect::instance(), &SocketConnect::imageListItemReceived,
            this, &FeatureMemoryPage::onImageListItemReceived);
}

FeatureMemoryPage::~FeatureMemoryPage()
{
    delete ui;
}

void FeatureMemoryPage::initUI()
{
    // 清空 UI 文件中的默认内容
    QLayout *oldLayout = this->layout();
    if (oldLayout) {
        QLayoutItem *child;
        while ((child = oldLayout->takeAt(0)) != nullptr) {
            if (child->widget()) delete child->widget();
            delete child;
        }
        delete oldLayout;
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== 列表视图 ==========
    m_listView = new QWidget(this);
    QVBoxLayout *listLayout = new QVBoxLayout(m_listView);
    listLayout->setContentsMargins(16, 12, 16, 12);
    listLayout->setSpacing(12);

    // 标题
    m_lblTitle = new QLabel("特征记录", m_listView);
    m_lblTitle->setStyleSheet("color: white; font-size: 22px; font-weight: bold; padding: 4px 0;");
    listLayout->addWidget(m_lblTitle);

    // 滚动区域
    m_scrollArea = new QScrollArea(m_listView);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { background: #1E1E2E; width: 8px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #555; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );

    // 网格容器
    m_gridContainer = new QWidget();
    m_gridContainer->setStyleSheet("background: transparent;");
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setSpacing(12);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);

    m_scrollArea->setWidget(m_gridContainer);
    listLayout->addWidget(m_scrollArea, 1);

    // 加载更多按钮
    m_btnLoadMore = new QPushButton("加载更多...", m_listView);
    m_btnLoadMore->setFixedHeight(40);
    m_btnLoadMore->setStyleSheet(
        "QPushButton { background-color: #00BFFF; color: white; border-radius: 6px; "
        "font-size: 14px; font-weight: bold; border: none; }"
        "QPushButton:hover { background-color: #1E90FF; }"
        "QPushButton:disabled { background-color: #444; color: #888; }"
    );
    m_btnLoadMore->setVisible(false);
    connect(m_btnLoadMore, &QPushButton::clicked, this, &FeatureMemoryPage::onLoadMoreClicked);
    listLayout->addWidget(m_btnLoadMore);

    mainLayout->addWidget(m_listView);

    // ========== 详情视图 ==========
    m_detailView = new QWidget(this);
    m_detailView->setVisible(false);
    QVBoxLayout *detailLayout = new QVBoxLayout(m_detailView);
    detailLayout->setContentsMargins(16, 12, 16, 12);
    detailLayout->setSpacing(12);

    // 返回按钮
    m_btnBack = new QPushButton(m_detailView);
    m_btnBack->setFixedSize(120, 36);
    m_btnBack->setIcon(QIcon("images/图片列表/返回.png"));
    m_btnBack->setIconSize(QSize(28, 28));
    m_btnBack->setText(" 返 回");
    m_btnBack->setStyleSheet(
        "QPushButton { background-color: #00BFFF; color: white; border-radius: 6px; "
        "font-size: 14px; font-weight: bold; border: none; }"
        "QPushButton:hover { background-color: #1E90FF; }"
    );
    connect(m_btnBack, &QPushButton::clicked, this, &FeatureMemoryPage::onBackClicked);
    detailLayout->addWidget(m_btnBack, 0, Qt::AlignLeft);

    // 大图
    m_detailImage = new QLabel(m_detailView);
    m_detailImage->setAlignment(Qt::AlignCenter);
    m_detailImage->setStyleSheet("background-color: #1E1E2E; border: 2px solid #00BFFF; border-radius: 8px;");
    m_detailImage->setMinimumHeight(300);
    m_detailImage->setScaledContents(true);
    detailLayout->addWidget(m_detailImage, 1);

    // 时间信息
    m_detailTime = new QLabel(m_detailView);
    m_detailTime->setAlignment(Qt::AlignCenter);
    m_detailTime->setStyleSheet("color: #00BFFF; font-size: 16px; font-weight: bold; padding: 8px;");
    detailLayout->addWidget(m_detailTime);

    mainLayout->addWidget(m_detailView);
}

void FeatureMemoryPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    
    // 强制切回列表视图，防止因保留在详情页而跳过刷新
    if (m_detailView && m_detailView->isVisible()) {
        showListView();
    }
    
    // 进入页面时刷新列表
    m_currentPage = 1;
    refreshList();
    
    qDebug() << "[FeatureMemory] showEvent: Refreshing list...";
}

void FeatureMemoryPage::refreshList()
{
    // 递增请求ID, 忽略旧的异步响应
    m_requestId++;
    
    // 第一页时清空现有内容
    if (m_currentPage == 1) {
        QLayoutItem *child;
        while ((child = m_gridLayout->takeAt(0)) != nullptr) {
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }
        m_itemCount = 0;
        m_totalRecords = 0;
    }

    m_btnLoadMore->setVisible(false);

    if (SocketConnect::instance()->isConnected()) {
        uint32_t userId = SocketConnect::instance()->getUserId();
        SocketConnect::instance()->requestImageList(userId, m_currentPage, m_pageSize);
    }
}

void FeatureMemoryPage::onImageListItemReceived(const PHOTOINFOITEMRES& item)
{
    // 页面不可见时忽略响应
    if (!isVisible()) return;
    
    // 更新总记录数
    m_totalRecords = item.totalRecords;

    const PHOTOINFO& info = item.photoInfo;

    // 创建图片卡片
    QWidget *card = createImageCard(info);

    // 计算在当前页中的位置 (基于已有总项数)
    int totalItems = (m_currentPage - 1) * m_pageSize + item.itemIndex;
    int row = totalItems / COLUMNS;
    int col = totalItems % COLUMNS;

    m_gridLayout->addWidget(card, row, col);
    m_itemCount++;

    // 检查是否还有更多数据
    int totalLoaded = (m_currentPage - 1) * m_pageSize + m_itemCount;
    if (totalLoaded < (int)m_totalRecords) {
        m_btnLoadMore->setVisible(true);
        m_btnLoadMore->setText(QString("加载更多... (%1/%2)").arg(totalLoaded).arg(m_totalRecords));
    } else {
        m_btnLoadMore->setVisible(false);
    }
}

void FeatureMemoryPage::onLoadMoreClicked()
{
    m_currentPage++;
    m_itemCount = 0;
    m_btnLoadMore->setEnabled(false);
    m_btnLoadMore->setText("加载中...");

    if (SocketConnect::instance()->isConnected()) {
        uint32_t userId = SocketConnect::instance()->getUserId();
        SocketConnect::instance()->requestImageList(userId, m_currentPage, m_pageSize);
    }

    m_btnLoadMore->setEnabled(true);
}

QWidget* FeatureMemoryPage::createImageCard(const PHOTOINFO& info)
{
    QWidget *card = new QWidget();
    card->setStyleSheet(
        "QWidget { background-color: #2A2A3A; border-radius: 8px; }"
    );
    card->setMinimumSize(180, 180);
    card->setMaximumHeight(220);

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(6, 6, 6, 6);
    cardLayout->setSpacing(6);

    // 缩略图
    QLabel *thumbnail = new QLabel(card);
    thumbnail->setAlignment(Qt::AlignCenter);
    thumbnail->setMinimumHeight(130);
    thumbnail->setStyleSheet("background-color: #1E1E2E; border: 1px solid #444; border-radius: 6px;");

    // 从本地 screenshots 目录加载图片 (截图时已保存在本地)
    QString localDir = QCoreApplication::applicationDirPath() + "/screenshots";
    QString filePath = localDir + "/" + QString(info.fileName);
    
    QPixmap pixmap(filePath);
    if (!pixmap.isNull()) {
        thumbnail->setPixmap(pixmap.scaled(
            QSize(240, 130), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        thumbnail->setText("📷");
        thumbnail->setStyleSheet(thumbnail->styleSheet() + " font-size: 48px; color: #00BFFF;");
    }
    cardLayout->addWidget(thumbnail, 1);

    // 文件名
    QLabel *lblName = new QLabel(QString(info.fileName), card);
    lblName->setAlignment(Qt::AlignCenter);
    lblName->setStyleSheet("color: #CCCCCC; font-size: 11px; background: transparent;");
    lblName->setWordWrap(true);
    cardLayout->addWidget(lblName);

    // 日期
    QLabel *lblDate = new QLabel(QString(info.createTime), card);
    lblDate->setAlignment(Qt::AlignCenter);
    lblDate->setStyleSheet("color: #00BFFF; font-size: 12px; font-weight: bold; background: transparent;");
    cardLayout->addWidget(lblDate);

    // 双击事件 - 通过鼠标事件实现
    QString path = filePath;
    QString time = QString(info.createTime);
    card->setProperty("filePath", path);
    card->setProperty("createTime", time);
    card->installEventFilter(this);
    thumbnail->installEventFilter(this);
    lblName->installEventFilter(this);
    lblDate->installEventFilter(this);

    card->setCursor(Qt::PointingHandCursor);

    return card;
}

void FeatureMemoryPage::showDetailView(const QString& filePath, const QString& createTime)
{
    // 加载大图
    QPixmap pixmap(filePath);
    if (!pixmap.isNull()) {
        m_detailImage->setPixmap(pixmap);
    } else {
        m_detailImage->setText("无法加载图片\n" + filePath);
        m_detailImage->setStyleSheet(m_detailImage->styleSheet() + " color: #FF6347; font-size: 16px;");
    }

    m_detailTime->setText(createTime);

    // 切换视图
    m_listView->setVisible(false);
    m_detailView->setVisible(true);
}

void FeatureMemoryPage::showListView()
{
    m_listView->setVisible(true);
    m_detailView->setVisible(false);
}

void FeatureMemoryPage::onBackClicked()
{
    showListView();
}

void FeatureMemoryPage::onImageDoubleClicked(const QString& filePath, const QString& createTime)
{
    showDetailView(filePath, createTime);
}

bool FeatureMemoryPage::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->button() != Qt::LeftButton) return QWidget::eventFilter(obj, event);
        // 向上查找带有 filePath 属性的 widget
        QWidget *widget = qobject_cast<QWidget*>(obj);
        while (widget) {
            QVariant pathVar = widget->property("filePath");
            if (pathVar.isValid()) {
                QString filePath = pathVar.toString();
                QString createTime = widget->property("createTime").toString();
                onImageDoubleClicked(filePath, createTime);
                return true;
            }
            widget = widget->parentWidget();
        }
    }
    return QWidget::eventFilter(obj, event);
}
