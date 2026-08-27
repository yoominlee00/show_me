# show_me — Agent Telemetry Ingestion Platform

여러 Linux 서버의 Agent가 보낸 telemetry를 중앙에서 수집하고, 부하·조회 요구가 커지는 시점에 메시지 처리와 캐시를 단계적으로 분리하는 관측 플랫폼입니다.

현재 목표는 **분산 Agent → ingestion API → 원시 이벤트 저장** 경로를 실제로 실행하고 부하를 측정하는 것입니다. 처음부터 Kafka, Redis, Kubernetes를 넣지 않습니다. 각 기술은 문제가 관측됐을 때만 도입합니다.

## Current status

| 영역 | 현재 구현 | 다음 도입 조건 |
| --- | --- | --- |
| Ingestion | Kotlin + Spring Boot MVC HTTP API | 동시 연결·I/O 요구가 MVC 한계를 보일 때 WebFlux 검토 |
| Storage | MySQL 8.4, Flyway, raw event 7일 보관 | 집계·장기 보관 요구가 생길 때 테이블 분리 |
| Agent | Go simulator | Linux 수집 요구가 확정되면 eBPF + C/libbpf Agent |
| Messaging | 미도입 | burst에서 processing이 API latency를 악화시킬 때 Kafka |
| Cache | 미도입 | latest state/baseline의 MySQL 조회 비용이 커질 때 Redis |
| Infra | Docker Compose | 멀티 노드 운영 요구가 생길 때 Kubernetes + DaemonSet |
| CI | GitHub Actions | 배포 대상이 정해지면 CD 추가 |

## Architecture

```text
┌────────────────────────────────────────────────────────────────┐
│ Go simulator                                                     │
│  sim-agent-001..N ── POST /api/v1/events + X-API-Token          │
└─────────────────────────────┬──────────────────────────────────┘
                              │ HTTP / JSON
                              ▼
┌────────────────────────────────────────────────────────────────┐
│ Spring Boot ingestion API                                        │
│  token validation → request validation → raw event persistence  │
└─────────────────────────────┬──────────────────────────────────┘
                              ▼
┌────────────────────────────────────────────────────────────────┐
│ MySQL 8.4                                                        │
│  raw_events (received_at 기준 7일 retention)                     │
└────────────────────────────────────────────────────────────────┘
```

자세한 기술 선택과 확장 기준은 [architecture.md](docs/architecture.md)를 참고하세요.

## Quick start

### Prerequisites

- Docker Desktop 또는 Docker Engine + Compose v2
- 선택 사항: Go 1.23 이상 (host에서 simulator를 실행할 경우)

### Start the stack

```bash
INGEST_API_TOKEN=change-me docker compose up --build
```

백그라운드 실행은 다음과 같습니다.

```bash
INGEST_API_TOKEN=change-me docker compose up -d --build
docker compose ps
docker compose logs -f backend simulator
```

| 서비스 | 호스트 주소 | 설명 |
| --- | --- | --- |
| Backend | `http://localhost:8080` | telemetry ingestion API |
| MySQL | `localhost:3307` | 기본 호스트 포트, 컨테이너 내부 포트는 3306 |
| Simulator | Compose 내부 | 기본 3 agents, agent당 초당 1 event |

`3307`은 기존 로컬 MySQL과의 충돌을 피하기 위한 기본값입니다. 필요하면 `MYSQL_PORT=13306 docker compose up`처럼 바꿀 수 있습니다.

### Stop the stack

```bash
docker compose down
```

위 명령은 MySQL volume을 보존합니다. 테스트 데이터를 포함해 완전히 초기화하려면 다음을 사용합니다.

```bash
docker compose down -v
```

## Ingestion API

### `POST /api/v1/events`

모든 수집 요청에는 `X-API-Token` 헤더가 필요합니다. 유효한 요청은 `202 Accepted`를 반환합니다.

```bash
curl --request POST http://localhost:8080/api/v1/events \
  --header 'Content-Type: application/json' \
  --header 'X-API-Token: change-me' \
  --data '{
    "agentId": "host-a",
    "eventType": "host.metrics",
    "occurredAt": "2026-08-27T00:00:00Z",
    "payload": {
      "cpuUsagePercent": 42.5,
      "memoryUsagePercent": 68.1,
      "loadAverage1m": 1.23
    }
  }'
```

```json
{
  "id": 1,
  "receivedAt": "2026-08-27T00:00:00.000000Z"
}
```

| 조건 | 결과 |
| --- | --- |
| `X-API-Token` 누락 또는 불일치 | `401 Unauthorized` |
| 필수 필드 누락·잘못된 형식 | `400 Bad Request` |
| 정상 요청 | `202 Accepted` |

### Event shape

| 필드 | 타입 | 설명 |
| --- | --- | --- |
| `agentId` | string, 최대 128자 | event를 보낸 Agent 식별자 |
| `eventType` | string, 최대 64자 | 예: `host.metrics` |
| `occurredAt` | ISO-8601 timestamp | Agent가 이벤트를 관측한 시각 |
| `payload` | JSON object | event type별 세부 데이터 |

원시 이벤트는 서버 수신 시각(`received_at`) 기준으로 7일 보관합니다. cleanup job은 매일 UTC 03:15에 실행됩니다.

## Run the simulator locally

Compose 외부에서 더 많은 Agent를 흉내 내려면 다음을 실행합니다.

```bash
cd simulator
INGEST_API_TOKEN=change-me go run . \
  -endpoint http://localhost:8080/api/v1/events \
  -agents 10 \
  -interval 250ms
```

이 예시는 초당 약 40 events를 만듭니다. 부하 실험 방법과 기록 양식은 [benchmark.md](docs/benchmark.md)에 있습니다.

## Development

### Test

Gradle이 설치돼 있다면 다음을 실행합니다.

```bash
gradle :backend:test
```

Gradle을 host에 설치하지 않았다면 Docker로 실행할 수 있습니다.

```bash
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace \
  gradle:8.10-jdk21 \
  gradle :backend:test --no-daemon
```

### Project layout

```text
.
├── backend/                 # Spring Boot API, Flyway migration, tests
├── simulator/               # Go telemetry generator
├── docs/                    # architecture, benchmark and roadmap
├── compose.yaml             # local three-service stack
└── .github/workflows/ci.yml # test and image-build CI
```

## CI

GitHub Actions는 `main` push와 pull request에서 다음을 실행합니다.

1. Spring backend test 및 JAR build
2. backend Docker image build
3. Go simulator Docker image build

이미지 registry push와 CD는 배포 대상이 정해진 뒤 추가합니다.

## Roadmap

다음 작업은 기능을 무작정 추가하는 대신, 각 단계의 도입 근거를 남기는 방향으로 진행합니다.

1. 최근 event와 agent별 latest state 조회 API
2. Agent 수·전송 간격별 ingestion benchmark 기록
3. ingestion과 processing이 병목으로 결합되는지 측정
4. 결과를 근거로 Kafka 도입 여부 결정
5. latest state/baseline 조회 비용을 근거로 Redis 도입 여부 결정
6. Prometheus/Grafana, eBPF Agent, 단일 VM 배포
7. 실제 멀티 노드 운영 필요 시 Kubernetes + DaemonSet

세부 계획은 [roadmap.md](docs/roadmap.md)에 있습니다.
