"""Run the production display engine against controlled readings and peripherals."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LowGlucoseTests(unittest.TestCase):
    def test_display_priority_navigation_recovery_and_freshness(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'low-glucose'
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                '-Werror', '-Wno-error=unused-variable',
                '-I', str(ROOT / 'tests/engine_stubs'), '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/test_low_glucose.cpp'),
                str(ROOT / 'src/weather_render.cpp'), '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)

    def test_navigation_api_reports_locked_commands(self):
        # Compile the actual HTTP handlers with the real engine and capture the
        # response fields. The request/JSON shims replace only server transport.
        source = (ROOT / 'src/web_server.cpp').read_text()
        handlers = source[source.index('// POST /api/display/next'):source.index('// POST /api/restart')]
        harness = r'''
#define main engine_regression_main
#include "__ENGINE_TEST__"
#undef main
#include <map>
#include <string>
struct JsonValue {
    std::string text;
    JsonValue& operator=(const char* value) { text=value; return *this; }
    JsonValue& operator=(bool value) { text=value?"true":"false"; return *this; }
};
using JsonDocument = std::map<std::string, JsonValue>;
using String = JsonDocument;
void serializeJson(const JsonDocument& doc, String& output) { output=doc; }
struct AsyncWebServerRequest {
    int status = 0;
    String body;
    void send(int code, const char*, const String& output) { status=code; body=output; }
};
__HANDLERS__
int main() {
    cfg.thresh_low=80; cfg.thresh_urgent_low=70; cfg.stale_timeout_min=20;
    cfg.glucose_only_when_low=true; cfg.time_display_enabled=true; cfg.default_mode=1;
    reading.valid=true; reading.glucose=65; reading.force_mode=-1;
    engine_init();
    const DisplayState before=engine_get_user_mode();
    assert(before==STATE_TIME_DISPLAY);
    for (auto handler : {handle_display_next, handle_display_prev}) {
        AsyncWebServerRequest request;
        handler(&request);
        assert(request.status==409);
        assert(request.body.at("locked").text=="true");
        assert(request.body.at("status").text=="locked");
        assert(request.body.at("reason").text=="low_glucose");
        assert(!request.body.at("error").text.empty());
        assert(request.body.at("mode").text==engine_state_name(before));
        assert(engine_get_user_mode()==before);
    }
    reading.glucose=100;
    AsyncWebServerRequest next;
    handle_display_next(&next);
    assert(next.status==200 && next.body.at("locked").text=="false");
    assert(next.body.at("status").text=="ok");
    assert(engine_get_user_mode()!=before);
    assert(next.body.at("mode").text==engine_state_name(engine_get_user_mode()));
    AsyncWebServerRequest prev;
    handle_display_prev(&prev);
    assert(prev.status==200 && prev.body.at("locked").text=="false");
    assert(engine_get_user_mode()==before);
    assert(prev.body.at("mode").text==engine_state_name(before));
    assert(!prev.body.count("error"));
}
'''.replace('__ENGINE_TEST__', str(ROOT / 'tests/test_low_glucose.cpp')).replace('__HANDLERS__', handlers)
        with tempfile.TemporaryDirectory() as tmp:
            cpp = Path(tmp) / 'navigation.cpp'
            exe = Path(tmp) / 'navigation'
            cpp.write_text(harness)
            subprocess.run([
                os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                '-Werror', '-Wno-error=unused-variable',
                '-I', str(ROOT / 'tests/engine_stubs'), '-I', str(ROOT / 'include'),
                str(cpp), str(ROOT / 'src/weather_render.cpp'), '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)

    def test_web_controls_explain_and_release_lock(self):
        script = r'''
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const nodes={}; const buttons=[{},{}]; let toast='';
const context={document:{getElementById:id=>nodes[id]||(nodes[id]={}),querySelectorAll:()=>buttons},
    showToast:msg=>{toast=msg;},friendlyMode:x=>x,setTimeout:()=>{},liveDisplay:{refresh(){}}};
vm.createContext(context);
const display=fs.readFileSync(process.argv[1],'utf8');
vm.runInContext(display.slice(display.indexOf('let glucoseDisplayLocked'),display.indexOf('let readingUnits')),context);
vm.runInContext(display.slice(display.indexOf('async function changeDisplay'),display.indexOf('refresh();setInterval')),context);
const settings=fs.readFileSync(process.argv[2],'utf8');
vm.runInContext(settings.slice(settings.indexOf('function updateDisplayControls'),settings.indexOf("document.getElementById('display-prev-btn').addEventListener")),context);
vm.runInContext(settings.slice(settings.indexOf('async function updateCurrentMode'),settings.indexOf('setInterval(updateCurrentMode')),context);
(async()=>{
    context.updateDisplayLock(true);
    assert(buttons.every(b=>b.disabled));
    assert.match(nodes['display-control-status'].textContent,/locked/);
    context.updateDisplayLock(false);
    assert(buttons.every(b=>!b.disabled));
    assert.equal(nodes['display-control-status'].textContent,'');
    const error='Blood sugar display is locked while glucose is low.';
    context.fetch=async()=>({ok:false,json:async()=>({locked:true,error})});
    await context.changeDisplay('next');
    assert.equal(nodes['display-control-status'].textContent,error);
    assert(buttons.every(b=>b.disabled));
    await context.displayControl('prev');
    assert.equal(toast,error);
    // The 409 itself disables settings controls, before the next status poll.
    assert(nodes['display-next-btn'].disabled && nodes['display-prev-btn'].disabled);
    assert.equal(nodes['current-mode'].textContent,'Blood sugar (locked)');
    context.fetch=async()=>({ok:true,json:async()=>({glucose_display_locked:true,state:'GLUCOSE'})});
    await context.updateCurrentMode();
    assert.equal(nodes['current-mode'].textContent,'Blood sugar (locked)');
    assert(nodes['display-next-btn'].disabled && nodes['display-prev-btn'].disabled);
    context.fetch=async()=>({ok:true,json:async()=>({glucose_display_locked:false,state:'TIME',locked:false,mode:'TIME'})});
    await context.updateCurrentMode();
    assert.equal(nodes['current-mode'].textContent,'TIME');
    assert(!nodes['display-next-btn'].disabled && !nodes['display-prev-btn'].disabled);
    await context.changeDisplay('next');
    assert.equal(nodes['display-control-status'].textContent,'Clock display changed');
    assert(buttons.every(b=>!b.disabled));
})().catch(e=>{console.error(e);process.exitCode=1;});
'''
        subprocess.run(['node', '-e', script, str(ROOT / 'data/www/display.html'),
                        str(ROOT / 'data/www/index.html')], check=True)
