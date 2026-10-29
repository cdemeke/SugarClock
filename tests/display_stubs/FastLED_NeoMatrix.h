#pragma once
#include "FastLED.h"
constexpr int NEO_MATRIX_TOP=0,NEO_MATRIX_LEFT=0,NEO_MATRIX_ROWS=0,NEO_MATRIX_ZIGZAG=0;
struct FastLED_NeoMatrix {
 CRGB* pixels;int count,cursor=0;
 FastLED_NeoMatrix(CRGB* p,int w,int h,int):pixels(p),count(w*h) {}
 void begin() {}
 void setTextWrap(bool) {}
 void fillScreen(int) {for(int i=0;i<count;++i)pixels[i]={};}
 uint16_t Color(uint8_t r,uint8_t g,uint8_t b) {return (r<<8)|(g<<3)|b;}
 void setTextColor(uint16_t) {}
 void setCursor(int x,int) {cursor=x;}
 void print(const char* s) {while(*s && cursor<count) pixels[cursor++].r=*s++;}
 void drawPixel(int x,int y,uint16_t color) {if(x>=0 && x<32 && y>=0 && y<8)pixels[y*32+x].r=color;}
};
