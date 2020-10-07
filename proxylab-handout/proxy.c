#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define DEFAULT_PORT 80

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char* connHdr = "Connection: close\r\n";
static const char* porxyConnHdr = "Proxy-Connection: close\r\n";

void forward(int connFd);
void parseUrl(const char* url, char* host, char* position, int* port);
void buildHttpHeader(char* http_header, const char* hostname, const char* path, int port, rio_t* client_rio);

int main(int argc, char** argv)
{
    if (argc != 2) {
        printf("error argument number");
        return 0;
    }

    int listenFd;
    int connFd;
    socklen_t clientLen;
    struct sockaddr_storage clientAddr;
    char clientHostname[MAXLINE];
    char clientPort[MAXLINE];

    listenFd = Open_listenfd(argv[1]);
    while(1) {
        clientLen = sizeof(struct sockaddr_storage);
        connFd = Accept(listenFd, (SA*)&clientAddr, &clientLen);
        Getnameinfo((SA*)&clientAddr, clientLen, clientHostname, MAXLINE, clientPort, MAXLINE, 0);
        forward(connFd);
        Close(connFd);
    }
    
    return 0;
}

void forward(int connFd)
{
    size_t n = 0;
    char buf[MAXLINE];
    char method[10];
    char url[MAXLINE];
    char httpVersion[10];

    rio_t clientRio;
    Rio_readinitb(&clientRio, connFd);

    if ((n == Rio_readlineb(&clientRio, buf, MAXLINE)) != 0) { // 从 client 读第一行
        sscanf(buf, "%s %s %s", method, url, httpVersion);
        
        if (strcmp(method, "GET") != 0) {
            printf("Do not support %s method yet.", method);
            exit(1);
        }
    }

    char host[MAXLINE];
    char position[MAXLINE];
    int port;
    parseUrl(url, host, position, &port);

    char httpHeader[MAXLINE];
    buildHttpHeader(httpHeader, host, position, port, &clientRio);

    int serverFd;
    char portStr[65];

    sprintf(portStr, "%d", port);
    serverFd = Open_clientfd(host, portStr);
    
    rio_t serverRio;
    Rio_readinitb(&serverRio, serverFd);
    
    Rio_writen(serverFd, httpHeader, sizeof(httpHeader)); // send http request to server

    while((n = Rio_readlineb(&serverRio, buf, MAXLINE)) != 0) {  // 接收 server 的信息
        Rio_writen(connFd, buf, n);                             // 转发给 client
    }

    Close(serverFd);
}

void parseUrl(const char* url, char* host, char* position, int* port)
{
    char* pattern = "https?:\\/\\/([^/:]+)(:\\d*)?([^# ]*)";
    regex_t reg;
    int err;
    char errbuf[1024];

    if ((err = regcomp(&reg, pattern, REG_EXTENDED)) != 0) {
        regerror(err, &reg, errbuf, sizeof(errbuf));
        printf("err: %s\n", errbuf);
        regfree(&reg);
        exit(1);
    }

    size_t nmatch = 3;
    regmatch_t pmatch[nmatch + 1];
    err = regexec(&reg, url, nmatch, pmatch, 0);
    if (err) {
        regerror(err, &reg, errbuf, sizeof(errbuf));
        printf("err: %s\n", errbuf);
        regfree(&reg);
        exit(1);
    }

    int len = 0;
    if (pmatch[1].rm_so != -1) {
        len = pmatch[1].rm_eo - pmatch[1].rm_so;
        memcpy(host, url + pmatch[1].rm_so, len);
        host[len] = 0;
    }

    if (pmatch[2].rm_so != -1) {
        len = pmatch[2].rm_eo - pmatch[2].rm_so - 1; // - 1 剪掉冒号长度
        char tmp[64 + 1];
        memcpy(tmp, url + pmatch[2].rm_so + 1, len); // + 1 除去冒号
        tmp[len] = 0;
        *port = atoi(tmp);
    } else {
        *port = DEFAULT_PORT;
    }

    if (pmatch[3].rm_so != -1) {
        len = pmatch[3].rm_eo - pmatch[3].rm_so;
        memcpy(position, url + pmatch[3].rm_so, len);
        position[len] = 0;
    }

    regfree(&reg);
}

void buildHttpHeader(char* http_header, const char* hostname, const char* path, int port, rio_t* client_rio)
{
    char buf[MAXLINE];
    char hostHdr[MAXLINE] = {0};
    char otherHdrs[MAXLINE] = {0};
    int n;

    while((n = Rio_readlineb(client_rio, buf, MAXLINE)) != 0) {
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }

        if (strncmp("HOST: ", buf, 6) == 0) {
            strcpy(hostHdr, buf);
            continue;
        }

        if (strncmp("Connection: ", buf, 12) && strncmp("Proxy-Connection: ", buf, 18) && strncmp("User-Agent: ", buf, 12)) {
            strcat(otherHdrs, buf);
        }
    }

    if (strlen(hostHdr) == 0) {
        sprintf(hostHdr, "HOST: %s\r\n", hostname);
    }

    char requestHdr[MAXLINE];
    sprintf(requestHdr, "GET %s HTTP/1.0\r\n", path);
    sprintf(http_header, "%s%s%s%s%s%s\r\n", requestHdr, hostHdr, connHdr, porxyConnHdr, user_agent_hdr, otherHdrs);
}