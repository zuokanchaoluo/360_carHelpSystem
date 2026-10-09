#include "ImageOperation.h"

ImageOperation::ImageOperation()
{
}

ImageOperation::~ImageOperation()
{
}

vector<PHOTOINFO> ImageOperation::queryImageList(uint32_t userId, int pageNumber, int pageSize, int& outTotal)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();
    vector<PHOTOINFO> result;
    
    try {
        PreparedStatement* countStmt = conn->prepareStatement(
            "SELECT COUNT(*) as total FROM feature_image WHERE user_id = ?");
        countStmt->setUInt(1, userId);
        ResultSet* countRs = countStmt->executeQuery();
        
        if (countRs->next()) {
            outTotal = countRs->getInt("total");
        }
        delete countRs;
        delete countStmt;
        
        int offset = (pageNumber - 1) * pageSize;
        PreparedStatement* pstmt = conn->prepareStatement(
            "SELECT file_name, file_path, create_time "
            "FROM feature_image WHERE user_id = ? ORDER BY create_time DESC LIMIT ? OFFSET ?");
        
        pstmt->setUInt(1, userId);
        pstmt->setInt(2, pageSize);
        pstmt->setInt(3, offset);
        
        ResultSet* rs = pstmt->executeQuery();
        
        while (rs->next()) {
            PHOTOINFO info;
            memset(&info, 0, sizeof(info));
            
            string fileName = rs->getString("file_name");
            strncpy(info.fileName, fileName.c_str(), sizeof(info.fileName) - 1);
            
            string filePath = rs->getString("file_path");
            strncpy(info.filePath, filePath.c_str(), sizeof(info.filePath) - 1);
            
            string createTime = rs->getString("create_time");
            strncpy(info.createTime, createTime.c_str(), sizeof(info.createTime) - 1);
            
            result.push_back(info);
        }
        
        delete rs;
        delete pstmt;
        
    } catch (SQLException& e) {
        cout << "[ImageOperation] queryImageList error: " << e.what() << endl;
    }
    
    return result;
}

bool ImageOperation::addImage(uint32_t userId, const PHOTOINFO& info, uint64_t fileSize)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();

    try {
        conn->setAutoCommit(false);
        
        PreparedStatement* pstmt = conn->prepareStatement(
            "INSERT INTO feature_image (user_id, file_name, file_path, file_size, create_time) VALUES (?, ?, ?, ?, ?)");
        
        pstmt->setUInt(1, userId);
        pstmt->setString(2, info.fileName);
        pstmt->setString(3, info.filePath);
        pstmt->setUInt64(4, fileSize);
        pstmt->setString(5, info.createTime);
        
        int res = pstmt->executeUpdate();
        
        if (res > 0) {
            conn->commit();
            delete pstmt;
            cout << "[ImageOperation] addImage success: " << info.fileName << endl;
            return true;
        } else {
            conn->rollback();
            delete pstmt;
            return false;
        }
    } catch (SQLException& e) {
        cout << "[ImageOperation] addImage error: " << e.what() << endl;
        try { conn->rollback(); } catch (...) {}
        return false;
    }
}

bool ImageOperation::deleteImage(const string& fileName)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();

    try {
        conn->setAutoCommit(false);
        
        PreparedStatement* pstmt = conn->prepareStatement(
            "DELETE FROM feature_image WHERE file_name = ?");
        
        pstmt->setString(1, fileName);
        
        int res = pstmt->executeUpdate();
        
        if (res > 0) {
            conn->commit();
            delete pstmt;
            return true;
        } else {
            conn->rollback();
            delete pstmt;
            return false;
        }
    } catch (SQLException& e) {
        cout << "[ImageOperation] deleteImage error: " << e.what() << endl;
        try { conn->rollback(); } catch (...) {}
        return false;
    }
}
