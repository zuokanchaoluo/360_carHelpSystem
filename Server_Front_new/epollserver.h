#ifndef EPOLLSERVER_H
#define EPOLLSERVER_H

#include <sys/epoll.h>
#include <unordered_map>
#include <functional>
#include <ctime>
#include "protocol.h"

class ThreadPool;

/**
 * @brief Epoll 服务器类
 * 
 * 使用 Epoll IO 多路复用实现高并发 TCP 服务器。
 */
class EpollServer
{
public:
    // 回调函数类型：处理收到的数据包
    using PacketHandler = std::function<void(int clientFd, uint16_t funcId, const char* data, uint32_t len)>;
    
    /**
     * @brief 创建 Epoll 服务器
     * @param port 监听端口
     * @param threadPool 线程池指针
     */
    EpollServer(int port, ThreadPool* threadPool);
    ~EpollServer();
    
    /**
     * @brief 设置数据包处理回调
     */
    void setPacketHandler(PacketHandler handler) { m_packetHandler = handler; }
    
    /**
     * @brief 启动服务器
     * @return 成功返回 true
     */
    bool start();
    
    /**
     * @brief 停止服务器
     */
    void stop();
    
    /**
     * @brief 运行事件循环（阻塞）
     */
    void run();
    
    /**
     * @brief 发送数据包给客户端
     * @param clientFd 客户端 socket fd
     * @param funcId 功能号
     * @param data 数据
     * @param len 数据长度
     */
    void sendPacket(int clientFd, uint16_t funcId, const void* data, uint32_t len);
    
    /**
     * @brief 关闭客户端连接
     */
    void closeClient(int clientFd);

private:
    bool createListenSocket();
    bool createEpoll();
    void handleAccept();
    void handleRead(int clientFd);
    void processPacket(int clientFd, const HEAD& head, const char* body);
    
    // 心跳超时检测
    void checkHeartbeatTimeout();
    
    // 设置 socket 非阻塞
    void setNonBlocking(int fd);
    
    // 添加 fd 到 epoll
    void addToEpoll(int fd, uint32_t events);
    void removeFromEpoll(int fd);

private:
    int m_port;
    int m_listenFd;
    int m_epollFd;
    bool m_running;
    
    ThreadPool* m_threadPool;
    PacketHandler m_packetHandler;
    
    // 客户端接收缓冲区
    struct ClientBuffer {
        char* buffer;
        int size;
        int used;
        
        ClientBuffer() : buffer(nullptr), size(0), used(0) {}
        ~ClientBuffer() { if (buffer) delete[] buffer; }
    };
    std::unordered_map<int, ClientBuffer*> m_clientBuffers;
    
    // 心跳超时管理
    std::unordered_map<int, time_t> m_lastActivity;
    static const int HEARTBEAT_TIMEOUT = 60;  // 超时时间 (秒)
    
    static const int MAX_EVENTS = 4096;
    static const int BUFFER_SIZE = 8192;
};

#endif // EPOLLSERVER_H
