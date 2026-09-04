#include "net_task.h"
#include "http_client.h"
#include "weather_client.h"
#include <Arduino.h>
#include <esp_task_wdt.h>

// TLS handshakes (mbedTLS) run on the calling task and need generous stack.
#define NET_TASK_STACK_BYTES 12288
// Above idle, below the WiFi/lwip system tasks.
#define NET_TASK_PRIORITY    1
// Arduino loopTask (rendering, buttons, buzzer) runs on core 1.
#define NET_TASK_CORE        0
#define NET_TASK_TICK_MS     100

static void net_task_fn(void* arg) {
    // Watch this task too: fetch code feeds the WDT between HTTP steps,
    // so only a genuinely hung network call trips it.
    esp_task_wdt_add(NULL);

    for (;;) {
        esp_task_wdt_reset();
        http_poll_tick();
        weather_poll_tick();
        vTaskDelay(pdMS_TO_TICKS(NET_TASK_TICK_MS));
    }
}

void net_task_start() {
    BaseType_t ok = xTaskCreatePinnedToCore(
        net_task_fn, "net_task", NET_TASK_STACK_BYTES, NULL,
        NET_TASK_PRIORITY, NULL, NET_TASK_CORE);

    if (ok == pdPASS) {
        Serial.println("[NET] Network task started on core 0");
    } else {
        Serial.println("[NET] ERROR: failed to start network task");
    }
}
