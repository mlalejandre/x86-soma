/* SOMA 145 - tests of the real generated JS prelude, without a browser. */
'use strict';
const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const header = fs.readFileSync(process.argv[2], 'utf8');
const rows = header.slice(header.indexOf('const char WEB_PRELUDE[] =') + 27).split('\n');
let source = '';
for (const row of rows) {
  const line = row.trim();
  if (line === ';') break;
  if (!line.startsWith('"')) continue;
  source += JSON.parse(line);
}
assert(source.includes('function EventTarget()'));
const until = source.indexOf('  /* ------------------------------------------------------------ URL */');
assert(until > 0);
const context = { console, performance: { now: () => 1234 }, Node: function Node(){} };
vm.createContext(context);
vm.runInContext(source.slice(0, until) + '\n})(this);', context, { timeout: 2000 });
const { Event, EventTarget } = context;
const win = vm.runInContext('this', context);
const test = (name, fn) => { fn(); process.stdout.write('OK ' + name + '\n'); };

test('capture before bubble at target, independent of registration order', () => {
  const t = new EventTarget(), log = [];
  t.addEventListener('click', () => log.push('bubble'));
  t.addEventListener('click', () => log.push('capture'), true);
  t.dispatchEvent(new Event('click'));
  assert.deepStrictEqual(log, ['capture', 'bubble']);
});

test('capture from parent, target capture/bubble, parent bubble', () => {
  const p = new EventTarget(), c = new EventTarget(), log = [];
  p.nodeType = 1; p.parentNode = context.document = new EventTarget();
  context.document.nodeType = 9;
  c.nodeType = 1; c.parentNode = p;
  win.addEventListener('click', () => log.push('win-c'), true);
  p.addEventListener('click', () => log.push('parent-c'), true);
  c.addEventListener('click', () => log.push('target-b'));
  c.addEventListener('click', () => log.push('target-c'), true);
  p.addEventListener('click', () => log.push('parent-b'));
  c.dispatchEvent(new Event('click', { bubbles: true }));
  assert.deepStrictEqual(log, ['win-c','parent-c','target-c','target-b','parent-b']);
});

test('stopImmediatePropagation in onclick suppresses registered listeners', () => {
  const t = new EventTarget(); let ran = false;
  t.onclick = ev => ev.stopImmediatePropagation();
  t.addEventListener('click', () => ran = true);
  t.dispatchEvent(new Event('click')); assert.strictEqual(ran, false);
});

test('removed listener does not fire in same dispatch', () => {
  const t = new EventTarget(), out = [];
  function second() { out.push('second'); }
  t.addEventListener('go', () => { out.push('first'); t.removeEventListener('go', second); });
  t.addEventListener('go', second);
  t.dispatchEvent(new Event('go')); assert.deepStrictEqual(out, ['first']);
});

test('once is removed before recursive call; duplicate registration ignored', () => {
  const t = new EventTarget(); let calls = 0;
  function once() { calls++; if (calls === 1) t.dispatchEvent(new Event('go')); }
  t.addEventListener('go', once, { once: true });
  t.addEventListener('go', once, { once: true });
  t.dispatchEvent(new Event('go')); assert.strictEqual(calls, 1);
});

test('passive listener cannot prevent default, normal listener can', () => {
  const t = new EventTarget();
  function stop(ev) { ev.preventDefault(); }
  t.addEventListener('go', stop, { passive: true });
  assert.strictEqual(t.dispatchEvent(new Event('go', { cancelable: true })), true);
  t.addEventListener('go', stop, false);
  // duplicate by callback/capture does not replace passive option
  assert.strictEqual(t.dispatchEvent(new Event('go', { cancelable: true })), true);
  t.removeEventListener('go', stop);
  t.addEventListener('go', stop);
  assert.strictEqual(t.dispatchEvent(new Event('go', { cancelable: true })), false);
});

test('inline return false cancels cancelable default', () => {
  const t = new EventTarget(); t.onclick = () => false;
  assert.strictEqual(t.dispatchEvent(new Event('click', { cancelable: true })), false);
  assert.strictEqual(t.dispatchEvent(new Event('click', { cancelable: false })), true);
});

test('aborted signal blocks registration; abort removes listener', () => {
  const signal = new EventTarget(); signal.aborted = true;
  const t = new EventTarget(); let n = 0;
  t.addEventListener('go', () => n++, { signal });
  t.dispatchEvent(new Event('go')); assert.strictEqual(n, 0);
  signal.aborted = false;
  t.addEventListener('go', () => n++, { signal });
  signal.dispatchEvent(new Event('abort'));
  t.dispatchEvent(new Event('go')); assert.strictEqual(n, 0);
});

test('event type __proto__ behaves like an ordinary event key', () => {
  const t = new EventTarget(); let n = 0;
  t.addEventListener('__proto__', () => n++);
  t.dispatchEvent(new Event('__proto__'));
  assert.strictEqual(n, 1);
});

test('stopPropagation on target still permits other target listeners', () => {
  const t = new EventTarget(), log = [];
  t.addEventListener('go', ev => { ev.stopPropagation(); log.push('first'); }, true);
  t.addEventListener('go', () => log.push('second'));
  t.dispatchEvent(new Event('go', { bubbles: true }));
  assert.deepStrictEqual(log, ['first', 'second']);
});

test('listener object with handleEvent is invoked with its receiver', () => {
  const t = new EventTarget(); let n = 0;
  const obj = { handleEvent(ev) { if (this === obj && ev.type === 'go') n++; } };
  t.addEventListener('go', obj);
  t.dispatchEvent(new Event('go')); assert.strictEqual(n, 1);
});

test('re-entrant redispatch of the same event throws and cleans up', () => {
  const t = new EventTarget(), e = new Event('go'); let caught = false;
  t.addEventListener('go', () => { try { t.dispatchEvent(e); } catch(err) { caught = true; } });
  t.dispatchEvent(e); assert.strictEqual(caught, true);
  assert.strictEqual(e.currentTarget, null);
  assert.strictEqual(e.eventPhase, 0);
});
console.log('SOMA 145: eventos DOM superados');
