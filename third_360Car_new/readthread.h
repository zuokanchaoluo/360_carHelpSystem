#ifndef READTHREAD_H
#define READTHREAD_H

#include <QThread>
#include <QMutex>
#include <QByteArray>
#include "protocol.h"

/**
 * @brief 网络读线程 (单例类)
 * 
 * 负责从 socket 阻塞读取数据，解析协议头，
 * 读取完整数据包后通过信号发送给主线程。
 */
class ReadThread : public QThread
{
    Q_OBJECT

public:
    static ReadThread* instance();
    
    /// 设置 socket fd 并启动线程
    void startRead(int sockfd);
    
    /// 停止线程
    void stopRead();

signals:
    /// 收到完整数据包
    void packetReceived(uint16_t funcId, const QByteArray &data);
    
    /// 连接断开
    void disconnected();
    
    /// 读取错误
    void errorOccurred(const QString &error);

protected:
    void run() override;

private:
    explicit ReadThread(QObject *parent = nullptr);
    ~ReadThread() override;
    
    /// 读取指定长度的数据（阻塞）
    bool readExact(void *buf, int len);

private:
    static ReadThread *s_instance;
    static QMutex s_mutex;
    
    int m_sockfd;
    volatile bool m_running;
};

#endif // READTHREAD_H
