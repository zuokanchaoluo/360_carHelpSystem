#ifndef WRITETHREAD_H
#define WRITETHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QByteArray>
#include "protocol.h"

/**
 * @brief 网络写线程 (单例类)
 * 
 * 负责从发送队列取数据，调用 send() 发送。
 * 主线程通过 enqueue() 添加发送任务。
 */
class WriteThread : public QThread
{
    Q_OBJECT

public:
    static WriteThread* instance();
    
    /// 设置 socket fd 并启动线程
    void startWrite(int sockfd);
    
    /// 停止线程
    void stopWrite();
    
    /// 添加发送任务（线程安全）
    void enqueue(uint16_t funcId, const QByteArray &data);

signals:
    /// 发送完成
    void sendCompleted(uint16_t funcId);
    
    /// 发送错误
    void errorOccurred(const QString &error);

protected:
    void run() override;

private:
    explicit WriteThread(QObject *parent = nullptr);
    ~WriteThread() override;
    
    /// 发送指定长度的数据（阻塞）
    bool sendExact(const void *buf, int len);

private:
    static WriteThread *s_instance;
    static QMutex s_instanceMutex;
    
    int m_sockfd;
    volatile bool m_running;
    
    // 发送队列
    struct SendTask {
        uint16_t funcId;
        QByteArray data;
    };
    QQueue<SendTask> m_queue;
    QMutex m_queueMutex;
    QWaitCondition m_queueCond;
};

#endif // WRITETHREAD_H
