#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
unsigned long millis();
struct CRGB {uint8_t r=0,g=0,b=0;};
struct WS2812B {};
constexpr int GRB=0,DISABLE_DITHER=0;
struct FakeLEDs {
 CRGB* pixels=nullptr;
 int count=0,shows=0;
 uint8_t brightness=0;
 CRGB last[256];
 template<class LED,int Pin,int Order> void addLeds(CRGB* p,int n) {pixels=p;count=n;}
 void setBrightness(int) {}
 void setDither(int) {}
 void setMaxPowerInVoltsAndMilliamps(int,int) {}
 void show(uint8_t value) {++shows;brightness=value;memcpy(last,pixels,count*sizeof(CRGB));}
};
extern FakeLEDs FastLED;
