#pragma once
#include "FreeRTOS.h"
#include <mutex>
#include <condition_variable>
#include <chrono>
struct TestSemaphore {
    std::mutex mutex;
    std::condition_variable ready;
    bool available;
    explicit TestSemaphore(bool available):available(available) {}
};
using SemaphoreHandle_t = TestSemaphore*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new TestSemaphore(true); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return new TestSemaphore(false); }
inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) {
    std::unique_lock<std::mutex> lock(s->mutex);
    if (ticks == portMAX_DELAY) s->ready.wait(lock, [&]{ return s->available; });
    else if (!s->ready.wait_for(lock, std::chrono::milliseconds(ticks), [&]{ return s->available; })) return pdFALSE;
    s->available = false;
    return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t s) {
    { std::lock_guard<std::mutex> lock(s->mutex); s->available = true; }
    s->ready.notify_one();
    return pdTRUE;
}
