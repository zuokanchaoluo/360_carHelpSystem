#pragma once
#include "dbpool.h"
#include <iostream>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <vector>
#include <string>
#include <cstring>
#include "protocol.h"

using namespace sql;
using namespace std;

/**
 * @brief 视频记录操作类
 * 
 * 负责视频表的 CRUD 操作
 */
class VideoOperation
{
public:
    VideoOperation();
    ~VideoOperation();
    
    vector<VIDEORECORD> queryVideoList(uint32_t userId, int pageNumber, int pageSize, int& outTotal);
    bool addVideo(uint32_t userId, const VIDEORECORD& record, uint32_t fileSize);
    bool updateProgress(uint32_t userId, uint32_t videoId, uint32_t progressSeconds, uint32_t totalDuration);
    bool deleteVideo(uint32_t videoId);
};
