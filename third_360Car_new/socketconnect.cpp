#include "socketconnect.h"
#include "readthread.h"
#include "writethread.h"
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QtConcurrent>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

SocketConnect* SocketConnect::s_instance = nullptr;

static const int HEARTBEAT_INTERVAL = 30;  // 心跳发送间隔 (秒)

SocketConnect* SocketConnect::instance()
{
    if (!s_instance) {
        s_instance = new SocketConnect();
    }
    return s_instance;
}

SocketConnect::SocketConnect(QObject *parent)
    : QObject(parent)
    , m_sockfd(-1)
    , m_connected(false)
    , m_readThread(nullptr)
    , m_writeThread(nullptr)
    , m_connectWatcher(nullptr)
    , m_pendingPort(0)
    , m_heartbeatTimer(nullptr)
{
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &SocketConnect::sendHeartbeat);
}

SocketConnect::~SocketConnect()
{
    disconnectFromServer();
    if (m_connectWatcher) {
        m_connectWatcher->cancel();
        delete m_connectWatcher;
    }
}

bool SocketConnect::connectToServer(const QString &ip, quint16 port)
{
    if (m_connected) {
        qDebug() << "[SocketConnect] Already connected";
        // 已连接时也发送 connected 信号，让 UI 更新
        emit connected();
        return true;
    }
    
    // 1. 创建 socket
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd < 0) {
        emit errorOccurred(QString("Failed to create socket: %1").arg(strerror(errno)));
        return false;
    }
    
    // 2. 设置服务器地址
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.toStdString().c_str(), &serverAddr.sin_addr) <= 0) {
        emit errorOccurred("Invalid IP address: " + ip);
        cleanup();
        return false;
    }
    
    // 3. 连接服务器
    qDebug() << "[SocketConnect] Connecting to" << ip << ":" << port;
    
    if (::connect(m_sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        emit errorOccurred(QString("Connect failed: %1").arg(strerror(errno)));
        cleanup();
        return false;
    }
    
    qDebug() << "[SocketConnect] Connected successfully!";
    m_connected = true;
    
    // 4. 创建并启动读写线程
    m_readThread = ReadThread::instance();
    m_writeThread = WriteThread::instance();
    
    // 先断开旧的信号连接，避免重复
    disconnect(m_readThread, nullptr, this, nullptr);
    
    // 连接信号
    connect(m_readThread, &ReadThread::packetReceived, 
            this, &SocketConnect::onPacketReceived, Qt::QueuedConnection);
    connect(m_readThread, &ReadThread::disconnected, 
            this, &SocketConnect::onReadDisconnected, Qt::QueuedConnection);
    connect(m_readThread, &ReadThread::errorOccurred, 
            this, &SocketConnect::onReadError, Qt::QueuedConnection);
    
    // 启动线程
    m_readThread->startRead(m_sockfd);
    m_writeThread->startWrite(m_sockfd);
    
    // 启动心跳定时器
    m_heartbeatTimer->start(HEARTBEAT_INTERVAL * 1000);
    
    emit connected();
    return true;
}

void SocketConnect::disconnectFromServer()
{
    if (!m_connected) return;
    
    qDebug() << "[SocketConnect] Disconnecting...";
    
    // 停止心跳定时器
    m_heartbeatTimer->stop();
    
    // 停止线程
    if (m_readThread) {
        disconnect(m_readThread, nullptr, this, nullptr);
        m_readThread->stopRead();
    }
    if (m_writeThread) {
        m_writeThread->stopWrite();
    }
    
    cleanup();
    m_connected = false;
    
    emit disconnected();
}

void SocketConnect::cleanup()
{
    if (m_sockfd >= 0) {
        close(m_sockfd);
        m_sockfd = -1;
    }
}

void SocketConnect::sendPacket(uint16_t funcId, const void *data, uint32_t len)
{
    if (!m_connected || !m_writeThread) {
        emit errorOccurred("Not connected");
        return;
    }
    
    QByteArray body(static_cast<const char*>(data), len);
    m_writeThread->enqueue(funcId, body);
}

void SocketConnect::sendPacket(uint16_t funcId, const QByteArray &data)
{
    if (!m_connected || !m_writeThread) {
        emit errorOccurred("Not connected");
        return;
    }
    
    m_writeThread->enqueue(funcId, data);
}

void SocketConnect::onReadDisconnected()
{
    qDebug() << "[SocketConnect] Server disconnected";
    disconnectFromServer();
}

void SocketConnect::onReadError(const QString &error)
{
    emit errorOccurred(error);
}

void SocketConnect::onPacketReceived(uint16_t funcId, const QByteArray &data)
{
    // 视频列表响应分发
    if (funcId == FID_GET_VIDEO_LIST && data.size() >= (int)sizeof(VIDEORECORDITEM)) {
        VIDEORECORDITEM item;
        memcpy(&item, data.constData(), sizeof(VIDEORECORDITEM));
        emit videoListItemReceived(item);
        return;
    }
    
    // 视频信息上传结果
    if (funcId == FID_INFO_UPLOAD && data.size() >= (int)sizeof(BASERES)) {
        BASERES res;
        memcpy(&res, data.constData(), sizeof(BASERES));
        emit videoInfoUploaded(res.resCode);
        return;
    }
    
    // 图片列表响应分发
    if (funcId == FID_GET_IMG_LIST && data.size() >= (int)sizeof(PHOTOINFOITEMRES)) {
        PHOTOINFOITEMRES item;
        memcpy(&item, data.constData(), sizeof(PHOTOINFOITEMRES));
        emit imageListItemReceived(item);
        return;
    }
    
    // 图片切片重传响应分发
    if (funcId == FID_RETRANSMISSION_IMG && data.size() >= (int)sizeof(RETMISSION)) {
        RETMISSION ret;
        memcpy(&ret, data.constData(), sizeof(RETMISSION));
        
        if (ret.resCode == SUCCESS) {
            // 上传成功，清除缓存
            qDebug() << "[SocketConnect] 图片上传成功, uploadId=" << ret.uploadId << ", 清除缓存";
            m_uploadCacheMap.remove(ret.uploadId);
            emit imgRetransmitResult(ret.uploadId, true);
        } else if (ret.resCode == ERR_SLICE_MISSING) {
            // 有丢包，重发缺失切片
            qDebug() << "[SocketConnect] 收到重传请求, uploadId=" << ret.uploadId;
            handleRetransmission(ret);
        }
        return;
    }
    
    // 转发给上层
    emit packetReceived(funcId, data);
}

void SocketConnect::connectToServerAsync(const QString &ip, quint16 port)
{
    if (m_connected) {
        qDebug() << "[SocketConnect] Already connected";
        emit connected();
        return;
    }
    
    // 保存连接参数
    m_pendingIp = ip;
    m_pendingPort = port;
    
    // 创建 watcher
    if (!m_connectWatcher) {
        m_connectWatcher = new QFutureWatcher<bool>(this);
        connect(m_connectWatcher, &QFutureWatcher<bool>::finished,
                this, &SocketConnect::onAsyncConnectFinished);
    }
    
    qDebug() << "[SocketConnect] Starting async connection to" << ip << ":" << port;
    
    // 在后台线程执行连接
    QFuture<bool> future = QtConcurrent::run([this, ip, port]() -> bool {
        // 1. 创建 socket
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            return false;
        }
        
        // 2. 设置服务器地址
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip.toStdString().c_str(), &serverAddr.sin_addr) <= 0) {
            close(sockfd);
            return false;
        }
        
        // 3. 连接服务器
        if (::connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            close(sockfd);
            return false;
        }
        
        // 连接成功，保存 sockfd
        m_sockfd = sockfd;
        return true;
    });
    
    m_connectWatcher->setFuture(future);
}

void SocketConnect::onAsyncConnectFinished()
{
    bool success = m_connectWatcher->result();
    
    if (success) {
        qDebug() << "[SocketConnect] Async connection successful!";
        m_connected = true;
        
        // 创建并启动读写线程
        m_readThread = ReadThread::instance();
        m_writeThread = WriteThread::instance();
        
        // 先断开旧的信号连接
        disconnect(m_readThread, nullptr, this, nullptr);
        
        // 连接信号
        connect(m_readThread, &ReadThread::packetReceived, 
                this, &SocketConnect::onPacketReceived, Qt::QueuedConnection);
        connect(m_readThread, &ReadThread::disconnected, 
                this, &SocketConnect::onReadDisconnected, Qt::QueuedConnection);
        connect(m_readThread, &ReadThread::errorOccurred, 
                this, &SocketConnect::onReadError, Qt::QueuedConnection);
        
        // 启动线程
        m_readThread->startRead(m_sockfd);
        m_writeThread->startWrite(m_sockfd);
        
        // 启动心跳定时器
        m_heartbeatTimer->start(HEARTBEAT_INTERVAL * 1000);
        
        emit connected();
    } else {
        qDebug() << "[SocketConnect] Async connection failed!";
        emit errorOccurred("Connection failed: " + m_pendingIp + ":" + QString::number(m_pendingPort));
    }
}

// =============================================================
// 视频模块协议实现
// =============================================================

void SocketConnect::requestVideoList(uint32_t userId, uint32_t page, uint32_t pageSize)
{
    PAGEREQ req;
    memset(&req, 0, sizeof(req));
    req.userId = userId;
    req.pageNumber = page;
    req.pageSize = pageSize;
    sendPacket(FID_GET_VIDEO_LIST, &req, sizeof(req));
    qDebug() << "[SocketConnect] 发送视频列表请求: userId=" << userId << "page=" << page;
}

void SocketConnect::uploadVideoRecord(uint32_t userId, uint32_t videoId, uint32_t seek, uint32_t duration)
{
    UPRECORDREQ req;
    memset(&req, 0, sizeof(req));
    req.user_id = userId;
    req.video_id = videoId;
    req.video_seek = seek;
    req.video_duration = duration;
    // 填充当前时间
    QByteArray timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toUtf8();
    strncpy(req.createTime, timeStr.constData(), sizeof(req.createTime) - 1);
    sendPacket(FID_RECORD_UPLOAD, &req, sizeof(req));
    qDebug() << "[SocketConnect] 上传播放记录: videoId=" << videoId << "seek=" << seek;
}

void SocketConnect::uploadVideoInfo(const QString& fileName, const QString& filePath, const QString& coverPath)
{
    UPVIDEOINFOREQ req;
    memset(&req, 0, sizeof(req));

    // 填充新增字段
    req.userId = m_userId;
    QFileInfo info(filePath);
    req.fileSize = (uint32_t)info.size();

    strncpy(req.fileName, fileName.toUtf8().constData(), sizeof(req.fileName) - 1);
    strncpy(req.filePath, filePath.toUtf8().constData(), sizeof(req.filePath) - 1);
    strncpy(req.coverPath, coverPath.toUtf8().constData(), sizeof(req.coverPath) - 1);
    sendPacket(FID_INFO_UPLOAD, &req, sizeof(req));
    qDebug() << "[SocketConnect] 上传视频信息:" << fileName << "Size:" << req.fileSize << "UserID:" << req.userId;
}

// =============================================================
// 图片模块协议实现
// =============================================================

void SocketConnect::requestImageList(uint32_t userId, uint32_t page, uint32_t pageSize)
{
    PAGEREQ req;
    memset(&req, 0, sizeof(req));
    req.userId = userId;
    req.pageNumber = page;
    req.pageSize = pageSize;
    sendPacket(FID_GET_IMG_LIST, &req, sizeof(req));
    qDebug() << "[SocketConnect] 发送图片列表请求: userId=" << userId << "page=" << page;
}

void SocketConnect::sendHeartbeat()
{
    if (m_connected) {
        sendPacket(FID_HEARTBEAT, nullptr, 0);
    }
}

// =============================================================
// 图片重传模块实现
// =============================================================

void SocketConnect::cacheUploadData(uint32_t uploadId, const QByteArray &fileData, uint32_t totalSlices)
{
    UploadCache cache;
    cache.fileData = fileData;
    cache.totalSlices = totalSlices;
    cache.retryCount = 0;
    m_uploadCacheMap.insert(uploadId, cache);
    qDebug() << "[SocketConnect] 缓存上传数据: uploadId=" << uploadId 
             << ", 文件大小=" << fileData.size() 
             << ", 切片数=" << totalSlices;
}

void SocketConnect::removeUploadCache(uint32_t uploadId)
{
    if (m_uploadCacheMap.remove(uploadId)) {
        qDebug() << "[SocketConnect] 清除上传缓存: uploadId=" << uploadId;
    }
}

void SocketConnect::handleRetransmission(const RETMISSION &ret)
{
    if (!m_uploadCacheMap.contains(ret.uploadId)) {
        qDebug() << "[SocketConnect] 重传失败: 未找到 uploadId=" << ret.uploadId << " 的缓存数据";
        emit imgRetransmitResult(ret.uploadId, false);
        return;
    }
    
    UploadCache &cache = m_uploadCacheMap[ret.uploadId];
    cache.retryCount++;
    
    if (cache.retryCount > MAX_RETRANSMIT_RETRY) {
        qDebug() << "[SocketConnect] 重传超过最大重试次数(" << MAX_RETRANSMIT_RETRY 
                 << "), uploadId=" << ret.uploadId << ", 放弃并清除缓存";
        m_uploadCacheMap.remove(ret.uploadId);
        emit imgRetransmitResult(ret.uploadId, false);
        return;
    }
    
    // 统计缺失切片数量
    int missingCount = ret.count;
    // 限制最大缺失数量为实际收到的和数组大小最小值，防止越界
    if (missingCount > 100) missingCount = 100;
    
    qDebug() << "[SocketConnect] 重传开始: uploadId=" << ret.uploadId 
             << ", 缺失" << missingCount << "片"
             << ", 第" << cache.retryCount << "次重试";
    
    const int sliceSize = 1024;
    const QByteArray &fileData = cache.fileData;
    uint32_t totalSlices = cache.totalSlices;
    uint32_t uploadId = ret.uploadId;
    
    // 在后台线程重发缺失切片，避免阻塞主线程
    // 拷贝缺失序号列表
    QVector<int32_t> missingList;
    for (int i = 0; i < missingCount; i++) {
        missingList.append(ret.num[i]);
    }
    
    QtConcurrent::run([=](){
        for (int i = 0; i < missingList.size(); i++) {
            int32_t sliceIndex = missingList[i];
            
            PHOTOSLICE slice;
            memset(&slice, 0, sizeof(slice));
            
            int offset = sliceIndex * sliceSize;
            int remaining = fileData.size() - offset;
            int currentSize = qMin(remaining, sliceSize);
            
            if (offset >= fileData.size() || currentSize <= 0) {
                qDebug() << "[SocketConnect] 重传跳过无效切片: sliceIndex=" << sliceIndex;
                continue;
            }
            
            // 判断是否是最后一片
            slice.isLastSlice = ((uint32_t)sliceIndex == totalSlices - 1) ? 1 : 0;
            // 如果是重传的最后一个包，也标记为 lastSlice 以触发服务器再次校验
            if (i == missingList.size() - 1) {
                slice.isLastSlice = 1;
            }
            
            slice.sliceIndex = sliceIndex;
            slice.sliceSize = currentSize;
            slice.uploadId = uploadId;
            memcpy(slice.buf, fileData.constData() + offset, currentSize);
            
            SocketConnect::instance()->sendPacket(FID_IMG_UPLOAD_SLICE, &slice, sizeof(slice));
            
            qDebug() << "[SocketConnect] 重传切片: sliceIndex=" << sliceIndex 
                     << ", size=" << currentSize 
                     << ", isLast=" << slice.isLastSlice;
            
            // 控制发送速度，防止粘包
            QThread::msleep(5);
        }
        
        qDebug() << "[SocketConnect] 重传发送完成: uploadId=" << uploadId 
                 << ", 重发" << missingList.size() << "片";
    });
}
