#include "epollserver.h"
#include "threadpool.h"
#include "ipc.h"
#include "protocol.h"
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <pthread.h>

// 全局变量
static EpollServer* g_server = nullptr;
static ThreadPool* g_threadPool = nullptr;
static SharedMemory* g_shm = nullptr;
static MessageQueue* g_msgToBack = nullptr;    // Front -> Back
static MessageQueue* g_msgFromBack = nullptr;  // Back -> Front
static pthread_t g_responseThread;
static bool g_running = true;

// 信号处理
void signalHandler(int sig)
{
    printf("\n[Main] Received signal %d, shutting down...\n", sig);
    g_running = false;
    if (g_server) {
        g_server->stop();
    }
    if (g_threadPool) {
        g_threadPool->stop();
    }
}

// 转发请求到后置服务器
bool forwardToBackServer(int clientFd, uint16_t funcId, const char* data, uint32_t len)
{
    // 1. 分配共享内存块
    int blockIndex = -1;
    if (len > 0 && data) {
        blockIndex = g_shm->allocBlock();
        if (blockIndex < 0) {
            printf("[Forward] Failed to alloc shm block\n");
            return false;
        }
        
        // 2. 写入数据到共享内存
        if (!g_shm->writeData(blockIndex, data, len)) {
            g_shm->freeBlock(blockIndex);
            return false;
        }
    }
    
    // 3. 发送消息到后置服务器
    IpcMessage msg;
    msg.mtype = 1;  // 消息类型
    msg.clientFd = clientFd;
    msg.funcId = funcId;
    msg.dataLen = len;
    msg.shmBlockIndex = blockIndex;
    
    if (!g_msgToBack->send(msg)) {
        if (blockIndex >= 0) g_shm->freeBlock(blockIndex);
        return false;
    }
    
    printf("[Forward] Sent to back server: funcId=%d, clientFd=%d, block=%d\n",
           funcId, clientFd, blockIndex);
    return true;
}

// 处理后置服务器的响应（独立线程）
void* responseHandler(void* arg)
{
    printf("[ResponseHandler] Started\n");
    
    while (g_running) {
        IpcMessage msg;
        if (g_msgFromBack->receive(msg, 0, true)) {
            printf("[ResponseHandler] Received response: funcId=%d, clientFd=%d\n",
                   msg.funcId, msg.clientFd);
            
            // 从共享内存读取响应数据
            if (msg.shmBlockIndex >= 0) {
                char buffer[4096];
                uint32_t actualLen = 0;
                
                if (g_shm->readData(msg.shmBlockIndex, buffer, sizeof(buffer), &actualLen)) {
                    // 发送响应给客户端
                    g_server->sendPacket(msg.clientFd, msg.funcId, buffer, actualLen);
                }
                
                // 释放共享内存块
                g_shm->freeBlock(msg.shmBlockIndex);
            } else {
                // 无数据的响应
                BASERES res;
                res.resCode = SUCCESS;
                g_server->sendPacket(msg.clientFd, msg.funcId, &res, sizeof(res));
            }
        }
    }
    
    printf("[ResponseHandler] Stopped\n");
    return nullptr;
}

// 处理收到的数据包
void handlePacket(int clientFd, uint16_t funcId, const char* data, uint32_t len)
{
    printf("[Handler] Processing funcId=%d from client fd=%d\n", funcId, clientFd);
    
    switch (funcId) {
        case FID_HEARTBEAT: {
            // 心跳包，直接回复（不需要转发）
            BASERES res;
            res.resCode = SUCCESS;
            g_server->sendPacket(clientFd, FID_HEARTBEAT, &res, sizeof(res));
            break;
        }
        
        case FID_LOGIN:
        case FID_REGISTER:
        case FID_GET_SMS:
        case FID_IMG_UPLOAD:
        case FID_IMG_UPLOAD_SLICE:
        case FID_GET_IMG_LIST:
        case FID_GET_VIDEO_LIST:
        case FID_RECORD_UPLOAD:
        case FID_INFO_UPLOAD: {
            // 需要转发到后置服务器处理
            if (!forwardToBackServer(clientFd, funcId, data, len)) {
                BASERES res;
                res.resCode = ERR_UPLOAD;
                g_server->sendPacket(clientFd, funcId, &res, sizeof(res));
            }
            break;
        }
        
        default:
            printf("[Handler] Unknown funcId: %d\n", funcId);
            BASERES res;
            res.resCode = 255;
            g_server->sendPacket(clientFd, funcId, &res, sizeof(res));
            break;
    }
}

int main(int argc, char* argv[])
{
    // 默认端口
    int port = 8080;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    
    printf("========================================\n");
    printf("  360Car Front Server\n");
    printf("  Port: %d\n", port);
    printf("========================================\n");
    
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, SIG_IGN);
    
    // 初始化 IPC
    printf("\n[Main] Initializing IPC...\n");
    
    g_shm = new SharedMemory();
    if (!g_shm->create()) {
        printf("[Main] Failed to create shared memory\n");
        return 1;
    }
    
    g_msgToBack = new MessageQueue();
    if (!g_msgToBack->create(IPC_KEY_MSG_F2B)) {
        printf("[Main] Failed to create message queue (F2B)\n");
        return 1;
    }
    
    g_msgFromBack = new MessageQueue();
    if (!g_msgFromBack->create(IPC_KEY_MSG_B2F)) {
        printf("[Main] Failed to create message queue (B2F)\n");
        return 1;
    }
    
    // 创建线程池
    printf("\n[Main] Starting thread pool...\n");
    g_threadPool = new ThreadPool(16);
    g_threadPool->start();
    
    // 创建服务器
    printf("\n[Main] Starting TCP server...\n");
    g_server = new EpollServer(port, g_threadPool);
    g_server->setPacketHandler(handlePacket);
    
    if (!g_server->start()) {
        printf("[Main] Failed to start server\n");
        delete g_server;
        delete g_threadPool;
        return 1;
    }
    
    // 启动响应处理线程
    pthread_create(&g_responseThread, nullptr, responseHandler, nullptr);
    
    printf("\n[Main] Front Server is ready!\n");
    printf("[Main] Waiting for back server...\n\n");
    
    // 运行事件循环
    g_server->run();
    
    // 等待响应线程结束
    pthread_join(g_responseThread, nullptr);
    
    // 清理
    g_msgToBack->destroy();
    g_msgFromBack->destroy();
    g_shm->destroy();
    
    delete g_msgToBack;
    delete g_msgFromBack;
    delete g_shm;
    delete g_server;
    delete g_threadPool;
    
    printf("[Main] Server shutdown complete\n");
    return 0;
}