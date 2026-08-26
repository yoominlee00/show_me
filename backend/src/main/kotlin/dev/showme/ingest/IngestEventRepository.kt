package dev.showme.ingest

import org.springframework.data.jpa.repository.JpaRepository
import org.springframework.data.jpa.repository.Modifying
import org.springframework.data.jpa.repository.Query
import java.time.Instant

interface IngestEventRepository : JpaRepository<IngestEvent, Long> {
    @Modifying
    @Query("delete from IngestEvent e where e.receivedAt < :cutoff")
    fun deleteOlderThan(cutoff: Instant): Int
}

