#include "companion.h"
#include "ambient_fish.h"
#include "config_manager.h"
#include "display.h"
#include "http_client.h"
#include "weather_client.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

static AppConfig cfg{};
static GlucoseReading reading{};
static WeatherReading weather{};
static unsigned long now_ms=3000,age=0;
static int hour=10,failures=0,pixel_count=0;
static bool ever=true;
static char text[16]{};
static uint16_t screen[8][32]{};
unsigned long millis() {return now_ms;}
AppConfig& config_get() {return cfg;}
GlucoseReading http_get_reading() {return reading;}
bool http_has_ever_received() {return ever;}
unsigned long http_time_since_last_reading() {return age;}
int http_get_failure_count() {return failures;}
bool time_is_available() {return true;}
int time_get_hour() {return hour;}
int time_get_day() {return 1;}
int time_get_month() {return 7;}
bool weather_has_data() {return false;}
const WeatherReading& weather_get_reading() {return weather;}
void display_clear() {pixel_count=0;text[0]=0;memset(screen,0,sizeof(screen));}
void display_draw_pixel(int x,int y,uint16_t color) {assert(x>=0&&x<32&&y>=0&&y<8);screen[y][x]=color;++pixel_count;}
uint16_t display_color(uint8_t r,uint8_t g,uint8_t b) {return ((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);}
int display_text_width(const char* value) {return strlen(value)*6;}
void display_draw_text(const char* value,int,int,uint16_t) {snprintf(text,sizeof(text),"%s",value);}

int main(int argc,char**) {
 if(argc>1) {
  printf("{\"palette\":{");bool first=true;
  for(char c:std::string("WMSEOYRPG LDHCVTB")) {if(c==' ')continue;printf("%s\"%c\":%u",first?"":",",c,companion_color(c));first=false;}
  printf("},\"frames\":[");first=true;
  for(int id=0;id<COMPANION_COUNT;++id) for(int frame:{-1,0,1,2,6,7,15,16,65,80,96}) {
   char pixels[8][32];uint32_t ms=frame<0?0:frame*100;
   companion_frame(id,ms,false,frame>=0 && frame%80<16,pixels,COMPANION_ONLY,COMPANION_IN_RANGE);
   printf("%s{\"id\":%d,\"frame\":%d,\"rows\":[",first?"":",",id,frame);first=false;
   for(int y=0;y<8;++y) printf("%s\"%.*s\"",y?",":"",32,pixels[y]);
   printf("]}");
  }
  printf("]}\n");return 0;
 }
 assert(companion_or_default(-1)==0 && companion_or_default(7)==0);
 cfg.glucose_enabled=true;cfg.data_source=2;cfg.thresh_urgent_low=70;cfg.thresh_low=80;
 cfg.thresh_high=180;cfg.thresh_urgent_high=250;cfg.stale_timeout_min=20;
 std::set<std::string> artwork;
 for(int id=0;id<COMPANION_COUNT;++id) {
  cfg.ambient_creature=id;reading.valid=true;reading.glucose=120;now_ms=3000;hour=10;
  ambient_fish_init();ambient_fish_render();assert(pixel_count>20 && !text[0]);
  artwork.insert(std::string(reinterpret_cast<char*>(screen),sizeof(screen)));
  for(int value:{60,69,251,253,300}) {
   reading.glucose=value;ambient_fish_interact();ambient_fish_render();
   char expected[16];snprintf(expected,sizeof(expected),"%d",value);
   assert(pixel_count==0 && strcmp(text,expected)==0);
  }
  reading.valid=false;ambient_fish_render();assert(pixel_count==0 && !strcmp(text,"---"));
  cfg.glucose_enabled=false;ambient_fish_render();assert(pixel_count>20 && !text[0]);cfg.glucose_enabled=true;
  reading.valid=true;reading.glucose=120;cfg.data_source=1;age=20UL*60000;
  ambient_fish_render();assert(pixel_count==0 && !strcmp(text,"---"));age=0;cfg.data_source=2;
  hour=23;ambient_fish_init();ambient_fish_render();uint16_t sleepy[8][32];memcpy(sleepy,screen,sizeof(screen));
  ambient_fish_interact();ambient_fish_render();assert(memcmp(sleepy,screen,sizeof(screen))!=0);
  now_ms+=1800;ambient_fish_render();assert(memcmp(sleepy,screen,sizeof(screen))==0);
 }
 assert(artwork.size()==7);
 // Exercise every production pose/style/frame with guard bytes around the matrix.
 struct Guarded {char before[8];char pixels[8][32];char after[8];} guarded;
 for(int id=0;id<7;++id) for(int style=0;style<3;++style) for(int range=0;range<3;++range)
 for(int mood=0;mood<3;++mood) for(unsigned ms=0;ms<15000;ms+=100) {
  memset(&guarded,'!',sizeof(guarded));
  companion_frame(id,ms,mood==1,mood==2,guarded.pixels,style,range);
  for(char c:guarded.before)assert(c=='!');for(char c:guarded.after)assert(c=='!');
  for(auto& row:guarded.pixels)for(char c:row)assert(c=='.'||companion_color(c)!=0);
 }
}
