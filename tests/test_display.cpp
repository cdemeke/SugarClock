#include "display.h"
#include "FastLED.h"
#include <cassert>
FakeLEDs FastLED;
unsigned long millis() {return 0;}
int main() {
 display_init();assert(FastLED.shows==1); // Boot still displays immediately.
 display_begin_frame();display_end_frame();assert(FastLED.shows==1);
 display_begin_frame();
 display_clear();display_draw_text("READING",0,0,1);display_show();
 assert(FastLED.shows==1); // The intermediate glucose image never reaches LEDs.
 display_clear();display_set_transition_level(255);
 display_draw_text("PAIR",0,0,1);display_show();
 assert(FastLED.shows==1);
 display_end_frame();assert(FastLED.shows==2);
 assert(FastLED.last[0].r=='P' && FastLED.last[3].r=='R' && FastLED.last[4].r==0);
 assert(FastLED.brightness==40); // Pairing does not modify configured brightness.
 display_end_frame();assert(FastLED.shows==2); // No duplicate commit.
 display_begin_frame();
 display_clear();display_draw_text("123456",0,0,1);display_show();
 display_end_frame();assert(FastLED.shows==3 && FastLED.last[5].r=='6');
 // If an urgent alert owns the frame, no pairing layer replaces it.
 display_begin_frame();
 display_clear();display_draw_text("ALERT",0,0,1);display_show();
 display_end_frame();assert(FastLED.shows==4 && FastLED.last[0].r=='A');
 display_begin_frame();display_end_frame();assert(FastLED.shows==4);
 display_show();assert(FastLED.shows==5); // Non-composed callers still work.
}
