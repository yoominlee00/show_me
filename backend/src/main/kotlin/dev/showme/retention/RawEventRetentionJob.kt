package dev.showme.retention

import dev.showme.ingest.IngestEventRepository
import org.springframework.beans.factory.annotation.Value
import org.springframework.scheduling.annotation.Scheduled
import org.springframework.stereotype.Component
import org.springframework.transaction.annotation.Transactional
import java.time.Clock
import java.time.Duration

@Component
class RawEventRetentionJob(
    private val repository: IngestEventRepository,
    @Value("\${app.retention.raw-events:7d}") private val retention: Duration,
    private val clock: Clock = Clock.systemUTC(),
) {
    @Transactional
    @Scheduled(cron = "\${app.retention.cleanup-cron:0 15 3 * * *}")
    fun cleanUp(): Int = repository.deleteOlderThan(clock.instant().minus(retention))
}

