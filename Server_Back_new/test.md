#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include "protocol.h" 

using namespace std;

#define SERVER_IP "<your_server_ip>"  // 请填写你的服务器 IP  
#define SERVER_PORT 8080    

long long g_userId = 0;

#define SLICE_SIZE 1024
#define LOCAL_FILE_PATH "/root/projects/test_client/dimoo.jpg"

int test_log(int sockfd)
{
    // ==========================================
    // 动作 1：模拟获取验证码
    // ==========================================
    cout << "--- [步骤 1] 测试获取验证码 ---" << endl;
    string testPhone = "13800000000"; // 测试手机号（请替换为你的手机号）

    // 一次性分配连续内存，将头和体打包，彻底解决半包截断！
    char smsSendBuf[1024] = { 0 };
    HEAD* pSmsHead = (HEAD*)smsSendBuf;
    pSmsHead->type = FID_GET_SMS;
    pSmsHead->length = sizeof(SMSREQ);

    SMSREQ* pSmsReq = (SMSREQ*)(smsSendBuf + sizeof(HEAD));
    strncpy(pSmsReq->phone, testPhone.c_str(), sizeof(pSmsReq->phone) - 1);

    cout << "请求已发送，正在等待短信网关发回执(约需1~3秒，请勿敲击键盘)..." << endl;

    // 一次性发送完整包裹
    ssize_t ret = write(sockfd, smsSendBuf, sizeof(HEAD) + sizeof(SMSREQ));
    if (ret < 0) {
        perror("Write error");
        close(sockfd);
        return -1;
    }

    HEAD recvHead;
    ret = read(sockfd, &recvHead, sizeof(HEAD));
    if (ret <= 0) {
        cout << "服务器断开或读取失败" << endl;
        close(sockfd);
        return -1;
    }
    BASERES smsRes;
    ret = read(sockfd, &smsRes, sizeof(BASERES));
    if (ret <= 0) {
        cout << "读取包体失败" << endl;
        close(sockfd);
        return -1;
    }

    if (smsRes.resCode == 0) { // SUCCESS 通常宏定义为 0
        cout << "验证码发送成功！" << endl;
    }
    else {
        cout << "验证码获取失败，错误码：" << smsRes.resCode << endl;
        close(sockfd);
        return -1;
    }

    // ==========================================
    // 动作 2：模拟填写验证码并注册
    // ==========================================
    cout << "--- [步骤 2] 测试用户注册 ---" << endl;
    string inputCode;
    cout << "请查看手机短信（或服务器终端打印的验证码），并在这里输入: ";
    cin >> inputCode;

    // 一次性分配连续内存
    char regSendBuf[1024] = { 0 };
    HEAD* pRegHead = (HEAD*)regSendBuf;
    pRegHead->type = FID_REGISTER;
    pRegHead->length = sizeof(REGISTERREQ);

    REGISTERREQ* pRegReq = (REGISTERREQ*)(regSendBuf + sizeof(HEAD));
    // 将数据安全拷贝到内存偏移位置
    strncpy(pRegReq->username, testPhone.c_str(), sizeof(pRegReq->username) - 1);
    strncpy(pRegReq->nickname, "Linux_Test_User", sizeof(pRegReq->nickname) - 1);
    strncpy(pRegReq->pwd, "e10adc3949ba59abbe56e057f20f883e", sizeof(pRegReq->pwd) - 1);
    strncpy(pRegReq->sms_code, inputCode.c_str(), sizeof(pRegReq->sms_code) - 1);

    cout << "注册请求已发送，等待数据库落盘..." << endl;

    // 一次性发送完整注册包裹
    ret = write(sockfd, regSendBuf, sizeof(HEAD) + sizeof(REGISTERREQ));
    if (ret < 0) {
        perror("Write error");
        return -1;
    }
    bzero(&recvHead, sizeof(recvHead));

    ret = read(sockfd, &recvHead, sizeof(HEAD));
    if (ret <= 0) {
        cout << "读取注册回包头失败" << endl;
        return -1;
    }
    BASERES regRes;
    ret = read(sockfd, &regRes, sizeof(BASERES));
    if (ret <= 0) {
        cout << "读取注册回包体失败" << endl;
        return -1;
    }

    if (regRes.resCode == 0) { // SUCCESS
        cout << "\n测试通关！注册成功！" << endl;
    }
    else if (regRes.resCode == 3) { // ERR_CODE_ERR
        cout << "\n注册失败：验证码错误或已过期！" << endl;
    }
    else if (regRes.resCode == 1) { // ERR_USER_EXIST
        cout << "\n注册失败：该手机号已经被注册过了！" << endl;
    }
    else {
        cout << "\n注册失败，未知错误码：" << regRes.resCode << endl;
    }

}

// 发送请求通用函数
void sendRequest(int sock, int fid, void* body, int bodyLen)
{
    char buf[2048] = { 0 };

    // 1. 填头
    HEAD* head = (HEAD*)buf;
    head->type = fid;
    head->length = bodyLen;
    head->crc = 0;

    // 2. 填体
    if (bodyLen > 0 && body != nullptr) {
        memcpy(buf + sizeof(HEAD), body, bodyLen);
    }

    // 3. 发送
    int totalLen = sizeof(HEAD) + bodyLen;
    if (send(sock, buf, totalLen, 0) < 0) {
        perror("Send error");
    }
}

// 接收通用响应头
HEAD recvHead(int sock)
{
    HEAD head;
    memset(&head, 0, sizeof(HEAD));
    if (recv(sock, &head, sizeof(HEAD), 0) <= 0) {
        cout << "服务器断开或读取失败" << endl;
        exit(-1);
    }
    return head;
}

// ==========================================
// 核心测试流程
// ==========================================

// 1. 执行登录，获取 UserID
bool doLogin(int sock)
{
    cout << "\n>>> [Step 0] 执行登录..." << endl;

    LOGINREQ req;
    memset(&req, 0, sizeof(req));
    strcpy(req.username, "13800000000");
    strcpy(req.pwd, "e10adc3949ba59abbe56e057f20f883e");

    sendRequest(sock, FID_LOGIN, &req, sizeof(req));

    HEAD head = recvHead(sock);
    if (head.type == FID_LOGIN)
    {
        LOGINRES res;
        recv(sock, &res, sizeof(LOGINRES), 0);

        if (res.resCode == 0) {
            g_userId = res.userId; // 保存 UserID
            cout << "登录成功！UserID: " << g_userId << ", 昵称: " << res.nickname << endl;
            return true;
        }
        else {
            cout << "登录失败，错误码: " << res.resCode << endl;
            return false;
        }
    }
    else {
        cout << "收到非登录回包，FID: " << head.type << endl;
        return false;
    }
}

// 2. 测试获取视频列表 (支持循环接收多条记录)
void doTestGetList(int sock, int pageNum, int pageSize)
{
    cout << "\n>>> [测试] 获取视频列表 (User: " << g_userId
        << ", Page: " << pageNum << ", Size: " << pageSize << ")" << endl;

    PAGEREQ req;
    req.userId = (uint32_t)g_userId;
    req.pageNumber = pageNum;
    req.pageSize = pageSize;

    sendRequest(sock, FID_GET_VIDEO_LIST, &req, sizeof(req));

    // 接收第一个包（无论有没有数据，服务器至少会发一个包）
    HEAD head = recvHead(sock);
    if (head.type == FID_GET_VIDEO_LIST)
    {
        VIDEORECORDITEM item;
        recv(sock, &item, sizeof(VIDEORECORDITEM), 0);

        uint32_t total = item.totalRecords;
        cout << "   [回包] 数据库总记录数: " << total << endl;

        // 判断这一页是否为空 (如果记录ID为0，通常代表服务器发来的空包)
        if (total == 0 || item.record.video_id == 0) {
            cout << "   [结果] 本页无数据！" << endl;
            return;
        }

        // 打印第一条
        cout << "   [数据 " << item.itemIndex + 1 << "] " << item.record.fileName
            << " (视频ID: " << item.record.video_id << ")" << endl;

        // 计算本页实际上应该有几条数据 (处理最后一页不满的情况)
        int itemsThisPage = pageSize;
        if (pageNum * pageSize > total) {
            itemsThisPage = total - (pageNum - 1) * pageSize;
        }
        if (itemsThisPage < 0) itemsThisPage = 0;

        for (int i = 1; i < itemsThisPage; ++i) 
        {
            HEAD nextHead = recvHead(sock);
            VIDEORECORDITEM nextItem;
            recv(sock, &nextItem, sizeof(VIDEORECORDITEM), 0);

            cout << "   [数据 " << nextItem.itemIndex + 1 << "] " << nextItem.record.fileName
                << " (视频ID: " << nextItem.record.video_id << ")" << endl;
        }
        cout << "   --- 本页 (" << itemsThisPage << "条) 接收完毕 ---" << endl;
    }
    else {
        cout << "收到异常回包 FID: " << head.type << endl;
    }
}

// 3. 测试上传视频信息 (带序号，方便生成不同的文件名)
void doTestUpload(int sock, int index)
{
    UPVIDEOINFOREQ req;
    memset(&req, 0, sizeof(req));
    req.userId = (uint32_t)g_userId;
    req.fileSize = 1024 * 1024 * 10 + index * 100; // 模拟不同大小

    // 动态生成文件名，如 "Test_Movie_01.mp4"
    char filename[64] = { 0 };
    sprintf(filename, "Test_Movie_%02d.mp4", index);

    strcpy(req.fileName, filename);
    strcpy(req.filePath, "/data/video/test.mp4");
    strcpy(req.coverPath, "/data/img/cover.png");

    sendRequest(sock, FID_INFO_UPLOAD, &req, sizeof(req));

    HEAD head = recvHead(sock);
    if (head.type == FID_INFO_UPLOAD)
    {
        BASERES res;
        recv(sock, &res, sizeof(BASERES), 0);
        if (res.resCode == 0) {
            cout << "   -> 插入视频 [" << filename << "] 成功！" << endl;
        }
        else {
            cout << "   -> 插入视频 [" << filename << "] 失败，Code: " << res.resCode << endl;
        }
    }
}

// 视频业务整合测试流
void test_video(int sockfd)
{
    // 1. 先登录，拿 Token/UserID
    if (!doLogin(sockfd)) {
        cout << "登录失败，无法继续后续测试！" << endl;
        return;
    }
    sleep(1);

    // ---------------------------------------------------------
    cout << "\n================= 1. 初始查询 =================" << endl;
    // 查询第1页，每页9条
    doTestGetList(sockfd, 1, 9);
    sleep(1);

    // ---------------------------------------------------------
    cout << "\n================= 2. 批量插入 15 条数据 =================" << endl;
    for (int i = 1; i <= 15; ++i) {
        doTestUpload(sockfd, i);
        usleep(100000); // 稍微停顿 100ms，防止发包太快造成粘包
    }
    sleep(1);

    // ---------------------------------------------------------
    cout << "\n================= 3. 查询第 1 页 (Size=9) =================" << endl;
    // 预期：返回 9 条记录
    doTestGetList(sockfd, 1, 9);
    sleep(1);

    // ---------------------------------------------------------
    cout << "\n================= 4. 查询第 2 页 (Size=9) =================" << endl;
    // 预期：返回 6 条记录 (加上之前可能存在的脏数据，可能会更多)
    doTestGetList(sockfd, 2, 9);

    cout << "\n=== 测试结束 ===" << endl;
}

// 网络发送辅助函数
void sendPacket(int sock, int fid, void* body, int bodyLen)
{
    char buf[4096] = { 0 }; // 足够大以容纳切片包

    // 1. 填头
    HEAD* head = (HEAD*)buf;
    head->type = fid;
    head->length = bodyLen;
    head->crc = 0;

    // 2. 填体
    if (bodyLen > 0 && body != nullptr) {
        memcpy(buf + sizeof(HEAD), body, bodyLen);
    }

    // 3. 发送
    int totalLen = sizeof(HEAD) + bodyLen;
    if (send(sock, buf, totalLen, 0) < 0) {
        perror("Send error");
    }
}

// 图片上传模块
void doUploadImage(int sock, const char* filepath)
{
    cout << "\n>>> [Step 2] 准备上传图片: " << filepath << endl;

    // 1. 打开文件并获取大小
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("打开本地文件失败 (请检查路径)");
        return;
    }

    // 2. 获取文件大小
    struct stat statbuf;
    fstat(fd, &statbuf);
    long fileSize = statbuf.st_size;
    if (fileSize == 0) 
    {
        cout << "文件大小为0，无法上传" << endl;
        close(fd);
        return;
    }

    // 3. 计算切片总数 (向上取整)
    // 公式: (总大小 + 切片大小 - 1) / 切片大小
    uint32_t totalSlices = (fileSize + SLICE_SIZE - 1) / SLICE_SIZE;
    uint32_t uploadId = (uint32_t)time(NULL); // 用时间戳模拟一个唯一ID

    cout << "    文件总大小: " << fileSize << " 字节" << endl;
    cout << "    切片总数量: " << totalSlices << " (每片 " << SLICE_SIZE << " 字节)" << endl;
    cout << "    本次上传ID: " << uploadId << endl;

    // 4. 发送“图片信息头” (FID 2001)
    IMGREQ headReq;
    memset(&headReq, 0, sizeof(headReq));

    headReq.user_id = g_userId;
    headReq.uploadId = uploadId;
    headReq.photoSlices = totalSlices;

    // 从路径中提取文件名 (例如 dimoo.jpg)
    const char* pFileName = strrchr(filepath, '/');
    if (pFileName == nullptr) pFileName = filepath;
    else pFileName++; // 跳过 '/'
    strncpy(headReq.fileName, pFileName, sizeof(headReq.fileName) - 1);

    // 模拟填充其他字段
    strncpy(headReq.filePath, filepath, sizeof(headReq.filePath) - 1);
    headReq.filePath[sizeof(headReq.filePath) - 1] = '\0';

    strcpy(headReq.createTime, "2026-02-16 12:00:00");

    sendPacket(sock, FID_IMG_UPLOAD, &headReq, sizeof(headReq));
    cout << ">>> 已发送图片元数据 (FID 2001)" << endl;
    cout << "    [Client Path] 将保存到数据库: " << headReq.filePath << endl;

    // 5. 循环拆分并发送切片 (FID 2002)
    char buf[SLICE_SIZE]; // 1024字节缓冲区
    int res = 0;          // 实际读取到的字节数
    uint32_t sliceIdx = 0;// 当前切片索引

    // 循环读取文件，直到读完
    while ((res = read(fd, buf, sizeof(buf))) > 0)
    {
        PHOTOSLICE sliceReq;
        memset(&sliceReq, 0, sizeof(sliceReq));

        sliceReq.uploadId = uploadId;
        sliceReq.sliceIndex = sliceIdx;
        sliceReq.sliceSize = res; // 记录这一片实际有多大 (最后一片可能小于1024)

        // 判断是否是最后一片
        // 逻辑：如果当前是第 (totalSlices-1) 个索引，就是最后一片
        if (sliceIdx == totalSlices - 1) {
            sliceReq.isLastSlice = 1;
        }
        else {
            sliceReq.isLastSlice = 0;
        }

        // 将读取到的文件数据拷贝到协议包体中
        memcpy(sliceReq.buf, buf, res);

        // 发送切片包
        sendPacket(sock, FID_IMG_UPLOAD_SLICE, &sliceReq, sizeof(sliceReq));

        // 打印进度条效果
        if (sliceIdx % 10 == 0 || sliceReq.isLastSlice) {
            cout << "    [发送中] 切片 " << sliceIdx + 1 << "/" << totalSlices
                << " (本片大小: " << res << ")" << endl;
        }

        // 【关键】稍微延时，防止客户端发送太快导致 TCP 粘包严重，减轻服务器压力
        usleep(50000);

        sliceIdx++;
        memset(buf, 0, sizeof(buf)); // 清空缓冲区，为下一片做准备
    }

    close(fd); // 关闭文件
    cout << ">>> 所有切片发送完毕，正在等待服务器响应..." << endl;

    // 6. 等待服务器回传结果
    char recvBuf[1024] = { 0 };
    // 阻塞等待
    int len = recv(sock, recvBuf, sizeof(recvBuf), 0);
    if (len > 0)
    {
        HEAD* head = (HEAD*)recvBuf;
        // 之前约定：服务器处理完 FID 2002 后，回发 FID 2001，包体是 BASERES
        if (head->type == FID_IMG_UPLOAD)
        {
            BASERES* res = (BASERES*)(recvBuf + sizeof(HEAD));
            if (res->resCode == 0) {
                cout << "图片上传并入库成功！" << endl;
            }
            else {
                cout << "上传失败，服务器返回码: " << (int)res->resCode << endl;
            }
        }
        else {
            cout << "收到未知的响应包 FID: " << head->type << endl;
        }
    }
    else {
        cout << "等待超时或服务器主动断开连接" << endl;
    }
}

void test_img(int sockfd)
{
    // 1. 先登录
    if (doLogin(sockfd))
    {
        // 2. 登录成功后上传图片
        // 确保这个文件存在！如果在 WSL，请确认路径是否正确
        doUploadImage(sockfd, LOCAL_FILE_PATH);
    }
}

int main() {
    cout << "====================================" << endl;
    cout << "  360智能驾驶系统 - 终端模拟测试脚本" << endl;
    cout << "====================================" << endl;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
    {
        perror("Socket 创建失败");
        return -1;
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    servaddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    cout << "正在连接前置服务器 " << SERVER_IP << ":" << SERVER_PORT << " ..." << endl;
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("连接失败");
        close(sockfd);
        return -1;
    }
    cout << "连接前置服务器成功！\n" << endl;

    // ----------------------------------------------------------------------------------

    // 调用视频测试主流程
    test_video(sockfd);

    // ----------------------------------------------------------------------------------

    close(sockfd);
    return 0;
}