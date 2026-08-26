# show_me

여러 Linux Agent가 중앙 서버로 telemetry를 전송하는 관측 플랫폼의 첫 단계입니다. 현재는 수집 경로의 부하와 저장 구조를 검증하기 위한 최소 구성만 포함합니다.

```
Go simulator ──HTTP + X-API-Token──> Spring Boot ingestion API ──> MySQL 8
```

Kafka, Redis, eBPF, Kubernetes, Prometheus/Grafana는 실제 부하·조회 요구가 확인되는 시점에 순서대로 추가합니다. 현재 수집과 처리가 아직 결합돼 있으므로, burst에서 처리 지연이 API 지연으로 이어진다는 근거가 생기면 Kafka를 도입합니다.

## Run

Docker Desktop을 실행한 뒤 다음을 실행합니다.

```bash
INGEST_API_TOKEN=change-me docker compose up --build
```

MySQL은 호스트의 기존 DB와 충돌하지 않도록 기본 `3307` 포트에 노출됩니다 (`MYSQL_PORT`로 변경 가능).

Simulator는 기본으로 3개 agent가 1초마다 `host.metrics` 이벤트를 전송합니다. 중단은 `Ctrl+C`입니다. 데이터베이스만 중지하면서 보존하려면 `docker compose down`; 볼륨까지 제거하려면 `docker compose down -v`를 사용합니다.

로컬 simulator 실행 예시:

```bash
cd simulator
go run . -agents 10 -interval 250ms -token change-me
```

## API

`POST /api/v1/events`는 `X-API-Token` 헤더가 필요하며, 성공하면 `202 Accepted`를 반환합니다.

```json
{
  "agentId": "host-a",
  "eventType": "host.metrics",
  "occurredAt": "2026-08-27T00:00:00Z",
  "payload": { "cpuUsagePercent": 42.5 }
}
```

원시 이벤트는 `received_at` 기준 7일 동안 보관됩니다. 매일 UTC 03:15에 정리되며, `app.retention.raw-events`로 변경할 수 있습니다.

## CI

GitHub Actions는 Spring 테스트·JAR 빌드와 backend/simulator Docker 이미지 빌드를 검사합니다. 이미지 푸시와 CD는 아직 포함하지 않습니다.
