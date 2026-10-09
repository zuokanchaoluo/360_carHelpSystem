#include "UserOperation.h"

UserOperation::UserOperation()
{
}

UserOperation::~UserOperation()
{
}

int UserOperation::login(const string& username, const string& pwdMd5,
                         uint32_t& outUserId, string& outNickname)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();

    try {
        PreparedStatement* pstmt = conn->prepareStatement(
            "SELECT id, nickname, password_hash FROM user WHERE username = ?");
        
        pstmt->setString(1, username);
        ResultSet* rs = pstmt->executeQuery();
        
        if (rs->next()) {
            string dbPassword = rs->getString("password_hash");
            
            if (dbPassword == pwdMd5) {
                outUserId = rs->getUInt("id");
                outNickname = rs->getString("nickname");
                delete rs;
                delete pstmt;
                return SUCCESS;
            } else {
                delete rs;
                delete pstmt;
                return ERR_NAME_OR_PWD_ERR;
            }
        } else {
            delete rs;
            delete pstmt;
            return ERR_NAME_OR_PWD_ERR;
        }
    } catch (SQLException& e) {
        cout << "[UserOperation] login error: " << e.what() << endl;
        return ERR_UPLOAD;
    }
}

int UserOperation::registerUser(const string& username, const string& nickname,
                                 const string& pwdMd5)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();

    try {
        if (userExists(username)) {
            return ERR_USER_EXIST;
        }
        
        conn->setAutoCommit(false);
        
        PreparedStatement* pstmt = conn->prepareStatement(
            "INSERT INTO user (username, nickname, password_hash) VALUES (?, ?, ?)");
        
        pstmt->setString(1, username);
        pstmt->setString(2, nickname);
        pstmt->setString(3, pwdMd5);
        
        int res = pstmt->executeUpdate();
        
        if (res > 0) {
            conn->commit();
            delete pstmt;
            cout << "[UserOperation] Register success: " << username << endl;
            return SUCCESS;
        } else {
            conn->rollback();
            delete pstmt;
            return ERR_UPLOAD;
        }
    } catch (SQLException& e) {
        cout << "[UserOperation] register error: " << e.what() << endl;
        try { conn->rollback(); } catch (...) {}
        return ERR_UPLOAD;
    }
}

bool UserOperation::userExists(const string& username)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();

    try {
        PreparedStatement* pstmt = conn->prepareStatement(
            "SELECT id FROM user WHERE username = ?");
        
        pstmt->setString(1, username);
        ResultSet* rs = pstmt->executeQuery();
        
        bool exists = rs->next();
        
        delete rs;
        delete pstmt;
        return exists;
    } catch (SQLException& e) {
        cout << "[UserOperation] userExists error: " << e.what() << endl;
        return false;
    }
}
