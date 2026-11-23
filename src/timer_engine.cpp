#include "timer_engine.h"
#include "config_manager.h"
#include "buzzer.h"
#include <Arduino.h>

// Pomodoro timer state
static TimerState timer_state = TIMER_IDLE;
static TimerState timer_paused_state = TIMER_RUNNING;
static unsigned long timer_start_ms = 0;
static unsigned long timer_paused_remaining_ms = 0;
static int timer_current_session = 1;
static unsigned long timer_duration_ms = 0;

// Stopwatch state
static StopwatchState sw_state = SW_IDLE;
static unsigned long sw_start_ms = 0;
static unsigned long sw_paused_elapsed_ms = 0;

// --- Pomodoro Timer ---

void timer_init() {
    timer_state = TIMER_IDLE;
    sw_state = SW_IDLE;
}

void timer_loop() {
    if (timer_state == TIMER_RUNNING || timer_state == TIMER_BREAK || timer_state == TIMER_LONG_BREAK) {
        unsigned long elapsed = millis() - timer_start_ms;
        if (elapsed >= (unsigned long)timer_duration_ms) {
            // Timer finished
            AppConfig& cfg = config_get();
            if (cfg.timer_buzzer) {
                buzzer_beep(3, 2000, 200);
            }

            if (timer_state == TIMER_RUNNING) {
                // Work session done
                if (timer_current_session >= cfg.timer_sessions) {
                    // All sessions done - long break
                    timer_state = TIMER_LONG_BREAK;
                    timer_duration_ms = (unsigned long)cfg.timer_long_break_min * 60000UL;
                    timer_start_ms = millis();
                    timer_current_session = 0;
                    Serial.println("[TIMER] Long break started");
                } else {
                    // Start short break
                    timer_state = TIMER_BREAK;
                    timer_duration_ms = (unsigned long)cfg.timer_break_min * 60000UL;
                    timer_start_ms = millis();
                    Serial.printf("[TIMER] Break started (session %d/%d done)\n",
                                  timer_current_session, cfg.timer_sessions);
                }
            } else {
                // Break done - start next work session
                timer_current_session++;
                timer_state = TIMER_RUNNING;
                timer_duration_ms = (unsigned long)cfg.timer_work_min * 60000UL;
                timer_start_ms = millis();
                Serial.printf("[TIMER] Work session %d started\n", timer_current_session);
            }
        }
    }
}

void timer_toggle_start_pause() {
    AppConfig& cfg = config_get();

    switch (timer_state) {
        case TIMER_IDLE:
        case TIMER_DONE:
            // Start new work session
            timer_state = TIMER_RUNNING;
            timer_duration_ms = (unsigned long)cfg.timer_work_min * 60000UL;
            timer_start_ms = millis();
            timer_current_session = 1;
            Serial.println("[TIMER] Started");
            break;

        case TIMER_RUNNING:
        case TIMER_BREAK:
        case TIMER_LONG_BREAK: {
            // Pause and remember which phase was active
            unsigned long elapsed = millis() - timer_start_ms;
            timer_paused_remaining_ms = (timer_duration_ms > elapsed) ?
                                        (timer_duration_ms - elapsed) : 0;
            timer_paused_state = timer_state;
            timer_state = TIMER_PAUSED;
            Serial.println("[TIMER] Paused");
            break;
        }

        case TIMER_PAUSED:
            // Resume the phase that was paused
            timer_state = timer_paused_state;
            timer_duration_ms = timer_paused_remaining_ms;
            timer_start_ms = millis();
            Serial.println("[TIMER] Resumed");
            break;
    }
}

void timer_reset() {
    AppConfig& cfg = config_get();
    timer_state = TIMER_IDLE;
    timer_paused_state = TIMER_RUNNING;
    timer_duration_ms = (unsigned long)cfg.timer_work_min * 60000UL;
    timer_paused_remaining_ms = timer_duration_ms;
    timer_current_session = 1;
    Serial.println("[TIMER] Reset");
}

TimerState timer_get_state() {
    return timer_state;
}

int timer_get_remaining_sec() {
    AppConfig& cfg = config_get();

    switch (timer_state) {
        case TIMER_IDLE:
            return cfg.timer_work_min * 60;

        case TIMER_RUNNING:
        case TIMER_BREAK:
        case TIMER_LONG_BREAK: {
            unsigned long elapsed = millis() - timer_start_ms;
            return (timer_duration_ms > elapsed) ? (int)((timer_duration_ms - elapsed) / 1000UL) : 0;
        }

        case TIMER_PAUSED:
            return timer_paused_remaining_ms / 1000;

        case TIMER_DONE:
            return 0;
    }
    return 0;
}

int timer_get_session() {
    return timer_current_session;
}

int timer_get_total_sessions() {
    return config_get().timer_sessions;
}

// --- Stopwatch ---

void stopwatch_toggle_start_pause() {
    switch (sw_state) {
        case SW_IDLE:
            sw_state = SW_RUNNING;
            sw_start_ms = millis();
            sw_paused_elapsed_ms = 0;
            Serial.println("[STOPWATCH] Started");
            break;

        case SW_RUNNING:
            sw_paused_elapsed_ms = millis() - sw_start_ms;
            sw_state = SW_PAUSED;
            Serial.println("[STOPWATCH] Paused");
            break;

        case SW_PAUSED:
            sw_start_ms = millis() - sw_paused_elapsed_ms;
            sw_state = SW_RUNNING;
            Serial.println("[STOPWATCH] Resumed");
            break;
    }
}

void stopwatch_reset() {
    sw_state = SW_IDLE;
    sw_start_ms = 0;
    sw_paused_elapsed_ms = 0;
    Serial.println("[STOPWATCH] Reset");
}

StopwatchState stopwatch_get_state() {
    return sw_state;
}

int stopwatch_get_elapsed_sec() {
    switch (sw_state) {
        case SW_IDLE:
            return 0;
        case SW_RUNNING:
            return (millis() - sw_start_ms) / 1000;
        case SW_PAUSED:
            return sw_paused_elapsed_ms / 1000;
    }
    return 0;
}
