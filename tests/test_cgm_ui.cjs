const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
class Element {
    constructor(tag) { this.tag = tag; this.children = []; this.value = ''; this.disabled = false; this.style = {}; this.listeners = {}; }
    appendChild(child) { this.children.push(child); child.parentNode = this; }
    insertBefore(child, next) { this.children.splice(this.children.indexOf(next), 0, child); child.parentNode = this; }
    replaceChildren() { this.children = []; }
    add(option) { this.appendChild(option); }
    setAttribute() {}
    addEventListener(event, callback) { this.listeners[event] = callback; }
    remove() { this.parentNode.children = this.parentNode.children.filter(child => child !== this); }
}
class Option extends Element {
    constructor(text, value) { super('option'); this.textContent = text; this.value = value; }
}
const nodes = {};
const context = { Option, document: {
    createElement: tag => new Element(tag),
    getElementById: id => nodes[id] ||= new Element('div')
}};
vm.createContext(context);
const source = fs.readFileSync('data/www/index.html', 'utf8');
vm.runInContext(source.slice(source.indexOf('// Provider codes match'), source.indexOf('async function loadConfig')), context);
const cgmRows = () => vm.runInContext('cgmRows', context); // script-scoped const
const rows = () => nodes['cgm-account-rows'].children;
const legend = row => row.children[0].textContent;
const button = (row, text) => row.children.find(c => c.textContent === text);

// Mixed providers load into their own rows; secrets are never echoed.
context.addCgmAccount({slot: 0, provider: 1, label: 'SAM', username: 'sam', has_password: true, us: false});
context.addCgmAccount({slot: 3, provider: 4, label: '<img>', url: 'https://ns.example', has_token: true});
context.addCgmAccount({slot: 1, provider: 3, username: 'a@example.com', has_password: true,
    patient_id: 'p2', patient_name: 'Gone', patients: [{id: 'p1', name: 'Pat'}]});
assert.deepEqual(rows().map(legend), ['Person 1', 'Person 2', 'Person 4']); // slot order
let accounts = context.collectCgmAccounts();
assert.deepEqual(Array.from(accounts, a => [a.slot, a.provider]), [[0, 1], [1, 3], [3, 4]]);
assert.equal(accounts[0].password, '');
assert.equal(accounts[0].us, false);
assert.equal(accounts[2].label, '<img>'); // data stays a text value
assert.equal(accounts[2].url, 'https://ns.example');
assert.equal(accounts[2].token, '');
assert.equal(accounts[1].patient_id, 'p2'); // a saved person missing from the list stays chosen
const libre = cgmRows().get(1);
assert.deepEqual(libre.patient.children.map(o => o.value), ['', 'p1', 'p2']);
assert.equal(libre.username.caption.textContent, 'LibreLinkUp email');
assert.equal(libre.us.parentNode.style.display, 'none');
assert.equal(libre.patient.parentNode.style.display, 'block');

// Saved secrets are offered only while the provider is unchanged.
const ns = cgmRows().get(3);
assert.equal(ns.token.placeholder, 'Saved — leave blank to keep');
ns.provider.value = '0'; ns.update();
assert.equal(ns.token.placeholder, '');
assert.equal(ns.url.caption.textContent, 'Server URL');
ns.provider.value = '4'; ns.update();

// Person 1 cannot be removed; others can, and a removal never renumbers.
assert.equal(button(rows()[0], 'Remove person'), undefined);
context.addCgmAccount({provider: 1});
assert.equal(nodes['add-cgm-account'].disabled, true);
assert.deepEqual(rows().map(legend), ['Person 1', 'Person 2', 'Person 3', 'Person 4']);
context.addCgmAccount();
assert.equal(context.collectCgmAccounts().length, 4);
button(rows()[1], 'Remove person').onclick();
assert.equal(nodes['add-cgm-account'].disabled, false);
accounts = context.collectCgmAccounts();
assert.deepEqual(Array.from(accounts, a => a.slot), [0, 2, 3]);
assert.equal(accounts[2].url, 'https://ns.example');

// A Libre test refreshes the person list only for an unchanged login.
context.addCgmAccount({slot: 1, provider: 3, username: 'a@example.com', has_password: true});
const tested = {slot: 1, username: 'a@example.com', patient_id: '', patients: [{id: 'p1', name: 'Pat'}, {id: 'p3', name: 'Lee'}]};
context.applyTestedAccount({account_config: [tested]});
assert.deepEqual(cgmRows().get(1).patient.children.map(o => o.value), ['', 'p1', 'p3']);
cgmRows().get(1).username.value = 'b@example.com';
context.applyTestedAccount({account_config: [{...tested, patients: []}]});
assert.deepEqual(cgmRows().get(1).patient.children.map(o => o.value), ['', 'p1', 'p3']);
