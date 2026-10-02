#!/usr/bin/env node
// Render repo-owned HTML/CSS layouts around unchanged simulator screenshots.
// This never invents an app screen, removes its sample-data label, or contacts a service.
const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const crypto = require('node:crypto');
const root = __dirname;
const output = path.join(root, 'assets');
const escape = s => s.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;');
const panels = [
  ['01-your-clocks','01-clocks','YOUR CLOCKS, TOGETHER','A place for\nevery clock.','Give each clock a name. Pick the one you want.',false],
  ['02-make-it-yours','02-settings','YOUR EVERYDAY COMPANION','Make it\nfeel like you.','Set up the details from your iPhone.',false],
  ['03-review-changes','03-pending','EDIT. REVIEW. UPDATE.','Your changes.\nYour call.','Review each edit before updating your clock.',false],
  ['04-pixel-pets','04-pets-dark','A LITTLE PERSONALITY','Meet your\nPixel Pet.','Seven companions. One that feels like yours.',true],
  ['05-display','05-display','SET THE MOOD','Just the\nright glow.','Fine-tune brightness and display preferences.',false]
];
const imageURL = name => pathToFileURL(path.join(root, 'screenshots', name+'.png')).href;
const iconURL = pathToFileURL(path.join(output, 'app-icon-1024.png')).href;
const base = `*{box-sizing:border-box}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#193c2a;background:#f0f6ed}img{display:block}.brand{display:flex;align-items:center;gap:18px;font-size:33px;font-weight:700;letter-spacing:-.5px}.brand img{width:60px;border-radius:13px}.eyebrow{font-weight:750;letter-spacing:5px;font-size:25px}.phone{position:absolute;overflow:hidden;border:14px solid #253c2d;border-radius:70px;box-shadow:0 38px 95px #16342222;background:white}.phone img{width:100%}`;
const html = (body, css='') => `<!doctype html><html><head><meta charset="utf-8"><style>${base}${css}</style></head><body>${body}</body></html>`;
const manifest = { app:'SugarClock', appVersion:'1.0.0 (24)', capturedSourceCommit:'0ff6ceefaa1325c2d0b780172f898737dc622182',
  data:'Synthetic fixture data in Debug-only ScreenshotPreview; production SwiftUI views; no Bluetooth connection. Labels retained.',
  icon:'Copy of SugarClock/Assets.xcassets/AppIcon.appiconset/icon_appstore.png; existing project branding.', files:[] };
(async () => {
  fs.mkdirSync(output,{recursive:true});
  const browser = await chromium.launch({channel:'chrome',headless:true});
  try {
    const page = await browser.newPage({deviceScaleFactor:1});
    async function render(name,width,height,content) {
      await page.setViewportSize({width,height});
      // Use a real local URL so Chromium permits local screenshot assets.
      const temp=path.join(output,'.render.html');fs.writeFileSync(temp,content);
      await page.goto(pathToFileURL(temp).href);
      await page.evaluate(async()=>{await document.fonts.ready;await Promise.all([...document.images].map(i=>i.decode()));});
      if(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth || document.documentElement.scrollHeight>innerHeight)) throw Error('Overflow: '+name);
      const target=path.join(output,name+'.png');
      await page.screenshot({path:target,type:'png',omitBackground:false});
      const bytes=fs.readFileSync(target);
      if(bytes[25]!==2) throw Error('Export must be RGB without alpha: '+name);
      manifest.files.push({file:'assets/'+name+'.png',width,height,sha256:crypto.createHash('sha256').update(bytes).digest('hex')});
      fs.unlinkSync(temp);
    }
    for(const [name,screen,kicker,title,subtitle,dark] of panels) {
      await render(name,1320,2868,html(`<div class="brand"><img src="${iconURL}" alt="">SugarClock</div><div class="copy"><div class="eyebrow">${kicker}</div><h1>${title.replace('\n','<br>')}</h1><p>${subtitle}</p></div><div class="phone"><img src="${imageURL(screen)}" alt="Actual app screen with sample data"></div><footer>Requires a compatible SugarClock · Sample screens</footer>`, `body{height:2868px;background:${dark?'#102c21':'radial-gradient(ellipse at 85% 67%,#d6e9c4,transparent 65%),#f0f6ed'};color:${dark?'#edf6e9':'#193c2a'}}.brand{position:absolute;top:74px;left:86px}.copy{position:absolute;top:206px;left:86px;right:86px}.eyebrow{color:${dark?'#a3d798':'#287c4e'}}h1{font-size:119px;letter-spacing:-6px;line-height:1.02;margin:32px 0 23px;font-weight:730}p{font-size:34px;line-height:1.4;margin:0;color:${dark?'#c0d6c6':'#50685a'}}.phone{width:936px;left:192px;top:702px}footer{position:absolute;bottom:39px;width:100%;text-align:center;font-size:23px;color:${dark?'#b2caba':'#58705f'}}`));
    }
    await render('social-1200x630',1200,630,html(`<div class="brand"><img src="${iconURL}" alt="">SugarClock</div><div class="copy"><div class="eyebrow">THE iOS COMPANION</div><h1>Your clock.<br><span>A little more you.</span></h1><p>Set up. Personalize. Make it yours.</p><small>Requires a compatible SugarClock.<br>Actual app screens with sample data.</small></div><div class="phone back"><img src="${imageURL('02-settings')}" alt=""></div><div class="phone front"><img src="${imageURL('04-pets-dark')}" alt=""></div>`,`.brand{position:absolute;left:54px;top:40px;font-size:23px;gap:12px}.brand img{width:44px}.copy{position:absolute;top:163px;left:54px}.eyebrow{font-size:12px;letter-spacing:2px;color:#287c4e}h1{font-size:58px;line-height:1.08;letter-spacing:-3px;margin:21px 0 26px}h1 span{color:#297649}p{font-size:20px;color:#54685b;margin:0 0 30px}small{font-size:12px;line-height:1.6;color:#66776b}.phone{width:226px;border-width:6px;border-radius:28px}.back{left:682px;top:34px;transform:rotate(-7deg)}.front{left:922px;top:98px;transform:rotate(6deg)}`));
    // Opaque native-size exports, with every pixel of the original UI visible.
    for(const folder of ['', 'ipad/']) for(const [,screen] of panels) {
      const source=path.join(root,'screenshots',folder,screen+'.png');
      const bytes=fs.readFileSync(source),width=bytes.readUInt32BE(16),height=bytes.readUInt32BE(20);
      await render((folder?'ipad-':'iphone-')+screen,width,height,html(`<img src="${pathToFileURL(source).href}" width="${width}" height="${height}" alt="Actual app screen with sample data">`));
    }
    // Review the actual landing page at desktop and phone widths.
    for(const [name,width,height] of [['site-desktop',1440,1000],['site-mobile',390,844]]) {
      await page.setViewportSize({width,height});
      await page.goto(pathToFileURL(path.join(root,'site/index.html')).href);
      await page.evaluate(async()=>Promise.all([...document.images].map(i=>{i.loading="eager";return i.decode();})));
      if(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth)) throw Error('Landing page overflows: '+name);
      await page.screenshot({path:path.join(output,name+'.png'),fullPage:true});
    }
    fs.writeFileSync(path.join(root,'asset-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
    console.log(`Rendered ${manifest.files.length} opaque assets and two landing-page previews.`);
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
