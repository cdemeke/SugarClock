// Load the production localization runtime with an English-only document for unit tests.
const fs = require('node:fs');
const vm = require('node:vm');
const window = {addEventListener() {}};
const document = {currentScript:{src:'http://localhost/i18n.js',dataset:{}}, readyState:'loading', documentElement:{dir:'ltr'},addEventListener(){}};
vm.runInNewContext(fs.readFileSync(require('node:path').join(__dirname,'../web/i18n/i18n.js'),'utf8'), {window,document,URL,queueMicrotask:()=>{},navigator:{languages:['en']}});
module.exports=window.I18n;
