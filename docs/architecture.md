# Architecture decision record

## Problem

Linux 서버의 Agent가 지속적으로 host telemetry를 보낼 때, 수집 API가 데이터를 안정적으로 저장하고 이후 처리 확장을 수용해야 한다. 이 프로젝트는 처음부터 모든 운영 기술을 조합하는 대신, 병목을 측정한 뒤 필요한 컴포넌트를 추가한다.

## Current decisions

### Kotlin + Spring Boot MVC

현재 API는 request validation, token verification, MySQL persistence라는 짧은 처리만 수행한다. Spring MVC는 이 단계에서 구현·디버깅 비용이 낮고 수집 경로를 빠르게 검증하기에 충분하다.

WebFlux는 JD 키워드 때문에 선행 도입하지 않는다. 높은 동시 연결과 외부 I/O 대기가 실제로 병목인지, 또는 ingestion server가 blocking 처리 때문에 포화되는지를 부하 측정으로 확인한 뒤 검토한다.

### MySQL 8.4

raw event의 append 중심 저장소로 MySQL을 사용한다. Flyway migration으로 테이블을 관리하며, `received_at` 및 `(agent_id, occurred_at)` 인덱스를 둔다. raw event는 7일만 보관한다.

### Go simulator

실제 eBPF Agent 전에 여러 Agent가 지속적으로 데이터를 보내는 환경을 재현한다. Agent 수와 전송 간격을 조절해 ingestion API의 성능 한계를 찾는 용도다.

### API token authentication

`X-API-Token`은 외부 Agent가 중앙 API에 접근하는 최소한의 경계다. 토큰은 `INGEST_API_TOKEN` 환경변수로 주입하며 repository에 기록하지 않는다. PoC 범위를 넘어가면 Agent별 credential rotation, mTLS 또는 workload identity를 검토한다.

## Intentional exclusions

| 기술 | 지금 넣지 않는 이유 | 도입 신호 |
| --- | --- | --- |
| Kafka | API와 DB 저장만 있는 단계에서는 운영 복잡도만 증가 | burst에서 processing/DB 지연이 ingestion latency와 실패율을 높임 |
| Redis | 아직 최신 상태·baseline 조회가 없음 | MySQL 기반 latest-state/baseline 조회가 hot path가 됨 |
| Schema Registry | producer/consumer가 아직 분리되지 않음 | event type 증가, 독립 consumer, 호환성 관리 필요 |
| eBPF | collector 요구가 아직 확정되지 않음 | 실제 Linux host에서 kernel-level signal 수집 필요 |
| Prometheus/Grafana | 앱·시스템 메트릭을 먼저 정의해야 함 | benchmark와 운영 지표를 시각화할 필요 |
| Kubernetes | 단일 Compose 배포로 현재 실험 범위를 충족 | 여러 node, rollout, Agent DaemonSet 요구 |
| Istio / Argo CD | 배포·트래픽 운영 문제가 아직 없음 | Kubernetes 운영에서 반복되는 배포/네트워크 요구 |

## Target evolution

```text
Phase 1  Go simulator → Spring ingestion → MySQL
Phase 2  Agent/eBPF → Spring ingestion → Kafka → consumer → MySQL
Phase 3  Kafka consumer → Redis latest state + MySQL history
Phase 4  Prometheus/Grafana → Docker VM deployment → Kubernetes DaemonSet
```

각 화살표는 기능 목록이 아니라, 이전 단계의 관측 결과로 정당화돼야 한다.

