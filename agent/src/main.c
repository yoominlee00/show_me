#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <bpf/libbpf.h>

#include "tcp_events.skel.h"

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

int main(void)
{
	struct tcp_events_bpf *skel = NULL;
	int error = 0;

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

	error = tcp_events_bpf__attach(skel);
	if (error != 0) {
		fprintf(stderr, "failed to attach BPF programs: %d\n", error);
		goto cleanup;
	}

	printf("show-me Agent loaded; no TCP hook is enabled in this skeleton yet\n");
	while (!exiting)
		pause();

cleanup:
	tcp_events_bpf__destroy(skel);
	return error == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
