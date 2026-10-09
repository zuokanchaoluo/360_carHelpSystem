#pragma once
#include "dbpool.h"
#include <iostream>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <vector>
#include <string>
#include "protocol.h"
#include <cstring>
using namespace sql;
using namespace std;

/**
 * @brief 图片记录操作类
 * 
 * 负责图片表的 CRUD 操作
 */
class ImageOperation
{
public:
    ImageOperation();
    ~ImageOperation();
    
    vector<PHOTOINFO> queryImageList(uint32_t userId, int pageNumber, int pageSize, int& outTotal);
    bool addImage(uint32_t userId, const PHOTOINFO& info, uint64_t fileSize);
    bool deleteImage(const string& fileName);
};
