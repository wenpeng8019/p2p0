CC       = cc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c99 -O2 -Iinclude -Isrc
LDFLAGS  =

BUILDDIR = build
TMPDIR   = $(BUILDDIR)/tmp
SRCDIR   = src

SRCS     = p2p_udp.c p2p_nat.c p2p_trans_reliable.c p2p_stream.c p2p_route.c p2p.c p2p_stun.c p2p_ice.c p2p_turn.c p2p_tcp_punch.c p2p_crypto.c p2p_trans_pseudotcp.c p2p_signal_relay.c p2p_signal_pubsub.c p2p_signal_protocol.c p2p_signal_compact.c p2p_crypto_extra.c p2p_log.c

# MbedTLS configuration (Prefer third_party/mbedtls if exists)
MBEDTLS_DIR = third_party/mbedtls
MBEDTLS_LIB_DIR = $(MBEDTLS_DIR)/library

ifeq ($(wildcard $(MBEDTLS_DIR)/include/mbedtls/ssl.h),)
	# Use system mbedtls
	MBEDTLS_INC = -I/opt/homebrew/include
	MBEDTLS_LIBS = -L/opt/homebrew/lib -lmbedtls -lmbedx509 -lmbedcrypto
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
	# Specifically try brew openssl @3 first, then general pkg-config
	OPENSSL_PREFIX = $(shell brew --prefix openssl@3 2>/dev/null || echo "/opt/homebrew/opt/openssl")
	CFLAGS += -DWITH_OPENSSL -I$(OPENSSL_PREFIX)/include
	LDFLAGS += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
endif
ifdef THREADED
CFLAGS  += -DP2P_THREADED
SRCS    += p2p_thread.c
LDFLAGS += -lpthread
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
