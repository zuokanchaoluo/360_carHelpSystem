#include "ipc.h"
#include "OperationFactory.h"
#include "protocol.h"
#include "dbpool.h"
#include <iostream>
#include <cstring>
#include <signal.h>
#include <pthread.h>

using namespace std;

// Worker 线程数量
#define BACK_WORKER_COUNT 32

// 全局变量
static SharedMemory* g_shm = nullptr;
static MessageQueue* g_msgFromFront = nullptr;  // Front -> Back
static MessageQueue* g_msgToFront = nullptr;    // Back -> Front
static bool g_running = true;

#include <map>
#include <string>
#include <ctime>
#include "SmsClient.h"

// 全局 map + 互斥锁（多线程保护）
static std::map<int, uint32_t> g_sessions;
static pthread_mutex_t g_sessionsMutex = PTHREAD_MUTEX_INITIALIZER;

static std::map<std::string, std::string> g_smsCodes;
static pthread_mutex_t g_smsCodesMutex = PTHREAD_MUTEX_INITIALIZER;

#include <vector>
#include <set>
#include <fstream>
struct UploadContext {
    uint32_t userId;
    uint32_t totalSlices;
    int clientFd;                                    // 关联客户端 fd，用于发送重传响应
    std::string fileName;
    std::string filePath;
    std::string createTime;
    std::map<uint32_t, std::vector<char>> sliceMap;   // 按序号存储切片数据
    std::set<uint32_t> receivedIndices;               // 已收到的切片序号集合
};
static std::map<uint32_t, UploadContext> g_uploads;
static pthread_mutex_t g_uploadsMutex = PTHREAD_MUTEX_INITIALIZER;

// 信号处理
void signalHandler(int sig)
{
    cout << "\n[Main] Received signal " << sig << ", shutting down..." << endl;
    g_running = false;
}

// 发送响应给前置服务器
void sendResponse(int clientFd, uint16_t funcId, const void* data, uint32_t len)
{
    int blockIndex = -1;
    
    if (len > 0 && data) {
        blockIndex = g_shm->allocBlock();
        if (blockIndex >= 0) {
            g_shm->writeData(blockIndex, data, len);
        }
    }
    
    IpcMessage msg;
    msg.mtype = 1;
    msg.clientFd = clientFd;
    msg.funcId = funcId;
    msg.dataLen = len;
    msg.shmBlockIndex = blockIndex;
    
    g_msgToFront->send(msg);
}

// =============================================================
// 业务处理函数
// =============================================================

// 处理登录请求
void handleLogin(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(LOGINREQ)) {
        LOGINRES res = {};
        res.resCode = ERR_NAME_OR_PWD_ERR;
        sendResponse(clientFd, FID_LOGIN, &res, sizeof(res));
        return;
    }
    
    const LOGINREQ* req = (const LOGINREQ*)data;
    cout << "[Handler] Login: username=" << req->username << endl;
    
    // 使用 UserOperation 进行登录验证
    UserOperation* userOp = OperationFactory::getInstance()->getUserOperation();
    
    uint32_t userId = 0;
    string nickname;
    
    int result = userOp->login(req->username, req->pwd, userId, nickname);
    
    LOGINRES res = {};
    res.resCode = result;
    if (result == SUCCESS) {
        res.userId = userId;
        strncpy(res.nickname, nickname.c_str(), sizeof(res.nickname) - 1);
        cout << "[Handler] Login success: userId=" << userId << ", nickname=" << nickname << endl;
        
        // 保存 Session
        pthread_mutex_lock(&g_sessionsMutex);
        g_sessions[clientFd] = userId;
        pthread_mutex_unlock(&g_sessionsMutex);
    }
    
    sendResponse(clientFd, FID_LOGIN, &res, sizeof(res));
}

// 处理注册请求
void handleRegister(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(REGISTERREQ)) {
        BASERES res = {ERR_UPLOAD};
        sendResponse(clientFd, FID_REGISTER, &res, sizeof(res));
        return;
    }
    
    const REGISTERREQ* req = (const REGISTERREQ*)data;
    cout << "[Handler] Register: username=" << req->username 
         << ", nickname=" << req->nickname << endl;
    
    // 验证短信验证码
    pthread_mutex_lock(&g_smsCodesMutex);
    if (g_smsCodes.find(req->username) == g_smsCodes.end()) {
        pthread_mutex_unlock(&g_smsCodesMutex);
        cout << "[Handler] Register failed: No SMS code found for " << req->username << endl;
        BASERES res = {ERR_CODE_ERR};
        sendResponse(clientFd, FID_REGISTER, &res, sizeof(res));
        return;
    }
    
    if (g_smsCodes[req->username] != req->sms_code) {
        cout << "[Handler] Register failed: Invalid code " << req->sms_code 
             << " for " << req->username << " (expected " << g_smsCodes[req->username] << ")" << endl;
        pthread_mutex_unlock(&g_smsCodesMutex);
        BASERES res = {ERR_CODE_ERR};
        sendResponse(clientFd, FID_REGISTER, &res, sizeof(res));
        return;
    }
    
    // 验证通过，清理验证码
    g_smsCodes.erase(req->username);
    pthread_mutex_unlock(&g_smsCodesMutex);
    
    UserOperation* userOp = OperationFactory::getInstance()->getUserOperation();
    int result = userOp->registerUser(req->username, req->nickname, req->pwd);
    
    BASERES res = {};
    res.resCode = result;
    
    sendResponse(clientFd, FID_REGISTER, &res, sizeof(res));
}

// 处理获取验证码请求
void handleGetSms(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(SMSREQ)) {
        BASERES res = {ERR_SMS_FAIL};
        sendResponse(clientFd, FID_GET_SMS, &res, sizeof(res));
        return;
    }
    
    const SMSREQ* req = (const SMSREQ*)data;
    cout << "[Handler] GetSMS: phone=" << req->phone << endl;
    
    // 生成6位随机验证码
    srand(time(NULL) + clientFd);
    int codeInt = rand() % 900000 + 100000; // 100000 - 999999
    string code = to_string(codeInt);
    
    // 保存验证码
    pthread_mutex_lock(&g_smsCodesMutex);
    g_smsCodes[req->phone] = code;
    pthread_mutex_unlock(&g_smsCodesMutex);
    cout << "[Handler] Generated SMS code for " << req->phone << ": " << code << endl;
    
    // 发送短信
    bool success = SmsClient::sendSms(req->phone, code);
    
    BASERES res = {};
    if (success) {
        res.resCode = SUCCESS;
        cout << "[Handler] SMS sent successfully" << endl;
    } else {
        res.resCode = ERR_SMS_FAIL;
        cout << "[Handler] SMS sent failed" << endl;
    }
    
    sendResponse(clientFd, FID_GET_SMS, &res, sizeof(res));
}

// 处理获取视频列表请求
void handleGetVideoList(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(PAGEREQ)) {
        BASERES res = {ERR_UPLOAD};
        sendResponse(clientFd, FID_GET_VIDEO_LIST, &res, sizeof(res));
        return;
    }
    
    const PAGEREQ* req = (const PAGEREQ*)data;
    cout << "[Handler] GetVideoList: userId=" << req->userId 
         << ", page=" << req->pageNumber << ", size=" << req->pageSize << endl;
    
    VideoOperation* videoOp = OperationFactory::getInstance()->getVideoOperation();
    
    int total = 0;
    vector<VIDEORECORD> records = videoOp->queryVideoList(
        req->userId, req->pageNumber, req->pageSize, total);
    
    if (records.empty()) {
        // 无记录
        BASERES res = {SUCCESS};
        sendResponse(clientFd, FID_GET_VIDEO_LIST, &res, sizeof(res));
        return;
    }
    
    // 发送每条记录
    for (size_t i = 0; i < records.size(); ++i) {
        VIDEORECORDITEM item = {};
        item.pageNumber = req->pageNumber;
        item.totalRecords = total;
        item.itemIndex = i;
        item.record = records[i];
        
        sendResponse(clientFd, FID_GET_VIDEO_LIST, &item, sizeof(item));
    }
}

// 处理获取图片列表请求
void handleGetImgList(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(PAGEREQ)) {
        BASERES res = {ERR_UPLOAD};
        sendResponse(clientFd, FID_GET_IMG_LIST, &res, sizeof(res));
        return;
    }
    
    const PAGEREQ* req = (const PAGEREQ*)data;
    cout << "[Handler] GetImgList: userId=" << req->userId 
         << ", page=" << req->pageNumber << ", size=" << req->pageSize << endl;
    
    ImageOperation* imageOp = OperationFactory::getInstance()->getImageOperation();
    
    int total = 0;
    vector<PHOTOINFO> records = imageOp->queryImageList(
        req->userId, req->pageNumber, req->pageSize, total);
    
    if (records.empty()) {
        BASERES res = {SUCCESS};
        sendResponse(clientFd, FID_GET_IMG_LIST, &res, sizeof(res));
        return;
    }
    
    for (size_t i = 0; i < records.size(); ++i) {
        PHOTOINFOITEMRES item = {};
        item.pageNumber = req->pageNumber;
        item.totalRecords = total;
        item.itemIndex = i;
        item.photoInfo = records[i];
        
        sendResponse(clientFd, FID_GET_IMG_LIST, &item, sizeof(item));
    }
}

// 处理上传视频信息 (FID 3003)
void handleVideoInfoUpload(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(UPVIDEOINFOREQ)) {
        BASERES res = {ERR_UPLOAD};
        sendResponse(clientFd, FID_INFO_UPLOAD, &res, sizeof(res));
        return;
    }
    
    // 获取 UserId
    if (g_sessions.find(clientFd) == g_sessions.end()) {
        cout << "[Handler] VideoInfoUpload failed: User not logged in (fd=" << clientFd << ")" << endl;
        BASERES res = {ERR_UPLOAD}; // 或者未登录码
        sendResponse(clientFd, FID_INFO_UPLOAD, &res, sizeof(res));
        return;
    }
    pthread_mutex_lock(&g_sessionsMutex);
    uint32_t userId = g_sessions[clientFd];
    pthread_mutex_unlock(&g_sessionsMutex);
    
    const UPVIDEOINFOREQ* req = (const UPVIDEOINFOREQ*)data;
    cout << "[Handler] VideoInfoUpload: file=" << req->fileName << " by user=" << userId << endl;
    
    VIDEORECORD record = {};
    memset(&record, 0, sizeof(record));
    strncpy(record.fileName, req->fileName, sizeof(record.fileName)-1);
    strncpy(record.filePath, req->filePath, sizeof(record.filePath)-1);
    strncpy(record.coverPath, req->coverPath, sizeof(record.coverPath)-1);
    record.durationSeconds = 10; // 默认 10秒
    
    VideoOperation* videoOp = OperationFactory::getInstance()->getVideoOperation();
    bool success = videoOp->addVideo(userId, record, req->fileSize);
    
    BASERES res;
    res.resCode = success ? SUCCESS : ERR_UPLOAD;
    sendResponse(clientFd, FID_INFO_UPLOAD, &res, sizeof(res));
}

// 处理上传播放记录 (FID 3002)
void handleRecordUpload(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(UPRECORDREQ)) {
        BASERES res = {ERR_UPLOAD};
        sendResponse(clientFd, FID_RECORD_UPLOAD, &res, sizeof(res));
        return;
    }
    
    const UPRECORDREQ* req = (const UPRECORDREQ*)data;
    
    // 获取 UserId (优先从 session，否则用请求中的)
    uint32_t userId = req->user_id;
    if (g_sessions.find(clientFd) != g_sessions.end()) {
        pthread_mutex_lock(&g_sessionsMutex);
        userId = g_sessions[clientFd];
        pthread_mutex_unlock(&g_sessionsMutex);
    }
    
    cout << "[Handler] RecordUpload: videoId=" << req->video_id 
         << ", userId=" << userId
         << ", seek=" << req->video_seek 
         << ", duration=" << req->video_duration << endl;
         
    VideoOperation* videoOp = OperationFactory::getInstance()->getVideoOperation();
    bool success = videoOp->updateProgress(userId, req->video_id, req->video_seek, req->video_duration);
    
    BASERES res;
    res.resCode = success ? SUCCESS : ERR_UPLOAD;
    sendResponse(clientFd, FID_RECORD_UPLOAD, &res, sizeof(res));
}

// 处理特征图片上传头部 (FID 2001)
void handleImgUpload(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(IMGREQ)) {
        BASERES res = {ERR_UPLOAD};
        sendResponse(clientFd, FID_IMG_UPLOAD, &res, sizeof(res));
        return;
    }
    
    const IMGREQ* req = (const IMGREQ*)data;
    
    // 获取 UserId
    uint32_t userId = req->user_id;
    if (g_sessions.find(clientFd) != g_sessions.end()) {
        pthread_mutex_lock(&g_sessionsMutex);
        userId = g_sessions[clientFd];
        pthread_mutex_unlock(&g_sessionsMutex);
    }
    
    cout << "[Handler] ImgUpload HEAD: userId=" << userId 
         << ", fileName=" << req->fileName 
         << ", slices=" << req->photoSlices 
         << ", uploadId=" << req->uploadId << endl;
    
    // 缓存上传上下文
    UploadContext ctx;
    ctx.userId = userId;
    ctx.totalSlices = req->photoSlices;
    ctx.clientFd = clientFd;
    ctx.fileName = req->fileName;
    ctx.filePath = req->filePath;
    ctx.createTime = req->createTime;
    
    pthread_mutex_lock(&g_uploadsMutex);
    g_uploads[req->uploadId] = ctx;
    pthread_mutex_unlock(&g_uploadsMutex);
    
    BASERES res = {SUCCESS};
    sendResponse(clientFd, FID_IMG_UPLOAD, &res, sizeof(res));
}

// 处理特征图片切片 (FID 2002)
void handleImgSliceUpload(int clientFd, const char* data, uint32_t len)
{
    if (len < sizeof(PHOTOSLICE)) {
        return;
    }
    
    const PHOTOSLICE* slice = (const PHOTOSLICE*)data;
    
    // 查找上传上下文
    pthread_mutex_lock(&g_uploadsMutex);
    auto it = g_uploads.find(slice->uploadId);
    if (it == g_uploads.end()) {
        pthread_mutex_unlock(&g_uploadsMutex);
        cout << "[Handler] ImgSlice: unknown uploadId=" << slice->uploadId << endl;
        return;
    }
    
    UploadContext& ctx = it->second;
    
    // 按序号存储切片数据（支持重传覆盖）
    ctx.sliceMap[slice->sliceIndex] = std::vector<char>(slice->buf, slice->buf + slice->sliceSize);
    ctx.receivedIndices.insert(slice->sliceIndex);
    
    // 收到 isLastSlice 标记时，检查完整性
    if (slice->isLastSlice == 1) {
        // 检查是否所有切片都已收到
        std::vector<int32_t> missingSlices;
        for (uint32_t i = 0; i < ctx.totalSlices; i++) {
            if (ctx.receivedIndices.find(i) == ctx.receivedIndices.end()) {
                missingSlices.push_back((int32_t)i);
                if (missingSlices.size() >= 100) break;  // RETMISSION.num 最多100个
            }
        }
        
        if (!missingSlices.empty()) {
            // ===== 有切片缺失，通知客户端重传 =====
            cout << "[Handler] ImgSlice: 切片不完整! uploadId=" << slice->uploadId
                 << ", 缺失 " << missingSlices.size() << " 片" 
                 << ", 已收到 " << ctx.receivedIndices.size() 
                 << "/" << ctx.totalSlices << endl;
            
            RETMISSION ret;
            memset(&ret, 0, sizeof(ret));
            ret.uploadId = slice->uploadId;
            ret.resCode = ERR_SLICE_MISSING;
            ret.count = (uint8_t)missingSlices.size();
            
            // 填充缺失序号
            for (size_t i = 0; i < missingSlices.size(); i++) {
                ret.num[i] = missingSlices[i];
            }
            // 未使用的位置 memset 已经清 0 了，不再强制写 -1
            
            // 注意：不清除上传上下文，保留等待客户端重传
            int responseFd = ctx.clientFd;
            pthread_mutex_unlock(&g_uploadsMutex);
            
            sendResponse(responseFd, FID_RETRANSMISSION_IMG, &ret, sizeof(ret));
        } else {
            // ===== 所有切片已收齐，合并数据并保存 =====
            // 按序号顺序合并所有切片
            std::vector<char> mergedData;
            for (uint32_t i = 0; i < ctx.totalSlices; i++) {
                auto& sliceData = ctx.sliceMap[i];
                mergedData.insert(mergedData.end(), sliceData.begin(), sliceData.end());
            }
            
            // 拷贝出需要的数据，尽早释放锁
            UploadContext localCtx = ctx;
            g_uploads.erase(it);
            pthread_mutex_unlock(&g_uploadsMutex);
            
            cout << "[Handler] ImgSlice: 所有切片已收齐, uploadId=" << slice->uploadId
                 << ", 合并后 " << mergedData.size() << " 字节" << endl;
            
            // 保存到服务器端目录
            string saveDir = "./uploads/photos";
            system(("mkdir -p " + saveDir).c_str());
            string savePath = saveDir + "/" + localCtx.fileName;
            
            ofstream ofs(savePath, ios::binary);
            if (ofs.is_open()) {
                ofs.write(mergedData.data(), mergedData.size());
                ofs.close();
                cout << "[Handler] Image saved: " << savePath << endl;
                
                // 插入数据库
                PHOTOINFO info;
                memset(&info, 0, sizeof(info));
                strncpy(info.fileName, localCtx.fileName.c_str(), sizeof(info.fileName) - 1);
                strncpy(info.filePath, savePath.c_str(), sizeof(info.filePath) - 1);
                strncpy(info.createTime, localCtx.createTime.c_str(), sizeof(info.createTime) - 1);
                
                ImageOperation* imageOp = OperationFactory::getInstance()->getImageOperation();
                imageOp->addImage(localCtx.userId, info, mergedData.size());
            } else {
                cout << "[Handler] Failed to save image: " << savePath << endl;
            }
            
            // 返回成功响应 (FID_RETRANSMISSION_IMG)
            RETMISSION ret;
            memset(&ret, 0, sizeof(ret));
            ret.uploadId = slice->uploadId;
            ret.resCode = SUCCESS;
            for (int i = 0; i < 50; i++) ret.num[i] = -1;
            
            sendResponse(localCtx.clientFd, FID_RETRANSMISSION_IMG, &ret, sizeof(ret));
        }
    } else {
        pthread_mutex_unlock(&g_uploadsMutex);
    }
}

// 处理请求
void processRequest(const IpcMessage& msg)
{
    char buffer[65536];
    uint32_t actualLen = 0;
    
    if (msg.shmBlockIndex >= 0) {
        g_shm->readData(msg.shmBlockIndex, buffer, sizeof(buffer), &actualLen);
        g_shm->freeBlock(msg.shmBlockIndex);
    }
    
    switch (msg.funcId) {
        case FID_LOGIN:
            handleLogin(msg.clientFd, buffer, actualLen);
            break;
        case FID_REGISTER:
            handleRegister(msg.clientFd, buffer, actualLen);
            break;
        case FID_GET_SMS:
            handleGetSms(msg.clientFd, buffer, actualLen);
            break;
        case FID_GET_VIDEO_LIST:
            handleGetVideoList(msg.clientFd, buffer, actualLen);
            break;
        case FID_GET_IMG_LIST:
            handleGetImgList(msg.clientFd, buffer, actualLen);
            break;
        case FID_INFO_UPLOAD:
            handleVideoInfoUpload(msg.clientFd, buffer, actualLen);
            break;
        case FID_RECORD_UPLOAD:
            handleRecordUpload(msg.clientFd, buffer, actualLen);
            break;
        case FID_IMG_UPLOAD:
            handleImgUpload(msg.clientFd, buffer, actualLen);
            break;
        case FID_IMG_UPLOAD_SLICE:
            handleImgSliceUpload(msg.clientFd, buffer, actualLen);
            break;
        default:
            cout << "[Handler] Unknown funcId: " << msg.funcId << endl;
            BASERES res = {255};
            sendResponse(msg.clientFd, msg.funcId, &res, sizeof(res));
            break;
    }
}

int main(int argc, char* argv[])
{
    cout << "========================================" << endl;
    cout << "  360Car Back Server" << endl;
    cout << "========================================" << endl;
    
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 初始化数据库连接池
    cout << "\n[Main] Initializing DB connection pool..." << endl;
    try {
        DBPool::instance().init(20);  // 20 个连接
        cout << "[Main] DB pool ready!" << endl;
    } catch (...) {
        cout << "[Main] Failed to initialize DB pool" << endl;
        return 1;
    }
    
    // 附加共享内存
    cout << "\n[Main] Attaching shared memory..." << endl;
    g_shm = new SharedMemory();
    if (!g_shm->attach()) {
        cout << "[Main] Failed to attach shared memory. Is front server running?" << endl;
        return 1;
    }
    
    // 打开消息队列
    cout << "[Main] Opening message queues..." << endl;
    g_msgFromFront = new MessageQueue();
    if (!g_msgFromFront->open(IPC_KEY_MSG_F2B)) {
        cout << "[Main] Failed to open message queue (F2B)" << endl;
        return 1;
    }
    
    g_msgToFront = new MessageQueue();
    if (!g_msgToFront->open(IPC_KEY_MSG_B2F)) {
        cout << "[Main] Failed to open message queue (B2F)" << endl;
        return 1;
    }
    
    cout << "\n[Main] Back Server is ready! (" << BACK_WORKER_COUNT << " workers)" << endl;
    cout << "[Main] Waiting for requests...\n" << endl;
    
    // 启动多个 Worker 线程并发处理请求
    pthread_t workers[BACK_WORKER_COUNT];
    for (int i = 0; i < BACK_WORKER_COUNT; i++) {
        pthread_create(&workers[i], nullptr, [](void* arg) -> void* {
            int id = *(int*)arg;
            delete (int*)arg;
            printf("[Worker %d] Started\n", id);
            
            while (g_running) {
                IpcMessage msg;
                if (g_msgFromFront->receive(msg, 0, true)) {
                    printf("[Worker %d] Processing funcId=%d, clientFd=%d\n",
                           id, msg.funcId, msg.clientFd);
                    processRequest(msg);
                }
            }
            
            printf("[Worker %d] Stopped\n", id);
            return nullptr;
        }, new int(i));
    }
    
    // 等待所有 Worker 结束
    for (int i = 0; i < BACK_WORKER_COUNT; i++) {
        pthread_join(workers[i], nullptr);
    }
    
    // 清理
    DBPool::instance().destroy();
    g_shm->detach();
    
    delete g_msgFromFront;
    delete g_msgToFront;
    delete g_shm;
    
    cout << "[Main] Server shutdown complete" << endl;
    return 0;
}