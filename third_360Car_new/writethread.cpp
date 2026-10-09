#include "writethread.h"
#include <QDebug>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

WriteThread* WriteThread::s_instance = nullptr;
QMutex WriteThread::s_instanceMutex;

WriteThread* WriteThread::instance()
{
    if (!s_instance) {
        QMutexLocker locker(&s_instanceMutex);
        if (!s_instance) {
            s_instance = new WriteThread();
        }
    }
    return s_instance;
}

WriteThread::WriteThread(QObject *parent)
    : QThread(parent)
    , m_sockfd(-1)
    , m_running(false)
{
}

WriteThread::~WriteThread()
{
    stopWrite();
}

void WriteThread::startWrite(int sockfd)
{
    m_sockfd = sockfd;
    m_running = true;
    start();
}

void WriteThread::stopWrite()
{
    m_running = false;
    
    // 唤醒等待中的线程
    m_queueCond.wakeAll();
    
    if (isRunning()) {
        quit();
        wait(3000);
        if (isRunning()) {
            terminate();
            wait();
        }
    }
    
    // 清空队列
    QMutexLocker locker(&m_queueMutex);
    m_queue.clear();
}

void WriteThread::enqueue(uint16_t funcId, const QByteArray &data)
{
    QMutexLocker locker(&m_queueMutex);
    
    SendTask task;
    task.funcId = funcId;
    task.data = data;
    m_queue.enqueue(task);
    
    // 唤醒写线程
    m_queueCond.wakeOne();
}

void WriteThread::run()
{
    qDebug() << "[WriteThread] Started, sockfd:" << m_sockfd;
    
    while (m_running && m_sockfd >= 0) {
        // 1. 从队列取任务
        SendTask task;
        {
            QMutexLocker locker(&m_queueMutex);
            while (m_queue.isEmpty() && m_running) {
                m_queueCond.wait(&m_queueMutex, 100);
            }
            if (!m_running) break;
            if (m_queue.isEmpty()) continue;
            task = m_queue.dequeue();
        }
        
        // 2. 构造数据包：合并 HEAD + 数据体一起发送，防止服务器半包处理错误
        QByteArray totalPacket;
        totalPacket.resize(sizeof(HEAD) + task.data.size());
        
        HEAD* head = (HEAD*)totalPacket.data();
        memset(head, 0, sizeof(HEAD));
        head->type = task.funcId;
        head->length = task.data.size();
        head->crc = 0;  // TODO: 计算 CRC
        
        // 拷贝包体
        if (task.data.size() > 0) {
            memcpy(totalPacket.data() + sizeof(HEAD), task.data.constData(), task.data.size());
        }
        
        // 3. 一次性发送完整包
        if (!sendExact(totalPacket.constData(), totalPacket.size())) {
            emit errorOccurred("Failed to send packet");
            break;
        }
        
        qDebug() << "[WriteThread] Sent packet, funcId:" << task.funcId << ", bodyLen:" << task.data.size();
        emit sendCompleted(task.funcId);
    }
    
    qDebug() << "[WriteThread] Stopped";
}

bool WriteThread::sendExact(const void *buf, int len)
{
    const char *ptr = static_cast<const char*>(buf);
    int remaining = len;
    
    while (remaining > 0 && m_running) {
        ssize_t n = ::send(m_sockfd, ptr, remaining, 0);
        if (n <= 0) {
            qDebug() << "[WriteThread] send error:" << errno;
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return remaining == 0;
}
