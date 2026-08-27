#ifndef SHOW_ME_AGENT_TRANSPORT_H
#define SHOW_ME_AGENT_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "event.h"

struct show_me_transport;

struct show_me_transport_config {
	const char *endpoint;
	const char *api_token;
	const char *agent_id;
	const char *host_name;
};

struct show_me_transport *show_me_transport_start(
	const struct show_me_transport_config *config);
bool show_me_transport_submit(struct show_me_transport *transport,
	const struct show_me_tcp_event *event);
size_t show_me_transport_dropped(const struct show_me_transport *transport);
void show_me_transport_stop(struct show_me_transport *transport);

#endif /* SHOW_ME_AGENT_TRANSPORT_H */

