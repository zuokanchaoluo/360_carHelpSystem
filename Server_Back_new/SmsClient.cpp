#include "SmsClient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <iostream>

#define MAXLINE 4096
#define MAXSUB  2000
#define MAXPARAM 2048

// 请在此处填入您的 APIID 和 APIKEY
const char *SMS_ACCOUNT = "<YOUR_SMS_API_ID>"; // APIID
const char *SMS_PASSWORD = "<YOUR_SMS_API_KEY>"; // APIKEY

const char *SMS_HOSTNAME = "api.ihuyi.com";
const char *SMS_URI = "/sms/Submit.json";

int SmsClient::socketConnect(const char *hostname, int port)
{
    struct sockaddr_in their_addr;
    struct hostent *host = NULL;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (sockfd < 0) {
        std::cout << "[SmsClient] Create socket failed" << std::endl;
        return -1;
    }
    
    if ((host = gethostbyname(hostname)) == NULL) {
        std::cout << "[SmsClient] Gethostname error" << std::endl;
        close(sockfd);
        return -1;
    }
    
    memset(&their_addr, 0, sizeof(their_addr));
    their_addr.sin_family = AF_INET;
    their_addr.sin_port = htons(port);
    their_addr.sin_addr = *((struct in_addr *)host->h_addr);
    
    if (connect(sockfd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) < 0) {
        std::cout << "[SmsClient] Connect failed" << std::endl;
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

int SmsClient::httpPost(int fd, const char *hostname, const char *page, const char *poststr)
{
    char sendline[MAXLINE + 1];
    char recvline[MAXLINE + 1];
    ssize_t n;
    
    snprintf(sendline, MAXSUB,
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-type: application/x-www-form-urlencoded\r\n"
        "Content-length: %lu\r\n\r\n"
        "%s", page, hostname, strlen(poststr), poststr);

    write(fd, sendline, strlen(sendline));
    
    while ((n = read(fd, recvline, MAXLINE)) > 0) {
        recvline[n] = '\0';
        std::cout << "[SmsClient] Response: " << recvline << std::endl;
        break; 
    }
    return n;
}

bool SmsClient::sendSms(const std::string& mobile, const std::string& code)
{
    int fd = socketConnect(SMS_HOSTNAME, 80);
    if (fd < 0) return false;
    
    std::string content = "您的验证码是：" + code + "。请不要把验证码泄露给其他人。";
    
    char params[MAXPARAM + 1];
    snprintf(params, MAXPARAM, "account=%s&password=%s&mobile=%s&content=%s&format=json", 
             SMS_ACCOUNT, SMS_PASSWORD, mobile.c_str(), content.c_str());
             
    int ret = httpPost(fd, SMS_HOSTNAME, SMS_URI, params);
    
    close(fd);
    return true;
}
