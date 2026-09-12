from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('node'), 'Node required for live display client')
class LiveDisplayTests(unittest.TestCase):
    def test_rgb_render_polling_visibility_and_reconnect(self):
        script = r'''
const assert = require('node:assert/strict');
const events = new Map(), timers = new Map();
let timerId = 0, requests = 0, resolveFetch, painted, mode, requestHeaders;
global.document = {hidden:false, addEventListener:(k,v)=>events.set(k,v), removeEventListener:k=>events.delete(k)};
global.setTimeout = (fn,delay) => {timers.set(++timerId,{fn,delay});return timerId;};
global.clearTimeout = id => timers.delete(id);
global.fetch = (_url, options) => {requestHeaders=options.headers;requests++;return new Promise(resolve=>resolveFetch=resolve);};
const context = {createImageData:()=>({data:new Uint8ClampedArray(1024)}), putImageData:image=>painted=image.data.slice()};
const canvas = {getContext:()=>context,dataset:{},setAttribute:()=>{}};
const status = {};
const settle = () => new Promise(resolve=>setImmediate(resolve));
const response = length => ({ok:true,arrayBuffer:async()=>new Uint8Array(length).fill(17).buffer,headers:{get:k=>k==='X-Display-Mode'?'TIME':'7'}});
require(process.argv[1]);
(async()=>{
 const client=startLiveDisplay({canvas,status,onMode:value=>mode=value});
 await client.refresh();assert.equal(requests,1); // No overlapping requests.
 resolveFetch(response(768));await settle();
 assert.equal(mode,'TIME');assert.equal(canvas.dataset.frameSequence,'7');
 for(let i=0;i<256;i++)assert.deepEqual([...painted.slice(i*4,i*4+4)],[17,17,17,255]);
 assert.equal(status.textContent,'Live from your clock');
 document.hidden=true;events.get('visibilitychange')();await client.refresh();assert.equal(requests,1);assert.equal(timers.size,0);
 document.hidden=false;events.get('visibilitychange')();assert.equal(requests,2);
 resolveFetch(response(2));await settle(); // Never draw a malformed frame.
 assert.ok(status.textContent.includes('disconnected'));
 assert.ok([...timers.values()].some(t=>t.delay===1500));
 const pending=client.refresh();resolveFetch(response(768));await pending;
 assert.equal(status.textContent,'Live from your clock');
 const originalPixels=painted;
 const conditional=client.refresh();
 assert.equal(requestHeaders['If-None-Match'],'7');
 resolveFetch({status:304,ok:false,headers:{get:()=> 'TIME'},arrayBuffer:()=>{throw Error('304 body must not be read');}});
 await conditional;assert.equal(painted,originalPixels);
 assert.equal(status.textContent,'Live from your clock');
 const fresh=client.refresh();resolveFetch({status:204,ok:true});await fresh;
 assert.ok(status.textContent.includes('Waiting'));assert.equal(painted,originalPixels);
 client.stop();assert.equal(timers.size,0);assert.equal(events.size,0);
})().catch(error=>{console.error(error);process.exit(1)});
'''
        subprocess.run(['node', '-e', script, str(ROOT / 'data/www/live-display.js')], check=True)
