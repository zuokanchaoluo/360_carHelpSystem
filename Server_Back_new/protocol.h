#pragma once

#include <stdint.h> // 引入标准整型定义

// =============================================================
// 1. 业务功能号定义 (FuncID)
// =============================================================

// --- 基础/用户模块 ---
#define FID_HEARTBEAT      		1000	// 心跳包 (保持连接)
#define FID_LOGIN          		1001	// 用户登录
#define FID_GET_SMS        		1002	// 获取短信验证码
#define FID_REGISTER       		1003	// 用户注册

// --- 特征图片模块 ---
#define FID_IMG_UPLOAD      	2001	// 特征图片上传
#define FID_IMG_UPLOAD_SLICE    2002	// 特征图片切片上传
#define FID_GET_IMG_LIST    	2003 	// 获取特征图片列表 
#define FID_RETRANSMISSION_IMG	2004	// 图片切片重传

// --- 视频模块 ---
#define FID_GET_VIDEO_LIST     	3001 	// 获取视频信息列表 (进入播放页时用)
#define FID_RECORD_UPLOAD  		3002 	// 上传视频播放记录 (停止播放/退出时用)
#define FID_INFO_UPLOAD  		3003 	// 上传视频信息 (视频录制时用)

// =============================================================
// 2. 状态码定义
// =============================================================
#define SUCCESS        			0 		// 成功
#define ERR_USER_EXIST     		1 		// 用户已存在
#define ERR_NAME_OR_PWD_ERR 	2 		// 用户名或密码错误
#define ERR_CODE_ERR       		3 		// 验证码错误
#define ERR_SMS_FAIL       		4 		// 短信发送失败
#define ERR_UPLOAD				5 		// 上传数据失败（包括没有保存到数据库）
#define ERR_SLICE_MISSING   	6 		// 图片切片上传不完整，有缺失

#pragma pack(push,1)

// 请求头/返回头
typedef struct head
{
	uint16_t type;			// 业务类
	uint32_t length;		// 包体长度
	uint32_t crc;			// 包体校验码
} HEAD;

// 通用返回体
typedef struct baseRes 
{
    uint8_t resCode;        // 状态码返回  
} BASERES;


// -------------------------------------------------------------
// [1001] 用户登录
// -------------------------------------------------------------
typedef struct loginReq 
{
    char username[20];    	// 手机号
    char pwd[33];         	// MD5密码
} LOGINREQ;

// 登录返回体
typedef struct loginRes 
{
	uint8_t resCode;        // 状态码返回
    uint32_t userId;		// 用户 id
    char nickname[32];    	// 登录成功顺便返回昵称，方便界面显示
} LOGINRES;


// -------------------------------------------------------------
// [1002] 获取短信验证码
// -------------------------------------------------------------
typedef struct getSmsReq 
{
    char phone[12];       	// 手机号 (11位+结束符)
} SMSREQ;


// -------------------------------------------------------------
// [1003] 用户注册
// 注：客户端负责校验"密码"与"重复密码"一致性，服务器只收最终密码
// -------------------------------------------------------------
typedef struct registerReq 
{
	char sms_code[8];     	// 短信验证码
    char username[12];    	// 手机号
    char nickname[32];    	// 昵称 (32字节支持中文)
    char pwd[33];         	// MD5密码
} REGISTERREQ;


// -------------------------------------------------------------
// [2001] 特征图片上传
// -------------------------------------------------------------
//	采用第一次包	→ HEAD + IMGREQ
//		后续   	→ HEAD + PHOTOSLICE
//	使用 FID_IMG_UPLOAD 和 FID_IMG_UPLOAD_SLICE 区分
typedef struct imgUploadReq 
{
    uint32_t  user_id;			// 用户 id
	char fileName[32];			// 文件名称
	char filePath[128];			// 文件路径
    char createTime[23];    	// "2023-10-01 12:00:00"
    uint32_t photoSlices;		// 图片切片总数
	uint32_t uploadId;          // 本次上传的唯一 ID（客户端生成）
} IMGREQ;


// -------------------------------------------------------------
// [2002] 特征图片切片上传
// -------------------------------------------------------------
//上传特征图片切片包体
typedef struct photoSlice
{
	uint8_t isLastSlice;    // 0-否 1-是 
	uint32_t sliceIndex;    // 当前切片序号
	uint32_t sliceSize;     // 本切片大小
	uint32_t uploadId;      // 关联 IMGREQ 的唯一 ID
	char buf[1024];			// 图片切片大小
}PHOTOSLICE;

// -------------------------------------------------------------
// [2003] 获取特征图片列表 
// -------------------------------------------------------------
//获取视频/图片列表包体
typedef struct pageReq
{
	uint32_t userId;		// 用户 id		
	uint32_t pageNumber;	// 页数
	uint32_t pageSize;		// 页大小
}PAGEREQ; 

// 图片信息包体
typedef struct photoInfo
{
	char fileName[64];			// 图片名称
	char filePath[256];			// 文件路径
	char createTime[32];		// 记录时间
}PHOTOINFO;

// 单条图片信息（用于图片列表分页分包返回）
typedef struct photoInfoItemRes
{
    uint32_t pageNumber;    	// 当前页码，从1开始
    uint32_t totalRecords;  	// 本页总记录数
    uint32_t itemIndex;     	// 本条记录在本页中的序号，从0开始
    PHOTOINFO photoInfo;      	// 图片信息
}PHOTOINFOITEMRES;

// -------------------------------------------------------------
// [2004] 切片上传返回
// -------------------------------------------------------------
// 如果全部成功上传则 resCode = SUCCESS，否则 resCode = ERR_SLICE_MISSING
typedef struct Retransmission
{
	uint32_t uploadId;		// 上传唯一 id
	uint8_t resCode;        // 状态码返回  
	uint8_t count;			// 缺失切片总数
	int32_t num[100];		// 缺失切片序号数组
}RETMISSION;

// -------------------------------------------------------------
// [3001] 获取视频信息列表
// -------------------------------------------------------------
// 视频记录包体
typedef struct videoRecord
{
	uint32_t video_id;			// 视频 id
	char fileName[64];			// 视频名称
	char filePath[256];			// 视频文件路径
	char coverPath[256];		// 视频封面路径
	uint32_t durationSeconds;	// 总时长
	uint32_t progressSeconds;	// 观看时长
	char createTime[32];		// 创建时间
}VIDEORECORD;

// 单条视频记录（用于视频列表分页分包返回）
typedef struct VideoRecordItem
{
    uint32_t pageNumber;    // 当前页码，从1开始
    uint32_t totalRecords;  // 本页总记录数
    uint32_t itemIndex;     // 本条记录在本页中的序号，从0开始
    VIDEORECORD record;     // 视频记录
}VIDEORECORDITEM;


// -------------------------------------------------------------
// [3002] 上传视频播放记录 
// -------------------------------------------------------------
typedef struct uploadRecordReq 
{
    uint32_t user_id;			// 用户 id
    uint32_t video_id;			// 视频 id
    uint32_t video_seek;       	// 当前停止时的秒数
    uint32_t video_duration;   	// 总时长
    char createTime[23];     	// 开始观看时间
} UPRECORDREQ;


// -------------------------------------------------------------
// [3003] 上传视频信息 
// -------------------------------------------------------------
typedef struct uploadVideoInfoReq
{
	uint32_t userId;			// 用户 ID
	char fileName[64];			// 视频名称
	char filePath[256];			// 视频文件路径
	char coverPath[256];		// 视频封面路径
	uint32_t fileSize;			// 视频大小
}UPVIDEOINFOREQ;

#pragma pack(pop)