/** Pixel-for-pixel port of src/weather_render.cpp and PR #45's approved design.
 * Icon stays in x=0..7; x=8..9 is a gutter; temperature never scrolls.
 */
const TALL = [[6,9,9,9,9,9,6],[2,6,2,2,2,2,7],[6,9,1,2,4,8,15],[14,1,1,6,1,1,14],[9,9,9,15,1,1,1],[15,8,8,14,1,1,14],[6,8,8,14,9,9,6],[15,1,2,2,4,4,4],[6,9,9,6,9,9,6],[6,9,9,7,1,1,6]];
const SMALL = [[7,5,5,5,7],[2,6,2,2,7],[6,1,2,4,7],[6,1,2,1,6],[5,5,7,1,1],[7,4,6,1,6],[3,4,7,5,7],[7,1,2,2,2],[7,5,7,5,7],[7,5,7,1,6]];
export const WEATHER_CONDITIONS = Object.freeze({
  sunny: 800, "partly-cloudy": 801, cloudy: 803, drizzle: 300,
  rain: 500, sleet: 611, snow: 600, storm: 200, tornado: 781,
  "cloud-fallback": 711,
});
export function weatherVisualForCondition(id) {
  if (id >= 200 && id < 300) return "storm";
  if (id >= 300 && id < 400) return "drizzle";
  if ([511,611,612,613,615,616].includes(id)) return "sleet";
  if (id >= 500 && id < 600) return "rain";
  if (id >= 600 && id < 700) return "snow";
  if ([701,721,741].includes(id)) return "rain";
  if (id === 800) return "sunny";
  if (id === 801 || id === 802) return "partly-cloudy";
  if (id === 803 || id === 804) return "cloudy";
  if (id === 781) return "tornado";
  return "cloud-fallback";
}
const rgb = (r,g,b,opacity=255) => "#" + [r,g,b].map(v => Math.floor(v*opacity/255).toString(16).padStart(2,"0")).join("");

/** Temperature is already in the requested unit, exactly as in firmware. */
export function renderWeather(temperature, useF, conditionId, elapsed = 0, textColor = "#ffffff") {
  const frame = Array(256).fill(null);
  elapsed = Math.max(0, Math.floor(elapsed)) >>> 0;
  const pixel = (x,y,color,icon=true) => {
    if (y >= 0 && y < 8 && x >= (icon ? 0 : 10) && x < (icon ? 8 : 32)) frame[y*32+x] = color;
  };
  const sprite = (rows,width,x,y,color,icon=true) => rows.forEach((bits,dy) => {
    for(let dx=0; dx<width; dx++) if(bits & (1 << (width-dx-1))) pixel(x+dx,y+dy,color,icon);
  });
  const cloud = (x,y) => {
    sprite([24,60,126,63],7,x,y,rgb(139,161,177));
    sprite([24,36],7,x,y,rgb(183,200,210));
  };
  const sun = partial => {
    const y = partial ? 0 : 1;
    sprite([8,34,0,65,0,34,8],7,0,y,rgb(239,180,86,204));
    sprite([28,28,28],7,0,y+2,rgb(239,180,86));
  };
  // C++ receives float and roundf rounds ties away from zero.
  temperature = Math.fround(temperature);
  const rounded = Math.sign(temperature) * Math.floor(Math.abs(temperature) + 0.5);
  const available = Number.isFinite(rounded) && rounded >= -999 && rounded <= 9999;
  const digits = available ? String(rounded) : "--";
  const visual = weatherVisualForCondition(conditionId);
  if (!available) sprite([15],4,2,3,rgb(103,114,123));
  else switch (visual) {
    case "sunny": sun(false); break;
    case "partly-cloudy":
      sun(true);
      cloud((Math.floor(elapsed/380)+7)%15-7,3);
      break;
    case "cloudy": cloud((Math.floor(elapsed/380)+7)%15-7,2); break;
    case "cloud-fallback": cloud(0,2); break;
    case "snow":
      for(let i=0;i<2;i++) {
        const y=(Math.floor(elapsed/650)+i*7+6)%14-5;
        sprite([4,21,14,21,4],5,1,y,rgb(196,212,237,230));
        pixel(3,y+2,rgb(237,244,255));
      }
      break;
    case "tornado": sprite([127,62,28,12,8,16],7,Math.floor(elapsed/850)%2,1,rgb(139,161,177,204)); break;
    default: {
      cloud(0,0);
      const drizzle=visual === "drizzle", storm=visual === "storm";
      const lanes=visual === "rain" ? 3 : 2, period=drizzle ? 400 : 270;
      for(let i=0;i<lanes;i++) {
        const step=Math.floor(elapsed/period)+Math.floor(((elapsed%period)*5+i*12*period)/(5*period));
        const y=4+step%6, icy=visual === "sleet" && i === 1;
        const x=storm ? (i === 0 ? 0 : 7) : 1+i*(visual === "rain" ? 2 : 3)+(icy && (Math.floor(elapsed/950)+i)%2 === 1 ? 1 : 0);
        pixel(x,y,icy ? rgb(196,212,237,204) : rgb(99,155,234,204));
        if(!icy && !drizzle && y-1 >= 4) pixel(x,y-1,rgb(99,155,234,87));
      }
      if(storm) sprite([3,6,15,2,4],4,2,3,rgb(255,206,104,elapsed%3200 < 850 ? 255 : 158));
    }
  }
  const numericWidth = small => [...digits].reduce((w,d)=>w+(d === "-" || small ? 4 : 5),0);
  const small=numericWidth(false)+(available ? 6 : 3)>22;
  const width=numericWidth(small)+(available ? 6 : 3);
  let x=10+Math.trunc((22-width)/2);
  const y=small ? 1 : 0;
  for(const digit of digits) {
    const minus=digit === "-";
    const rows=minus ? (small ? [0,0,7,0,0] : [0,0,0,7,0,0,0]) : (small ? SMALL : TALL)[Number(digit)];
    const glyphWidth=minus || small ? 3 : 4;
    sprite(rows,glyphWidth,x,y,textColor,false);
    x+=glyphWidth+1;
  }
  if(available) { sprite([3,3],2,x,y,textColor,false); x+=3; }
  const unit=useF ? (small ? [7,4,6,4,4] : [7,4,4,6,4,4,4]) : (small ? [3,4,4,4,3] : [3,4,4,4,4,4,3]);
  sprite(unit,3,x,y,textColor,false);
  return frame;
}
