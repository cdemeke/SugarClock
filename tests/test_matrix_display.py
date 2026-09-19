from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('node'), 'Node required for matrix renderer')
class MatrixDisplayTests(unittest.TestCase):
    def test_rgb_fidelity_geometry_and_companion_adapter(self):
        subprocess.run(['node', '-e', r'''
const assert = require('node:assert/strict');
require(process.argv[1]);
require(process.argv[2]);
let tiles = [], position, stops;
const ctx = {
 fillRect(){}, save(){}, restore(){}, beginPath(){},
 translate(x,y){position=[x,y]},
 createRadialGradient(){stops=[];return {addColorStop:(s,c)=>stops.push([s,c])}},
 roundRect(x,y,w,h,r){assert.deepEqual([x,y,w,h,r],[0,0,36,36,1])},
 fill(){tiles.push({position,stops})}
};
const canvas = {width:32,height:8,getContext:()=>ctx};
const rgb = new Uint8Array(768); rgb.set([148,230,161],0); rgb.set([255,0,0],765);
const original = rgb.slice(); MatrixDisplay.draw(canvas,rgb);
assert.deepEqual(rgb,original);
assert.deepEqual([canvas.width,canvas.height],[1280,320]);
assert.deepEqual(tiles.map(t=>t.position),[[2,2],[1242,282]]);
assert.equal(tiles[0].stops[0][1],'rgb(148,230,161)');
assert.equal(tiles[1].stops[0][1],'rgb(255,0,0)');
assert.throws(()=>MatrixDisplay.draw(canvas,new Uint8Array(3)));
let actual;
MatrixDisplay.draw=(_canvas,frame)=>actual=frame;
for(let id=0;id<7;id++)for(let range=0;range<3;range++)for(const mood of ['awake','sleepy','happy']) {
 PixelCompanions.draw(canvas,id,1500,mood,0,range);
 const frame=PixelCompanions.frame(id,1500,mood==='sleepy',mood==='happy',0,range).flat();
 frame.forEach((c,i)=>{
  const expected=c==='.'?'#000000':PixelCompanions.resolveColor(c,null);
  assert.deepEqual([...actual.slice(i*3,i*3+3)],expected.slice(1).match(/../g).map(h=>parseInt(h,16)));
 });
}
''', str(ROOT / 'data/www/matrix-display.js'), str(ROOT / 'data/www/companions.js')], check=True)
