# Ingestion benchmark guide

## Purpose

이 benchmark는 “Kafka를 사용했다”는 사실을 만들기 위한 것이 아니라, 현재 ingestion API가 어느 지점에서 느려지거나 실패하는지 확인하기 위한 것이다. 결과가 안정적이면 Kafka를 추가하지 않는 것도 올바른 결론이다.

## Before running

1. stack을 시작한다: `INGEST_API_TOKEN=benchmark-token docker compose up -d --build`
2. backend와 MySQL이 healthy인지 확인한다: `docker compose ps`
3. 아래 결과 표에 실행 환경과 설정값을 기록한다.

## Scenarios

| Scenario | Agents | Interval | Expected rate |
| --- | ---: | ---: | ---: |
| baseline | 3 | 1s | 약 3 events/s |
| moderate | 10 | 250ms | 약 40 events/s |
| burst | 50 | 100ms | 약 500 events/s |
| stress | 100 | 100ms | 약 1,000 events/s |

Compose simulator 대신 host에서 실행한다.

```bash
cd simulator
INGEST_API_TOKEN=benchmark-token go run . \
  -endpoint http://localhost:8080/api/v1/events \
  -agents 50 \
  -interval 100ms
```

## Measure

- simulator log의 request failure / non-202 response 수
- backend container CPU, memory: `docker stats`
- MySQL에 저장된 event 수
- 요청 latency(p50/p95/p99): 다음 단계에서 load generator 또는 app metric으로 추가

```bash
docker compose exec -T mysql \
  mysql -ushowme -pshowme -D showme \
  -e 'SELECT COUNT(*) AS raw_event_count FROM raw_events;'
```

## Record template

| Date | Commit | Agents | Interval | Duration | Expected events | Stored events | Failures | Notes |
| --- | --- | ---: | --- | --- | ---: | ---: | ---: | --- |
| | | | | | | | | |

## Decision rule

Kafka 후보가 되는 신호는 다음과 같다.

- burst 때 DB write 또는 downstream processing이 API latency/오류율을 명확히 높인다.
- ingestion은 빠르게 acknowledge해야 하지만 분석·집계 작업이 더 오래 걸린다.
- replay 가능한 event stream과 독립 consumer가 필요하다.

단순히 events/s가 증가했다는 사실만으로 Kafka를 도입하지 않는다.

