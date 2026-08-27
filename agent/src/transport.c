#include <arpa/inet.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "transport.h"

#define SHOW_ME_QUEUE_CAPACITY 1024
#define SHOW_ME_STRING_CAPACITY 256
#define SHOW_ME_JSON_CAPACITY 2048

struct show_me_transport {
	struct show_me_tcp_event queue[SHOW_ME_QUEUE_CAPACITY];
	size_t head, tail, count, dropped;
	bool stopping;
	pthread_mutex_t lock;
	pthread_cond_t has_items;
	pthread_t worker;
	char endpoint[SHOW_ME_STRING_CAPACITY];
	char token[SHOW_ME_STRING_CAPACITY];
	char agent_id[SHOW_ME_STRING_CAPACITY];
	char host_name[SHOW_ME_STRING_CAPACITY];
};

static const char *event_type(const struct show_me_tcp_event *event)
{
	return event->kind == SHOW_ME_EVENT_TCP_RETRANSMIT ?
		"network.tcp.retransmit.v1" : "network.tcp.connection.v1";
}

static const char *action(const struct show_me_tcp_event *event)
{
	return event->action == SHOW_ME_TCP_ACTION_ESTABLISHED ? "established" : "closed";
}

static void timestamp_now(char *output, size_t output_size)
{
	struct timespec now;
	struct tm utc;

	clock_gettime(CLOCK_REALTIME, &now);
	gmtime_r(&now.tv_sec, &utc);
	strftime(output, output_size, "%Y-%m-%dT%H:%M:%S", &utc);
	snprintf(output + strlen(output), output_size - strlen(output), ".%03ldZ",
		 now.tv_nsec / 1000000L);
}

static int send_event(CURL *curl, const struct show_me_transport *transport,
			      const struct show_me_tcp_event *event)
{
	char local[INET6_ADDRSTRLEN], remote[INET6_ADDRSTRLEN], occurred_at[32];
	char body[SHOW_ME_JSON_CAPACITY], token_header[SHOW_ME_STRING_CAPACITY + 32];
	struct curl_slist *headers = NULL;
	long response_code = 0;
	CURLcode result;

	if (inet_ntop(event->family, event->local_address, local, sizeof(local)) == NULL ||
	    inet_ntop(event->family, event->remote_address, remote, sizeof(remote)) == NULL)
		return -1;

	timestamp_now(occurred_at, sizeof(occurred_at));
	if (event->kind == SHOW_ME_EVENT_TCP_RETRANSMIT) {
		snprintf(body, sizeof(body),
			"{\"agentId\":\"%s\",\"eventType\":\"%s\",\"occurredAt\":\"%s\",\"payload\":{\"hostName\":\"%s\",\"pid\":%u,\"tgid\":%u,\"processName\":\"%s\",\"processKnown\":true,\"protocol\":\"tcp\",\"remoteAddress\":\"%s\",\"remotePort\":%u,\"retransmitCount\":%u}}",
			transport->agent_id, event_type(event), occurred_at, transport->host_name,
			event->pid, event->tgid, event->comm, remote, event->remote_port,
			event->retransmit_count);
	} else {
		snprintf(body, sizeof(body),
			"{\"agentId\":\"%s\",\"eventType\":\"%s\",\"occurredAt\":\"%s\",\"payload\":{\"hostName\":\"%s\",\"pid\":%u,\"tgid\":%u,\"processName\":\"%s\",\"processKnown\":true,\"protocol\":\"tcp\",\"localAddress\":\"%s\",\"localPort\":%u,\"remoteAddress\":\"%s\",\"remotePort\":%u,\"action\":\"%s\",\"direction\":\"unknown\"}}",
			transport->agent_id, event_type(event), occurred_at, transport->host_name,
			event->pid, event->tgid, event->comm, local, event->local_port,
			remote, event->remote_port, action(event));
	}

	snprintf(token_header, sizeof(token_header), "X-API-Token: %s", transport->token);
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, token_header);
	curl_easy_setopt(curl, CURLOPT_URL, transport->endpoint);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	result = curl_easy_perform(curl);
	if (result == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	curl_slist_free_all(headers);
	return result == CURLE_OK && response_code == 202 ? 0 : -1;
}

static int send_with_retry(CURL *curl, const struct show_me_transport *transport,
			   const struct show_me_tcp_event *event)
{
	struct timespec backoff = { .tv_sec = 0, .tv_nsec = 200000000L };
	int attempt;

	for (attempt = 0; attempt < 3; attempt++) {
		if (send_event(curl, transport, event) == 0)
			return 0;
		if (attempt < 2)
			nanosleep(&backoff, NULL);
		backoff.tv_nsec *= 2;
	}
	return -1;
}

static void *transport_worker(void *context)
{
	struct show_me_transport *transport = context;
	CURL *curl = curl_easy_init();

	if (curl == NULL)
		return NULL;
	for (;;) {
		struct show_me_tcp_event event;
		pthread_mutex_lock(&transport->lock);
		while (!transport->stopping && transport->count == 0)
			pthread_cond_wait(&transport->has_items, &transport->lock);
		if (transport->stopping && transport->count == 0) {
			pthread_mutex_unlock(&transport->lock);
			break;
		}
		event = transport->queue[transport->head];
		transport->head = (transport->head + 1) % SHOW_ME_QUEUE_CAPACITY;
		transport->count--;
		pthread_mutex_unlock(&transport->lock);
		if (send_with_retry(curl, transport, &event) != 0)
			fprintf(stderr, "failed to publish %s event\n", event_type(&event));
	}
	curl_easy_cleanup(curl);
	return NULL;
}

struct show_me_transport *show_me_transport_start(const struct show_me_transport_config *config)
{
	struct show_me_transport *transport;

	if (config == NULL || config->endpoint == NULL || config->api_token == NULL ||
	    config->agent_id == NULL || config->host_name == NULL || curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
		return NULL;
	transport = calloc(1, sizeof(*transport));
	if (transport == NULL)
		return NULL;
	snprintf(transport->endpoint, sizeof(transport->endpoint), "%s", config->endpoint);
	snprintf(transport->token, sizeof(transport->token), "%s", config->api_token);
	snprintf(transport->agent_id, sizeof(transport->agent_id), "%s", config->agent_id);
	snprintf(transport->host_name, sizeof(transport->host_name), "%s", config->host_name);
	pthread_mutex_init(&transport->lock, NULL);
	pthread_cond_init(&transport->has_items, NULL);
	if (pthread_create(&transport->worker, NULL, transport_worker, transport) != 0) {
		free(transport);
		return NULL;
	}
	return transport;
}

bool show_me_transport_submit(struct show_me_transport *transport, const struct show_me_tcp_event *event)
{
	bool accepted = false;
	pthread_mutex_lock(&transport->lock);
	if (transport->count < SHOW_ME_QUEUE_CAPACITY) {
		transport->queue[transport->tail] = *event;
		transport->tail = (transport->tail + 1) % SHOW_ME_QUEUE_CAPACITY;
		transport->count++;
		accepted = true;
		pthread_cond_signal(&transport->has_items);
	} else {
		transport->dropped++;
	}
	pthread_mutex_unlock(&transport->lock);
	return accepted;
}

size_t show_me_transport_dropped(const struct show_me_transport *transport) { return transport->dropped; }

void show_me_transport_stop(struct show_me_transport *transport)
{
	if (transport == NULL) return;
	pthread_mutex_lock(&transport->lock);
	transport->stopping = true;
	pthread_cond_signal(&transport->has_items);
	pthread_mutex_unlock(&transport->lock);
	pthread_join(transport->worker, NULL);
	pthread_cond_destroy(&transport->has_items);
	pthread_mutex_destroy(&transport->lock);
	free(transport);
	curl_global_cleanup();
}
