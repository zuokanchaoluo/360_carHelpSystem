#include "ipc.h"
#include <cstdio>
#include <errno.h>

// =============================================================
// SharedMemory 实现
// =============================================================

SharedMemory::SharedMemory()
    : m_shmId(-1)
    , m_shmAddr(nullptr)
    , m_semId(-1)
{
}

SharedMemory::~SharedMemory()
{
    detach();
}

bool SharedMemory::create()
{
    // 创建共享内存
    m_shmId = shmget(IPC_KEY_SHM, SHM_SIZE, IPC_CREAT | IPC_EXCL | 0666);
    if (m_shmId < 0) {
        if (errno == EEXIST) {
            // 已存在，直接获取
            m_shmId = shmget(IPC_KEY_SHM, SHM_SIZE, 0666);
        }
        if (m_shmId < 0) {
            perror("[SharedMemory] shmget failed");
            return false;
        }
    }
    
    // 附加共享内存
    m_shmAddr = shmat(m_shmId, nullptr, 0);
    if (m_shmAddr == (void*)-1) {
        perror("[SharedMemory] shmat failed");
        m_shmAddr = nullptr;
        return false;
    }
    
    // 创建信号量
    m_semId = semget(IPC_KEY_SEM, SHM_BLOCK_COUNT, IPC_CREAT | 0666);
    if (m_semId < 0) {
        perror("[SharedMemory] semget failed");
        return false;
    }
    
    // 初始化所有块为未使用
    for (int i = 0; i < SHM_BLOCK_COUNT; ++i) {
        ShmBlockHeader* header = getBlockHeader(i);
        header->inUse = 0;
    }
    
    printf("[SharedMemory] Created: shmId=%d, size=%d, blocks=%d\n", 
           m_shmId, SHM_SIZE, SHM_BLOCK_COUNT);
    return true;
}

bool SharedMemory::attach()
{
    // 获取已存在的共享内存
    m_shmId = shmget(IPC_KEY_SHM, SHM_SIZE, 0666);
    if (m_shmId < 0) {
        perror("[SharedMemory] shmget failed");
        return false;
    }
    
    // 附加共享内存
    m_shmAddr = shmat(m_shmId, nullptr, 0);
    if (m_shmAddr == (void*)-1) {
        perror("[SharedMemory] shmat failed");
        m_shmAddr = nullptr;
        return false;
    }
    
    // 获取信号量
    m_semId = semget(IPC_KEY_SEM, SHM_BLOCK_COUNT, 0666);
    if (m_semId < 0) {
        perror("[SharedMemory] semget failed");
        return false;
    }
    
    printf("[SharedMemory] Attached: shmId=%d\n", m_shmId);
    return true;
}

void SharedMemory::detach()
{
    if (m_shmAddr) {
        shmdt(m_shmAddr);
        m_shmAddr = nullptr;
    }
}

void SharedMemory::destroy()
{
    detach();
    
    if (m_shmId >= 0) {
        shmctl(m_shmId, IPC_RMID, nullptr);
        m_shmId = -1;
    }
    
    if (m_semId >= 0) {
        semctl(m_semId, 0, IPC_RMID);
        m_semId = -1;
    }
    
    printf("[SharedMemory] Destroyed\n");
}

int SharedMemory::allocBlock()
{
    if (!m_shmAddr) return -1;
    
    // 查找空闲块
    for (int i = 0; i < SHM_BLOCK_COUNT; ++i) {
        ShmBlockHeader* header = getBlockHeader(i);
        if (header->inUse == 0) {
            header->inUse = 1;
            return i;
        }
    }
    
    printf("[SharedMemory] No free block available!\n");
    return -1;
}

void SharedMemory::freeBlock(int index)
{
    if (!m_shmAddr || index < 0 || index >= SHM_BLOCK_COUNT) return;
    
    ShmBlockHeader* header = getBlockHeader(index);
    header->inUse = 0;
}

bool SharedMemory::writeData(int blockIndex, const void* data, uint32_t len)
{
    if (!m_shmAddr || blockIndex < 0 || blockIndex >= SHM_BLOCK_COUNT) return false;
    
    uint32_t maxDataLen = SHM_BLOCK_SIZE - sizeof(ShmBlockHeader);
    if (len > maxDataLen) {
        printf("[SharedMemory] Data too large: %u > %u\n", len, maxDataLen);
        return false;
    }
    
    ShmBlockHeader* header = getBlockHeader(blockIndex);
    header->dataLen = len;
    
    void* dataPtr = getBlockData(blockIndex);
    memcpy(dataPtr, data, len);
    
    return true;
}

bool SharedMemory::readData(int blockIndex, void* data, uint32_t maxLen, uint32_t* actualLen)
{
    if (!m_shmAddr || blockIndex < 0 || blockIndex >= SHM_BLOCK_COUNT) return false;
    
    ShmBlockHeader* header = getBlockHeader(blockIndex);
    uint32_t len = header->dataLen;
    
    if (len > maxLen) {
        len = maxLen;
    }
    
    void* dataPtr = getBlockData(blockIndex);
    memcpy(data, dataPtr, len);
    
    if (actualLen) *actualLen = len;
    return true;
}

ShmBlockHeader* SharedMemory::getBlockHeader(int blockIndex)
{
    char* base = (char*)m_shmAddr + blockIndex * SHM_BLOCK_SIZE;
    return (ShmBlockHeader*)base;
}

void* SharedMemory::getBlockData(int blockIndex)
{
    char* base = (char*)m_shmAddr + blockIndex * SHM_BLOCK_SIZE;
    return base + sizeof(ShmBlockHeader);
}


// =============================================================
// MessageQueue 实现
// =============================================================

MessageQueue::MessageQueue()
    : m_msgId(-1)
{
}

MessageQueue::~MessageQueue()
{
    // 不自动销毁，由调用者决定
}

bool MessageQueue::create(key_t key)
{
    m_msgId = msgget(key, IPC_CREAT | IPC_EXCL | 0666);
    if (m_msgId < 0) {
        if (errno == EEXIST) {
            m_msgId = msgget(key, 0666);
        }
        if (m_msgId < 0) {
            perror("[MessageQueue] msgget failed");
            return false;
        }
    }
    
    printf("[MessageQueue] Created: msgId=%d, key=0x%x\n", m_msgId, key);
    return true;
}

bool MessageQueue::open(key_t key)
{
    m_msgId = msgget(key, 0666);
    if (m_msgId < 0) {
        perror("[MessageQueue] msgget failed");
        return false;
    }
    
    printf("[MessageQueue] Opened: msgId=%d, key=0x%x\n", m_msgId, key);
    return true;
}

void MessageQueue::destroy()
{
    if (m_msgId >= 0) {
        msgctl(m_msgId, IPC_RMID, nullptr);
        printf("[MessageQueue] Destroyed: msgId=%d\n", m_msgId);
        m_msgId = -1;
    }
}

bool MessageQueue::send(const IpcMessage& msg)
{
    if (m_msgId < 0) return false;
    
    int ret = msgsnd(m_msgId, &msg, sizeof(IpcMessage) - sizeof(long), 0);
    if (ret < 0) {
        perror("[MessageQueue] msgsnd failed");
        return false;
    }
    
    return true;
}

bool MessageQueue::receive(IpcMessage& msg, long mtype, bool block)
{
    if (m_msgId < 0) return false;
    
    int flags = block ? 0 : IPC_NOWAIT;
    ssize_t ret = msgrcv(m_msgId, &msg, sizeof(IpcMessage) - sizeof(long), mtype, flags);
    if (ret < 0) {
        if (errno != ENOMSG) {
            perror("[MessageQueue] msgrcv failed");
        }
        return false;
    }
    
    return true;
}
