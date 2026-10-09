#pragma once
#include <iostream>
#include <mysql_connection.h>
#include <mysql_driver.h>
#include <string>
using namespace std;
using namespace sql;
using namespace sql::mysql;

const string localhost = "127.0.0.1";
const string localuser = "root";
const string localpassword = "Password_123";
const string localport = "3306";
const string localschema = "OMO250701"; // 数据库名称
class DBConnection
{
public:
	static DBConnection* connection;
	static DBConnection* getInstance();
	~DBConnection();

	sql::Connection* getConnection();
	void disConnection();
private:
	DBConnection();
	MySQL_Driver* driver;

	unique_ptr<sql::Connection> conn;
	string path;
};

