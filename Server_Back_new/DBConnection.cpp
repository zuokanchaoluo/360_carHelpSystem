#include "DBConnection.h"

// 初始化静态成员变量
DBConnection* DBConnection::connection = nullptr;

DBConnection::DBConnection()
{
    // 获取Mysql驱动实例
    this->driver = get_mysql_driver_instance();

    // 构建连接路径，格式: tcp://IP:Port
    string p1 = "tcp://" + localhost;
    string p2 = ":" + localport;

    this->path = p1 + p2;
    cout << "this->path = " << this->path << endl;

    // 创建数据库连接对象
    this->conn.reset(this->driver->connect(this->path, localuser, localpassword));

    this->conn->setSchema(localschema);

    cout << "数据库连接成功" << endl;
}

DBConnection* DBConnection::getInstance()
{
    // 获取单例实例
    if (connection == nullptr) {
        connection = new DBConnection();
    }
    return connection;
}

DBConnection::~DBConnection()
{
    disConnection();
}

sql::Connection* DBConnection::getConnection()
{
    // 返回底层连接指针
    return this->conn.get();
}

void DBConnection::disConnection()
{
    // 关闭连接并释放资源
    if (this->connection) {

        this->conn->close();
        this->conn.reset();
    }
}
