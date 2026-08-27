#ifndef SHOW_ME_AGENT_EVENT_H
#define SHOW_ME_AGENT_EVENT_H

#ifndef __BPF__
#include <linux/types.h>
#endif

#define SHOW_ME_COMM_LEN 16
#define SHOW_ME_IP_LEN 16

enum show_me_event_kind {
	SHOW_ME_EVENT_TCP_STATE = 1,
	SHOW_ME_EVENT_TCP_RETRANSMIT = 2,
};

enum show_me_tcp_action {
	SHOW_ME_TCP_ACTION_ESTABLISHED = 1,
	SHOW_ME_TCP_ACTION_CLOSED = 2,
};

/*
 * Fixed-size data copied from a BPF ring buffer to user space.
 * Addresses are stored as 16 raw bytes; IPv4 uses the first four bytes.
 */
struct show_me_tcp_event {
	__u64 timestamp_ns;
	__u32 pid;
	__u32 tgid;
	__u16 family;
	__u16 local_port;
	__u16 remote_port;
	__u32 retransmit_count;
	__u8 kind;
	__u8 action;
	char comm[SHOW_ME_COMM_LEN];
	__u8 local_address[SHOW_ME_IP_LEN];
	__u8 remote_address[SHOW_ME_IP_LEN];
};

#endif /* SHOW_ME_AGENT_EVENT_H */
