#include "display.h"
#include "FastLED.h"
#include <cassert>
#include <initializer_list>
static unsigned long now=0;
unsigned long millis() {return now;}
FakeLEDs FastLED;
static bool enabled=true,suspended=false,secure=false,admission=true;
static bool updating=false,buzzing=false,urgent=false;
static uint32_t passkeyUntil=0,passkey=123456;
struct Reading {bool valid=true;int glucose=100;} reading;
struct Config {int thresh_urgent_low=55,thresh_urgent_high=300;} cfg;
bool windowOpen() {return admission;}
bool ota_is_busy() {return updating;}
bool buzzer_is_active() {return buzzing;}
bool notify_is_urgent() {return urgent;}
const Reading& http_get_reading() {return reading;}
const Config& config_get() {return cfg;}
// The test runner supplies the actual renderer body from ble_manager.cpp.
#include "ble_render.inc"
static void frame(char expected) {
 int before=FastLED.shows;
 display_begin_frame();display_clear();display_draw_text("GLUCOSE",3,0,1);display_show();
 ble_render();assert(FastLED.shows==before);
 display_end_frame();assert(FastLED.shows==before+1);
 assert(FastLED.last[3].r==expected);
}
int main() {
 display_init();
 for(auto t:{0UL,600UL,1500UL,3999UL,4000UL}) {now=t;frame('P');}
 assert(display_get_brightness()==40);
 suspended=true;enabled=false;frame('W');enabled=true;suspended=false;
 admission=false;frame('G');admission=true;
 secure=true;frame('G');secure=false;
 updating=true;frame('G');updating=false;
 buzzing=true;frame('G');buzzing=false;
 urgent=true;frame('G');urgent=false;
 reading.glucose=40;frame('G');reading.glucose=350;frame('G');reading.glucose=100;
 enabled=false;frame('G');enabled=true;
 passkeyUntil=now+30000;
 display_begin_frame();display_clear();display_show();ble_render();display_end_frame();
 assert(FastLED.last[3].r==0); // Code pixels replace all normal/admission text.
 int lit=0;for(auto pixel:FastLED.last)if(pixel.r)++lit;assert(lit>0);
 now=passkeyUntil;frame('P'); // Expired passkey is never left visible.
}
