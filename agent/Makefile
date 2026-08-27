CLANG ?= clang
CC ?= cc
BPFTOOL ?= bpftool

ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
TARGET_ARCH := x86
else ifneq (,$(filter $(ARCH),aarch64 arm64))
TARGET_ARCH := arm64
else
$(error Unsupported architecture: $(ARCH). Add a TARGET_ARCH mapping.)
endif

BUILD_DIR := build
BPF_DIR := bpf
INCLUDE_DIR := include
SRC_DIR := src

VMLINUX := $(BPF_DIR)/vmlinux.h
BPF_OBJ := $(BUILD_DIR)/tcp_events.bpf.o
SKEL := $(BUILD_DIR)/tcp_events.skel.h
AGENT := $(BUILD_DIR)/show-me-agent

BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(TARGET_ARCH) \
	-I$(INCLUDE_DIR) -I$(BPF_DIR)
USER_CFLAGS := -g -O2 -Wall -Wextra -Werror -I$(INCLUDE_DIR) -I$(BUILD_DIR)
USER_LIBS := -lbpf -lelf -lz

.PHONY: all clean check-prerequisites

all: check-prerequisites $(AGENT)

check-prerequisites:
	@test -r /sys/kernel/btf/vmlinux || \
		(printf '%s\n' 'Missing /sys/kernel/btf/vmlinux. Build this Agent on a Linux host with BTF enabled.' && false)
	@command -v $(BPFTOOL) >/dev/null || \
		(printf '%s\n' 'bpftool is required to generate vmlinux.h and the libbpf skeleton.' && false)

$(BUILD_DIR):
	mkdir -p $@

$(VMLINUX): | $(BUILD_DIR)
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

$(BPF_OBJ): $(BPF_DIR)/tcp_events.bpf.c $(INCLUDE_DIR)/event.h $(VMLINUX) | $(BUILD_DIR)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

$(SKEL): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

$(AGENT): $(SRC_DIR)/main.c $(INCLUDE_DIR)/event.h $(SKEL) | $(BUILD_DIR)
	$(CC) $(USER_CFLAGS) $< -o $@ $(USER_LIBS)

clean:
	rm -rf $(BUILD_DIR) $(VMLINUX)
