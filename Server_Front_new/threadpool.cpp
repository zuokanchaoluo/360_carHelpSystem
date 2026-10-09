#include "threadpool.h"
#include <cstdio>

ThreadPool::ThreadPool(int threadCount)
    : m_threadCount(threadCount)
    , m_threads(nullptr)
    , m_running(false)
{
    pthread_mutex_init(&m_mutex, nullptr);
    pthread_cond_init(&m_cond, nullptr);
}

ThreadPool::~ThreadPool()
{
    stop();
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

void ThreadPool::start()
{
    if (m_running) return;
    
    m_running = true;
    m_threads = new pthread_t[m_threadCount];
    
    for (int i = 0; i < m_threadCount; ++i) {
        pthread_create(&m_threads[i], nullptr, workerThread, this);
        printf("[ThreadPool] Worker thread %d started\n", i);
    }
    
    printf("[ThreadPool] Started with %d threads\n", m_threadCount);
}

void ThreadPool::stop()
{
    if (!m_running) return;
    
    m_running = false;
    
    // 唤醒所有等待的线程
    pthread_cond_broadcast(&m_cond);
    
    // 等待所有线程结束
    for (int i = 0; i < m_threadCount; ++i) {
        pthread_join(m_threads[i], nullptr);
    }
    
    delete[] m_threads;
    m_threads = nullptr;
    
    // 清空任务队列
    pthread_mutex_lock(&m_mutex);
    while (!m_taskQueue.empty()) {
        m_taskQueue.pop();
    }
    pthread_mutex_unlock(&m_mutex);
    
    printf("[ThreadPool] Stopped\n");
}

void ThreadPool::addTask(Task task)
{
    pthread_mutex_lock(&m_mutex);
    m_taskQueue.push(task);
    pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_mutex);
}

ThreadPool::Task ThreadPool::getTask()
{
    pthread_mutex_lock(&m_mutex);
    
    while (m_taskQueue.empty() && m_running) {
        pthread_cond_wait(&m_cond, &m_mutex);
    }
    
    Task task = nullptr;
    if (!m_taskQueue.empty()) {
        task = m_taskQueue.front();
        m_taskQueue.pop();
    }
    
    pthread_mutex_unlock(&m_mutex);
    return task;
}

void* ThreadPool::workerThread(void* arg)
{
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    
    while (pool->m_running) {
        Task task = pool->getTask();
        if (task) {
            task();
        }
    }
    
    return nullptr;
}
