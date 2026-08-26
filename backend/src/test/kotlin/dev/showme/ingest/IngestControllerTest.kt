package dev.showme.ingest

import com.fasterxml.jackson.databind.ObjectMapper
import org.junit.jupiter.api.Test
import org.mockito.kotlin.any
import org.mockito.kotlin.doAnswer
import org.mockito.kotlin.whenever
import org.springframework.beans.factory.annotation.Autowired
import org.springframework.boot.test.autoconfigure.web.servlet.WebMvcTest
import org.springframework.boot.test.mock.mockito.MockBean
import org.springframework.http.MediaType
import org.springframework.test.context.TestPropertySource
import org.springframework.test.web.servlet.MockMvc
import org.springframework.test.web.servlet.post

@WebMvcTest(IngestController::class)
@TestPropertySource(properties = ["app.ingest.api-token=test-token"])
class IngestControllerTest(
    @Autowired private val mockMvc: MockMvc,
    @Autowired private val objectMapper: ObjectMapper,
) {
    @MockBean private lateinit var repository: IngestEventRepository

    @Test
    fun `rejects a request without a token`() {
        mockMvc.post("/api/v1/events") { contentType = MediaType.APPLICATION_JSON }
            .andExpect { status { isUnauthorized() } }
    }

    @Test
    fun `accepts and stores a valid event`() {
        doAnswer { it.getArgument<IngestEvent>(0) }.whenever(repository).save(any())
        val body = """{"agentId":"agent-1","eventType":"cpu.sample","occurredAt":"2026-08-27T00:00:00Z","payload":{"usage":42.5}}"""
        mockMvc.post("/api/v1/events") {
            header("X-API-Token", "test-token")
            contentType = MediaType.APPLICATION_JSON
            content = body
        }.andExpect { status { isAccepted() } }
    }
}
