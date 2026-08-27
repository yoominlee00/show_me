#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "event.h"

char LICENSE[] SEC("license") = "GPL";

#define SHOW_ME_AF_INET 2
#define SHOW_ME_AF_INET6 10
#define SHOW_ME_IPPROTO_TCP 6
#define SHOW_ME_TCP_ESTABLISHED 1
#define SHOW_ME_TCP_CLOSE 7

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

struct show_me_connection_metadata {
	__u32 pid;
	__u32 tgid;
	__u16 family;
	__u16 local_port;
	__u16 remote_port;
	__u32 retransmit_count;
	char comm[SHOW_ME_COMM_LEN];
	__u8 local_address[SHOW_ME_IP_LEN];
	__u8 remote_address[SHOW_ME_IP_LEN];
};

/* Socket keys are kernel-local correlation data and are never emitted. */
struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 16384);
	__type(key, __u64);
	__type(value, struct show_me_connection_metadata);
} connections SEC(".maps");

static __always_inline void copy_metadata_to_event(
	struct show_me_tcp_event *event,
	const struct show_me_connection_metadata *metadata)
{
	event->pid = metadata->pid;
	event->tgid = metadata->tgid;
	event->family = metadata->family;
	event->local_port = metadata->local_port;
	event->remote_port = metadata->remote_port;
	event->retransmit_count = metadata->retransmit_count;
	__builtin_memcpy(event->comm, metadata->comm, sizeof(event->comm));
	__builtin_memcpy(event->local_address, metadata->local_address,
			 sizeof(event->local_address));
	__builtin_memcpy(event->remote_address, metadata->remote_address,
			 sizeof(event->remote_address));
}

static __always_inline void emit_state_event(
	const struct show_me_connection_metadata *metadata, __u8 action)
{
	struct show_me_tcp_event *event;

	event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
	if (event == NULL)
		return;

	event->timestamp_ns = bpf_ktime_get_ns();
	event->kind = SHOW_ME_EVENT_TCP_STATE;
	event->action = action;
	copy_metadata_to_event(event, metadata);
	bpf_ringbuf_submit(event, 0);
}

SEC("tracepoint/sock/inet_sock_set_state")
int handle_inet_sock_set_state(struct trace_event_raw_inet_sock_set_state *ctx)
{
	struct show_me_connection_metadata metadata = {};
	struct show_me_connection_metadata *saved_metadata;
	__u64 pid_tgid;
	__u64 socket_key = (__u64)ctx->skaddr;

	if (ctx->protocol != SHOW_ME_IPPROTO_TCP)
		return 0;

	if (ctx->family != SHOW_ME_AF_INET && ctx->family != SHOW_ME_AF_INET6)
		return 0;

	if (ctx->newstate != SHOW_ME_TCP_ESTABLISHED &&
	    ctx->newstate != SHOW_ME_TCP_CLOSE)
		return 0;

	if (ctx->newstate == SHOW_ME_TCP_CLOSE) {
		saved_metadata = bpf_map_lookup_elem(&connections, &socket_key);
		if (saved_metadata != NULL) {
			emit_state_event(saved_metadata, SHOW_ME_TCP_ACTION_CLOSED);
			bpf_map_delete_elem(&connections, &socket_key);
		}
		return 0;
	}

	pid_tgid = bpf_get_current_pid_tgid();
	metadata.pid = (__u32)pid_tgid;
	metadata.tgid = (__u32)(pid_tgid >> 32);
	metadata.family = ctx->family;
	metadata.local_port = ctx->sport;
	metadata.remote_port = ctx->dport;
	bpf_get_current_comm(metadata.comm, sizeof(metadata.comm));

	if (ctx->family == SHOW_ME_AF_INET) {
		bpf_probe_read_kernel(metadata.local_address, 4, &ctx->saddr);
		bpf_probe_read_kernel(metadata.remote_address, 4, &ctx->daddr);
	} else {
		bpf_probe_read_kernel(metadata.local_address,
				      sizeof(metadata.local_address), &ctx->saddr_v6);
		bpf_probe_read_kernel(metadata.remote_address,
				      sizeof(metadata.remote_address), &ctx->daddr_v6);
	}

	bpf_map_update_elem(&connections, &socket_key, &metadata, BPF_ANY);
	emit_state_event(&metadata, SHOW_ME_TCP_ACTION_ESTABLISHED);
	return 0;
}

SEC("tp_btf/tcp_retransmit_skb")
int BPF_PROG(handle_tcp_retransmit_skb, struct sock *sk, struct sk_buff *skb)
{
	struct show_me_connection_metadata *metadata;
	struct show_me_tcp_event *event;
	__u64 socket_key = (__u64)sk;

	(void)skb;

	metadata = bpf_map_lookup_elem(&connections, &socket_key);
	if (metadata == NULL)
		return 0;

	__sync_fetch_and_add(&metadata->retransmit_count, 1);
	event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
	if (event == NULL)
		return 0;

	event->timestamp_ns = bpf_ktime_get_ns();
	event->kind = SHOW_ME_EVENT_TCP_RETRANSMIT;
	event->action = 0;
	copy_metadata_to_event(event, metadata);
	bpf_ringbuf_submit(event, 0);
	return 0;
}
