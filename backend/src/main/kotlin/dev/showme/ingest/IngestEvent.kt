package dev.showme.ingest

import jakarta.persistence.Column
import jakarta.persistence.Entity
import jakarta.persistence.GeneratedValue
import jakarta.persistence.GenerationType
import jakarta.persistence.Id
import jakarta.persistence.Table
import java.time.Instant

@Entity
@Table(name = "raw_events")
class IngestEvent(
    @Column(nullable = false, length = 128)
    val agentId: String,
    @Column(nullable = false, length = 64)
    val eventType: String,
    @Column(nullable = false)
    val occurredAt: Instant,
    @Column(nullable = false, columnDefinition = "json")
    val payload: String,
    @Column(nullable = false, updatable = false)
    val receivedAt: Instant = Instant.now(),
    @Id @GeneratedValue(strategy = GenerationType.IDENTITY)
    val id: Long = 0,
)

