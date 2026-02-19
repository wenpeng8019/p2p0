/*
 * P2P 诊断工具
 *
 * 此工具演示统一的连接 API：
 * 1. SIMPLE 模式 - 简单信令服务器（UDP）
 * 2. RELAY 模式 - RELAY 信令服务器（TCP）
 * 3. PUBSUB 模式 - 公共信令（GitHub Gist，角色由 target 参数决定）
 */

#include <p2p.h>
#include "p2p_internal.h"
#include "p2p_signal_relay.h"
#include "p2p_signal_pubsub.h"
#include "p2p_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <time.h>

static void print_help(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --dtls            Enable DTLS (MbedTLS)\n");
    printf("  --openssl         Enable DTLS (OpenSSL)\n");
    printf("  --pseudo          Enable PseudoTCP\n");
    printf("  --server IP       Standard Signaling Server IP\n");
    printf("  --compact          Use COMPACT mode (UDP signaling, default is ICE/TCP)\n");
    printf("  --github TOKEN    GitHub Token for Public Signaling\n");
    printf("  --gist ID         GitHub Gist ID for Public Signaling\n");
    printf("  --name NAME       Your Peer Name\n");
    printf("  --to TARGET       Target Peer Name (if specified: active role; if omitted: passive role)\n");
    printf("  --disable-lan     Disable LAN shortcut (force NAT punch test)\n");
    printf("  --verbose-punch   Enable verbose NAT punch logging\n");
}

static const char* state_name(int state) {
    switch (state) {
        case P2P_STATE_IDLE: return "IDLE";
        case P2P_STATE_REGISTERING: return "REGISTERING";
        case P2P_STATE_PUNCHING: return "PUNCHING";
        case P2P_STATE_CONNECTED: return "CONNECTED";
        case P2P_STATE_RELAY: return "RELAY";
        case P2P_STATE_CLOSING: return "CLOSING";
        case P2P_STATE_CLOSED: return "CLOSED";
        case P2P_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static void log_state_change(p2p_session_t *s) {
    static int last_state = -1;
    if (s->state != last_state) {
        printf("[STATE] %s (%d) -> %s (%d)\n", 
               state_name(last_state), last_state, 
               state_name(s->state), s->state);
        last_state = s->state;
        fflush(stdout);
    }
}

/* 连接建立回调 */
static void on_connected(p2p_session_t *s, void *userdata) {
    (void)userdata;
    log_state_change(s);   /* 立即打印状态变更，不等主循环 */
    printf("[EVENT] Connection established!\n");
    fflush(stdout);
    
    /* 发送初始 PING */
    const char *hello = "P2P_PING_ALIVE";
    p2p_send(s, hello, strlen(hello));
    printf("[DATA] Sent PING\n");
    fflush(stdout);
}

/* 连接断开回调 */
static void on_disconnected(p2p_session_t *s, void *userdata) {
    (void)s;
    (void)userdata;
    printf("[EVENT] Connection closed\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    printf("=== P2P Ping Diagnostic Tool ===\n\n");

    int use_dtls = 0, use_openssl = 0, use_pseudo = 0, use_compact = 0;
    int disable_lan = 0, verbose_punch = 0;
    const char *server_ip = NULL, *gh_token = NULL, *gist_id = NULL;
    const char *my_name = "unnamed", *target_name = NULL;
    int server_port = 8888;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dtls") == 0) use_dtls = 1;
        else if (strcmp(argv[i], "--openssl") == 0) use_openssl = 1;
        else if (strcmp(argv[i], "--pseudo") == 0) use_pseudo = 1;
        else if (strcmp(argv[i], "--compact") == 0) use_compact = 1;
        else if (strcmp(argv[i], "--disable-lan") == 0) disable_lan = 1;
        else if (strcmp(argv[i], "--verbose-punch") == 0) verbose_punch = 1;
        else if (strcmp(argv[i], "--server") == 0 && i+1 < argc) server_ip = argv[++i];
        else if (strcmp(argv[i], "--github") == 0 && i+1 < argc) gh_token = argv[++i];
        else if (strcmp(argv[i], "--gist") == 0 && i+1 < argc) gist_id = argv[++i];
        else if (strcmp(argv[i], "--name") == 0 && i+1 < argc) my_name = argv[++i];
        else if (strcmp(argv[i], "--to") == 0 && i+1 < argc) target_name = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) { print_help(argv[0]); return 0; }
    }

    // 解析 server_ip 中的端口号（支持 IP:PORT 格式）
    char server_host_buf[256] = {0};
    const char *server_host = server_ip;
    if (server_ip) {
        const char *colon = strchr(server_ip, ':');
        if (colon) {
            size_t len = colon - server_ip;
            if (len < sizeof(server_host_buf)) {
                memcpy(server_host_buf, server_ip, len);
                server_host_buf[len] = '\0';
                server_host = server_host_buf;
                server_port = atoi(colon + 1);
            }
        }
    }

    p2p_config_t cfg = {0};
    cfg.use_dtls = use_dtls;
    cfg.use_openssl = use_openssl;
    cfg.use_pseudotcp = use_pseudo;
    cfg.use_ice = !use_compact;  // --compact 标志禁用 ICE
    cfg.stun_server = "stun.l.google.com";
    cfg.stun_port = 3478;
    cfg.server_host = server_host;
    cfg.server_port = server_port;
    cfg.gh_token = gh_token;
    cfg.gist_id = gist_id;
    cfg.bind_port = 0;
    strncpy(cfg.local_peer_id, my_name, P2P_PEER_ID_MAX);
    
    // 测试选项
    cfg.disable_lan_shortcut = disable_lan;
    cfg.verbose_nat_punch = verbose_punch;
    
    // 设置事件回调
    cfg.on_connected = on_connected;
    cfg.on_disconnected = on_disconnected;
    cfg.userdata = NULL;
    
    // 根据配置确定信令模式
    if (server_ip) {
        cfg.signaling_mode = cfg.use_ice ? P2P_SIGNALING_MODE_RELAY : P2P_SIGNALING_MODE_COMPACT;
    } else if (gh_token && gist_id) {
        cfg.signaling_mode = P2P_SIGNALING_MODE_PUBSUB;
    }

    p2p_session_t *s = p2p_create(&cfg);
    if (!s) { printf("Failed to create session\n"); return 1; }

    // 统一的连接流程
    const char *mode_name = NULL;
    if (server_ip) {
        mode_name = cfg.use_ice ? "ICE" : "SIMPLE";
    } else if (gh_token && gist_id) {
        mode_name = "PUBSUB";
    } else {
        printf("Error: No connection mode specified.\n");
        printf("Use one of: --server or --github\n");
        print_help(argv[0]);
        return 1;
    }

    // 如果启用了测试选项，显示信息
    if (disable_lan) {
        printf("[TEST] LAN shortcut disabled - forcing NAT punch\n");
    }
    if (verbose_punch) {
        printf("[TEST] Verbose NAT punch logging enabled\n");
    }
    
    // 调用 p2p_connect（target_name 为 NULL 表示被动等待）
    if (p2p_connect(s, target_name) < 0) {
        printf("Failed to initialize connection\n");
        return 1;
    }

    // 打印运行模式
    if (target_name) {
        printf("Running in %s mode (connecting to %s)...\n\n", mode_name, target_name);
    } else {
        printf("Running in %s mode (waiting for connection)...\n\n", mode_name);
    }

    /* 主循环 */
    while (1) {
        p2p_update(s);  /* 自动处理信令交换 */

        log_state_change(s);

        /* 连接后进行数据交换 */
        if (p2p_is_ready(s)) {
            char data[256] = {0};
            int r = p2p_recv(s, data, sizeof(data)-1);
            if (r > 0) {
                printf("[DATA] Received: %s\n", data);
                fflush(stdout);
            }
        }

        p2p_sleep_ms(10);
    }

    return 0;
}
