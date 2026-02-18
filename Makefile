CC       = cc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c99 -O2 -Iinclude -Isrc
LDFLAGS  =

# Detect OS
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

# OpenSSL for p2p_signal_pubsub.c (DES encryption)
# Priority: pkg-config > brew (macOS) > distro default paths
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

SRCS     = p2p_udp.c p2p_nat.c p2p_trans_reliable.c p2p_stream.c p2p_route.c p2p.c p2p_stun.c p2p_ice.c p2p_turn.c p2p_tcp_punch.c p2p_crypto.c p2p_trans_pseudotcp.c p2p_signal_relay.c p2p_signal_pubsub.c p2p_signal_compact.c p2p_crypto_extra.c p2p_log.c p2p_http.c

# MbedTLS configuration (Prefer third_party/mbedtls if exists)
MBEDTLS_DIR = third_party/mbedtls
MBEDTLS_LIB_DIR = $(MBEDTLS_DIR)/library

ifeq ($(wildcard $(MBEDTLS_DIR)/include/mbedtls/ssl.h),)
	# Use system mbedtls: try pkg-config first, then brew (macOS), then distro default
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
	# Use vendored mbedtls
	MBEDTLS_INC  = -I$(MBEDTLS_DIR)/include
	MBEDTLS_LIBS = $(MBEDTLS_LIB_DIR)/libmbedtls.a $(MBEDTLS_LIB_DIR)/libmbedx509.a $(MBEDTLS_LIB_DIR)/libmbedcrypto.a
	MBEDTLS_DEP  = $(MBEDTLS_LIBS)
endif

# Optional DTLS support (MbedTLS)
ifeq ($(WITH_DTLS), 1)
	SRCS += p2p_trans_mbedtls.c
	CFLAGS += -DWITH_DTLS $(MBEDTLS_INC)
	LDFLAGS += $(MBEDTLS_LIBS)
endif

# Optional DTLS support (OpenSSL)
ifeq ($(WITH_OPENSSL), 1)
	SRCS += p2p_trans_openssl.c
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
ifdef THREADED
CFLAGS  += -DP2P_THREADED
SRCS    += p2p_thread.c
ifneq ($(UNAME_S), Darwin)
LDFLAGS += -lpthread
endif
endif

# Generate object file list after all source modifications
OBJS     = $(patsubst %.c,$(TMPDIR)/%.o,$(SRCS))

# Static library target
LIBP2P   = $(BUILDDIR)/libp2p.a

.PHONY: all clean

all: $(LIBP2P)

$(LIBP2P): $(OBJS) $(MBEDTLS_DEP) | $(BUILDDIR)
	ar rcs $@ $(OBJS)

$(MBEDTLS_LIBS):
	$(MAKE) -C $(MBEDTLS_DIR) library

$(TMPDIR)/%.o: $(SRCDIR)/%.c include/p2p.h $(SRCDIR)/p2p_internal.h | $(TMPDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TMPDIR): | $(BUILDDIR)
	mkdir -p $(TMPDIR)

clean:
	rm -rf $(BUILDDIR)
