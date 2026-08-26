package dev.showme.ingest

import com.fasterxml.jackson.databind.JsonNode
import com.fasterxml.jackson.databind.ObjectMapper
import jakarta.validation.Valid
import jakarta.validation.constraints.NotBlank
import jakarta.validation.constraints.Size
import org.springframework.http.HttpStatus
import org.springframework.web.bind.annotation.PostMapping
import org.springframework.web.bind.annotation.RequestBody
import org.springframework.web.bind.annotation.RequestMapping
import org.springframework.web.bind.annotation.ResponseStatus
import org.springframework.web.bind.annotation.RestController
import java.time.Instant

data class IngestRequest(
    @field:NotBlank @field:Size(max = 128) val agentId: String,
    @field:NotBlank @field:Size(max = 64) val eventType: String,
    val occurredAt: Instant,
    val payload: JsonNode,
)

data class IngestResponse(val id: Long, val receivedAt: Instant)

@RestController
@RequestMapping("/api/v1/events")
class IngestController(
    private val repository: IngestEventRepository,
    private val objectMapper: ObjectMapper,
) {
    @PostMapping
    @ResponseStatus(HttpStatus.ACCEPTED)
    fun ingest(@Valid @RequestBody request: IngestRequest): IngestResponse {
        val saved = repository.save(
            IngestEvent(
                agentId = request.agentId,
                eventType = request.eventType,
                occurredAt = request.occurredAt,
                payload = objectMapper.writeValueAsString(request.payload),
            ),
        )
        return IngestResponse(saved.id, saved.receivedAt)
    }
}

