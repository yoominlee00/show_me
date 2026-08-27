#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "event.h"
#include "tcp_events.skel.h"
#include "transport.h"

static volatile sig_atomic_t exiting;

static int libbpf_log(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG)
		return 0;

	return vfprintf(stderr, format, args);
}

static void handle_signal(int signal_number)
{
	(void)signal_number;
	exiting = 1;
}

static const char *action_name(__u8 action)
{
	return action == SHOW_ME_TCP_ACTION_ESTABLISHED ? "established" : "closed";
}

static const char *event_name(__u8 kind, __u8 action)
{
	if (kind == SHOW_ME_EVENT_TCP_RETRANSMIT)
		return "retransmit";

	return action_name(action);
}

static int handle_event(void *context, void *data, size_t size)
{
	const struct show_me_tcp_event *event = data;
	char local_address[INET6_ADDRSTRLEN];
	char remote_address[INET6_ADDRSTRLEN];
	const char *local;
	const char *remote;

	struct show_me_transport *transport = context;
	if (size != sizeof(*event)) {
		fprintf(stderr, "unexpected event size: %zu\n", size);
		return 0;
	}

	local = inet_ntop(event->family, event->local_address, local_address,
			  sizeof(local_address));
	remote = inet_ntop(event->family, event->remote_address, remote_address,
			   sizeof(remote_address));
	if (local == NULL || remote == NULL) {
		fprintf(stderr, "unable to format socket address: %s\n", strerror(errno));
		return 0;
	}

	printf("tcp.%s pid=%u tgid=%u comm=%s local=%s:%u remote=%s:%u retransmits=%u\n",
	       event_name(event->kind, event->action), event->pid, event->tgid,
	       event->comm, local, event->local_port, remote, event->remote_port,
	       event->retransmit_count);
	if (!show_me_transport_submit(transport, event))
		fprintf(stderr, "transport queue full; event dropped\n");
	return 0;
}

int main(void)
{
	struct ring_buffer *ring_buffer = NULL;
	struct tcp_events_bpf *skel = NULL;
	struct show_me_transport *transport = NULL;
	struct show_me_transport_config transport_config;
	const char *token = getenv("INGEST_API_TOKEN");
	const char *endpoint = getenv("SHOW_ME_INGEST_URL");
	const char *configured_agent_id = getenv("SHOW_ME_AGENT_ID");
	char host_name[128];
	char agent_id[128];
	int error = 0;

	if (token == NULL || token[0] == '\0') {
		fprintf(stderr, "INGEST_API_TOKEN is required\n");
		return EXIT_FAILURE;
	}
	if (gethostname(host_name, sizeof(host_name)) != 0) {
		perror("gethostname");
		return EXIT_FAILURE;
	}
	host_name[sizeof(host_name) - 1] = '\0';
	snprintf(agent_id, sizeof(agent_id), "%s", configured_agent_id != NULL ? configured_agent_id : host_name);
	transport_config = (struct show_me_transport_config){
		.endpoint = endpoint != NULL ? endpoint : "http://localhost:8080/api/v1/events",
		.api_token = token,
		.agent_id = agent_id,
		.host_name = host_name,
	};
	transport = show_me_transport_start(&transport_config);
	if (transport == NULL) {
		fprintf(stderr, "failed to start ingestion transport\n");
		return EXIT_FAILURE;
	}

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
	libbpf_set_print(libbpf_log);

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	skel = tcp_events_bpf__open();
	if (skel == NULL) {
		fprintf(stderr, "failed to open BPF skeleton\n");
		return EXIT_FAILURE;
	}

	error = tcp_events_bpf__load(skel);
	if (error != 0) {
		fprintf(stderr, "failed to load BPF programs: %d\n", error);
		goto cleanup;
	}

	{
		__u32 key = 0;
		__u32 agent_process_id = (__u32)getpid();
		error = bpf_map_update_elem(bpf_map__fd(skel->maps.agent_pid), &key,
					    &agent_process_id, BPF_ANY);
		if (error != 0) {
			fprintf(stderr, "failed to configure Agent PID filter: %d\n", error);
			goto cleanup;
		}
	}

	error = tcp_events_bpf__attach(skel);
	if (error != 0) {
		fprintf(stderr, "failed to attach BPF programs: %d\n", error);
		goto cleanup;
	}

	ring_buffer = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event,
				      transport, NULL);
	if (ring_buffer == NULL) {
		error = -errno;
		fprintf(stderr, "failed to create ring buffer: %d\n", error);
		goto cleanup;
	}

	printf("show-me Agent loaded; collecting TCP established and closed events\n");
	while (!exiting) {
		error = ring_buffer__poll(ring_buffer, 100);
		if (error == -EINTR)
			continue;
		if (error < 0) {
			fprintf(stderr, "ring-buffer poll failed: %d\n", error);
			goto cleanup;
		}
	}

	error = 0;

cleanup:
	ring_buffer__free(ring_buffer);
	tcp_events_bpf__destroy(skel);
	if (transport != NULL)
		fprintf(stderr, "transport dropped events: %zu\n", show_me_transport_dropped(transport));
	show_me_transport_stop(transport);
	return error == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
