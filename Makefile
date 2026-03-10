CC       = cc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c99 -O2 -Iinclude -Isrc -Ii18n -Istdc
LDFLAGS  =

# Language support (set ENABLE_CHINESE=1 to enable Chinese translations)
ifdef ENABLE_CHINESE
	CFLAGS += -DP2P_ENABLE_CHINESE
endif

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

SRCS     = p2p_udp.c p2p_nat.c p2p_trans_reliable.c p2p_stream.c p2p_route.c p2p_probe.c p2p.c p2p_stun.c p2p_ice.c p2p_turn.c p2p_tcp_punch.c p2p_crypto.c p2p_trans_pseudotcp.c p2p_signal_relay.c p2p_signal_pubsub.c p2p_signal_compact.c p2p_http.c p2p_path_manager.c p2p_channel.c

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
	SRCS += p2p_dtls_mbedtls.c
	CFLAGS += -DWITH_DTLS $(MBEDTLS_INC)
	LDFLAGS += $(MBEDTLS_LIBS)
endif

# Optional DTLS support (OpenSSL)
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

# Optional SCTP support (usrsctp)
ifeq ($(WITH_SCTP), 1)
	SRCS += p2p_trans_sctp.c
	USRSCTP_DIR = third_party/usrsctp/usrsctplib
	ifeq ($(wildcard $(USRSCTP_DIR)/usrsctp.h),)
		$(error usrsctp not found at $(USRSCTP_DIR))
	endif
	CFLAGS  += -DWITH_SCTP -I$(USRSCTP_DIR)
	# usrsctp 需要单独编译为静态库，参见 CMakeLists.txt
	LDFLAGS += -lusrsctp
endif

ifdef THREADED
CFLAGS  += -DP2P_THREADED
SRCS    += p2p_thread.c
ifneq ($(UNAME_S), Darwin)
LDFLAGS += -lpthread
endif
endif

# --- i18n code generation ---
# Usage:  make                    (debug: SID-based stable IDs)
#         make I18N_NDEBUG=1      (release: compact sequential IDs)
#         make i18n               (force re-run i18n extraction)
I18N_SCRIPT  = i18n/i18n.sh
I18N_FLAGS   =
ifdef I18N_NDEBUG
	I18N_FLAGS += --ndebug
endif

# Stamp file breaks the dependency cycle: i18n.sh rewrites *.c (updating mtime),
# but the stamp only updates after a successful run, not on every source change.
I18N_STAMP   = $(BUILDDIR)/.i18n_stamp

# Generate object file list after all source modifications
OBJS     = $(patsubst %.c,$(TMPDIR)/%.o,$(SRCS))

# Static library target
LIBP2P   = $(BUILDDIR)/libp2p.a

.PHONY: all clean i18n

all: $(LIBP2P)

# i18n: explicit target always re-runs extraction
i18n:
	$(I18N_SCRIPT) $(SRCDIR) $(I18N_FLAGS)
	@mkdir -p $(BUILDDIR) && touch $(I18N_STAMP)

# Stamp rule: run i18n.sh when script itself changes or stamp is missing.
# Source file changes don't auto-trigger (avoids mtime cycle from rewrite).
# Use 'make i18n' to manually refresh after adding/removing LA_* calls.
$(I18N_STAMP): $(I18N_SCRIPT) | $(BUILDDIR)
	$(I18N_SCRIPT) $(SRCDIR) $(I18N_FLAGS)
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
