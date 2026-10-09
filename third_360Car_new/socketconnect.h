#ifndef SOCKETCONNECT_H
#define SOCKETCONNECT_H

#include <QObject>
#include <QString>
#include <QFutureWatcher>
#include <QTimer>
#include <QMap>
#include "protocol.h"

class ReadThread;
class WriteThread;

/**
 * @brief Socket 连接管理类 (单例)
 * 
 * 负责管理 socket 连接及读写线程，协调网络通信。
 */
class SocketConnect : public QObject
{
    Q_OBJECT

public:
    static SocketConnect* instance();
    
    /// 连接服务器（同步，会阻塞）
    bool connectToServer(const QString &ip, quint16 port);
    
    /// 连接服务器（异步，不阻塞 UI）
    void connectToServerAsync(const QString &ip, quint16 port);
    
    /// 断开连接
    void disconnectFromServer();
    
    /// 发送数据包
    void sendPacket(uint16_t funcId, const void *data, uint32_t len);
    void sendPacket(uint16_t funcId, const QByteArray &data);
    
    /// 视频模块请求
    void requestVideoList(uint32_t userId, uint32_t page, uint32_t pageSize);
    void uploadVideoRecord(uint32_t userId, uint32_t videoId, uint32_t seek, uint32_t duration);
    void uploadVideoInfo(const QString& fileName, const QString& filePath, const QString& coverPath);
    
    /// 图片模块请求
    void requestImageList(uint32_t userId, uint32_t page, uint32_t pageSize);
    
    /// 图片重传：缓存上传数据（上传方在发送切片前调用）
    void cacheUploadData(uint32_t uploadId, const QByteArray &fileData, uint32_t totalSlices);
    /// 图片重传：清除指定缓存
    void removeUploadCache(uint32_t uploadId);
    
    /// 状态查询
    bool isConnected() const { return m_connected; }
    int sockfd() const { return m_sockfd; }
    
    // 用户ID存取
    void setUserId(uint32_t id) { m_userId = id; }
    uint32_t getUserId() const { return m_userId; }

signals:
    /// 连接成功
    void connected();
    
    /// 连接断开
    void disconnected();
    
    /// 收到数据包
    void packetReceived(uint16_t funcId, const QByteArray &data);
    
    /// 收到视频列表项
    void videoListItemReceived(const VIDEORECORDITEM& item);
    
    /// 视频上传结果
    void videoInfoUploaded(uint8_t resCode);
    
    /// 收到图片列表项
    void imageListItemReceived(const PHOTOINFOITEMRES& item);
    
    /// 图片上传重传结果 (success=true 全部成功, false 超过最大重试次数)
    void imgRetransmitResult(uint32_t uploadId, bool success);
    
    /// 发生错误
    void errorOccurred(const QString &error);

private slots:
    void onReadDisconnected();
    void onReadError(const QString &error);
    void onPacketReceived(uint16_t funcId, const QByteArray &data);
    void onAsyncConnectFinished();
    void sendHeartbeat();

private:
    explicit SocketConnect(QObject *parent = nullptr);
    ~SocketConnect() override;
    
    void cleanup();
    void handleRetransmission(const RETMISSION &ret);

private:
    // 图片上传缓存（供重传使用）
    struct UploadCache {
        QByteArray fileData;    // 原始文件数据
        uint32_t totalSlices;   // 切片总数
        int retryCount;         // 已重试次数
    };
    QMap<uint32_t, UploadCache> m_uploadCacheMap;
    static const int MAX_RETRANSMIT_RETRY = 3; // 最大重试次数

    static SocketConnect *s_instance;
    
    int m_sockfd;
    bool m_connected;
    
    ReadThread *m_readThread;
    WriteThread *m_writeThread;
    
    // 异步连接相关
    QFutureWatcher<bool> *m_connectWatcher;
    QString m_pendingIp;
    quint16 m_pendingPort;
    
    // 心跳定时器
    QTimer *m_heartbeatTimer;
    
    // 当前登录用户ID
    uint32_t m_userId = 0;
};

#endif // SOCKETCONNECT_H
