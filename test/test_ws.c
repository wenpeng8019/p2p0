/*
 * test_ws.c — ws_server + ws_client 集成测试
 *
 * 测试方案：在同一进程内同时驱动 ws_server 和 ws_client：
 *   - server 监听 localhost 随机端口
 *   - client 连接并完成 WebSocket 握手
 *   - 双向收发文本帧 / 二进制帧
 *   - 断开连接，验证回调序列
 *
 * 测试用例：
 *   1. server_start_stop        — 创建/销毁服务器，无连接
 *   2. client_create_destroy    — 创建/销毁客户端，无连接
 *   3. handshake                — client 与 server 完成 WS 握手
 *   4. send_text                — client 发文本，server on_message 收到
 *   5. send_binary              — client 发二进制，server on_message 收到
 *   6. server_send_text         — server 发文本，client on_message 收到
 *   7. broadcast                — server 广播给一个 client
 *   8. client_close             — client 主动发 Close 帧
 */

#ifdef WITH_WSLAY

#include "../test/test_framework.h"
#include "../p2p_server/ws_server.h"
#include "../src/ws_client.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>

#ifndef _WIN32
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#endif

/* -----------------------------------------------------------------------
 * 工具：驱动 server + client 若干轮，直到条件满足或超时
 * -------------------------------------------------------------------- */
#define TICK_LIMIT 2000   /* 最多 2000 轮 */

#ifndef _WIN32
#  include <time.h>
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#else
#  include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
#endif

static void pump(ws_server_t *srv, ws_client_t *cli, int ticks) {
    for (int i = 0; i < ticks; i++) {
        if (srv) ws_server_update(srv);
        if (cli) ws_client_update(cli);
        sleep_ms(1);
    }
}

static int pump_until(ws_server_t *srv, ws_client_t *cli,
                       int *flag, int ticks) {
    for (int i = 0; i < ticks; i++) {
        if (srv) ws_server_update(srv);
        if (cli) ws_client_update(cli);
        if (*flag) return 1;
        sleep_ms(1);
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * 获取可用端口（绑定到 0 让 OS 分配，然后取出端口号）
 * -------------------------------------------------------------------- */
static uint16_t pick_free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 19876;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return 19876;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr *)&addr, &len);
    uint16_t port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

/* -----------------------------------------------------------------------
 * 测试 1: server_start_stop
 * -------------------------------------------------------------------- */
TEST(server_start_stop) {
    uint16_t port = pick_free_port();
    ws_server_cfg_t cfg = {0};
    ws_server_t *srv = ws_server_create(&cfg, port);
    ASSERT(srv != NULL);
    ASSERT_EQ(ws_server_client_count(srv), 0);
    ws_server_destroy(srv);
}

/* -----------------------------------------------------------------------
 * 测试 2: client_create_destroy
 * -------------------------------------------------------------------- */
TEST(client_create_destroy) {
    ws_client_cfg_t cfg = {0};
    ws_client_t *cli = ws_client_create(&cfg);
    ASSERT(cli != NULL);
    ASSERT_EQ(ws_client_state(cli), WS_CLIENT_CLOSED);
    ws_client_destroy(cli);
}

/* -----------------------------------------------------------------------
 * 测试 3: handshake
 * -------------------------------------------------------------------- */
static int g_hs_connected = 0;
static int g_hs_server_got = 0;

static void hs_on_open(ws_client_t *c, void *ud) {
    (void)c; (void)ud;
    g_hs_connected = 1;
}
static void hs_srv_on_connect(ws_server_t *s, ws_client_id_t cid, void *ud) {
    (void)s; (void)cid; (void)ud;
    g_hs_server_got = 1;
}

TEST(handshake) {
    g_hs_connected = 0;
    g_hs_server_got = 0;

    uint16_t port = pick_free_port();

    ws_server_cfg_t scfg = {0};
    scfg.on_connect = hs_srv_on_connect;
    ws_server_t *srv = ws_server_create(&scfg, port);
    ASSERT(srv != NULL);

    ws_client_cfg_t ccfg = {0};
    ccfg.on_open = hs_on_open;
    ws_client_t *cli = ws_client_create(&ccfg);
    ASSERT(cli != NULL);

    int ret = ws_client_connect(cli, "127.0.0.1", port, "/");
    ASSERT_EQ(ret, 0);

    int ok = pump_until(srv, cli, &g_hs_connected, TICK_LIMIT);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(ws_client_state(cli), WS_CLIENT_OPEN);

    pump(srv, cli, 20);
    ASSERT_EQ(g_hs_server_got, 1);
    ASSERT_EQ(ws_server_client_count(srv), 1);

    ws_client_destroy(cli);
    ws_server_destroy(srv);
}

/* -----------------------------------------------------------------------
 * 测试 4: send_text（client → server）
 * -------------------------------------------------------------------- */
static int g_st_srv_open  = 0;
static int g_st_cli_open  = 0;
static char g_st_recv_buf[256];
static int  g_st_recv_flag = 0;

static void st_srv_on_connect(ws_server_t *s, ws_client_id_t cid, void *ud) {
    (void)s; (void)cid; (void)ud; g_st_srv_open = 1;
}
static void st_cli_on_open(ws_client_t *c, void *ud) {
    (void)c; (void)ud; g_st_cli_open = 1;
}
static void st_srv_on_msg(ws_server_t *s, ws_client_id_t cid,
                           ws_srv_msg_type_t type,
                           const uint8_t *data, size_t len,
                           void *ud) {
    (void)s; (void)cid; (void)ud;
    if (type == WS_SRV_MSG_TEXT && len < sizeof(g_st_recv_buf)) {
        memcpy(g_st_recv_buf, data, len);
        g_st_recv_buf[len] = '\0';
        g_st_recv_flag = 1;
    }
}

TEST(send_text) {
    g_st_srv_open = g_st_cli_open = g_st_recv_flag = 0;
    memset(g_st_recv_buf, 0, sizeof(g_st_recv_buf));

    uint16_t port = pick_free_port();

    ws_server_cfg_t scfg = {0};
    scfg.on_connect = st_srv_on_connect;
    scfg.on_message = st_srv_on_msg;
    ws_server_t *srv = ws_server_create(&scfg, port);
    ASSERT(srv != NULL);

    ws_client_cfg_t ccfg = {0};
    ccfg.on_open = st_cli_on_open;
    ws_client_t *cli = ws_client_create(&ccfg);
    ASSERT(cli != NULL);
    ASSERT_EQ(ws_client_connect(cli, "127.0.0.1", port, "/"), 0);

    /* 等待握手 */
    ASSERT_EQ(pump_until(srv, cli, &g_st_cli_open, TICK_LIMIT), 1);

    /* 发送文本 */
    ASSERT_EQ(ws_client_send_text(cli, "hello ws"), 0);

    /* 等待 server 收到 */
    ASSERT_EQ(pump_until(srv, cli, &g_st_recv_flag, TICK_LIMIT), 1);
    ASSERT_EQ(strcmp(g_st_recv_buf, "hello ws"), 0);

    ws_client_destroy(cli);
    ws_server_destroy(srv);
}

/* -----------------------------------------------------------------------
 * 测试 5: send_binary（client → server）
 * -------------------------------------------------------------------- */
static uint8_t g_bin_recv[16];
static size_t  g_bin_recv_len = 0;
static int     g_bin_recv_flag = 0;
static int     g_bin_cli_open = 0;

static void bin_cli_on_open(ws_client_t *c, void *ud) {
    (void)c; (void)ud; g_bin_cli_open = 1;
}
static void bin_srv_on_msg(ws_server_t *s, ws_client_id_t cid,
                            ws_srv_msg_type_t type,
                            const uint8_t *data, size_t len,
                            void *ud) {
    (void)s; (void)cid; (void)ud;
    if (type == WS_SRV_MSG_BINARY && len <= sizeof(g_bin_recv)) {
        memcpy(g_bin_recv, data, len);
        g_bin_recv_len = len;
        g_bin_recv_flag = 1;
    }
}

TEST(send_binary) {
    g_bin_cli_open = g_bin_recv_flag = 0;
    g_bin_recv_len = 0;

    uint16_t port = pick_free_port();

    ws_server_cfg_t scfg = {0};
    scfg.on_message = bin_srv_on_msg;
    ws_server_t *srv = ws_server_create(&scfg, port);
    ASSERT(srv != NULL);

    ws_client_cfg_t ccfg = {0};
    ccfg.on_open = bin_cli_on_open;
    ws_client_t *cli = ws_client_create(&ccfg);
    ASSERT(cli != NULL);
    ASSERT_EQ(ws_client_connect(cli, "127.0.0.1", port, "/"), 0);

    ASSERT_EQ(pump_until(srv, cli, &g_bin_cli_open, TICK_LIMIT), 1);

    static const uint8_t payload[] = {0x01, 0x02, 0x03, 0xAB, 0xCD};
    ASSERT_EQ(ws_client_send_binary(cli, payload, sizeof(payload)), 0);

    ASSERT_EQ(pump_until(srv, cli, &g_bin_recv_flag, TICK_LIMIT), 1);
    ASSERT_EQ((int)g_bin_recv_len, (int)sizeof(payload));
    ASSERT_EQ(memcmp(g_bin_recv, payload, sizeof(payload)), 0);

    ws_client_destroy(cli);
    ws_server_destroy(srv);
}

/* -----------------------------------------------------------------------
 * 测试 6: server_send_text（server → client）
 * -------------------------------------------------------------------- */
static int    g_ss_cli_open = 0;
static int    g_ss_srv_open = 0;
static char   g_ss_cli_recv[256];
static int    g_ss_cli_recv_flag = 0;
static ws_client_id_t g_ss_cid = 0;

static void ss_srv_on_connect(ws_server_t *s, ws_client_id_t cid, void *ud) {
    (void)s; (void)ud; g_ss_srv_open = 1; g_ss_cid = cid;
}
static void ss_cli_on_open(ws_client_t *c, void *ud) {
    (void)c; (void)ud; g_ss_cli_open = 1;
}
static void ss_cli_on_msg(ws_client_t *c,
                           ws_msg_type_t type,
                           const uint8_t *data, size_t len,
                           void *ud) {
    (void)c; (void)ud;
    if (type == WS_MSG_TEXT && len < sizeof(g_ss_cli_recv)) {
        memcpy(g_ss_cli_recv, data, len);
        g_ss_cli_recv[len] = '\0';
        g_ss_cli_recv_flag = 1;
    }
}

TEST(server_send_text) {
    g_ss_cli_open = g_ss_srv_open = g_ss_cli_recv_flag = 0;
    g_ss_cid = 0;
    memset(g_ss_cli_recv, 0, sizeof(g_ss_cli_recv));

    uint16_t port = pick_free_port();

    ws_server_cfg_t scfg = {0};
    scfg.on_connect = ss_srv_on_connect;
    ws_server_t *srv = ws_server_create(&scfg, port);
    ASSERT(srv != NULL);

    ws_client_cfg_t ccfg = {0};
    ccfg.on_open    = ss_cli_on_open;
    ccfg.on_message = ss_cli_on_msg;
    ws_client_t *cli = ws_client_create(&ccfg);
    ASSERT(cli != NULL);
    ASSERT_EQ(ws_client_connect(cli, "127.0.0.1", port, "/"), 0);

    /* 等待双方都 open */
    ASSERT_EQ(pump_until(srv, cli, &g_ss_cli_open,  TICK_LIMIT), 1);
    ASSERT_EQ(pump_until(srv, cli, &g_ss_srv_open,  TICK_LIMIT), 1);

    /* server 发文本给 client */
    ASSERT_EQ(ws_server_send_text(srv, g_ss_cid, "from server"), 0);

    ASSERT_EQ(pump_until(srv, cli, &g_ss_cli_recv_flag, TICK_LIMIT), 1);
    ASSERT_EQ(strcmp(g_ss_cli_recv, "from server"), 0);

    ws_client_destroy(cli);
    ws_server_destroy(srv);
}

/* -----------------------------------------------------------------------
 * 测试 7: broadcast
 * -------------------------------------------------------------------- */
static int g_bc_cli_open = 0;
static int g_bc_recv_flag = 0;
static char g_bc_recv[256];

static void bc_cli_on_open(ws_client_t *c, void *ud) {
    (void)c; (void)ud; g_bc_cli_open = 1;
}
static void bc_cli_on_msg(ws_client_t *c, ws_msg_type_t type,
                           const uint8_t *data, size_t len, void *ud) {
    (void)c; (void)ud;
    if (type == WS_MSG_TEXT && len < sizeof(g_bc_recv)) {
        memcpy(g_bc_recv, data, len);
        g_bc_recv[len] = '\0';
        g_bc_recv_flag = 1;
    }
}

TEST(broadcast) {
    g_bc_cli_open = g_bc_recv_flag = 0;
    memset(g_bc_recv, 0, sizeof(g_bc_recv));

    uint16_t port = pick_free_port();

    ws_server_cfg_t scfg = {0};
    ws_server_t *srv = ws_server_create(&scfg, port);
    ASSERT(srv != NULL);

    ws_client_cfg_t ccfg = {0};
    ccfg.on_open    = bc_cli_on_open;
    ccfg.on_message = bc_cli_on_msg;
    ws_client_t *cli = ws_client_create(&ccfg);
    ASSERT(cli != NULL);
    ASSERT_EQ(ws_client_connect(cli, "127.0.0.1", port, "/"), 0);

    ASSERT_EQ(pump_until(srv, cli, &g_bc_cli_open, TICK_LIMIT), 1);
    pump(srv, cli, 20);  /* 确保 server 也已记录连接 */
    ASSERT_EQ(ws_server_client_count(srv), 1);

    ws_server_broadcast_text(srv, "broadcast!");

    ASSERT_EQ(pump_until(srv, cli, &g_bc_recv_flag, TICK_LIMIT), 1);
    ASSERT_EQ(strcmp(g_bc_recv, "broadcast!"), 0);

    ws_client_destroy(cli);
    ws_server_destroy(srv);
}

/* -----------------------------------------------------------------------
 * 测试 8: client_close
 * -------------------------------------------------------------------- */
static int g_cl_cli_open  = 0;
static int g_cl_cli_close = 0;
static int g_cl_srv_disc  = 0;

static void cl_cli_on_open (ws_client_t *c, void *ud) { (void)c;(void)ud; g_cl_cli_open=1; }
static void cl_cli_on_close(ws_client_t *c, uint16_t code, const char *r, void *ud) {
    (void)c;(void)code;(void)r;(void)ud; g_cl_cli_close=1;
}
static void cl_srv_on_disc(ws_server_t *s, ws_client_id_t cid, void *ud) {
    (void)s;(void)cid;(void)ud; g_cl_srv_disc=1;
}

TEST(client_close) {
    g_cl_cli_open = g_cl_cli_close = g_cl_srv_disc = 0;

    uint16_t port = pick_free_port();

    ws_server_cfg_t scfg = {0};
    scfg.on_disconnect = cl_srv_on_disc;
    ws_server_t *srv = ws_server_create(&scfg, port);
    ASSERT(srv != NULL);

    ws_client_cfg_t ccfg = {0};
    ccfg.on_open  = cl_cli_on_open;
    ccfg.on_close = cl_cli_on_close;
    ws_client_t *cli = ws_client_create(&ccfg);
    ASSERT(cli != NULL);
    ASSERT_EQ(ws_client_connect(cli, "127.0.0.1", port, "/"), 0);

    ASSERT_EQ(pump_until(srv, cli, &g_cl_cli_open, TICK_LIMIT), 1);

    ws_client_close(cli, 1000);

    /* 等待 client 关闭完成 */
    ASSERT_EQ(pump_until(srv, cli, &g_cl_cli_close, TICK_LIMIT), 1);

    /* server 应检测到断开 */
    pump(srv, cli, 100);
    ASSERT_EQ(g_cl_srv_disc, 1);
    ASSERT_EQ(ws_server_client_count(srv), 0);

    ws_client_destroy(cli);
    ws_server_destroy(srv);
}

/* -----------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------- */
int main(void) {
#ifndef _WIN32
    /* 忽略 SIGPIPE：非阻塞 socket 写入关闭连接时触发，用 errno 处理即可 */
    signal(SIGPIPE, SIG_IGN);
#endif
    printf("=== test_ws ===\n");

    RUN_TEST(server_start_stop);
    RUN_TEST(client_create_destroy);
    RUN_TEST(handshake);
    RUN_TEST(send_text);
    RUN_TEST(send_binary);
    RUN_TEST(server_send_text);
    RUN_TEST(broadcast);
    RUN_TEST(client_close);

    printf("\nResults: %d passed, %d failed\n", test_passed, test_failed);
    return test_failed ? 1 : 0;
}

#else /* WITH_WSLAY */

#include <stdio.h>
int main(void) {
    printf("test_ws: WITH_WSLAY not enabled, skipping.\n");
    return 0;
}

#endif /* WITH_WSLAY */
