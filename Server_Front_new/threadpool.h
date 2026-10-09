#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <queue>
#include <functional>

/**
 * @brief 线程池类
 * 
 * 使用 pthread 实现的线程池，用于处理并发任务。
 */
class ThreadPool
{
public:
    using Task = std::function<void()>;
    
    /**
     * @brief 创建线程池
     * @param threadCount 线程数量，默认4
     */
    explicit ThreadPool(int threadCount = 4);
    ~ThreadPool();
    
    /**
     * @brief 启动线程池
     */
    void start();
    
    /**
     * @brief 停止线程池
     */
    void stop();
    
    /**
     * @brief 添加任务到队列
     * @param task 任务函数
     */
    void addTask(Task task);
    
    /**
     * @brief 获取线程池是否运行中
     */
    bool isRunning() const { return m_running; }

private:
    static void* workerThread(void* arg);
    Task getTask();

private:
    int m_threadCount;
    pthread_t* m_threads;
    
    std::queue<Task> m_taskQueue;
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
    
    bool m_running;
};

#endif // THREADPOOL_H
