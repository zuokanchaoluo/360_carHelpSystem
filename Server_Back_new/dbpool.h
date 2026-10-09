#pragma once
#include <iostream>
#include <queue>
#include <pthread.h>
#include <mysql_connection.h>
#include <mysql_driver.h>
#include <string>

using namespace std;
using namespace sql;
using namespace sql::mysql;

/**
 * @brief 数据库连接池
 * 
 * 预创建多个 MySQL 连接，线程安全地借用/归还。
 * 用于替代单例 DBConnection，支持多线程并发数据库操作。
 */
class DBPool {
public:
    static DBPool& instance();

    /**
     * @brief 初始化连接池
     * @param poolSize 连接池大小，默认 20
     */
    void init(int poolSize = 20);

    /**
     * @brief 从池中获取一个连接（阻塞等待）
     * @return sql::Connection* 数据库连接指针
     */
    sql::Connection* acquire();

    /**
     * @brief 归还连接到池中
     * @param conn 要归还的连接指针
     */
    void release(sql::Connection* conn);

    /**
     * @brief 销毁连接池，关闭所有连接
     */
    void destroy();

    /**
     * @brief RAII 连接守卫，自动获取和归还连接
     * 
     * 用法：
     *   DBPool::Guard guard(DBPool::instance());
     *   sql::Connection* conn = guard.get();
     */
    class Guard {
    public:
        Guard(DBPool& pool) : m_pool(pool), m_conn(pool.acquire()) {}
        ~Guard() { if (m_conn) m_pool.release(m_conn); }

        sql::Connection* get() { return m_conn; }

        // 禁止拷贝
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    private:
        DBPool& m_pool;
        sql::Connection* m_conn;
    };

private:
    DBPool();
    ~DBPool();
    DBPool(const DBPool&) = delete;
    DBPool& operator=(const DBPool&) = delete;

    sql::Connection* createConnection();

    std::queue<sql::Connection*> m_pool;
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
    bool m_initialized;
};
