#include "dbpool.h"
#include "DBConnection.h"  // 复用已有的连接参数

DBPool& DBPool::instance()
{
    static DBPool inst;
    return inst;
}

DBPool::DBPool()
    : m_initialized(false)
{
    pthread_mutex_init(&m_mutex, nullptr);
    pthread_cond_init(&m_cond, nullptr);
}

DBPool::~DBPool()
{
    destroy();
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

sql::Connection* DBPool::createConnection()
{
    MySQL_Driver* driver = get_mysql_driver_instance();
    string url = "tcp://" + localhost + ":" + localport;

    sql::Connection* conn = driver->connect(url, localuser, localpassword);
    conn->setSchema(localschema);
    return conn;
}

void DBPool::init(int poolSize)
{
    pthread_mutex_lock(&m_mutex);
    if (m_initialized) {
        pthread_mutex_unlock(&m_mutex);
        return;
    }

    cout << "[DBPool] 初始化连接池, 大小: " << poolSize << endl;

    for (int i = 0; i < poolSize; i++) {
        try {
            sql::Connection* conn = createConnection();
            m_pool.push(conn);
            cout << "[DBPool] 创建连接 " << (i + 1) << "/" << poolSize << endl;
        } catch (SQLException& e) {
            cout << "[DBPool] 创建连接失败: " << e.what() << endl;
        }
    }

    m_initialized = true;
    cout << "[DBPool] 连接池就绪, 可用连接: " << m_pool.size() << endl;
    pthread_mutex_unlock(&m_mutex);
}

sql::Connection* DBPool::acquire()
{
    pthread_mutex_lock(&m_mutex);

    // 等待直到有可用连接
    while (m_pool.empty()) {
        cout << "[DBPool] 无可用连接，等待归还..." << endl;
        pthread_cond_wait(&m_cond, &m_mutex);
    }

    sql::Connection* conn = m_pool.front();
    m_pool.pop();

    pthread_mutex_unlock(&m_mutex);

    // 检查连接是否仍然有效，无效则重新创建
    try {
        if (conn->isClosed()) {
            cout << "[DBPool] 连接已关闭，重新创建" << endl;
            delete conn;
            conn = createConnection();
        }
    } catch (...) {
        cout << "[DBPool] 连接检查异常，重新创建" << endl;
        try { delete conn; } catch (...) {}
        conn = createConnection();
    }

    return conn;
}

void DBPool::release(sql::Connection* conn)
{
    if (!conn) return;

    pthread_mutex_lock(&m_mutex);
    m_pool.push(conn);
    pthread_cond_signal(&m_cond);  // 唤醒一个等待的线程
    pthread_mutex_unlock(&m_mutex);
}

void DBPool::destroy()
{
    pthread_mutex_lock(&m_mutex);
    while (!m_pool.empty()) {
        sql::Connection* conn = m_pool.front();
        m_pool.pop();
        try {
            conn->close();
            delete conn;
        } catch (...) {}
    }
    m_initialized = false;
    cout << "[DBPool] 连接池已销毁" << endl;
    pthread_mutex_unlock(&m_mutex);
}
