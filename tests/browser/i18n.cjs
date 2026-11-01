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
function serve(directory) {
  return new Promise(resolve => {
    const server = http.createServer((req, res) => {
      let relative = decodeURIComponent(new URL(req.url, 'http://localhost').pathname).replace(/^\//, '') || 'index.html';
      let file = path.join(directory, relative);
      if (!file.startsWith(root) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {res.writeHead(404);res.end();return;}
      const type = {'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json','.png':'image/png'}[path.extname(file)] || 'application/octet-stream';
      res.writeHead(200, {'Content-Type':type});fs.createReadStream(file).pipe(res);
    }).listen(0, '127.0.0.1', () => resolve([server, `http://127.0.0.1:${server.address().port}`]));
  });
}
before(async () => {
  browser = await chromium.launch({headless:true});
  [deviceServer, deviceURL] = await serve(path.join(root,'data/www'));
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
  // Clock pages fetch non-English catalogs from the public site's checked-in copy.
  await page.route('https://sugarclock.com/js/device-locales/*.json',route=>route.fulfill({
    path:path.join(root,'docs/js/device-locales',path.basename(new URL(route.request().url()).pathname)),
    contentType:'application/json',headers:{'Access-Control-Allow-Origin':'*'}}));
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

test('clock pages stay usable in English without internet access',async()=>{
 const {page,context,errors}=await pageFor('de-DE');
 await page.route('https://sugarclock.com/**',route=>route.abort('internetdisconnected'));
 await page.goto(deviceURL);await page.waitForFunction(()=>document.querySelector('[data-language-status]')?.textContent.length>0);
 assert.equal(await page.locator('html').getAttribute('lang'),'en');
 assert.equal(await page.locator('[data-i18n="Dashboard"]').first().innerText(),'Dashboard');
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

test('learn more & feedback dialog is translated, RTL-aware, and keyboard accessible',async()=>{
 for(const mobile of [false,true]){
  const {page,context,errors}=await pageFor('en-US',mobile);
  // The video thumbnail is external media; keep the test hermetic unless capturing screenshots.
  if(!process.env.CAPTURE_SCREENSHOTS) await page.route('https://img.youtube.com/**',route=>route.abort());
  const suffix=mobile?'mobile':'desktop';
  for(const route of ['/','/display.html','/device.html','/debug.html']){
   await page.goto(deviceURL+route);await ready(page,'en');
   if(mobile) await page.locator('.mobile-nav-toggle').click();
   const toggle=button(page);await toggle.click();
   const dialog=page.locator('dialog.help-dialog');
   assert.ok(await dialog.evaluate(el=>el.open));
   assert.equal(await dialog.getAttribute('aria-labelledby'),'help-dialog-title');
   assert.equal(await page.locator('#help-dialog-title').innerText(),'Learn more & feedback');
   assert.equal(await page.locator('.help-email-open').getAttribute('href'),'mailto:feedback@sugarclock.com?subject=SugarClock%20feedback');
   assert.equal(await page.locator('.help-email-address').inputValue(),'feedback@sugarclock.com');
   assert.match(await page.locator('.help-action').nth(1).getAttribute('href'),/^https:\/\/www\.youtube\.com\/@ChrisDemeke/);
   await page.locator('.help-video-facade').click();
   assert.match(await page.locator('.help-video-embed').getAttribute('src'),/^https:\/\/www\.youtube-nocookie\.com\/embed\/vfTbYTnHQdA/);
   await page.keyboard.press('Escape');
   assert.equal(await dialog.evaluate(el=>el.open),false);
   // The dialog's close event is queued as a task after Esc.
   await page.waitForFunction(()=>!document.querySelector('.help-video-embed')&&document.querySelector('.help-video-facade'));
   assert.ok(await toggle.evaluate(el=>document.activeElement===el),'focus returns to the help button');
  }
  await button(page).click();
  if(!mobile){await captureHelp(page,'help-english-'+suffix);await page.locator('.help-dialog-close').click();}
  else await page.keyboard.press('Escape');
  // Clicking the panel's own padding keeps it open; clicking the backdrop closes it.
  await button(page).click();
  const box=await page.locator('dialog.help-dialog').boundingBox();
  await page.mouse.click(box.x+4,box.y+box.height/2);
  assert.ok(await page.locator('dialog.help-dialog').evaluate(el=>el.open));
  await page.mouse.click(2,2);
  assert.equal(await page.locator('dialog.help-dialog').evaluate(el=>el.open),false);
  await page.evaluate(()=>toggleTheme());
  await button(page).click();await captureHelp(page,'help-dark-'+suffix);await page.keyboard.press('Escape');
  await page.evaluate(()=>toggleTheme());
  await choose(page,'ar');
  await button(page).click();
  assert.equal(await page.locator('#help-dialog-title').innerText(),'اعرف المزيد وأرسل ملاحظاتك');
  assert.equal(await page.locator('.help-dialog-close').getAttribute('aria-label'),'إغلاق');
  assert.equal(await page.locator('.help-site a').innerText(),await page.evaluate(()=>I18n.t(I18n.message('Setup guide, FAQ, and updates at {0}',['sugarclock.com']))));
  assert.equal(decodeURIComponent(await page.locator('.help-email-open').getAttribute('href')),'mailto:feedback@sugarclock.com?subject=ملاحظات حول SugarClock');
  const layout=await page.evaluate(()=>{const d=document.querySelector('dialog.help-dialog').getBoundingClientRect();
   const close=document.querySelector('.help-dialog-close').getBoundingClientRect(),title=document.querySelector('#help-dialog-title').getBoundingClientRect();
   const icon=document.querySelector('.help-action .nav-icon').getBoundingClientRect(),label=document.querySelector('.help-action span').getBoundingClientRect();
   const field=document.querySelector('.help-email-address').getBoundingClientRect(),copy=document.querySelector('.help-email-copy').getBoundingClientRect();
   return {inside:d.left>=0&&d.right<=innerWidth,closeLeftOfTitle:close.right<=title.left,iconRightOfLabel:icon.left>=label.right,copyLeftOfField:copy.right<=field.left+1,
    emailLtr:getComputedStyle(document.querySelector('.help-email-address')).direction,dir:getComputedStyle(document.querySelector('.help-dialog')).direction};});
  assert.deepEqual(layout,{inside:true,closeLeftOfTitle:true,iconRightOfLabel:true,copyLeftOfField:true,emailLtr:'ltr',dir:'rtl'});
  await captureHelp(page,'help-arabic-'+suffix);await page.keyboard.press('Escape');
  await choose(page,'en');
  assert.deepEqual(errors,[]);await context.close();
 }
});
function button(page){return page.locator('.help-toggle');}
test('help dialog copies the feedback address with and without the async clipboard API',async()=>{
 const {page,context,errors}=await pageFor('en-US');
 await context.grantPermissions(['clipboard-read','clipboard-write'],{origin:deviceURL});
 await page.route('https://img.youtube.com/**',route=>route.abort());
 await page.goto(deviceURL+'/device.html');await ready(page,'en');
 for(const insecure of [false,true]){
  // The real device is plain http, where navigator.clipboard does not exist.
  if(insecure) await page.evaluate(()=>{Object.defineProperty(navigator,'clipboard',{value:undefined});navigator.clipboard===undefined||(()=>{throw Error('clipboard still present')})();});
  await page.evaluate(()=>{document.querySelector('.help-email-address')?.blur();});
  await button(page).click();
  await page.evaluate(()=>navigator.clipboard?navigator.clipboard.writeText('stale'):null);
  await page.locator('.help-email-copy').click();
  await page.waitForFunction(()=>document.querySelector('.help-email-status').textContent==='Copied to clipboard.');
  if(!insecure) assert.equal(await page.evaluate(()=>navigator.clipboard.readText()),'feedback@sugarclock.com');
  await page.keyboard.press('Escape');
  await page.waitForFunction(()=>!document.querySelector('dialog.help-dialog').open&&document.querySelector('.help-email-status').textContent==='');
 }
 assert.deepEqual(errors,[]);await context.close();
});
async function captureHelp(page,name){
 if(process.env.CAPTURE_SCREENSHOTS) await page.waitForFunction(()=>{const img=document.querySelector('.help-video-facade img');return img.complete&&img.naturalWidth>0;});
 await capture(page,name);
}
test('first-visit help hint points at the footer button and stays dismissed',async()=>{
 const geometry=page=>page.evaluate(()=>{
  const hint=document.querySelector('.help-hint').getBoundingClientRect(),button=document.querySelector('.help-toggle').getBoundingClientRect();
  const sidebar=document.querySelector('.sidebar').getBoundingClientRect(),picker=document.querySelector('[data-language-trigger]').getBoundingClientRect();
  return {hint:{left:hint.left,right:hint.right,top:hint.top,bottom:hint.bottom},button:{top:button.top,bottom:button.bottom},sidebar:{left:sidebar.left,right:sidebar.right},
   pickerClear:picker.bottom<=hint.top||picker.top>=hint.bottom,onScreen:hint.left>=0&&hint.right<=innerWidth};});
 // Desktop, light: callout beside the sidebar, vertically overlapping the button.
 let {page,context,errors}=await pageFor('en-US');
 await page.route('https://img.youtube.com/**',route=>route.abort());
 await page.goto(deviceURL+'/device.html');await ready(page,'en');
 await page.waitForSelector('.help-hint.is-visible');
 assert.equal(await page.locator('.help-toggle').getAttribute('aria-describedby'),'help-hint');
 let g=await geometry(page);
 assert.ok(g.hint.left>=g.sidebar.right&&g.onScreen,JSON.stringify(g));
 assert.ok(g.hint.bottom>=g.button.top&&g.hint.top<=g.button.bottom,JSON.stringify(g));
 await page.waitForTimeout(400);await capture(page,'help-hint-desktop');
 // "Show me" opens the dialog and retires the hint.
 await page.locator('.help-hint-open').click();
 assert.ok(await page.locator('dialog.help-dialog').evaluate(el=>el.open));
 assert.equal(await page.locator('.help-hint').count(),0);
 await page.keyboard.press('Escape');
 await page.reload();await ready(page,'en');await page.waitForTimeout(1200);
 assert.equal(await page.locator('.help-hint').count(),0,'hint stays dismissed');
 assert.equal(await page.locator('.help-toggle').getAttribute('aria-describedby'),null);
 // Arabic: callout mirrors to the other side of the sidebar.
 await page.evaluate(()=>localStorage.removeItem('sugarclock.helpHintDismissed'));
 await page.reload();await ready(page,'en');await choose(page,'ar');
 await page.waitForSelector('.help-hint.is-visible');g=await geometry(page);
 assert.ok(g.hint.right<=g.sidebar.left&&g.onScreen,JSON.stringify(g));
 await page.waitForTimeout(400);await capture(page,'help-hint-arabic-desktop');
 // "Got it" (and Esc) dismiss without opening anything.
 await page.locator('.help-hint-dismiss').click();
 assert.equal(await page.locator('.help-hint').count(),0);
 assert.ok(await page.locator('.help-toggle').evaluate(el=>document.activeElement===el));
 assert.equal(await page.locator('dialog.help-dialog').count(),0);
 await choose(page,'en');
 assert.deepEqual(errors,[]);await context.close();
 // Mobile: a dot on the Menu button, and the callout sits inline in the drawer.
 ({page,context,errors}=await pageFor('en-US',true));
 await page.goto(deviceURL+'/');await ready(page,'en');
 await page.waitForSelector('.help-hint.is-visible');
 assert.ok(await page.locator('.mobile-nav-toggle').evaluate(el=>el.classList.contains('has-hint')&&getComputedStyle(el,'::after').content!=='none'));
 await page.locator('.mobile-nav-toggle').click();await page.waitForTimeout(400);
 g=await geometry(page);
 assert.ok(g.onScreen&&g.hint.bottom<=g.button.top+1&&g.pickerClear,JSON.stringify(g));
 await noOverflow(page);await capture(page,'help-hint-mobile');
 await page.locator('.help-hint-dismiss').focus();await page.keyboard.press('Escape');
 assert.equal(await page.locator('.help-hint').count(),0);
 assert.equal(await page.locator('.mobile-nav-toggle.has-hint').count(),0);
 assert.deepEqual(errors,[]);await context.close();
});
test('help hint: opening help early retires it, and short phone drawers keep every control reachable',async()=>{
 // Opening help before the 800 ms hint delay must stop the hint from appearing later.
 let {page,context,errors}=await pageFor('en-US');
 await page.route('https://img.youtube.com/**',route=>route.abort());
 await page.goto(deviceURL+'/debug.html');
 await page.evaluate(()=>openHelp(document.querySelector('.help-toggle')));
 await page.waitForTimeout(1200);
 assert.equal(await page.locator('.help-hint').count(),0);
 await page.keyboard.press('Escape');
 await page.reload();await ready(page,'en');await page.waitForTimeout(1200);
 assert.equal(await page.locator('.help-hint').count(),0,'early open is remembered');
 assert.deepEqual(errors,[]);await context.close();
 // Landscape phone: the drawer scrolls as a whole instead of squashing the nav.
 ({page,context,errors}=await pageFor('en-US'));
 await page.setViewportSize({width:667,height:375});
 await page.goto(deviceURL+'/');await ready(page,'en');await page.waitForSelector('.help-hint.is-visible');
 await page.locator('.mobile-nav-toggle').click();await page.waitForTimeout(400);
 const nav=await page.locator('.sidebar-nav').evaluate(el=>({client:el.clientHeight,scroll:el.scrollHeight}));
 assert.ok(nav.client>=nav.scroll-1,'navigation keeps its full height: '+JSON.stringify(nav));
 for(const selector of ['.sidebar-link >> nth=0','[data-language-trigger]','.theme-toggle','.help-hint-dismiss','.help-toggle']){
  const target=page.locator('.sidebar '+selector).first();await target.scrollIntoViewIfNeeded();
  assert.ok(await target.evaluate(el=>{const r=el.getBoundingClientRect();return r.top>=0&&r.bottom<=innerHeight&&r.height>0;}),selector+' reachable');
 }
 await page.locator('.help-toggle').click();
 assert.ok(await page.locator('dialog.help-dialog').evaluate(el=>el.open));
 assert.deepEqual(errors,[]);await context.close();
});
