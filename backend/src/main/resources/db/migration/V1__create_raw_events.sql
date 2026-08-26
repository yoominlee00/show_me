CREATE TABLE raw_events (
    id BIGINT NOT NULL AUTO_INCREMENT,
    agent_id VARCHAR(128) NOT NULL,
    event_type VARCHAR(64) NOT NULL,
    occurred_at TIMESTAMP(6) NOT NULL,
    payload JSON NOT NULL,
    received_at TIMESTAMP(6) NOT NULL,
    PRIMARY KEY (id),
    INDEX idx_raw_events_received_at (received_at),
    INDEX idx_raw_events_agent_occurred (agent_id, occurred_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

