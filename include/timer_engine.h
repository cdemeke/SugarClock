#ifndef TIMER_ENGINE_H
#define TIMER_ENGINE_H

#include <stdint.h>

// Pomodoro timer states
enum TimerState {
    TIMER_IDLE,
    TIMER_RUNNING,
    TIMER_PAUSED,
    TIMER_BREAK,
    TIMER_LONG_BREAK,
    TIMER_DONE
};

// Stopwatch states
enum StopwatchState {
    SW_IDLE,
    SW_RUNNING,
    SW_PAUSED
};

// Pomodoro timer
void timer_init();
void timer_loop();
void timer_toggle_start_pause();
void timer_reset();
TimerState timer_get_state();
int timer_get_remaining_sec();
int timer_get_session();
int timer_get_total_sessions();

// Stopwatch
void stopwatch_toggle_start_pause();
void stopwatch_reset();
StopwatchState stopwatch_get_state();
int stopwatch_get_elapsed_sec();

#endif // TIMER_ENGINE_H
