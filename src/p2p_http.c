/*
 * p2p_http.c — 跨平台最小 HTTPS 客户端实现
 *
 * 见 p2p_http.h 中的说明。
 */

#include "p2p_http.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * Windows 后端：WinHTTP
 *
 * WinHTTP 是 Windows 系统自带的 HTTP/HTTPS 客户端库，
 * 无需任何第三方依赖，支持 XP SP2 及以上所有 Windows 版本。
 * 通过 #pragma comment 自动链接，不需要在 CMakeLists 中额外指定。
 * ============================================================ */
#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

/*
 * 内部实现：所有 HTTP 操作走这一个函数。
 * method    "GET" 或 "PATCH"
 * body      NULL 表示无请求体（GET）
 * resp_buf  NULL 表示不需要响应体（写操作）
 */
static int winhttp_request(const char *method,
                            const char *url,
                            const char *token,
                            const char *body,
                            char       *resp_buf,
                            int         resp_size)
{
    int result = -1;

    /* URL → wchar_t */
    wchar_t w_url[2048] = {0};
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, w_url, 2048)) return -1;

    /* 解析 URL（提取 host、path、port）*/
    wchar_t w_host[256] = {0};
    wchar_t w_path[1024] = {0};
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize      = sizeof(uc);
    uc.lpszHostName      = w_host;
    uc.dwHostNameLength  = 256;
    uc.lpszUrlPath       = w_path;
    uc.dwUrlPathLength   = 1024;
    if (!WinHttpCrackUrl(w_url, 0, 0, &uc)) return -1;

    /* 创建 WinHTTP 会话句柄 */
    HINTERNET hSession = WinHttpOpen(L"p2p/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return -1;

    /* 建立连接 */
    INTERNET_PORT port = uc.nPort ? uc.nPort : INTERNET_DEFAULT_HTTPS_PORT;
    HINTERNET hConnect = WinHttpConnect(hSession, w_host, port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return -1; }

    /* 创建请求 */
    wchar_t w_method[16] = {0};
    MultiByteToWideChar(CP_UTF8, 0, method, -1, w_method, 16);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, w_method, w_path,
                                             NULL,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
    if (!hRequest) goto cleanup_connect;

    /* Authorization 头 */
    if (token && token[0]) {
        char   hdr_a[600];
        wchar_t hdr_w[600];
        snprintf(hdr_a, sizeof(hdr_a), "Authorization: token %s\r\n", token);
        MultiByteToWideChar(CP_UTF8, 0, hdr_a, -1, hdr_w, 600);
        WinHttpAddRequestHeaders(hRequest, hdr_w, (DWORD)-1,
                                  WINHTTP_ADDREQ_FLAG_ADD);
    }

    /* Content-Type 头（有 body 时） */
    if (body) {
        WinHttpAddRequestHeaders(hRequest,
                                  L"Content-Type: application/json\r\n",
                                  (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    /* 发送请求 */
    DWORD body_len = body ? (DWORD)strlen(body) : 0;
    if (!WinHttpSendRequest(hRequest,
                             WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             body ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
                             body_len, body_len, 0)) {
        goto cleanup_request;
    }

    /* 等待响应 */
    if (!WinHttpReceiveResponse(hRequest, NULL)) goto cleanup_request;

    /* 读取响应体 */
    result = 0;
    if (resp_buf && resp_size > 1) {
        int total = 0;
        DWORD avail = 0, nread = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            DWORD to_read = avail;
            if (total + (int)to_read >= resp_size - 1)
                to_read = (DWORD)(resp_size - total - 1);
            if (!WinHttpReadData(hRequest, resp_buf + total, to_read, &nread))
                break;
            total += (int)nread;
            if (total >= resp_size - 1) break;
        }
        resp_buf[total] = '\0';
        result = total;
    }

cleanup_request:
    WinHttpCloseHandle(hRequest);
cleanup_connect:
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

int p2p_http_get(const char *url, const char *token,
                 char *resp_buf, int resp_size)
{
    return winhttp_request("GET", url, token, NULL, resp_buf, resp_size);
}

int p2p_http_patch(const char *url, const char *token, const char *body)
{
    int r = winhttp_request("PATCH", url, token, body, NULL, 0);
    return (r >= 0) ? 0 : -1;
}

/* ============================================================
 * Unix 后端（macOS / Linux）：popen + curl
 *
 * macOS：/usr/bin/curl 随系统预装，保证存在。
 * Linux：curl 在主流发行版中几乎必然预装；
 *        若不存在，popen() 返回 NULL，函数返回 -1。
 *
 * GET  —— popen("curl ... url", "r")，直接从 stdout 读取响应体，
 *          无需临时文件。
 * PATCH—— popen("curl ... -d @- url", "w")，将 body 写入 curl 的
 *          stdin（-d @- 表示从 stdin 读请求体），无需临时文件。
 * ============================================================ */
#else /* !_WIN32 */

#include <errno.h>

int p2p_http_get(const char *url, const char *token,
                 char *resp_buf, int resp_size)
{
    if (!resp_buf || resp_size <= 1) return -1;

    char cmd[2048];
    if (token && token[0]) {
        snprintf(cmd, sizeof(cmd),
                 "curl -s -m 15 -H 'Authorization: token %s' '%s'",
                 token, url);
    } else {
        snprintf(cmd, sizeof(cmd), "curl -s -m 15 '%s'", url);
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    int total = 0;
    size_t n;
    while ((n = fread(resp_buf + total, 1,
                      (size_t)(resp_size - total - 1), fp)) > 0) {
        total += (int)n;
        if (total >= resp_size - 1) break;
    }
    resp_buf[total] = '\0';
    pclose(fp);
    return total;
}

int p2p_http_patch(const char *url, const char *token, const char *body)
{
    if (!body) return -1;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 15 -X PATCH "
             "-H 'Authorization: token %s' "
             "-H 'Content-Type: application/json' "
             "-d @- '%s' > /dev/null",
             token ? token : "", url);

    /* popen "w" 模式：写入 curl 的 stdin，curl 通过 -d @- 读取 */
    FILE *fp = popen(cmd, "w");
    if (!fp) return -1;

    fputs(body, fp);
    int rc = pclose(fp);
    /* pclose 返回 shell 退出码；curl 成功为 0 */
    return (rc == 0) ? 0 : -1;
}

#endif /* _WIN32 */
