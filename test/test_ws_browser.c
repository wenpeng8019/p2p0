/*
 * test_ws_browser.c — WebSocket 浏览器端对端手动演示
 *
 * 用法：
 *   ./test/test_ws_browser           （自动选端口）
 *   ./test/test_ws_browser 9100 9101  （relay_port ws_port）
 *
 * 流程：
 *   1. 启动 p2p_server（--ws-port 独立端口）
 *   2. 生成 HTML 页面写到 /tmp/p2p_ws_test.html
 *   3. 自动打开浏览器（macOS: open, Linux: xdg-open）
 *   4. ws_client 连接 WS 端口，依次发送演示消息
 *   5. 消息经 ws_server 广播到浏览器页面显示
 *   6. 按 Enter 结束
 *
 * 注意：需要编译时开启 WITH_WSLAY
 */

#ifdef WITH_WSLAY

#include "../src/ws_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#ifndef _WIN32
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <sys/wait.h>
#  include <errno.h>
#endif

/* -----------------------------------------------------------------------
 * 工具
 * -------------------------------------------------------------------- */
static void sleep_ms(int ms) {
#ifndef _WIN32
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#else
    Sleep(ms);
#endif
}

static uint16_t pick_free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return 0; }
    socklen_t l = sizeof(a);
    getsockname(fd, (struct sockaddr *)&a, &l);
    uint16_t port = ntohs(a.sin_port);
    close(fd);
    return port;
}

/* -----------------------------------------------------------------------
 * 查找 p2p_server 二进制（与 test_ws_server_integration.c 一致）
 * -------------------------------------------------------------------- */
static const char *find_server_binary(void) {
    static const char *candidates[] = {
        "p2p_server/p2p_server",
        "../p2p_server/p2p_server",
        "./p2p_server",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], X_OK) == 0) return candidates[i];
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * 启动 / 停止服务器子进程
 * -------------------------------------------------------------------- */
typedef struct { pid_t pid; } server_proc_t;

static server_proc_t start_server(const char *bin, uint16_t relay_port, uint16_t ws_port) {
    server_proc_t sp = { -1 };
    char relay_str[8], ws_str[8];
    snprintf(relay_str, sizeof(relay_str), "%u", (unsigned)relay_port);
    snprintf(ws_str,   sizeof(ws_str),   "%u", (unsigned)ws_port);

    pid_t pid = fork();
    if (pid < 0) return sp;
    if (pid == 0) {
        /* 子进程：将 stdout/stderr 重定向到 /dev/null */
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 1); dup2(dn, 2); close(dn); }
        execlp(bin, bin, "-p", relay_str, "--ws-port", ws_str, (char *)NULL);
        _exit(1);
    }
    sp.pid = pid;

    /* 等待 TCP 端口就绪（最多 1s）*/
    for (int i = 0; i < 100; i++) {
        sleep_ms(10);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(relay_port);
        if (connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) { close(fd); break; }
        close(fd);
    }
    return sp;
}

static void stop_server(server_proc_t *sp) {
    if (sp->pid <= 0) return;
    kill(sp->pid, SIGTERM);
    int st;
    waitpid(sp->pid, &st, 0);
    sp->pid = -1;
}

/* -----------------------------------------------------------------------
 * 生成并写入 HTML 页面
 * -------------------------------------------------------------------- */
/* 页面使用深色主题，连接后实时显示收到的 WS 消息 */
static const char HTML_FMT[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>P2P WebSocket Demo</title>\n"
    "<style>\n"
    "* { box-sizing:border-box; margin:0; padding:0 }\n"
    "body { font-family:'SF Mono',Consolas,monospace; background:#0d1117;\n"
    "       color:#c9d1d9; padding:28px }\n"
    "h1  { color:#58a6ff; font-size:1.4em; margin-bottom:18px }\n"
    "#bar { display:flex; align-items:center; gap:10px; margin-bottom:20px }\n"
    "#dot { width:11px; height:11px; border-radius:50%%;\n"
    "       background:#6e7681; transition:background .3s }\n"
    "#dot.ok  { background:#3fb950 }\n"
    "#dot.err { background:#f85149 }\n"
    "#info { color:#8b949e; font-size:.9em }\n"
    "#log  { list-style:none }\n"
    "#log li { padding:9px 14px; margin:5px 0;\n"
    "           background:#161b22;\n"
    "           border-left:3px solid #58a6ff;\n"
    "           border-radius:0 6px 6px 0;\n"
    "           animation:fadein .25s ease }\n"
    "@keyframes fadein { from{opacity:0;transform:translateX(-6px)}\n"
    "                    to  {opacity:1;transform:translateX(0)} }\n"
    "#log li .t { color:#6e7681; margin-right:10px; font-size:.82em }\n"
    "#log li .msg { color:#e6edf3 }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<h1>&#127760; P2P WebSocket Demo</h1>\n"
    "<div id=\"bar\">\n"
    "  <div id=\"dot\"></div>\n"
    "  <div id=\"info\">Connecting to ws://127.0.0.1:%u ...</div>\n"
    "</div>\n"
    "<ul id=\"log\"></ul>\n"
    "<script>\n"
    "const WS_PORT = %u;\n"
    "const ws  = new WebSocket('ws://127.0.0.1:' + WS_PORT);\n"
    "const dot  = document.getElementById('dot');\n"
    "const info = document.getElementById('info');\n"
    "const log  = document.getElementById('log');\n"
    "function ts() {\n"
    "  const d = new Date();\n"
    "  return d.toTimeString().slice(0,8) + '.' +\n"
    "         String(d.getMilliseconds()).padStart(3,'0');\n"
    "}\n"
    "function esc(s) {\n"
    "  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');\n"
    "}\n"
    "ws.onopen = () => {\n"
    "  dot.className = 'ok';\n"
    "  info.textContent = 'Connected  ws://127.0.0.1:' + WS_PORT;\n"
    "};\n"
    "ws.onclose = () => {\n"
    "  dot.className = '';\n"
    "  info.textContent = 'Disconnected';\n"
    "};\n"
    "ws.onerror = () => {\n"
    "  dot.className = 'err';\n"
    "  info.textContent = 'Connection error';\n"
    "};\n"
    "ws.onmessage = (e) => {\n"
    "  const li = document.createElement('li');\n"
    "  li.innerHTML = '<span class=\"t\">' + ts() + '</span>'\n"
    "               + '<span class=\"msg\">' + esc(e.data) + '</span>';\n"
    "  log.appendChild(li);\n"
    "  window.scrollTo(0, document.body.scrollHeight);\n"
    "};\n"
    "</script>\n"
    "</body>\n"
    "</html>\n";

static const char *HTML_PATH = "/tmp/p2p_ws_test.html";

static int write_html(uint16_t ws_port) {
    FILE *f = fopen(HTML_PATH, "w");
    if (!f) return -1;
    fprintf(f, HTML_FMT, (unsigned)ws_port, (unsigned)ws_port);
    fclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * 打开浏览器
 * -------------------------------------------------------------------- */
static void open_browser(const char *path) {
#if defined(__APPLE__)
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "open '%s'", path);
    system(cmd);
#elif defined(__linux__)
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' &", path);
    system(cmd);
#else
    (void)path;
    printf("  [!] 请手动打开: file://%s\n", path);
#endif
}

/* -----------------------------------------------------------------------
 * ws_client 辅助
 * -------------------------------------------------------------------- */
static int g_connected = 0;
static void on_open(ws_client_t *c, void *ud) { (void)c; (void)ud; g_connected = 1; }

/* pump：驱动 ws_client 事件循环，最多等 ms 毫秒直到 flag 为真 */
static int pump_until(ws_client_t *c, int *flag, int ms) {
    int ticks = ms / 5;
    for (int i = 0; i < ticks; i++) {
        ws_client_update(c);
        if (*flag) return 1;
        sleep_ms(5);
    }
    return 0;
}

/* 发送一条消息并 pump 足够长时间让 wslay 把帧写出去 */
static void send_and_flush(ws_client_t *c, const char *msg) {
    ws_client_send_text(c, msg);
    for (int i = 0; i < 60; i++) {   /* ~300ms */
        ws_client_update(c);
        sleep_ms(5);
    }
}

/* -----------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------- */
int main(int argc, char **argv) {
    printf("=== P2P WebSocket Browser Demo ===\n\n");

    /* ---- 1. 找 p2p_server 二进制 ---- */
    const char *bin = find_server_binary();
    if (!bin) {
        fprintf(stderr, "[错误] 找不到 p2p_server 可执行文件\n"
                        "       请先编译项目：cd build && make p2p_server\n");
        return 1;
    }
    printf("[1] 找到 p2p_server: %s\n", bin);

    /* ---- 2. 确定端口 ---- */
    uint16_t relay_port, ws_port;
    if (argc >= 3) {
        relay_port = (uint16_t)atoi(argv[1]);
        ws_port    = (uint16_t)atoi(argv[2]);
    } else {
        relay_port = pick_free_port();
        ws_port    = pick_free_port();
    }
    if (!relay_port || !ws_port || relay_port == ws_port) {
        fprintf(stderr, "[错误] 无法分配端口\n");
        return 1;
    }

    /* ---- 3. 启动 p2p_server ---- */
    printf("[2] 启动 p2p_server — relay:%u  ws:%u\n", relay_port, ws_port);
    server_proc_t sp = start_server(bin, relay_port, ws_port);
    if (sp.pid < 0) {
        fprintf(stderr, "[错误] 启动 p2p_server 失败\n");
        return 1;
    }
    printf("    PID=%d  端口已就绪\n", (int)sp.pid);

    /* ---- 4. 生成 HTML 页面 ---- */
    printf("[3] 生成 HTML → %s\n", HTML_PATH);
    if (write_html(ws_port) != 0) {
        fprintf(stderr, "[错误] 无法写 HTML 文件\n");
        stop_server(&sp);
        return 1;
    }

    /* ---- 5. 打开浏览器 ---- */
    printf("[4] 打开浏览器...\n");
    open_browser(HTML_PATH);

    /* ---- 6. 等浏览器一点时间建立连接，再连 ws_client ---- */
    printf("[5] 等待浏览器加载 (1.5s)...\n");
    sleep_ms(1500);

    /* ---- 7. ws_client 连接 ---- */
    printf("[6] ws_client 连接 ws://127.0.0.1:%u ...\n", ws_port);
    g_connected = 0;
    ws_client_cfg_t cfg = {0};
    cfg.on_open = on_open;
    ws_client_t *cli = ws_client_create(&cfg);
    if (!cli) { fprintf(stderr, "[错误] ws_client_create 失败\n"); stop_server(&sp); return 1; }

    if (ws_client_connect(cli, "127.0.0.1", ws_port, "/") != 0) {
        fprintf(stderr, "[错误] ws_client_connect 失败\n");
        ws_client_destroy(cli); stop_server(&sp); return 1;
    }
    if (!pump_until(cli, &g_connected, 3000)) {
        fprintf(stderr, "[错误] WS 握手超时（3s）\n");
        ws_client_destroy(cli); stop_server(&sp); return 1;
    }
    printf("    握手成功！\n\n");

    /* ---- 8. 发送演示消息 ---- */
    const char *messages[] = {
        "Hello, Browser! \xe4\xbd\xa0\xe5\xa5\xbd\xef\xbc\x8c\xe6\xb5\x8f\xe8\xa7\x88\xe5\x99\xa8\xef\xbc\x81",
        "This message is sent from a C WebSocket client.",
        "\xe8\xbf\x99\xe6\x98\xaf\xe6\x9d\xa5\xe8\x87\xaa C \xe5\xae\xa2\xe6\x88\xb7\xe7\xab\xaf\xe7\x9a\x84\xe7\xac\xac\xe4\xb8\x89\xe6\x9d\xa1\xe6\xb6\x88\xe6\x81\xaf\xe3\x80\x82",
        "p2p WebSocket: same server port for both relay and WS!",
        "\xe6\xb5\x8b\xe8\xaf\x95\xe5\xae\x8c\xe6\x88\x90\xe3\x80\x82\xe6\x8a\x80\xe6\x9c\xaf\xe6\xa0\x88: p2p_server + wslay + ws_client \u2192 Browser",
        NULL
    };

    printf("[7] \xe5\x8f\x91\xe9\x80\x81\xe6\xbc\x94\xe7\xa4\xba\xe6\xb6\x88\xe6\x81\xaf...\n");
    for (int i = 0; messages[i]; i++) {
        printf("    \xe2\x86\x92 [%d] %s\n", i + 1, messages[i]);
        send_and_flush(cli, messages[i]);
        sleep_ms(700);   /* 每条消息间隔 700ms，让浏览器逐条显示 */
    }
    printf("\n");

    /* ---- 9. 等用户确认后退出 ---- */
    printf("[\xe5\xae\x8c\xe6\x88\x90] \xe6\xb6\x88\xe6\x81\xaf\xe5\xb7\xb2\xe5\x8f\x91\xe9\x80\x81\xe3\x80\x82\n");
    printf("         \xe8\xaf\xb7\xe5\x9c\xa8\xe6\xb5\x8f\xe8\xa7\x88\xe5\x99\xa8\xe4\xb8\xad\xe6\x9f\xa5\xe7\x9c\x8b\xe6\x95\x88\xe6\x9e\x9c\xef\xbc\x8c\xe6\x8c\x89 Enter \xe7\xbb\x93\xe6\x9d\x9f...\n");
    (void)getchar();

    /* ---- 10. 清理 ---- */
    ws_client_destroy(cli);
    stop_server(&sp);
    printf("[\xe9\x80\x80\xe5\x87\xba] \xe6\xb8\x85\xe7\x90\x86\xe5\xae\x8c\xe6\xaf\x95\xe3\x80\x82\n");
    return 0;
}

#else  /* !WITH_WSLAY */

#include <stdio.h>
int main(void) {
    fprintf(stderr, "该演示需要 WITH_WSLAY 支持，请用 -DWITH_WSLAY=ON 重新编译。\n");
    return 1;
}

#endif /* WITH_WSLAY */
