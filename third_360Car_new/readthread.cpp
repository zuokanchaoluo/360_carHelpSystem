#include "readthread.h"
#include <QDebug>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

ReadThread* ReadThread::s_instance = nullptr;
QMutex ReadThread::s_mutex;

ReadThread* ReadThread::instance()
{
    if (!s_instance) {
        QMutexLocker locker(&s_mutex);
        if (!s_instance) {
            s_instance = new ReadThread();
        }
    }
    return s_instance;
}

ReadThread::ReadThread(QObject *parent)
    : QThread(parent)
    , m_sockfd(-1)
    , m_running(false)
{
}

ReadThread::~ReadThread()
{
    stopRead();
}

void ReadThread::startRead(int sockfd)
{
    m_sockfd = sockfd;
    m_running = true;
    start();
}

void ReadThread::stopRead()
{
    m_running = false;
    if (isRunning()) {
        quit();
        wait(3000);
        if (isRunning()) {
            terminate();
            wait();
        }
    }
}

void ReadThread::run()
{
    qDebug() << "[ReadThread] Started, sockfd:" << m_sockfd;
    
    while (m_running && m_sockfd >= 0) {
        // 1. 读取协议头
        HEAD head;
        if (!readExact(&head, sizeof(HEAD))) {
            if (m_running) {
                emit disconnected();
            }
            break;
        }
        
        // 2. 解析协议头
        uint16_t funcId = head.type;
        uint32_t bodyLen = head.length;
        
        qDebug() << "[ReadThread] Received packet, funcId:" << funcId << ", bodyLen:" << bodyLen;
        
        // 2.5 包体大小安全校验（防止网络错位后分配巨量内存）
        if (bodyLen > 1024 * 1024) {  // 超过 1MB 视为异常
            qDebug() << "[ReadThread] 异常包体大小:" << bodyLen << ", 断开连接";
            emit disconnected();
            break;
        }
        
        // 3. 读取数据体
        QByteArray body;
        if (bodyLen > 0) {
            body.resize(bodyLen);
            if (!readExact(body.data(), bodyLen)) {
                if (m_running) {
                    emit disconnected();
                }
                break;
            }
        }
        
        // 4. 发射信号通知主线程
        emit packetReceived(funcId, body);
    }
    
    qDebug() << "[ReadThread] Stopped";
}

bool ReadThread::readExact(void *buf, int len)
{
    char *ptr = static_cast<char*>(buf);
    int remaining = len;
    
    while (remaining > 0 && m_running) {
        ssize_t n = ::recv(m_sockfd, ptr, remaining, 0);
        if (n <= 0) {
            if (n == 0) {
                qDebug() << "[ReadThread] Connection closed by server";
            } else {
                qDebug() << "[ReadThread] recv error:" << errno;
                emit errorOccurred(QString("recv error: %1").arg(errno));
            }
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return remaining == 0;
}
