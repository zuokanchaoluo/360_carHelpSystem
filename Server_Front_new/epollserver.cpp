#include "epollserver.h"
#include "threadpool.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <vector>

EpollServer::EpollServer(int port, ThreadPool* threadPool)
    : m_port(port)
    , m_listenFd(-1)
    , m_epollFd(-1)
    , m_running(false)
    , m_threadPool(threadPool)
    , m_packetHandler(nullptr)
{
}

EpollServer::~EpollServer()
{
    stop();
}

bool EpollServer::start()
{
    if (!createListenSocket()) {
        return false;
    }
    
    if (!createEpoll()) {
        close(m_listenFd);
        return false;
    }
    
    m_running = true;
    printf("[EpollServer] Started on port %d\n", m_port);
    return true;
}

void EpollServer::stop()
{
    m_running = false;
    
    // 关闭所有客户端
    for (auto& pair : m_clientBuffers) {
        close(pair.first);
        delete pair.second;
    }
    m_clientBuffers.clear();
    m_lastActivity.clear();
    
    if (m_epollFd >= 0) {
        close(m_epollFd);
        m_epollFd = -1;
    }
    
    if (m_listenFd >= 0) {
        close(m_listenFd);
        m_listenFd = -1;
    }
    
    printf("[EpollServer] Stopped\n");
}

bool EpollServer::createListenSocket()
{
    // 创建 socket
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        perror("[EpollServer] socket failed");
        return false;
    }
    
    // 设置端口复用
    int opt = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_port);
    
    if (bind(m_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[EpollServer] bind failed");
        close(m_listenFd);
        return false;
    }
    
    // 监听
    if (listen(m_listenFd, SOMAXCONN) < 0) {
        perror("[EpollServer] listen failed");
        close(m_listenFd);
        return false;
    }
    
    // 设置非阻塞
    setNonBlocking(m_listenFd);
    
    return true;
}

bool EpollServer::createEpoll()
{
    m_epollFd = epoll_create1(0);
    if (m_epollFd < 0) {
        perror("[EpollServer] epoll_create1 failed");
        return false;
    }
    
    // 添加监听 socket 到 epoll
    addToEpoll(m_listenFd, EPOLLIN);
    
    return true;
}

void EpollServer::run()
{
    struct epoll_event events[MAX_EVENTS];
    int heartbeatCounter = 0;
    
    printf("[EpollServer] Running event loop...\n");
    
    while (m_running) {
        int nfds = epoll_wait(m_epollFd, events, MAX_EVENTS, 1000);
        
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("[EpollServer] epoll_wait failed");
            break;
        }
        
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            
            if (fd == m_listenFd) {
                // 新连接
                handleAccept();
            } else if (events[i].events & EPOLLIN) {
                // 可读事件
                handleRead(fd);
            }
        }
        
        // 心跳超时检测（每 5 秒一次）
        if (++heartbeatCounter % 5 == 0) {
            checkHeartbeatTimeout();
        }
    }
}

void EpollServer::handleAccept()
{
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    
    int clientFd = accept(m_listenFd, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientFd < 0) {
        perror("[EpollServer] accept failed");
        return;
    }
    
    // 设置非阻塞
    setNonBlocking(clientFd);
    
    // 添加到 epoll
    addToEpoll(clientFd, EPOLLIN | EPOLLET);
    
    // 创建接收缓冲区
    ClientBuffer* buf = new ClientBuffer();
    buf->buffer = new char[BUFFER_SIZE];
    buf->size = BUFFER_SIZE;
    buf->used = 0;
    m_clientBuffers[clientFd] = buf;
    m_lastActivity[clientFd] = time(nullptr);
    
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
    printf("[EpollServer] Client connected: %s:%d (fd=%d)\n", 
           ip, ntohs(clientAddr.sin_port), clientFd);
}

void EpollServer::handleRead(int clientFd)
{
    auto it = m_clientBuffers.find(clientFd);
    if (it == m_clientBuffers.end()) {
        closeClient(clientFd);
        return;
    }
    
    ClientBuffer* buf = it->second;
    
    // 读取数据
    while (true) {
        int remaining = buf->size - buf->used;
        if (remaining <= 0) {
            // 缓冲区已满，扩容
            int newSize = buf->size * 2;
            char* newBuf = new char[newSize];
            memcpy(newBuf, buf->buffer, buf->used);
            delete[] buf->buffer;
            buf->buffer = newBuf;
            buf->size = newSize;
            remaining = buf->size - buf->used;
        }
        
        ssize_t n = recv(clientFd, buf->buffer + buf->used, remaining, 0);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据读完
                break;
            }
            perror("[EpollServer] recv failed");
            closeClient(clientFd);
            return;
        } else if (n == 0) {
            // 客户端关闭连接
            printf("[EpollServer] Client disconnected (fd=%d)\n", clientFd);
            closeClient(clientFd);
            return;
        }
        
        buf->used += n;
    }
    
    // 收到数据，刷新活跃时间
    m_lastActivity[clientFd] = time(nullptr);
    
    // 解析协议包
    while (buf->used >= (int)sizeof(HEAD)) {
        HEAD* head = (HEAD*)buf->buffer;
        int packetLen = sizeof(HEAD) + head->length;
        
        if (buf->used < packetLen) {
            // 数据不完整，等待更多数据
            break;
        }
        
        // 提取完整的包
        char* body = buf->buffer + sizeof(HEAD);
        
        // 放入线程池处理
        int fd = clientFd;
        HEAD headCopy = *head;
        char* bodyCopy = nullptr;
        if (head->length > 0) {
            bodyCopy = new char[head->length];
            memcpy(bodyCopy, body, head->length);
        }
        
        m_threadPool->addTask([this, fd, headCopy, bodyCopy]() {
            processPacket(fd, headCopy, bodyCopy);
            if (bodyCopy) delete[] bodyCopy;
        });
        
        // 移除已处理的数据
        int consumed = packetLen;
        memmove(buf->buffer, buf->buffer + consumed, buf->used - consumed);
        buf->used -= consumed;
    }
}

void EpollServer::processPacket(int clientFd, const HEAD& head, const char* body)
{
    printf("[EpollServer] Processing packet from fd=%d, funcId=%d, len=%d\n",
           clientFd, head.type, head.length);
    
    if (m_packetHandler) {
        m_packetHandler(clientFd, head.type, body, head.length);
    }
}

void EpollServer::sendPacket(int clientFd, uint16_t funcId, const void* data, uint32_t len)
{
    HEAD head;
    memset(&head, 0, sizeof(HEAD));
    head.type = funcId;
    head.length = len;
    head.crc = 0;  // TODO: 计算 CRC
    
    // 发送协议头
    send(clientFd, &head, sizeof(HEAD), 0);
    
    // 发送数据体
    if (len > 0 && data) {
        send(clientFd, data, len, 0);
    }
    
    printf("[EpollServer] Sent packet to fd=%d, funcId=%d, len=%d\n", clientFd, funcId, len);
}

void EpollServer::closeClient(int clientFd)
{
    removeFromEpoll(clientFd);
    close(clientFd);
    
    auto it = m_clientBuffers.find(clientFd);
    if (it != m_clientBuffers.end()) {
        delete it->second;
        m_clientBuffers.erase(it);
    }
    m_lastActivity.erase(clientFd);
}

void EpollServer::setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void EpollServer::addToEpoll(int fd, uint32_t events)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
}

void EpollServer::removeFromEpoll(int fd)
{
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
}

void EpollServer::checkHeartbeatTimeout()
{
    time_t now = time(nullptr);
    // 收集超时 fd（避免遍历时修改 map）
    std::vector<int> timeoutFds;
    for (auto& pair : m_lastActivity) {
        if (now - pair.second > HEARTBEAT_TIMEOUT) {
            timeoutFds.push_back(pair.first);
        }
    }
    for (int fd : timeoutFds) {
        printf("[EpollServer] Heartbeat timeout: fd=%d, closing...\n", fd);
        closeClient(fd);
    }
}
