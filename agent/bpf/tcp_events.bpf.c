#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
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

SEC("tracepoint/sock/inet_sock_set_state")
int handle_inet_sock_set_state(struct trace_event_raw_inet_sock_set_state *ctx)
{
	struct show_me_tcp_event *event;
	__u64 pid_tgid;

	if (ctx->protocol != SHOW_ME_IPPROTO_TCP)
		return 0;

	if (ctx->family != SHOW_ME_AF_INET && ctx->family != SHOW_ME_AF_INET6)
		return 0;

	if (ctx->newstate != SHOW_ME_TCP_ESTABLISHED &&
	    ctx->newstate != SHOW_ME_TCP_CLOSE)
		return 0;

	event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
	if (event == NULL)
		return 0;

	pid_tgid = bpf_get_current_pid_tgid();
	event->timestamp_ns = bpf_ktime_get_ns();
	event->pid = (__u32)pid_tgid;
	event->tgid = (__u32)(pid_tgid >> 32);
	event->family = ctx->family;
	event->local_port = ctx->sport;
	event->remote_port = ctx->dport;
	event->kind = SHOW_ME_EVENT_TCP_STATE;
	event->action = ctx->newstate == SHOW_ME_TCP_ESTABLISHED ?
		SHOW_ME_TCP_ACTION_ESTABLISHED : SHOW_ME_TCP_ACTION_CLOSED;
	bpf_get_current_comm(event->comm, sizeof(event->comm));

	if (ctx->family == SHOW_ME_AF_INET) {
		__builtin_memcpy(event->local_address, ctx->saddr, 4);
		__builtin_memcpy(event->remote_address, ctx->daddr, 4);
	} else {
		__builtin_memcpy(event->local_address, ctx->saddr_v6,
				 sizeof(event->local_address));
		__builtin_memcpy(event->remote_address, ctx->daddr_v6,
				 sizeof(event->remote_address));
	}

	bpf_ringbuf_submit(event, 0);
	return 0;
}
