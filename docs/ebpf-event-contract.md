# eBPF Agent event contract

## Envelope

Every Agent event uses the existing `POST /api/v1/events` request format.

```json
{
  "agentId": "linux:7c4d8f2a",
  "eventType": "network.tcp.connection.v1",
  "occurredAt": "2026-08-27T13:00:04.123456Z",
  "payload": {}
}
```

| Field | Rule |
| --- | --- |
| `agentId` | stable configured ID or one-way hash of machine ID; no raw machine ID |
| `eventType` | version suffix required; additive changes stay in the same version, breaking changes create a new version |
| `occurredAt` | agent UTC timestamp captured as close as possible to kernel event delivery |
| `payload` | metadata only; no packet payload, argv, environment, authorization header or credentials |

The first version sends one event per HTTP request for simplicity. User-space batching may be introduced after the backend adds a batch endpoint; it must not silently change this API contract.

## Common payload fields

| Field | Type | Description |
| --- | --- | --- |
| `hostName` | string | configured/sanitized host name |
| `pid` | integer | process ID at observed event time |
| `tgid` | integer | thread-group/process ID |
| `processName` | string | kernel `comm`, truncated by kernel limit |
| `processKnown` | boolean | whether socket-to-process correlation succeeded |
| `protocol` | string | initially always `tcp` |
| `addressFamily` | string | `ipv4` or `ipv6` |
| `localAddress` | string | local IP; may be disabled by configuration |
| `localPort` | integer | local TCP port |
| `remoteAddress` | string | remote IP, no DNS reverse lookup in hot path |
| `remotePort` | integer | remote TCP port |

## `network.tcp.connection.v1`

Emitted for observed `established` and `closed` connection transitions.

```json
{
  "agentId": "linux:7c4d8f2a",
  "eventType": "network.tcp.connection.v1",
  "occurredAt": "2026-08-27T13:00:04.123456Z",
  "payload": {
    "hostName": "api-01",
    "pid": 1824,
    "tgid": 1824,
    "processName": "payment-api",
    "processKnown": true,
    "protocol": "tcp",
    "addressFamily": "ipv4",
    "localAddress": "10.0.0.11",
    "localPort": 49820,
    "remoteAddress": "10.0.0.20",
    "remotePort": 3306,
    "action": "established",
    "direction": "outbound"
  }
}
```

| Field | Values / rule |
| --- | --- |
| `action` | `established` or `closed` |
| `direction` | `outbound`, `inbound`, or `unknown`; only set when the Agent can establish it reliably |
| `processKnown` | `false` requires `pid`, `tgid`, `processName` to be omitted or null, never guessed |

## `network.tcp.retransmit.v1`

Emitted as an aggregate over a short configurable interval, not necessarily once for every kernel retransmission.

```json
{
  "agentId": "linux:7c4d8f2a",
  "eventType": "network.tcp.retransmit.v1",
  "occurredAt": "2026-08-27T13:00:14.000000Z",
  "payload": {
    "hostName": "api-01",
    "pid": 1824,
    "tgid": 1824,
    "processName": "payment-api",
    "processKnown": true,
    "protocol": "tcp",
    "addressFamily": "ipv4",
    "remoteAddress": "10.0.0.20",
    "remotePort": 3306,
    "windowSeconds": 10,
    "retransmitCount": 18
  }
}
```

| Field | Rule |
| --- | --- |
| `windowSeconds` | aggregation window used by the Agent |
| `retransmitCount` | count observed during this window for the correlation key |
| local tuple | optional for this event; omit when it risks high cardinality without diagnostic value |

## `process.exec.v1` (deferred)

This event is not part of the first Agent demo. If added, it contains PID/TGID, `processName`, executable basename and a timestamp. It must not include command-line arguments or environment variables.

## Cardinality rules

- Do not use `pid`, client local port, full connection tuple, or raw IP as a default dashboard label.
- Store raw values in event payload for investigation; aggregate Grafana/Prometheus views by host, process name, remote port and an approved destination label.
- Apply destination allow/deny or hash policy before a public/demo deployment if real addresses are sensitive.

