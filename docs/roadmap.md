# Roadmap

## Phase 1 — reliable ingestion (current)

- [x] Kotlin/Spring Boot ingestion API
- [x] API token verification
- [x] MySQL + Flyway migration
- [x] raw event 7-day retention
- [x] Go multi-agent simulator
- [x] Docker Compose and CI
- [ ] request/response observability and latency measurement
- [ ] benchmark results committed to this repository

## Phase 2 — queryable state

Goal: raw event를 직접 읽지 않고 운영자가 최근 host 상태를 확인한다.

- [ ] recent event query API
- [ ] agent별 latest state projection
- [ ] pagination, time range and agent filtering
- [ ] latest-state query performance measurement

## Phase 3 — decoupled processing

Goal: burst와 분석 처리 비용이 ingestion availability에 영향을 주지 않게 한다.

- [ ] benchmark에서 decoupling 필요성 확인
- [ ] Kafka topic 및 producer 추가
- [ ] idempotent consumer와 retry/DLQ 설계
- [ ] raw history와 aggregate projection 분리

## Phase 4 — operations

Goal: 운영자가 시스템과 Agent 상태를 관찰할 수 있다.

- [ ] application metrics 정의
- [ ] Prometheus scraping
- [ ] Grafana dashboard
- [ ] 단일 Linux VM에 Docker Compose 배포
- [ ] 배포 runbook 및 장애 복구 절차 문서화

## Phase 5 — real Agent and orchestration

Goal: simulator를 실제 Linux signal collector로 대체하고 여러 node로 확장한다.

- [ ] eBPF + C/libbpf Agent prototype
- [ ] compatibility and privilege model
- [ ] Kubernetes DaemonSet deployment
- [ ] Kubernetes에서 실제 운영 요구가 생길 때 GitOps/mesh 검토

