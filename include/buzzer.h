#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include "alarm_pattern.h"

// Initialize buzzer hardware (LEDC PWM on PIN_BUZZER)
void buzzer_init();

// Queue a beep pattern: count beeps at given frequency for duration_ms each.
// Ignored while a glucose alarm or sound test is playing.
void buzzer_beep(int count, int freq = 2000, int duration_ms = 200);

// Non-blocking buzzer update - call every loop iteration
void buzzer_loop();

// Stop ordinary beeping immediately (alarms and tests are unaffected)
void buzzer_stop();

// True while a beep, glucose alarm or sound test is playing
bool buzzer_is_active();

// Glucose alarm to play (ALARM_NONE silences it) with its pitch and style.
// Call every loop from the engine task; the pattern restarts only when the
// kind changes, while pitch and style changes apply at once.
void buzzer_set_alarm(AlarmKind kind, uint16_t hz = ALARM_DEFAULT_HZ,
                      AlarmStyle style = ALARM_STYLE_STEADY);

// Alarm pattern currently audible: a drill, else the glucose alarm. ALARM_NONE
// while a tone or sweep test overrides it.
AlarmKind buzzer_active_alarm();

// True while that alarm pattern has the buzzer on (for display flashing).
bool buzzer_alarm_output_on();

// Time-limited sound tests. Safe to call from any task: they only queue the
// request, which buzzer_loop() starts. Tests take priority over alarms.
void buzzer_test_drill(AlarmKind kind, uint32_t duration_ms, uint16_t hz, AlarmStyle style);
// A tone test plays `style` continuously (steady = one fixed pitch).
void buzzer_test_tone(uint16_t hz, uint32_t duration_ms, AlarmStyle style = ALARM_STYLE_STEADY);
void buzzer_test_sweep(uint16_t from_hz, uint16_t to_hz, uint16_t step_hz, uint32_t step_ms);
void buzzer_test_stop();

// True while a sound test is queued or playing.
bool buzzer_test_active();

// Pitch of the current sweep step, or 0 when no sweep is playing.
uint16_t buzzer_sweep_hz();

#endif // BUZZER_H
