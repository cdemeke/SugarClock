const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
class Element {
    constructor(tag) { this.tag = tag; this.children = []; this.value = ''; this.disabled = false; }
    appendChild(child) { this.children.push(child); child.parent = this; }
    setAttribute() {}
    addEventListener(event, callback) { this[event] = callback; }
    remove() { this.parent.children = this.parent.children.filter(child => child !== this); }
}
const nodes = {};
const context = { document: {
    createElement: tag => new Element(tag),
    getElementById: id => nodes[id] ||= new Element('div')
}};
vm.createContext(context);
const source = fs.readFileSync('data/www/index.html', 'utf8');
vm.runInContext(source.slice(source.indexOf('const dexcomRows'), source.indexOf('async function loadConfig')), context);
context.addDexcomAccount({slot: 1, label: 'SAM', username: 'sam', has_password: true, us: false});
context.addDexcomAccount({slot: 3, label: '<img>', username: 'pat', has_password: true, us: true});
let accounts = context.collectDexcomAccounts();
assert.deepEqual(Array.from(accounts, a => a.slot), [1, 3]);
assert.equal(accounts[0].password, '');
assert.equal(accounts[1].label, '<img>'); // data stays a text value
context.addDexcomAccount();
assert.equal(nodes['add-dexcom-account'].disabled, true);
assert.deepEqual(Array.from(context.collectDexcomAccounts(), a => a.slot), [1, 3, 2]);
context.addDexcomAccount();
assert.equal(context.collectDexcomAccounts().length, 3);
const rows = nodes['extra-dexcom-rows'].children;
rows[0].children.find(c => c.textContent === 'Remove account').onclick();
assert.equal(nodes['add-dexcom-account'].disabled, false);
accounts = context.collectDexcomAccounts();
assert.equal(accounts[0].slot, 3); // a removal never renumbers another person's slot
assert.equal(accounts[0].username, 'pat');
assert.equal(accounts[0].password, '');
