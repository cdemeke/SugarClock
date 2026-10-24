const {test, before, after} = require('node:test');
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const root = path.resolve(__dirname, '../..');
let browser, deviceServer, siteServer, deviceURL, siteURL;
const screenshots = path.join(root, 'docs/pr/languages');
function serve(directory, device = false) {
  return new Promise(resolve => {
    const server = http.createServer((req, res) => {
      let relative = decodeURIComponent(new URL(req.url, 'http://localhost').pathname).replace(/^\//, '') || 'index.html';
      let file = path.join(directory, relative);
      if (device && relative.startsWith('locales/')) file = path.join(root, 'web/i18n/device-locales', path.basename(relative));
      if (!file.startsWith(root) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {res.writeHead(404);res.end();return;}
      const type = {'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json','.png':'image/png'}[path.extname(file)] || 'application/octet-stream';
      res.writeHead(200, {'Content-Type':type});fs.createReadStream(file).pipe(res);
    }).listen(0, '127.0.0.1', () => resolve([server, `http://127.0.0.1:${server.address().port}`]));
  });
}
before(async () => {
  browser = await chromium.launch({headless:true});
  [deviceServer, deviceURL] = await serve(path.join(root,'data/www'), true);
  [siteServer, siteURL] = await serve(path.join(root,'docs'));
  if (process.env.CAPTURE_SCREENSHOTS) fs.mkdirSync(screenshots, {recursive:true});
});
after(async () => {await browser?.close();deviceServer?.close();siteServer?.close();});
async function pageFor(locale='en-US', mobile=false) {
  const context=await browser.newContext({locale, viewport:mobile?{width:390,height:844}:{width:1440,height:1000},reducedMotion:'reduce'});
  const page=await context.newPage();const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.route('**/api/**',async route=>{
    const pathname=new URL(route.request().url()).pathname;
    if (pathname.startsWith('/admin/')) {await route.continue();return;}
    let body={};
    if(pathname==='/api/config')body={data_source:1,poll_interval:30,brightness:100,timezone:'EST5EDT,M3.2.0,M11.1.0',dexcom_username:'Connected',dexcom_password:'do-not-translate',weather_city:'Green',countdown_name:'Weather',ambient_character:0};
    if(pathname==='/api/wifi/scan')body={networks:[{ssid:'Save',rssi:-50}],scanning:false};
    if(pathname==='/api/wifi/status')body={connected:true,ip:'192.0.2.1',checks:{}};
    if(pathname==='/api/status')body={state:'GLUCOSE',glucose:123,delta:4,trend:4,valid:true,wifi_connected:true,wifi_rssi:-50,uptime_sec:60,glucose_display_locked:false};
    if(pathname==='/api/display/frame'){await route.fulfill({status:204});return;}
    if(pathname==='/api/ota/status')body={current_version:'0.2.15',state:'idle'};
    if(pathname==='/api/debug')body={battery_voltage:4.1,battery_percent:80};
    await route.fulfill({json:body});
  });
  // External install widget/media are outside localization and remain untouched.
  await page.route('https://unpkg.com/feather-icons',route=>route.fulfill({contentType:'text/javascript',body:'window.feather={replace(){}}'}));
  await page.route('https://unpkg.com/esp-web-tools**',route=>route.fulfill({contentType:'text/javascript',body:''}));
  return {page,context,errors};
}
async function ready(page, language){await page.waitForFunction(code=>window.I18n?.language===code&&document.querySelector('[data-language-trigger]'),language);}
// The device picker lives in the sidebar, which is a closed drawer on phones.
async function pick(page, value){
 const trigger=page.locator('[data-language-trigger]');
 const settled=()=>page.waitForFunction(()=>new Promise(done=>{const el=document.querySelector('[data-language-trigger]');const a=el.getBoundingClientRect().left;setTimeout(()=>requestAnimationFrame(()=>done(el.getBoundingClientRect().left===a)),100);}));
 const onScreen=async()=>{await settled();return trigger.evaluate(el=>{const r=el.getBoundingClientRect();return r.width>0&&r.left>=0&&r.right<=innerWidth;});};
 if(!(await onScreen())){await page.locator('.mobile-nav-toggle').click();await page.waitForFunction(()=>{const r=document.querySelector('[data-language-trigger]').getBoundingClientRect();return r.left>=0&&r.right<=innerWidth;});}
 await trigger.click();await page.locator(`[data-language-option="${value}"]`).click();
 assert.equal(await trigger.getAttribute('aria-expanded'),'false');
}
async function choose(page, language){await pick(page,language);await ready(page,language);}
async function noOverflow(page){
 const layout=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth,language:document.documentElement.lang,
  overflow:[...document.body.querySelectorAll('*')].filter(el=>{const r=el.getBoundingClientRect();return r.width>0&&(r.right>innerWidth+1||r.left < -1)&&!el.closest('.sidebar');}).slice(0,10).map(el=>({tag:el.tagName,id:el.id,class:el.className,text:el.textContent.slice(0,80),width:el.getBoundingClientRect().width}))}));
 assert.ok(layout.scroll<=layout.width+1, 'page should fit viewport: '+JSON.stringify(layout));
}
async function capture(page,name){if(process.env.CAPTURE_SCREENSHOTS){await page.evaluate(()=>window.scrollTo(0,0));await page.screenshot({path:path.join(screenshots,name+'.png'),fullPage:false,animations:'disabled'});}}

test('all 20 languages render marketing and admin without overflow or script errors',async()=>{
 const {page,context,errors}=await pageFor();
 for(const url of [siteURL,deviceURL]){
  await page.goto(url);await ready(page,'en');
  const languages=await page.evaluate(()=>I18n.languages.map(([code])=>code));assert.equal(languages.length,20);
  for(const language of languages){await choose(page,language);assert.equal(await page.locator('html').getAttribute('dir'),['ar','ur'].includes(language)?'rtl':'ltr');await noOverflow(page);}
  await choose(page,'en');
 }
 assert.deepEqual(errors,[]);await context.close();
});
test('regional browser language, persistence, FAQ navigation and English override',async()=>{
 const {page,context,errors}=await pageFor('es-MX');await page.goto(siteURL);await ready(page,'es');
 assert.equal(await page.locator('[data-language-trigger]').getAttribute('aria-label'),'Idioma: Español');
 await page.locator('[data-language-trigger]').press('ArrowDown');
 assert.equal(await page.evaluate(()=>document.activeElement.dataset.languageOption),'auto');
 await page.keyboard.press('Escape');assert.equal(await page.evaluate(()=>'languageTrigger' in document.activeElement.dataset),true);
 await capture(page,'marketing-spanish-desktop');
 await page.goto(siteURL+'/faq.html');await ready(page,'es');
 const question=page.locator('.faq-question').first();await question.click();
 assert.ok(await question.evaluate(el=>el.closest('.faq-item').classList.contains('active')));
 await choose(page,'en');await page.reload();await ready(page,'en');
 await pick(page,'auto');await ready(page,'es');
 assert.deepEqual(errors,[]);await context.close();
});
test('language changes preserve form state and API payloads, including data equal to UI copy',async()=>{
 const {page,context,errors}=await pageFor('es-ES');await page.goto(deviceURL);await ready(page,'es');
 await page.locator('#dexcom_username').fill('Connected');await page.locator('#dexcom_password').fill('Weather');
 const before=await page.locator('#config-form').evaluate(form=>[...form.querySelectorAll('input,select,textarea')].map(el=>[el.id,el.value,el.checked]));
 await capture(page,'admin-spanish-desktop');await choose(page,'ar');
 assert.deepEqual(await page.locator('#config-form').evaluate(form=>[...form.querySelectorAll('input,select,textarea')].map(el=>[el.id,el.value,el.checked])),before);
 const request=page.waitForRequest(req=>req.url().endsWith('/api/config')&&req.method()==='POST');
 await page.locator('#save-btn').click();const payload=(await request).postDataJSON();
 assert.equal(payload.dexcom_username,'Connected');assert.equal(payload.dexcom_password,'Weather');assert.equal(payload.data_source,1);assert.equal(payload.poll_interval,30);
 await page.waitForFunction(()=>document.querySelector('#toast').textContent===I18n.t('Configuration saved!'));
 await capture(page,'admin-arabic-desktop');
 await choose(page,'en');assert.equal(await page.locator('#dexcom_username').inputValue(),'Connected');
 assert.deepEqual(errors,[]);await context.close();
});
test('RTL mobile navigation, tabs, and marketing layout',async()=>{
 const {page,context,errors}=await pageFor('ar-EG',true);
 await page.goto(siteURL);await ready(page,'ar');await noOverflow(page);await capture(page,'marketing-arabic-mobile');
 await page.goto(deviceURL);await ready(page,'ar');await noOverflow(page);
 await page.locator('.mobile-nav-toggle').click();await page.locator('[data-tab="display"]').click();
 assert.ok(await page.locator('#tab-display').evaluate(el=>el.classList.contains('active')));
 await noOverflow(page);await capture(page,'admin-arabic-mobile');
 await choose(page,'ur');await noOverflow(page);await capture(page,'admin-urdu-mobile');
 assert.deepEqual(errors,[]);await context.close();
});
test('unsupported locale, unavailable storage, failed catalog, rapid selection and injection safety',async()=>{
 const {page,context,errors}=await pageFor('zz-ZZ');
 await context.addInitScript(()=>{Storage.prototype.getItem=()=>{throw Error('blocked')};Storage.prototype.setItem=()=>{throw Error('blocked')};Storage.prototype.removeItem=()=>{throw Error('blocked')};});
 await page.goto(siteURL);await ready(page,'en');
 await choose(page,'es');
 await page.route('**/locales/de.json',route=>route.fulfill({status:503,body:'Unavailable'}));
 await pick(page,'de');await page.waitForFunction(()=>document.querySelector('[data-language-status]').textContent.length>0);
 assert.equal(await page.locator('html').getAttribute('lang'),'es');
 await page.evaluate(async()=>{await Promise.all([I18n.setLanguage('fr'),I18n.setLanguage('ar'),I18n.setLanguage('en')]);});
 assert.equal(await page.locator('html').getAttribute('lang'),'en');
 await page.evaluate(()=>{const div=document.createElement('div');div.id='injection';document.body.append(div);I18n.text(div,I18n.message('Showing: {0} ({1})',['<img src=x onerror=alert(1)>',42]));});
 assert.equal(await page.evaluate(()=>I18n.t(I18n.message('{0}',[I18n.message('Language')]))),'Language');
 assert.equal(await page.locator('#injection img').count(),0);assert.match(await page.locator('#injection').innerText(),/<img/);
 assert.deepEqual(errors,[]);await context.close();
});

test('fleet pages translate controls and preserve nicknames, search, CSRF actions, and release choices',async()=>{
 const port=18381;
 const demo=spawn(process.env.PYTHON || 'python3',['-m','fleet.demo','--port',String(port)],{cwd:root,stdio:['ignore','pipe','pipe']});
 const url=`http://127.0.0.1:${port}`;
 const {page,context,errors}=await pageFor('es-MX');
 try{
  await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('Fleet demo startup timeout')),15000);demo.stderr.on('data',data=>{if(data.toString().includes('Running on')){clearTimeout(timer);resolve();}});demo.on('exit',code=>{clearTimeout(timer);reject(new Error('Fleet demo exited: '+code));});});
  for(const route of ['/admin/overview','/admin/devices','/admin/devices/1','/admin/releases']){
   await page.goto(url+route);await ready(page,'es');await noOverflow(page);
   await choose(page,'ar');await noOverflow(page);
   if(route==='/admin/overview') {await page.locator('#refresh').click();await page.waitForFunction(()=>!document.querySelector('#refresh').disabled);await capture(page,'fleet-arabic-desktop');}
   if(route==='/admin/devices') {await page.locator('#device-search').fill('My desk');assert.equal(await page.locator('tbody tr:visible').count(),1);assert.match(await page.locator('tbody tr:visible').innerText(),/My desk/);}
   if(route==='/admin/devices/1'){
    assert.equal(await page.locator('#friendly-name').inputValue(),'My desk');
    await page.locator('details summary').click();await page.locator('#command-type').selectOption('notify');
    await page.locator('#notification-message').fill('Weather');await choose(page,'es');
    assert.equal(await page.locator('#notification-message').inputValue(),'Weather');
    const request=page.waitForRequest(req=>req.method()==='POST'&&req.url().includes('/commands'));
    await page.locator('#command-form button').click();assert.equal((await request).postDataJSON().payload.message,'Weather');
    await page.waitForFunction(()=>document.querySelector('#command-result').textContent===I18n.t('Queued for the next check-in.'));
   }
   if(route==='/admin/releases') {await page.locator('#deployment-kind').selectOption('stable');assert.equal(await page.locator('#deployment-kind').inputValue(),'stable');assert.ok(await page.locator('#stable-options').isVisible());}
   await choose(page,'es');
  }
  await page.setViewportSize({width:390,height:844});await page.goto(url+'/admin/overview');await ready(page,'es');await choose(page,'ur');await noOverflow(page);await capture(page,'fleet-urdu-mobile');
  assert.deepEqual(errors,[]);
 }finally{demo.kill();await context.close();}
});

test('other device pages and mobile layouts retain usable controls',async()=>{
 const {page,context,errors}=await pageFor('es',true);
 for(const route of ['/display.html','/device.html','/debug.html','/notify.html']){
  await page.goto(deviceURL+route);await ready(page,route==='/display.html'?'es':'en');await choose(page,'ur');await noOverflow(page);await choose(page,'en');
 }
 await page.goto(siteURL);await ready(page,'es');
 for(const code of await page.evaluate(()=>I18n.languages.map(([code])=>code))){await choose(page,code);await noOverflow(page);}
 assert.deepEqual(errors,[]);await context.close();
});
