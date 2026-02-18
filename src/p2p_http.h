/*
 * p2p_http — 跨平台最小 HTTPS 客户端
 *
 * 只实现 PUBSUB 信令所需的两个操作：GET 和 PATCH。
 *
 * 后端选择（编译期自动）：
 * ┌─────────────────┬──────────────────────────────────────────────────┐
 * │  平台           │  后端                                            │
 * ├─────────────────┼──────────────────────────────────────────────────┤
 * │  Windows        │  WinHTTP（Windows 系统库，零外部依赖）           │
 * │  macOS          │  popen + /usr/bin/curl（系统预装，零外部依赖）   │
 * │  Linux / 其他   │  popen + curl（通常预装；不存在时返回 -1）       │
 * └─────────────────┴──────────────────────────────────────────────────┘
 *
 * 使用约束：
 *   - 仅支持 HTTPS
 *   - 响应体截断至 resp_size - 1 字节（总是以 '\0' 结尾）
 *   - 不解析 HTTP 状态码（调用方自行判断响应内容）
 *   - 函数均为阻塞调用，建议只在信令线程中调用
 */

#ifndef P2P_HTTP_H
#define P2P_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * p2p_http_get — 发起 HTTPS GET 请求
 *
 * @param url        完整 URL（必须以 "https://" 开头）
 * @param token      GitHub token，用于 "Authorization: token <token>" 头；
 *                   传 NULL 或空字符串则不附加 Authorization 头
 * @param resp_buf   响应体输出缓冲区（调用方分配）
 * @param resp_size  缓冲区字节数（含结尾 '\0'）
 * @return           实际写入 resp_buf 的字节数（不含 '\0'），< 0 表示失败
 */
int p2p_http_get(const char *url, const char *token,
                 char *resp_buf, int resp_size);

/*
 * p2p_http_patch — 发起 HTTPS PATCH 请求
 *
 * Content-Type 固定为 application/json。
 * 本函数不读取响应体（信令写入不需要检查返回内容）。
 *
 * @param url    完整 URL
 * @param token  GitHub token
 * @param body   JSON 请求体（以 '\0' 结尾的字符串）
 * @return       0=成功，< 0=失败
 */
int p2p_http_patch(const char *url, const char *token, const char *body);

#ifdef __cplusplus
}
#endif

#endif /* P2P_HTTP_H */
