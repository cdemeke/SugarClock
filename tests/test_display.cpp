#include "display.h"
#include "FastLED.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <climits>
#include <initializer_list>
#include "glucose_format.h"
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
 // The actual display renderer honors the unit switch and centers the longer
 // decimal text with room for the trend arrow. Switching back restores mg/dL.
 for(bool mmol:{false,true,false}) {
  display_draw_glucose(180,1,mmol);display_show();
  const char* expected=mmol ? "10.0":"180";
  int x=(32-(int(std::strlen(expected))*6+6))/2;
  for(size_t i=0;i<std::strlen(expected);++i) assert(FastLED.last[x+i].r==expected[i]);
 }
 // All supported readings and deltas round once to one decimal, keep their
 // sign, and fit the callers' eight-byte buffers.
 for(int mgdl=-600;mgdl<=600;++mgdl) {
  char formatted[8],expected[16];
  format_glucose(formatted,sizeof(formatted),mgdl,true,true);
  snprintf(expected,sizeof(expected),"%+.1f",std::round((mgdl/18.0)*10)/10);
  assert(std::strcmp(formatted,expected)==0);
  format_glucose(formatted,sizeof(formatted),mgdl,false,true);
  snprintf(expected,sizeof(expected),"%+d",mgdl);
  assert(std::strcmp(formatted,expected)==0);
 }
 for(int mgdl:{INT_MIN,INT_MAX}) {
  struct {char text[8];char sentinel;} small={{},'X'};
  format_glucose(small.text,sizeof(small.text),mgdl,true,true);
  assert(small.sentinel=='X' && small.text[7]=='\0');
 }
}
