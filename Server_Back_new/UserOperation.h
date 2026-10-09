#pragma once
#include "dbpool.h"
#include <iostream>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <string>
#include "protocol.h"

using namespace sql;
using namespace std;

/**
 * @brief 用户操作类
 * 
 * 负责用户表的 CRUD 操作。
 * 每次操作从连接池获取连接，操作完自动归还。
 */
class UserOperation
{
public:
    UserOperation();
    ~UserOperation();
    
    int login(const string& username, const string& pwdMd5, 
              uint32_t& outUserId, string& outNickname);
    
    int registerUser(const string& username, const string& nickname, 
                     const string& pwdMd5);
    
    bool userExists(const string& username);
};
