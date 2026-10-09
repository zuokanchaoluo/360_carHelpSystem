#ifndef IPC_H
#define IPC_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <stdint.h>
#include <cstring>

// =============================================================
// IPC 配置
// =============================================================
#define IPC_KEY_SHM     0x360001    // 共享内存 key
#define IPC_KEY_MSG_F2B 0x360002    // 消息队列 key (Front -> Back)
#define IPC_KEY_MSG_B2F 0x360003    // 消息队列 key (Back -> Front)
#define IPC_KEY_SEM     0x360004    // 信号量 key

#define SHM_SIZE        (4 * 1024 * 1024)   // 共享内存大小 4MB
#define SHM_BLOCK_SIZE  (4 * 1024)          // 单个数据块大小 4KB
#define SHM_BLOCK_COUNT (SHM_SIZE / SHM_BLOCK_SIZE)  // 数据块数量 (1024)

// =============================================================
// 消息队列消息结构
// =============================================================
struct IpcMessage {
    long mtype;             // 消息类型 (必须 > 0)
    int clientFd;           // 客户端 fd
    uint16_t funcId;        // 功能号
    uint32_t dataLen;       // 数据长度
    int shmBlockIndex;      // 共享内存块索引 (-1 表示无数据)
};

// =============================================================
// 共享内存块头
// =============================================================
struct ShmBlockHeader {
    int inUse;              // 是否被使用
    int clientFd;           // 客户端 fd
    uint16_t funcId;        // 功能号
    uint32_t dataLen;       // 数据长度
};

// =============================================================
// 共享内存管理类
// =============================================================
class SharedMemory {
public:
    SharedMemory();
    ~SharedMemory();
    
    // 创建/获取共享内存
    bool create();
    bool attach();
    void detach();
    void destroy();
    
    // 分配/释放数据块
    int allocBlock();
    void freeBlock(int index);
    
    // 读写数据
    bool writeData(int blockIndex, const void* data, uint32_t len);
    bool readData(int blockIndex, void* data, uint32_t maxLen, uint32_t* actualLen);
    
    // 获取块指针
    void* getBlockData(int blockIndex);
    ShmBlockHeader* getBlockHeader(int blockIndex);
    
    bool isValid() const { return m_shmAddr != nullptr; }

private:
    int m_shmId;
    void* m_shmAddr;
    int m_semId;    // 信号量用于同步
};

// =============================================================
// 消息队列管理类
// =============================================================
class MessageQueue {
public:
    MessageQueue();
    ~MessageQueue();
    
    // 创建/获取消息队列
    bool create(key_t key);
    bool open(key_t key);
    void destroy();
    
    // 发送/接收消息
    bool send(const IpcMessage& msg);
    bool receive(IpcMessage& msg, long mtype = 0, bool block = true);
    
    bool isValid() const { return m_msgId >= 0; }

private:
    int m_msgId;
};

#endif // IPC_H
