/** Compare the demo directly to the compiled firmware renderer, no fixtures to drift. */
import assert from "node:assert/strict";
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";
const root = resolve(new URL("..", import.meta.url).pathname);
const source = readFileSync(join(root,"docs/demo/weather-renderer.js"),"utf8");
const { renderWeather, WEATHER_CONDITIONS, weatherVisualForCondition } = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);
const rgb565 = color => {
  if (!color) return 0;
  const n = Number.parseInt(color.slice(1),16);
  return (((n>>16)&248)<<8) | (((n>>8)&252)<<3) | ((n&255)>>3);
};

test("weather frames match real C++ firmware for every visual, animation phase, and temperature width", () => {
  const directory = mkdtempSync(join(tmpdir(),"sugar-weather-"));
  try {
    const harness = join(directory,"compare.cpp"), executable = join(directory,"compare");
    writeFileSync(harness, `#include "weather_render.h"
#include "display.h"
#include <iostream>
#include <cstring>
uint16_t pixels[256];
uint16_t display_color(uint8_t r,uint8_t g,uint8_t b) { return ((r&248)<<8)|((g&252)<<3)|(b>>3); }
void display_draw_pixel(int x,int y,uint16_t c) { pixels[y*32+x]=c; }
int main() { float temp; int use_f,id; unsigned elapsed;
 while(std::cin >> temp >> use_f >> id >> elapsed) {
  memset(pixels,0,sizeof(pixels)); weather_render(temp,use_f,id,elapsed,0xffff);
  std::cout << static_cast<int>(weather_visual_for_condition(id));
  for(auto pixel:pixels) std::cout << " " << pixel;
  std::cout << "\\n";
 }
}`);
    const compile = spawnSync(process.env.CXX || "c++", ["-std=c++11","-Wall","-Wextra","-Werror","-I",join(root,"include"),harness,join(root,"src/weather_render.cpp"),"-o",executable],{encoding:"utf8"});
    assert.equal(compile.status,0,compile.stderr);
    const cases=[];
    const ids=[200,300,500,511,600,611,612,613,615,616,701,721,741,800,801,802,803,804,781,711,999];
    for(const id of ids) for(const elapsed of [0,269,270,379,380,649,650,849,850,950,1600,3199,3200,5700,0xffffffff])
      cases.push([72,1,id,elapsed]);
    for(const temperature of [-999,-100,-9.5,-0.5,0,7,99,100,999,1000,9999,10000,-1000])
      for(const unit of [0,1]) cases.push([temperature,unit,800,0]);
    const run = spawnSync(executable,[],{input:cases.map(c=>c.join(" ")).join("\n")+"\n",encoding:"utf8",maxBuffer:8*1024*1024});
    assert.equal(run.status,0,run.stderr);
    const lines=run.stdout.trim().split("\n");
    assert.equal(lines.length,cases.length);
    const visuals=["sunny","partly-cloudy","cloudy","drizzle","rain","sleet","snow","storm","tornado","cloud-fallback"];
    cases.forEach(([temp,useF,id,elapsed],i)=>{
      const [visual,...pixels]=lines[i].split(" ").map(Number);
      assert.equal(weatherVisualForCondition(id),visuals[visual],`mapping ${id}`);
      assert.deepEqual(renderWeather(temp,Boolean(useF),id,elapsed).map(rgb565),pixels,`temp=${temp} unit=${useF} id=${id} elapsed=${elapsed}`);
    });
  } finally { rmSync(directory,{recursive:true,force:true}); }
});

test("all ten conditions are selectable, and animations never affect gutter or temperature",()=>{
  assert.equal(Object.keys(WEATHER_CONDITIONS).length,10);
  for(const [condition,id] of Object.entries(WEATHER_CONDITIONS)) {
    const frames=[0,380,850,1600,3200].map(t=>renderWeather(72,true,id,t));
    for(const frame of frames) for(let y=0;y<8;y++) {
      assert.deepEqual(frame.slice(y*32+8,y*32+10),[null,null]);
      assert.deepEqual(frame.slice(y*32+10,y*32+32),frames[0].slice(y*32+10,y*32+32));
    }
    if(["sunny","cloud-fallback"].includes(condition)) assert(frames.every(frame=>JSON.stringify(frame)===JSON.stringify(frames[0])));
    else assert(frames.some(frame=>JSON.stringify(frame)!==JSON.stringify(frames[0])),condition);
  }
});
