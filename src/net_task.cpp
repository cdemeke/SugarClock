#include "net_task.h"
#include "http_client.h"
#include "weather_client.h"
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/semphr.h>

// TLS handshakes (mbedTLS) run on the calling task and need generous stack.
#define NET_TASK_STACK_BYTES 12288
// Above idle, below the WiFi/lwip system tasks.
#define NET_TASK_PRIORITY    1
// Arduino loopTask (rendering, buttons, buzzer) runs on core 1.
#define NET_TASK_CORE        0
#define NET_TASK_TICK_MS     100

static SemaphoreHandle_t network_gate = nullptr;

bool net_task_quiesce(unsigned long timeout_ms) {
    return network_gate && xSemaphoreTake(network_gate, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void net_task_resume() {
    xSemaphoreGive(network_gate);
}

static void net_task_fn(void*) {
    // Watch this task too: fetch code feeds the WDT between HTTP steps,
    // so only a genuinely hung network call trips it.
    esp_task_wdt_add(NULL);

    for (;;) {
        esp_task_wdt_reset();
        if (xSemaphoreTake(network_gate, pdMS_TO_TICKS(100)) == pdTRUE) {
            http_poll_tick();
            xSemaphoreGive(network_gate);
        }
        esp_task_wdt_reset();
        if (xSemaphoreTake(network_gate, pdMS_TO_TICKS(100)) == pdTRUE) {
            weather_poll_tick();
            xSemaphoreGive(network_gate);
        }
        vTaskDelay(pdMS_TO_TICKS(NET_TASK_TICK_MS));
    }
}

void net_task_start() {
    if (network_gate) return;
    network_gate = xSemaphoreCreateMutex();
    if (!network_gate) {
        Serial.println("[NET] Cannot allocate network gate");
        ESP.restart();
        return;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(
        net_task_fn, "net_task", NET_TASK_STACK_BYTES, NULL,
        NET_TASK_PRIORITY, NULL, NET_TASK_CORE);

    if (ok == pdPASS) {
        Serial.println("[NET] Network task started on core 0");
    } else {
        Serial.println("[NET] ERROR: failed to start network task");
        ESP.restart();
    }
}
