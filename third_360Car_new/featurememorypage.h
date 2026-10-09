#ifndef FEATUREMEMORYPAGE_H
#define FEATUREMEMORYPAGE_H

#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QEvent>
#include <QMouseEvent>
#include "protocol.h"

namespace Ui {
class FeatureMemoryPage;
}

/**
 * @brief 特征记录页面
 * 
 * 展示从服务器获取的特征图片列表，支持：
 * - 网格布局展示缩略图 + 日期
 * - 懒加载分页
 * - 双击放大查看
 * - 返回按钮
 */
class FeatureMemoryPage : public QWidget
{
    Q_OBJECT

public:
    explicit FeatureMemoryPage(QWidget *parent = nullptr);
    ~FeatureMemoryPage();

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onImageListItemReceived(const PHOTOINFOITEMRES& item);
    void onLoadMoreClicked();
    void onBackClicked();
    void onImageDoubleClicked(const QString& filePath, const QString& createTime);

private:
    void initUI();
    void refreshList();
    QWidget* createImageCard(const PHOTOINFO& info);
    void showDetailView(const QString& filePath, const QString& createTime);
    void showListView();

    Ui::FeatureMemoryPage *ui;

    // 列表视图
    QWidget *m_listView;
    QScrollArea *m_scrollArea;
    QWidget *m_gridContainer;
    QGridLayout *m_gridLayout;
    QPushButton *m_btnLoadMore;
    QLabel *m_lblTitle;
    
    // 详情视图
    QWidget *m_detailView;
    QLabel *m_detailImage;
    QLabel *m_detailTime;
    QPushButton *m_btnBack;

    // 分页
    int m_currentPage;
    int m_pageSize;
    int m_itemCount;    // 当前页已收到的项数
    int m_totalRecords; // 服务器返回的总记录数
    static const int COLUMNS = 3; // 每行3列
    uint32_t m_requestId;       // 请求ID，用于忽略过期响应
};

#endif // FEATUREMEMORYPAGE_H
