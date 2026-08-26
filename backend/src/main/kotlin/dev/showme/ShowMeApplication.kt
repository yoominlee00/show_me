package dev.showme

import org.springframework.boot.autoconfigure.SpringBootApplication
import org.springframework.boot.runApplication
import org.springframework.scheduling.annotation.EnableScheduling

@EnableScheduling
@SpringBootApplication
class ShowMeApplication

fun main(args: Array<String>) {
    runApplication<ShowMeApplication>(*args)
}

