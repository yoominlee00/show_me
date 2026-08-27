# eBPF TCP Agent implementation plan

## 1. Objective

Linux host 안에서 발생하는 TCP 연결 품질 문제를 **프로세스와 목적지 기준으로** 수집한다. Agent는 이벤트를 중앙 ingestion API에 보내며, 이 단계에서는 탐지·자동 대응을 하지 않는다.

### The question this Agent must answer

> 어느 host의 어느 프로세스가, 어느 목적지와 TCP 통신할 때, 연결 실패·종료·재전송을 경험했는가?

예: `api-01`의 `payment-api`(PID 1824)가 `db.internal:3306` 연결에서 재전송을 반복한다.

## 2. Non-goals

- 모든 syscall, 파일 접근, 패킷 payload 수집
- 사용자 요청 본문·비밀번호·토큰 수집
- 패킷을 차단하거나 socket 동작을 변경하는 보안 Agent
- 첫 버전의 TLS 복호화·HTTP 요청 추적
- Kafka, Redis, Kubernetes 동시 도입

첫 Agent는 관찰만 하며, 데이터 최소화 원칙을 따른다.

## 3. Scope and event priority

| Priority | Event | 판단 가능한 것 | 이유 |
| --- | --- | --- | --- |
| P0 | TCP connection established / closed | 프로세스별 목적지 의존성, 연결 수명 | connection metadata의 기준점 |
| P0 | TCP retransmit | 프로세스↔목적지별 네트워크 품질 저하 | eBPF 사용 이유가 가장 분명함 |
| P1 | TCP reset / connection failure | failure가 발생한 host·process·destination | 장애 구간 축소 |
| P2 | process exec / exit | 재시작과 connection burst의 상관관계 | TCP 이상을 해석하는 보조 signal |

`host.metrics`의 CPU, memory, load average는 eBPF가 아니라 `/proc` 또는 기존 simulator와 같은 user-space 수집으로 유지한다.

## 4. Runtime architecture

```text
Linux kernel
 ├─ sock:inet_sock_set_state      ─┐
 ├─ tcp:tcp_retransmit_skb         ├─> eBPF programs
 └─ sched:sched_process_exec      ─┘       │
                                            │ ring buffer
                                            ▼
                                  C/libbpf Agent process
                                  - host/process enrichment
                                  - aggregation and bounded queue
                                  - JSON batch encoding
                                  - retry with backoff
                                            │ HTTPS/HTTP + X-API-Token
                                            ▼
                                  Spring ingestion API → MySQL raw_events
```

### eBPF side

1. Tracepoint program receives a kernel event.
2. It reads only fixed metadata: timestamp, PID/TGID, process name, socket tuple/state.
3. It associates socket metadata and process identity in BPF maps when correlation is required.
4. It emits a fixed-size event through a BPF ring buffer.
5. It never performs network I/O, JSON encoding, retry, or unbounded aggregation.

### User-space Agent side

1. Loads CO-RE BPF object through libbpf.
2. Polls the ring buffer and maps raw fields to the external JSON contract.
3. Aggregates retransmission events by `(host, pid, destination)` in a short window.
4. Sends a bounded batch to the existing ingestion endpoint.
5. On API failure, retries with exponential backoff and jitter. Its queue has a hard size limit; overflow is counted and reported locally rather than consuming host memory indefinitely.

## 5. Kernel hooks and correlation design

Hook names and available tracepoint fields must be verified on the target Ubuntu kernel with `bpftool`; the table is the intended design, not an assumption that every kernel exposes identical fields.

| Signal | Primary hook | Fields to retain | Correlation |
| --- | --- | --- | --- |
| state transition | `sock:inet_sock_set_state` | old/new state, local/remote address and port, socket reference | socket metadata map |
| retransmission | `tcp:tcp_retransmit_skb` | socket reference, TCP state, timestamp | lookup socket metadata map |
| process start | `sched:sched_process_exec` | PID/TGID, `comm`, executable filename | process metadata map / direct event |

### Connection lifecycle

- On initial outbound connection state, store `pid`, `tgid`, `comm`, tuple and first-seen timestamp against a socket key.
- On `ESTABLISHED`, emit `network.tcp.connection` with `action=established`.
- On `CLOSE`, emit the close event and remove the socket metadata map entry.
- On retransmit, read the associated metadata and increment a short-window counter. Emit an aggregate event when the window closes or a threshold is crossed.
- If correlation data does not exist, emit an explicitly marked `processKnown=false` event or drop it and increment an Agent diagnostic counter. Never invent a process identity.

The concrete map key (socket cookie where available, otherwise a carefully managed socket reference) is an implementation decision validated against the target kernel. Pointer values must never leave the host as telemetry.

## 6. External event contract

The backend already accepts arbitrary JSON payloads under an `eventType`; the Agent must follow the versioned contract in [ebpf-event-contract.md](ebpf-event-contract.md).

Initial event types:

```text
network.tcp.connection.v1
network.tcp.retransmit.v1
process.exec.v1                 # P2, not required for the first demo
```

The Agent uses a stable `agentId` such as `linux:<machine-id-hash>` or an explicitly configured ID. Do not send raw machine IDs, packet payloads, command arguments, or environment variables.

## 7. Repository layout to add

```text
agent/
├── Makefile                    # build BPF object and user-space binary
├── README.md                    # Linux-only build/run instructions
├── bpf/
│   ├── tcp_events.bpf.c         # tracepoint programs and BPF maps
│   └── vmlinux.h                # generated locally; do not hand-edit
├── include/
│   └── event.h                  # shared fixed-size BPF/user-space struct
├── src/
│   ├── main.c                   # libbpf lifecycle and ring-buffer polling
│   ├── transport.c              # HTTP batching, retry, queue limit
│   └── config.c                 # endpoint, token, agent ID, filters
└── tests/
    ├── integration/             # Linux VM smoke scenarios
    └── fixtures/
```

`vmlinux.h` is generated from the test host BTF with `bpftool btf dump file /sys/kernel/btf/vmlinux format c`; the build script should regenerate it rather than relying on an OS-specific file in source control.

## 8. Implementation sequence

### Milestone A — Linux lab and build skeleton

**Deliverable:** Ubuntu VM에서 빈 libbpf Agent가 BPF object를 load/unload한다.

1. Create an Ubuntu 22.04 or 24.04 Linux VM.
2. Verify BTF: `/sys/kernel/btf/vmlinux` exists.
3. Install `clang`, `llvm`, `libbpf-dev`, `libelf-dev`, `zlib1g-dev`, `make`, `linux-tools-common`, and the matching `linux-tools-$(uname -r)` package that provides `bpftool`.
4. Add `agent/` Makefile and generate skeleton/header artifacts.
5. Implement clean SIGINT/SIGTERM unload.

**Exit criteria:** `make` succeeds; Agent starts and exits without leaving attached programs/maps.

### Milestone B — connection lifecycle

**Deliverable:** Agent prints normalized connect/close events from the ring buffer.

1. Add `inet_sock_set_state` tracepoint program.
2. Define the fixed-size shared raw event structure in `include/event.h`.
3. Apply `pid` allow-list and remote-port filters in BPF maps/config.
4. Emit established/closed event logs from user space.

**Exit criteria:** a controlled `curl` or test client produces one identifiable connection lifecycle with no packet payload captured.

### Milestone C — retransmission correlation

**Deliverable:** retransmit event points to its process and destination.

1. Add `tcp_retransmit_skb` tracepoint.
2. Correlate retransmissions with the connection metadata map.
3. Aggregate repeated retransmits for a configurable short window (initially 10 seconds).
4. Report correlation misses and dropped ring-buffer records as Agent diagnostics.

**Exit criteria:** controlled loss using `tc netem` produces a `network.tcp.retransmit.v1` event with destination and known process when available.

### Milestone D — ingestion transport

**Deliverable:** Agent data is stored through the existing API.

1. Add endpoint, token, agent ID, batch size and flush interval config.
2. Implement a bounded in-memory queue and HTTP POST transport.
3. Send `X-API-Token`; use request timeout and exponential backoff.
4. Add an integration script that confirms stored rows in MySQL.

**Exit criteria:** stopping the backend temporarily does not crash the Agent; after recovery, queued events are sent up to the configured queue limit.

### Milestone E — demo and evidence

**Deliverable:** repeatable scenario and recorded benchmark.

1. Normal connection scenario.
2. Network-loss/retransmit scenario.
3. Process restart scenario (if P2 implemented).
4. Record event count, Agent CPU/memory, drop count and ingestion failures.
5. Add screenshots or JSON samples only after sanitizing host/IP information.

**Exit criteria:** README can reproduce the demo from a clean Linux VM and explains one observed diagnosis end-to-end.

## 9. Linux lab requirements

| Requirement | Baseline | Why |
| --- | --- | --- |
| OS | Ubuntu 22.04/24.04 VM | reproducible Linux target |
| Kernel | BTF enabled, ring buffer capable (kernel 5.8+ baseline) | CO-RE and ring buffer |
| Build | clang/LLVM, libbpf, libelf, zlib, bpftool | compile and load BPF object |
| Runtime privilege | `CAP_BPF` + `CAP_PERFMON` where supported; older kernels may need `CAP_SYS_ADMIN` | attach tracing programs |
| Network test | `tc netem` on an isolated test interface/namespace | deterministic loss and retransmit |

macOS and Docker Desktop are not the validation environment for this Agent. They can run the central backend, but eBPF integration tests require a real Linux kernel under the team’s control.

## 10. Safety, privacy and performance controls

- Default to metadata only; never capture packet payload or process arguments.
- Allow-list event types and optional PID/cgroup filters before broad host collection.
- Cap BPF maps, ring-buffer size, user-space queue size and batch size.
- Count drops at every boundary: BPF map/ring-buffer, user-space queue, HTTP transport.
- Use a dedicated least-privilege service account/capabilities where the target kernel supports it.
- Keep raw events for the existing 7-day policy; avoid putting secrets in any payload.
- Start in a disposable VM or non-production host. Do not first test on a shared production server.

## 11. Definition of done for the first eBPF release

- [ ] CO-RE C/libbpf Agent builds on documented Ubuntu version.
- [ ] Agent cleanly attaches/detaches tracepoint programs.
- [ ] It emits TCP established, closed and retransmit events with documented fields.
- [ ] A retransmit demo attributes an event to a destination and, where kernel correlation allows, a process.
- [ ] Events reach the existing API with API token authentication and appear in MySQL.
- [ ] Queue overflow and correlation misses are observable in Agent logs/counters.
- [ ] No payload, command-line argument, environment variable or raw machine ID is transmitted.
- [ ] Linux setup, permissions, run command, cleanup and demo evidence are documented.
