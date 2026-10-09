#include "VideoOperation.h"

VideoOperation::VideoOperation()
{
}

VideoOperation::~VideoOperation()
{
}

vector<VIDEORECORD> VideoOperation::queryVideoList(uint32_t userId, int pageNumber, int pageSize, int& outTotal)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();
    vector<VIDEORECORD> result;
    
    try {
        PreparedStatement* countStmt = conn->prepareStatement(
            "SELECT COUNT(*) as total FROM video WHERE user_id = ?");
        countStmt->setUInt(1, userId);
        ResultSet* countRs = countStmt->executeQuery();
        
        if (countRs->next()) {
            outTotal = countRs->getInt("total");
        }
        delete countRs;
        delete countStmt;
        
        int offset = (pageNumber - 1) * pageSize;
        PreparedStatement* pstmt = conn->prepareStatement(
            "SELECT v.id, v.video_name, v.client_path, v.cover_path, v.create_time, "
            "IFNULL(MAX(r.total_duration), 0) AS total_duration, "
            "IFNULL(MAX(r.watch_duration), 0) AS watch_duration "
            "FROM video v "
            "LEFT JOIN video_record r ON v.id = r.video_id AND r.user_id = v.user_id "
            "WHERE v.user_id = ? GROUP BY v.id ORDER BY v.create_time DESC LIMIT ? OFFSET ?");
        
        pstmt->setUInt(1, userId);
        pstmt->setInt(2, pageSize);
        pstmt->setInt(3, offset);
        
        ResultSet* rs = pstmt->executeQuery();
        
        while (rs->next()) {
            VIDEORECORD record;
            memset(&record, 0, sizeof(record));
            
            record.video_id = rs->getUInt("id");
            
            string fileName = rs->getString("video_name");
            strncpy(record.fileName, fileName.c_str(), sizeof(record.fileName) - 1);
            
            string filePath = rs->getString("client_path");
            strncpy(record.filePath, filePath.c_str(), sizeof(record.filePath) - 1);
            
            string coverPath = rs->getString("cover_path");
            strncpy(record.coverPath, coverPath.c_str(), sizeof(record.coverPath) - 1);
            
            record.durationSeconds = rs->getUInt("total_duration");
            record.progressSeconds = rs->getUInt("watch_duration");
            
            string createTime = rs->getString("create_time");
            strncpy(record.createTime, createTime.c_str(), sizeof(record.createTime) - 1);
            
            result.push_back(record);
        }
        
        delete rs;
        delete pstmt;
        
    } catch (SQLException& e) {
        cout << "[VideoOperation] queryVideoList error: " << e.what() << endl;
    }
    
    return result;
}

bool VideoOperation::addVideo(uint32_t userId, const VIDEORECORD& record, uint32_t fileSize)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();

    try {
        PreparedStatement* checkStmt = conn->prepareStatement(
            "SELECT id FROM video WHERE user_id = ? AND video_name = ?");
        checkStmt->setUInt(1, userId);
        checkStmt->setString(2, record.fileName);
        ResultSet* rs = checkStmt->executeQuery();
        
        if (rs->next()) {
            cout << "[VideoOperation] addVideo skipped (duplicate): " << record.fileName << endl;
            delete rs;
            delete checkStmt;
            return true;
        }
        delete rs;
        delete checkStmt;
        
        conn->setAutoCommit(false);
        
        PreparedStatement* pstmt = conn->prepareStatement(
            "INSERT INTO video (user_id, video_name, client_path, cover_path, file_size, status) "
            "VALUES (?, ?, ?, ?, ?, 0)");
        
        pstmt->setUInt(1, userId);
        pstmt->setString(2, record.fileName);
        pstmt->setString(3, record.filePath);
        pstmt->setString(4, record.coverPath);
        pstmt->setUInt(5, fileSize);
        
        int res = pstmt->executeUpdate();
        
        if (res > 0) {
            conn->commit();
            delete pstmt;
            cout << "[VideoOperation] addVideo success: " << record.fileName << endl;
            return true;
        } else {
            conn->rollback();
            delete pstmt;
            return false;
        }
    } catch (SQLException& e) {
        cout << "[VideoOperation] addVideo error: " << e.what() << endl;
        try { conn->rollback(); } catch (...) {}
        return false;
    }
}

bool VideoOperation::updateProgress(uint32_t userId, uint32_t videoId, uint32_t progressSeconds, uint32_t totalDuration)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();

    try {
        PreparedStatement* checkStmt = conn->prepareStatement(
            "SELECT id FROM video_record WHERE video_id = ? AND user_id = ?");
        checkStmt->setUInt(1, videoId);
        checkStmt->setUInt(2, userId);
        ResultSet* rs = checkStmt->executeQuery();
        bool exists = rs->next();
        delete rs;
        delete checkStmt;
        
        conn->setAutoCommit(false);
        PreparedStatement* pstmt;
        
        if (exists) {
            pstmt = conn->prepareStatement(
                "UPDATE video_record SET watch_duration = ?, total_duration = ? "
                "WHERE video_id = ? AND user_id = ?");
            pstmt->setUInt(1, progressSeconds);
            pstmt->setUInt(2, totalDuration);
            pstmt->setUInt(3, videoId);
            pstmt->setUInt(4, userId);
        } else {
            pstmt = conn->prepareStatement(
                "INSERT INTO video_record (video_id, user_id, total_duration, watch_duration) "
                "VALUES (?, ?, ?, ?)");
            pstmt->setUInt(1, videoId);
            pstmt->setUInt(2, userId);
            pstmt->setUInt(3, totalDuration);
            pstmt->setUInt(4, progressSeconds);
        }
        
        int res = pstmt->executeUpdate();
        
        if (res > 0) {
            conn->commit();
            delete pstmt;
            cout << "[VideoOperation] updateProgress OK: videoId=" << videoId 
                 << " userId=" << userId << " progress=" << progressSeconds 
                 << "/" << totalDuration << endl;
            return true;
        } else {
            conn->rollback();
            delete pstmt;
            return false;
        }
    } catch (SQLException& e) {
        cout << "[VideoOperation] updateProgress error: " << e.what() << endl;
        try { conn->rollback(); } catch (...) {}
        return false;
    }
}

bool VideoOperation::deleteVideo(uint32_t videoId)
{
    DBPool::Guard guard(DBPool::instance());
    sql::Connection* conn = guard.get();

    try {
        conn->setAutoCommit(false);
        
        PreparedStatement* pstmt = conn->prepareStatement(
            "DELETE FROM video WHERE id = ?");
        
        pstmt->setUInt(1, videoId);
        
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
        cout << "[VideoOperation] deleteVideo error: " << e.what() << endl;
        try { conn->rollback(); } catch (...) {}
        return false;
    }
}
