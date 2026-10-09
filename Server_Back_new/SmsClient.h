#pragma once
#include <string>

class SmsClient {
public:
    /**
     * @brief 发送短信验证码
     * @param mobile 手机号
     * @param code 验证码
     * @return true 发送成功 (HTTP 200且返回内容正常)
     */
    static bool sendSms(const std::string& mobile, const std::string& code);

private:
    static int socketConnect(const char* hostname, int port);
    static int httpPost(int fd, const char* hostname, const char* page, const char* poststr);
};
