package dev.showme.config

import jakarta.servlet.FilterChain
import jakarta.servlet.http.HttpServletRequest
import jakarta.servlet.http.HttpServletResponse
import org.springframework.beans.factory.annotation.Value
import org.springframework.http.MediaType
import org.springframework.stereotype.Component
import org.springframework.web.filter.OncePerRequestFilter
import java.security.MessageDigest

@Component
class ApiTokenFilter(
    @Value("\${app.ingest.api-token}") private val expectedToken: String,
) : OncePerRequestFilter() {
    override fun shouldNotFilter(request: HttpServletRequest): Boolean =
        !request.requestURI.startsWith("/api/v1/events")

    override fun doFilterInternal(
        request: HttpServletRequest,
        response: HttpServletResponse,
        filterChain: FilterChain,
    ) {
        val supplied = request.getHeader("X-API-Token")
        val valid = supplied != null && MessageDigest.isEqual(
            supplied.toByteArray(Charsets.UTF_8),
            expectedToken.toByteArray(Charsets.UTF_8),
        )
        if (!valid) {
            response.status = HttpServletResponse.SC_UNAUTHORIZED
            response.contentType = MediaType.APPLICATION_JSON_VALUE
            response.writer.write("{\"error\":\"invalid API token\"}")
            return
        }
        filterChain.doFilter(request, response)
    }
}

