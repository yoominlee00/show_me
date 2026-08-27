#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "event.h"

char LICENSE[] SEC("license") = "GPL";

/*
 * This program intentionally attaches to no hook in the skeleton commit.
 * The next commit adds the inet_sock_set_state tracepoint and ring-buffer
 * emission while keeping this map ABI stable.
 */
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

