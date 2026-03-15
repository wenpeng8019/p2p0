CC       = cc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c99 -O2 -Iinclude -Isrc -Ii18n -Istdc
LDFLAGS  =

# --- 构建选项 ---
# I18N_ENABLED=1  启用 i18n 多语言支持（默认禁用，仅使用字面量）
# I18N_NDEBUG=1   紧凑模式（Release 构建），连续编号
# I18N_CN=1       生成中文翻译头文件 (--import cn)
ifdef I18N_ENABLED
	CFLAGS += -DI18N_ENABLED
endif

# 操作系统检测
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

# p2p_signal_pubsub.c 需要 OpenSSL（DES 加密）
# 优先级: pkg-config > brew (macOS) > 发行版默认路径
ifeq ($(shell pkg-config --exists libcrypto 2>/dev/null && echo yes), yes)
    OPENSSL_CFLAGS  := $(shell pkg-config --cflags libcrypto)
    OPENSSL_LDFLAGS := $(shell pkg-config --libs libcrypto)
else ifeq ($(UNAME_S), Darwin)
    OPENSSL_PREFIX  := $(shell brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null || echo /usr/local)
    OPENSSL_CFLAGS  := -I$(OPENSSL_PREFIX)/include
    OPENSSL_LDFLAGS := -L$(OPENSSL_PREFIX)/lib -lcrypto
else
    OPENSSL_CFLAGS  :=
    OPENSSL_LDFLAGS := -lcrypto
endif
CFLAGS  += $(OPENSSL_CFLAGS)
LDFLAGS += $(OPENSSL_LDFLAGS)

BUILDDIR = build
TMPDIR   = $(BUILDDIR)/tmp
SRCDIR   = src

SRCS     = p2p_udp.c p2p_nat.c p2p_trans_reliable.c p2p_stream.c p2p_route.c p2p_probe.c p2p.c p2p_stun.c p2p_ice.c p2p_turn.c p2p_tcp_punch.c p2p_crypto.c p2p_trans_pseudotcp.c p2p_signal_relay.c p2p_signal_pubsub.c p2p_signal_compact.c p2p_http.c p2p_path_manager.c p2p_channel.c p2p_instrument.c

# --- MbedTLS 配置（优先使用 third_party/mbedtls）---
MBEDTLS_DIR = third_party/mbedtls
MBEDTLS_LIB_DIR = $(MBEDTLS_DIR)/library

ifeq ($(wildcard $(MBEDTLS_DIR)/include/mbedtls/ssl.h),)
	# 使用系统 mbedtls: 优先 pkg-config，然后 brew (macOS)，最后发行版默认
	ifeq ($(shell pkg-config --exists mbedtls 2>/dev/null && echo yes), yes)
		MBEDTLS_INC  = $(shell pkg-config --cflags mbedtls)
		MBEDTLS_LIBS = $(shell pkg-config --libs mbedtls)
	else ifeq ($(UNAME_S), Darwin)
		HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
		MBEDTLS_INC  = -I$(HOMEBREW_PREFIX)/include
		MBEDTLS_LIBS = -L$(HOMEBREW_PREFIX)/lib -lmbedtls -lmbedx509 -lmbedcrypto
	else
		MBEDTLS_INC  =
		MBEDTLS_LIBS = -lmbedtls -lmbedx509 -lmbedcrypto
	endif
else
	# 使用内置 mbedtls
	MBEDTLS_INC  = -I$(MBEDTLS_DIR)/include
	MBEDTLS_LIBS = $(MBEDTLS_LIB_DIR)/libmbedtls.a $(MBEDTLS_LIB_DIR)/libmbedx509.a $(MBEDTLS_LIB_DIR)/libmbedcrypto.a
	MBEDTLS_DEP  = $(MBEDTLS_LIBS)
endif

# 可选: DTLS 支持 (MbedTLS)
ifeq ($(WITH_DTLS), 1)
	SRCS += p2p_dtls_mbedtls.c
	CFLAGS += -DWITH_DTLS $(MBEDTLS_INC)
	LDFLAGS += $(MBEDTLS_LIBS)
endif

# 可选: DTLS 支持 (OpenSSL)
ifeq ($(WITH_OPENSSL), 1)
	SRCS += p2p_dtls_openssl.c
	ifeq ($(shell pkg-config --exists openssl 2>/dev/null && echo yes), yes)
		CFLAGS  += -DWITH_OPENSSL $(shell pkg-config --cflags openssl)
		LDFLAGS += $(shell pkg-config --libs openssl)
	else ifeq ($(UNAME_S), Darwin)
		OPENSSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null || brew --prefix openssl 2>/dev/null || echo /usr/local)
		CFLAGS  += -DWITH_OPENSSL -I$(OPENSSL_PREFIX)/include
		LDFLAGS += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
	else
		CFLAGS  += -DWITH_OPENSSL
		LDFLAGS += -lssl -lcrypto
	endif
endif

# 可选: SCTP 支持 (usrsctp)
ifeq ($(WITH_SCTP), 1)
	SRCS += p2p_trans_sctp.c
	USRSCTP_DIR = third_party/usrsctp/usrsctplib
	ifeq ($(wildcard $(USRSCTP_DIR)/usrsctp.h),)
		$(error usrsctp 未找到: $(USRSCTP_DIR))
	endif
	CFLAGS  += -DWITH_SCTP -I$(USRSCTP_DIR)
	LDFLAGS += -lusrsctp
endif

# 可选: 内部线程支持
ifdef THREADED
CFLAGS  += -DP2P_THREADED
SRCS    += p2p_thread.c
ifneq ($(UNAME_S), Darwin)
LDFLAGS += -lpthread
endif
endif

# --- i18n 代码生成 ---
# 用法:  make                    (调试: 基于 SID 的稳定 ID)
#         make I18N_NDEBUG=1      (发布: 紧凑连续 ID)
#         make I18N_CN=1          (生成中文翻译头文件 LANG.cn.h)
#         make i18n               (强制重新运行 i18n 提取)
I18N_SCRIPT  = i18n/i18n.sh
I18N_FLAGS   =
ifdef I18N_NDEBUG
	I18N_FLAGS += --ndebug
endif
ifdef I18N_CN
	I18N_FLAGS += --import cn
endif

# 标记文件打破依赖循环: i18n.sh 会回写 *.c（更新 mtime），
# 但标记文件仅在成功运行后更新，而非每次源文件变化时。
I18N_STAMP   = $(BUILDDIR)/.i18n_stamp

# 生成目标文件列表（在所有源文件修改后）
OBJS     = $(patsubst %.c,$(TMPDIR)/%.o,$(SRCS))

# 静态库目标
LIBP2P   = $(BUILDDIR)/libp2p.a

.PHONY: all clean i18n

all: $(LIBP2P)

# i18n: 显式目标，始终重新运行提取
# 需要 compile_commands.json 提供完整编译参数（含条件编译文件）
# 若无则自动调用 cmake 生成
i18n:
	@if [ ! -f build_cmake/compile_commands.json ]; then \
		echo "生成 compile_commands.json ..."; \
		cmake -B build_cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null 2>&1 || true; \
	fi
	$(I18N_SCRIPT) $(SRCDIR) --name p2p $(I18N_FLAGS)
	@mkdir -p $(BUILDDIR) && touch $(I18N_STAMP)

# 标记规则: 仅当脚本本身变化或标记文件不存在时运行 i18n.sh
# 源文件变化不会自动触发（避免回写导致的 mtime 循环）
# 添加/删除 LA_* 调用后使用 'make i18n' 手动刷新
$(I18N_STAMP): $(I18N_SCRIPT) | $(BUILDDIR)
	@if [ ! -f build_cmake/compile_commands.json ]; then \
		echo "生成 compile_commands.json ..."; \
		cmake -B build_cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null 2>&1 || true; \
	fi
	$(I18N_SCRIPT) $(SRCDIR) --name p2p $(I18N_FLAGS)
	@touch $@

$(LIBP2P): $(OBJS) $(MBEDTLS_DEP) | $(BUILDDIR)
	ar rcs $@ $(OBJS)

$(MBEDTLS_LIBS):
	$(MAKE) -C $(MBEDTLS_DIR) library

$(TMPDIR)/%.o: $(SRCDIR)/%.c include/p2p.h $(SRCDIR)/p2p_internal.h $(I18N_STAMP) | $(TMPDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TMPDIR): | $(BUILDDIR)
	mkdir -p $(TMPDIR)

clean:
	rm -rf $(BUILDDIR)
