#include "companion.h"
#include "ambient_fish.h"
#include "config_manager.h"
#include "display.h"
#include "http_client.h"
#include "weather_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>

static AppConfig cfg = {};
static GlucoseReading reading = {};
static WeatherReading weather = {};
static unsigned long now_ms = 3000;
static int hour = 10, pixel_count = 0, failures = 0;
static bool ever_received = true;
static unsigned long reading_age = 0;
static char text[16];
static uint16_t screen[8][32];
unsigned long millis() { return now_ms; }
AppConfig& config_get() { return cfg; }
const GlucoseReading& http_get_reading() { return reading; }
bool http_has_ever_received() { return ever_received; }
unsigned long http_time_since_last_reading() { return reading_age; }
int http_get_failure_count() { return failures; }
bool time_is_available() { return true; }
int time_get_hour() { return hour; }
int time_get_day() { return 31; }
int time_get_month() { return 10; }
bool weather_has_data() { return false; }
const WeatherReading& weather_get_reading() { return weather; }
void display_clear() { pixel_count = 0; text[0] = 0; memset(screen, 0, sizeof(screen)); }
void display_draw_pixel(int x, int y, uint16_t color) {
    assert(x >= 0 && x < 32 && y >= 0 && y < 8);
    screen[y][x] = color; ++pixel_count;
}
uint16_t display_color(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3); }
int display_text_width(const char* value) { return strlen(value) * 6; }
void display_draw_text(const char* value, int, int, uint16_t) { snprintf(text, sizeof(text), "%s", value); }

int main(int argc, char**) {
    if (argc > 1) {
        for (int style = 0; style < 3; ++style) for (int range = 0; range < 3; ++range)
        for (int id = 0; id < 4; ++id) for (int mood = 0; mood < 3; ++mood)
            for (uint32_t ms = 0; ms < 15000; ms += 100) {
                char pixels[8][32]; companion_frame(id, ms, mood == 1, mood == 2, pixels, style, range);
                fwrite(pixels, 1, sizeof(pixels), stdout); putchar('\n');
            }
        return 0;
    }
    assert(companion_or_default(-1) == COMPANION_FISH);
    assert(companion_or_default(4) == COMPANION_FISH);
    assert(companion_style_or_default(-1) == COMPANION_TEXT);
    assert(companion_style_or_default(3) == COMPANION_TEXT);
    assert(companion_color('a') == 0xf6a644);
    assert(companion_color('r') == 0xf06460);
    cfg.data_source = 2; cfg.thresh_urgent_low = 70; cfg.thresh_low = 80;
    cfg.thresh_high = 180; cfg.thresh_urgent_high = 250;
    cfg.ambient_seasonal = true;
    for (int style = 0; style < 3; ++style) for (int id = 0; id < 4; ++id) {
        cfg.ambient_style = style;
        cfg.ambient_character = id;
        ambient_fish_init(); reading.valid = true; reading.glucose = 120;
        ambient_fish_render(); assert(pixel_count > 20 && text[0] == 0);
        for (int glucose : {60, 69, 251, 300}) {
            reading.glucose = glucose;
            ambient_fish_interact(); ambient_fish_render();
            char expected[16]; snprintf(expected, sizeof(expected), "%d", glucose);
            assert(pixel_count == 0); assert(strcmp(text, expected) == 0);
            cfg.use_mmol = true; ambient_fish_render();
            snprintf(expected, sizeof(expected), "%.1f", glucose / 18.018f);
            assert(pixel_count == 0 && strcmp(text, expected) == 0);
            cfg.use_mmol = false;
        }
        for (int glucose : {70, 79, 80, 180, 181, 250}) {
            reading.glucose = glucose; ambient_fish_render();
            assert(pixel_count > 20 && text[0] == 0);
        }
        reading.valid = false; ambient_fish_render();
        assert(pixel_count == 0 && strcmp(text, "---") == 0);
        reading.valid = true; reading.glucose = 120;
        cfg.ambient_seasonal = false; cfg.color_in_range = 0;
        hour = 23; now_ms = 3000; ambient_fish_init(); ambient_fish_render();
        assert(pixel_count > 20); uint16_t sleepy[8][32]; memcpy(sleepy, screen, sizeof(screen));
        ambient_fish_interact(); ambient_fish_render();
        assert(memcmp(sleepy, screen, sizeof(screen)) != 0);
        now_ms += 1800; ambient_fish_render();
        memcpy(sleepy, screen, sizeof(screen));
        ambient_fish_init(); ambient_fish_render();
        assert(memcmp(sleepy, screen, sizeof(screen)) == 0);
        // Animated frames remain bounded across poses and millis rollover.
        for (uint32_t ms : {0u, 100u, 4030u, 0xfffffff0u}) {
            for (int mood = 0; mood < 3; ++mood) {
                char pixels[8][32]; companion_frame(id, ms, mood == 1, mood == 2, pixels);
                for (auto& row : pixels) for (char c : row) assert(c == '.' || companion_color(c) != 0);
            }
        }
        // Missing/stale data must never gain an OK label or range icon.
        cfg.data_source = 0; cfg.stale_timeout_min = 20;
        reading_age = 20UL * 60 * 1000; ambient_fish_render();
        assert(pixel_count == 0 && strcmp(text, "---") == 0);
        reading_age = 0; failures = 5; ambient_fish_render();
        assert(pixel_count == 0 && strcmp(text, "---") == 0);
        failures = 0; ever_received = false; ambient_fish_render();
        assert(pixel_count == 0 && strcmp(text, "---") == 0);
        ever_received = true; cfg.data_source = 2;
        cfg.ambient_seasonal = true; hour = 10;
    }
    for (int style=0; style<3; ++style) for (int id=0; id<4; ++id) for (int range=0; range<3; ++range) {
        char initial[8][32]; companion_frame(id,0,false,false,initial,style,range);
        for (uint32_t ms=0; ms<10000; ms+=100) {
            char pixels[8][32]; companion_frame(id,ms,false,false,pixels,style,range);
            int left=32,right=-1;
            for (int y=0;y<8;++y) for (int x=0;x<32;++x) {
                char c=pixels[y][x]; assert(c=='.'||companion_color(c)!=0);
                if (style==COMPANION_ONLY) {
                    assert(c!='a'&&c!='r'&&c!='i'&&c!='u');
                    if(c!='.') { if(x<left)left=x; if(x>right)right=x; }
                } else if (x>=15) assert(c==initial[y][x]); // words/icons stay still
            }
            if (style==COMPANION_ONLY) assert(left+right==30 || left+right==31);
            else {
                char color=range==1?'a':range==2?'r':'i'; int lit=0;
                for (auto& row:pixels) for(char c:row) if(c==color)++lit;
                assert(lit>0);
            }
            if(range!=COMPANION_IN_RANGE) {
                char sleepy[8][32]; companion_frame(id,ms,true,false,sleepy,style,range);
                assert(memcmp(sleepy,pixels,sizeof(pixels))==0);
            }
        }
    }

}
