"""Exercise the actual pre-fetch callback with two simulated FreeRTOS tasks."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WeatherPrefetchTests(unittest.TestCase):
    def test_only_engine_task_can_redraw_or_reset_animation(self):
        # Compile the production callback, without the engine's unrelated WiFi,
        # glucose, buzzer and timer dependencies. Its callees record side effects.
        source = (ROOT / 'src/glucose_engine.cpp').read_text()
        signature = 'static void on_weather_pre_fetch() {'
        end_marker = '// Data-driven toggle order'
        self.assertEqual(source.count(signature), 1,
                         'Expected one on_weather_pre_fetch definition; update the callback extraction.')
        remainder = source.split(signature, 1)[1]
        self.assertIn(end_marker, remainder,
                      'Callback end marker is missing; update the callback extraction before compiling.')
        callback = signature + remainder.split(end_marker, 1)[0]
        harness = r'''
#include "weather_render.h"
#include <assert.h>
#include <vector>
using TaskHandle_t = void*;
static int loop_task, async_task;
static TaskHandle_t engine_task = &loop_task;
static TaskHandle_t current_task = &loop_task;
static WeatherAnimationState weather_animation = {};
static const int STATE_WEATHER_DISPLAY = 1;
static int current_state = STATE_WEATHER_DISPLAY;
static uint8_t transition_level = 73;
static std::vector<int> calls;
TaskHandle_t xTaskGetCurrentTaskHandle() { return current_task; }
uint32_t millis() { return 1250; }
void display_clear() { calls.push_back(1); }
void display_set_transition_level(uint8_t level) {
    assert(level == 73); calls.push_back(2);
}
void draw_weather_content(uint32_t now) {
    assert(now == 1250);
    assert(weather_animation_elapsed(weather_animation, 600, now) == 0);
    calls.push_back(3);
}
void display_show() { calls.push_back(4); }
void display_draw_pixel(int, int, uint16_t) {}
uint16_t display_color(uint8_t, uint8_t, uint8_t) { return 0; }
__CALLBACK__
int main() {
    weather_animation_elapsed(weather_animation, 600, 1000);
    assert(weather_animation_elapsed(weather_animation, 600, 1200) == 200);
    const WeatherAnimationState before = weather_animation;
    current_task = &async_task;
    on_weather_pre_fetch();
    assert(calls.empty());
    assert(weather_animation.active == before.active);
    assert(weather_animation.condition_id == before.condition_id);
    assert(weather_animation.epoch_ms == before.epoch_ms);
    assert(weather_animation.last_render_ms == before.last_render_ms);

    current_task = &loop_task;
    on_weather_pre_fetch();
    assert((calls == std::vector<int>{1, 2, 3, 4}));
    assert(weather_animation.epoch_ms == 1250);

    calls.clear();
    current_state = 2;
    on_weather_pre_fetch();
    assert(calls.empty());
    assert(!weather_animation.active);
}
'''.replace('__CALLBACK__', callback)
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / 'weather-prefetch.cpp'
            exe = Path(tmp) / 'weather-prefetch'
            cpp.write_text(harness)
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'include'), str(cpp),
                str(ROOT / 'src/weather_render.cpp'), '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)
