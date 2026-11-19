/* SOMA_188: pruebas en el anfitrion del parche 188 (navegador profesional): Proxy/Reflect, with, funciones con nombre,
 * bloques static, eval directo, Error.stack, pila de corrutinas, on*, arrays genericos y DOM nuevo (comentarios en el
 * arbol, TreeWalker/NodeIterator vivos, shadow DOM, custom elements, CSSStyleSheet construible, <template>.content).
 * La maquetacion (pseudo-elementos, ::placeholder, grid por columnas, controles, innerText, getComputedStyle) se
 * comprueba en QEMU y con el motor en el anfitrion frente a Chrome (ver docs/PARCHE_188_NAVEGADOR_PROFESIONAL.md). */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_js.h"

void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_now = 10000;
uint64_t web_now_ms(void) { return g_now; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
int g_jsco_off;
extern int g_ju16_off, g_jbin_off, g_jstream_off;
static void jlog(void *ud, int level, const char *msg) { (void)ud; (void)level; (void)msg; }
static void dirty(void *ud, int v) { (void)ud; (void)v; }

struct tc { const char *grp, *src, *post, *exp; };
static const struct tc CASES[] = {
    { "on*", "('oninput' in document)+','+('onclick' in document.createElement('div'))+','+('onload' in window)", 0, "true,true,true" },
    { "on*", "var d=document.createElement('div'); d.setAttribute('oninput','return;'); typeof d.oninput", 0, "function" },
    { "on*", "var d=document.createElement('div'); d.onclick === null", 0, "true" },
    { "on*", "var n=0; var d=document.createElement('div'); d.onclick=function(){n++}; d.click(); d.onclick=null; d.click(); n", 0, "1" },
    { "on*", "var n=0; var d=document.createElement('div'); d.setAttribute('onclick','window.zz=(window.zz||0)+1'); d.click(); window.zz", 0, "1" },
    { "on*", "var d=document.createElement('div'); d.onfoo = 5; d.onfoo", 0, "5" },
    { "with", "var o={a:1,b:2}; var r; with(o){ r=a+b; a=10 } r+','+o.a", 0, "3,10" },
    { "with", "var x=5; var o={y:1}; with(o){ x=x+y } x", 0, "6" },
    { "with", "var o={p:1}; var f; with(o){ f=function(){return p} } o.p=7; f()", 0, "7" },
    { "with", "var t=function(obj){var __p='';with(obj||{}){__p+='<li>'+title+'</li>'}return __p}; t({title:'hola'})", 0, "<li>hola</li>" },
    { "with", "(function(){ var v=3; with({}) { v++ } return v })()", 0, "4" },
    { "Proxy", "var p=new Proxy({a:1},{get:function(t,k){return k in t?t[k]:'def'}}); p.a+','+p.zz", 0, "1,def" },
    { "Proxy", "var log=[]; var p=new Proxy({},{set:function(t,k,v){log.push(k+'='+v); t[k]=v*2; return true}}); p.x=3; log.join()+','+p.x", 0, "x=3,6" },
    { "Proxy", "var p=new Proxy({a:1,b:2},{ownKeys:function(t){return ['b','a']}}); Object.keys(p).join()", 0, "b,a" },
    { "Proxy", "var p=new Proxy({a:1},{has:function(t,k){return k==='zz'}}); ('zz' in p)+','+('a' in p)", 0, "true,false" },
    { "Proxy", "var p=new Proxy({a:1},{deleteProperty:function(t,k){delete t[k]; return true}}); delete p.a; JSON.stringify(p)", 0, "{}" },
    { "Proxy", "var p=new Proxy(function(a,b){return a+b},{apply:function(t,th,args){return t.apply(th,args)*10}}); p(1,2)+','+typeof p", 0, "30,function" },
    { "Proxy", "function C(x){this.x=x} var P=new Proxy(C,{}); var o=new P(5); o.x+','+(o instanceof C)", 0, "5,true" },
    { "Proxy", "Array.isArray(new Proxy([1,2],{}))+','+new Proxy([1,2,3],{}).length", 0, "true,3" },
    { "Proxy", "var r=Proxy.revocable({a:1},{}); r.proxy.a; r.revoke(); var e; try{ r.proxy.a }catch(x){ e=x.constructor.name } e", 0, "TypeError" },
    { "Proxy", "Reflect.ownKeys({a:1,b:2}).join()+','+Reflect.has({x:1},'x')+','+Reflect.get({get g(){return this.v}},'g',{v:9})", 0, "a,b,true,9" },
    { "Proxy", "var o={}; Reflect.defineProperty(o,'k',{value:1,enumerable:false}); Reflect.getOwnPropertyDescriptor(o,'k').enumerable+','+o.k", 0, "false,1" },
    { "Proxy", "var t=[1,2,3]; var p=new Proxy(t,{get:function(t,k,r){ return Reflect.get(t,k,r) }}); var s=0; for (var v of p) s+=v; s+','+p.map(function(x){return x*2}).join()", 0, "6,2,4,6" },
    { "Proxy", "var tr=[]; var p=new Proxy({a:{b:1}},{get:function(t,k){tr.push(k); return t[k]}}); p.a.b; JSON.stringify(p); tr.join()", 0, "a,toJSON,a" },
    { "Proxy", "var p=new Proxy({},{get:function(t,k){return typeof k==='symbol'?'sym':k}}); p[Symbol.iterator]+','+p.foo", 0, "sym,foo" },
    { "Proxy", "Object.getPrototypeOf(new Proxy([], {}))===Array.prototype", 0, "true" },
    { "Proxy", "var p=new Proxy({x:1},{getOwnPropertyDescriptor:function(t,k){return {value:t[k],enumerable:true,configurable:true}}}); Object.getOwnPropertyDescriptor(p,'x').value", 0, "1" },
    { "arrays genericos", "var o={length:0}; Array.prototype.push.call(o,'a','b'); o.length+','+o[1]", 0, "2,b" },
    { "arrays genericos", "var o={0:'a',1:'b',2:'c',length:3}; var r=Array.prototype.splice.call(o,1,1,'x','y'); r.join()+'|'+o.length+','+o[0]+o[1]+o[2]+o[3]", 0, "b|4,axyc" },
    { "arrays genericos", "var o={0:'a',1:'b',length:2}; Array.prototype.shift.call(o)+','+o.length+','+o[0]", 0, "a,1,b" },
    { "arrays genericos", "var o={0:'a',length:1}; Array.prototype.unshift.call(o,'z')+','+o[0]+o[1]", 0, "2,za" },
    { "arrays genericos", "var o={0:'a',1:'b',length:2}; Array.prototype.pop.call(o)+','+o.length+','+(1 in o)", 0, "b,1,false" },
    { "arrays genericos", "var p=new Proxy([1,2],{}); p.push(3); p.length+','+p[2]", 0, "3,3" },
    { "arrays genericos", "var a=[1,2,3]; a.splice(1,1); a.join()", 0, "1,3" },
    { "func. con nombre", "var e={}; var f=function e(n){return n?e(n-1)+1:0}; f(3)", 0, "3" },
    { "func. con nombre", "(function(e){ e.x=function e(n){return n?e(n-1)+1:0}; return e.x(2) })({})", 0, "2" },
    { "func. con nombre", "var g=function h(){ return typeof h }; g()+','+typeof h", 0, "function,undefined" },
    { "func. con nombre", "(function(e){ var r=function e(t){ if (t) return 1; return e(1) }; return r(0) })(5)", 0, "1" },
    { "objetos", "var ek=Object.assign({menuitem:!0},{area:!0,base:!0,br:!0,col:!0,embed:!0,hr:!0,img:!0,input:!0,keygen:!0,link:!0,meta:!0,param:!0,source:!0,track:!0,wbr:!0}); String(ek['a'])+','+String(ek['div'])+','+String(ek.br)", 0, "undefined,undefined,true" },
    { "objetos", "var o={}; for (var i=0;i<20;i++) o['k'+i]=i; String(o['a'])+','+o.k7", 0, "undefined,7" },
    { "Reflect.set", "var t=Object.defineProperty({},'a',{configurable:true}); Reflect.set(Object.getPrototypeOf(t),'a',1,t)+','+('a' in {})", 0, "false,false" },
    { "Reflect.set", "var r={}; Reflect.set({},'x',5,r)+','+r.x", 0, "true,5" },
    { "Reflect.set", "var o={set v(x){this.w=x}}; var r={}; Reflect.set(o,'v',3,r); r.w+','+o.w", 0, "3,undefined" },
    { "eval/import()", "typeof eval", 0, "function" },
    { "eval/import()", "(0,eval)('(function(a){return a*2})')(21)", 0, "42" },
    { "eval/import()", "var r; try { import('./x.js'); r='ok' } catch(e) { r=e.constructor.name } r", 0, "ok" },
    { "bloques static", "class A { static { this.s = 5; } } A.s", 0, "5" },
    { "bloques static", "let B = class extends Object { static { this.styles = [1,2]; } }; B.styles.length", 0, "2" },
    { "bloques static", "class C { static x = 1; static { this.y = this.x + 1; } static { this.z = this.y * 10; } } C.z", 0, "20" },
    { "bloques static", "class D { static #p = 3; static { D.q = D.#p; } } D.q", 0, "3" },
    { "bloques static", "var r=[]; class E { static { r.push(typeof this, this.name); } } r.join()", 0, "function,E" },
    { "pila async", "var r; function f(n){ return n ? 1 + f(n-1) : 0 } async function g(){ try { return f(200) } catch(e) { return 'E' } } g().then(function(v){ r = v })", "r", "200" },
    { "pila async", "var r; function f(n){ return n ? 1 + f(n-1) : 0 } async function g(){ await 0; try { return f(390) } catch(e) { return 'E' } } g().then(function(v){ r = v })", "r", "390" },
    { "pila async", "var r; function f(n){ var o={a:[n]}; return n ? [1].map(function(){ return f(n-1) })[0] + 1 : 0 } async function g(){ await 0; try { return f(190) } catch(e) { return 'E:'+e.message } } g().then(function(v){ r = v })", "r", "190" },
    { "Reflect.set+Proxy", "var t={a:1}; var p=new Proxy(t,{set:function(s,k,v,r){return Reflect.set(s,k,v,r)}}); p.a=5; p.b=7; [t.a,t.b,Object.keys(t).join()].join()", 0, "5,7,a,b" },
    { "Reflect.set+Proxy", "var log=[]; var t={}; var p=new Proxy(t,{set:function(s,k,v,r){return Reflect.set(s,k,v,r)},defineProperty:function(s,k,d){log.push(k+':'+d.value+':'+!!d.writable);return Reflect.defineProperty(s,k,d)}}); p.x=1; p.x=2; log.join()", 0, "x:1:true,x:2:false" },
    { "Reflect.set+Proxy", "var a=[1,2]; var p=new Proxy(a,{set:function(s,k,v,r){return Reflect.set(s,k,v,r)}}); p.push(3); p[0]=9; [a.join(),a.length].join('|')", 0, "9,2,3|3" },
    { "Reflect.set+Proxy", "var o={set v(x){this._v=x*2}}; var p=new Proxy(o,{}); Reflect.set(o,'v',4,p); o._v", 0, "8" },
    { "Error.stack", "function foo(){ return new Error('x').stack } function bar(){ return foo() } bar().split('\\n').slice(1,3).map(function(s){return s.trim().split(' ')[1]}).join()", 0, "foo,bar" },
    { "Error.stack", "var s; try { null.x } catch(e) { s = e.stack.split('\\n')[0] } s", 0, "TypeError: No se pueden leer propiedades de null (leyendo 'x')" },
    { "Error.stack", "typeof new TypeError('m').stack", 0, "string" },
    { "eval directo (enkoder)", "var document={write:function(s){W.push(s)}}; var W=[]; function hivelogic_enkoder(){var kode=                                 \"kode=\\\"oked\\\\\\\"=kode\\\\\\\"\\\\\\\\k=do\\\\\\\\e\\\\\\\\\\\\\\\"d\\\\\\\\c=monu.erttw\\\\\\\\ie\\\\\\\\\\\\\"+                                 \"\\\\\\\\\\\\\\\\\\\"\\\\\\\\\\\\\\\\ah\\\\\\\\e(=<\\\\\\\\ r\\\\\\\\\\\\\\\\f\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\"+                                 \"\\\"\\\\\\\\\\\\\\\\a\\\\\\\\\\\\\\\\lo\\\\\\\\amiist@:ol.soa\\\\\\\\vx\\\\\\\\\\\\\\\\c\\\\\\\\\\\\\\\\m\\\\\\\\\\\\\\\\\\\\\"+                                 \"\\\\\\\\\\\\\\\\\\\\\\\\\\\\ \\\\\\\\\\\\\\\\i\\\\\\\\\\\\\\\\l=\\\\\\\"\\\\\\\\tt\\\\\\\\\\\\\\\\e\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\"+                                 \"\\\\\\\\\\\\\\\\\\\\\\\"\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\"+                                 \"\\\"\\\\\\\\\\\\\\\\a\\\\\\\\\\\\\\\\is\\\\\\\\@>ol.soa<vax\\\\\\\\cm\\\\\\\\\\\\\\\\/\\\\\\\\\\\\\\\\>\\\\\\\\\\\\\\\\);\\\\\"+                                 \"\\\"\\\\\\\\;\\\\\\\\\\\\\\\"x\\\\\\\\'=;'of(r=i;0<ik(do.eelgnht1-;)+i2={)+xk=do.ehcratAi(1+\"+                                 \"+)okedc.ahAr(t)ik}do=e+xi(k<do.eelgnhtk?do.ehcratAk(do.eelgnht1-:)''\\\\\\\\);\"+                                 \"\\\\\\\";x='';for(i=0;i<(kode.length-1);i+=2){x+=kode.charAt(i+1)+kode.charAt(\"+                                 \"i)}kode=x+(i<kode.length?kode.charAt(kode.length-1):'');;\\\\\\\"=x''f;roi(0=i\"+                                 \";(<okedl.netg-h)1i;=+)2x{=+okedc.ahAr(t+i)1k+do.ehcratAi(})okedx=(+<iokedl\"+                                 \".netg?hokedc.ahAr(tokedl.netg-h)1':)';\\\";x='';for(i=0;i<(kode.length-1);i+\"+                                 \"=2){x+=kode.charAt(i+1)+kode.charAt(i)}kode=x+(i<kode.length?kode.charAt(k\"+                                 \"ode.length-1):'');\"                                 ;var i,c,x;var n=0; var outs=[]; while(n<30 && eval(kode)){ n++; outs.push(kode.length); } return [n, outs.join(\",\"), String(kode).slice(0,80)].join(\" | \");} (function(){ var r=hivelogic_enkoder(); return r + \" W=\" + W.join(\"\").slice(0,60) })()", 0, "4 | 559,380,225,82 | document.write(\"<a href=\\\"mailto:alissa@vox.com\\\" title=\\\"\\\">alissa@vox.com</a>\" W=<a href=\"mailto:alissa@vox.com\" title=\"\">alissa@vox.com</a>" },
    { "eval directo", "function f(){ var a=1; eval('a=2'); return a } f()", 0, "2" },
    { "eval directo", "function f(){ var a=1; return eval('a+10') } f()", 0, "11" },
    { "eval directo", "function f(){ eval('var b=5'); return typeof b } f()", 0, "number" },
    { "eval directo", "var a=1; function f(){ var a=2; return (0,eval)('a') } f()", 0, "1" },
    { "eval directo", "function f(x){ return eval('x*3') } f(4)", 0, "12" },
    { "eval directo", "function f(){ var e=eval; var a=7; return typeof e('a') } var a='g'; f()", 0, "string" },
    { "eval directo", "function f(){ let q=3; return eval('q') } f()", 0, "3" },
    { "DOM/web components", "var d=document.createElement('div'); d.innerHTML='<b></b><!--c--><!--?lit$1$-->x'; d.childNodes.length+'|'+d.childNodes[1].nodeType+'|'+d.childNodes[2].data+'|'+d.innerHTML", 0, "4|8|?lit$1$|<b></b><!--c--><!--?lit$1$-->x" },
    { "DOM/web components", "var t=document.createElement('template'); t.innerHTML='<div a$lit$=\"x\"><span></span><!--m--></div><p></p>'; var w=document.createTreeWalker(document,129); w.currentNode=t.content; var r=[],o; while((o=w.nextNode())!==null) r.push(o.nodeName); r.join()", 0, "DIV,SPAN,#comment,P" },
    { "DOM/web components", "var d=document.createElement('div'); d.innerHTML='<a><b></b></a><c></c>'; var w=document.createTreeWalker(d,1,{acceptNode:function(n){return n.nodeName==='A'?3:1}}); var r=[]; while(w.nextNode()) r.push(w.currentNode.nodeName); r.join()", 0, "B,C" },
    { "DOM/web components", "var d=document.createElement('div'); d.innerHTML='<i></i><u></u>'; var it=document.createNodeIterator(d,1); var r=[],n; while((n=it.nextNode())) r.push(n.nodeName); r.join()+'|'+it.previousNode().nodeName", 0, "DIV,I,U|U" },
    { "DOM/web components", "var h=document.createElement('div'); var sr=h.attachShadow({mode:'open'}); sr.innerHTML='<h3 id=\"x\">t</h3>'; [sr.host===h, h.shadowRoot===sr, sr.firstChild.id, sr.firstChild.textContent, sr.firstChild.getRootNode()===sr, sr instanceof ShadowRoot, sr.mode, sr.firstChild.parentNode===sr, sr.parentNode].join()", 0, "true,true,x,t,true,true,open,true," },
    { "DOM/web components", "var h=document.createElement('div'); var sr=h.attachShadow({mode:'closed'}); [h.shadowRoot, sr.mode].join()", 0, ",closed" },
    { "DOM/web components", "var log=[]; class XE extends HTMLElement { static get observedAttributes(){return ['v']} constructor(){super(); log.push('c')} connectedCallback(){log.push('in')} disconnectedCallback(){log.push('out')} attributeChangedCallback(n,o,v){log.push(n+'='+v)} } customElements.define('x-e',XE); var e=document.createElement('x-e'); e.setAttribute('v','1'); document.body.appendChild(e); e.remove(); [log.join(), e instanceof XE, customElements.get('x-e')===XE, customElements.getName(XE)].join('|')", 0, "c,v=1,in,out|true|true|x-e" },
    { "DOM/web components", "var e=document.createElement('y-e'); document.body.appendChild(e); var up=0; customElements.define('y-e', class extends HTMLElement { connectedCallback(){ up++ } }); customElements.upgrade(e); up", 0, "1" },
    { "DOM/web components", "var r; customElements.whenDefined('z-e').then(function(c){ r=typeof c }); customElements.define('z-e', class extends HTMLElement {})", "r", "function" },
    { "DOM/web components", "var ok; try { customElements.define('nodash', class extends HTMLElement {}) } catch(e) { ok=e.name } ok", 0, "SyntaxError" },
    { "DOM/web components", "var s=new CSSStyleSheet(); s.replaceSync('a{color:red} b{color:blue}'); var h=document.createElement('div'); var sr=h.attachShadow({mode:'open'}); sr.adoptedStyleSheets=[s]; sr.adoptedStyleSheets.push(new CSSStyleSheet()); [s.cssRules.length, s.cssRules[1].selectorText, sr.adoptedStyleSheets.length, 'adoptedStyleSheets' in Document.prototype].join()", 0, "2,b,2,true" },
    { "DOM/web components", "var s=new CSSStyleSheet(); s.insertRule('p{x:1}'); s.insertRule('q{y:2}',1); s.deleteRule(0); s.cssRules[0].cssText", 0, "q{y:2}" },
    { "DOM/web components", "var t=document.createElement('template'); t.innerHTML='<p>uno</p><p>dos</p>'; var c=t.content; [c.nodeType, c.childNodes.length, t.childNodes.length, document.importNode(c,true).childNodes.length].join()", 0, "11,2,0,2" },
    { "DOM/web components", "var f=document.createDocumentFragment(); f.append('a', document.createElement('b')); f.prepend('z'); f.childNodes.length+'|'+f.firstChild.data", 0, "3|z" },
    { "DOM/web components", "var d=document.createElement('div'); var c=document.createElement('span'); d.appendChild(c); c.getRootNode()===d", 0, "true" },
    { "DOM/web components", "typeof NodeFilter+NodeFilter.SHOW_ELEMENT+NodeFilter.FILTER_SKIP", 0, "object13" },
    { "plantillas/eventos", "var x=`a${(b=1)=>b}c${`d${2}`}`; x", 0, "a(b=1)=>bcd2" },
    { "plantillas/eventos", "function f(s){return s} function g(){ return f`a${1}b` } g()===g()", 0, "true" },
    { "plantillas/eventos", "function f(s){return s} var a=f`x`, b=f`x`; a===b", 0, "false" },
    { "plantillas/eventos", "function f(s){return s} var s=(function(){ return f`q${2}w` })(); [Object.isFrozen(s), s.raw.join('|'), s.length].join()", 0, "true,q|w,2" },
    { "plantillas/eventos", "var log=[]; var h=document.createElement('x-h'); document.body.appendChild(h); var sr=h.attachShadow({mode:'closed'}); sr.innerHTML='<div><button>b</button></div>'; var b=sr.firstChild.firstChild; document.addEventListener('ping', function(e){ log.push('doc:'+e.target.tagName+':'+e.composedPath().length) }); h.addEventListener('ping', function(e){ log.push('host:'+e.target.tagName) }); sr.firstChild.addEventListener('ping', function(e){ log.push('in:'+e.target.tagName) }); b.dispatchEvent(new Event('ping',{bubbles:true,composed:true})); b.dispatchEvent(new Event('ping',{bubbles:true})); log.join(' ')", 0, "in:BUTTON host:X-H doc:X-H:5 in:BUTTON" },
    { "plantillas/eventos", "var d=document.createElement('div'); d.text='hola'; [d.text, d.textContent, document.createElement('a').text===''].join()", 0, "hola,,true" },
    { "plantillas/eventos", "var d=document.createElement('div'); d.innerHTML='a<?lit$7$>b<!x>'; d.childNodes.length+'|'+d.childNodes[1].data+'|'+d.childNodes[3].data", 0, "4|?lit$7$|x" },
    { "FormData/inert/template", "var f=document.createElement('form'); f.innerHTML='<input type=hidden name=a value=1><button name=x value=y>go</button>'; var b=f.lastChild; Array.from(new FormData(f,b).entries()).join(';')", 0, "a,1;x,y" },
    { "FormData/inert/template", "var d=document.createElement('div'); d.inert=true; [d.inert, d.hasAttribute('inert')].join()", 0, "true,true" },
    { "FormData/inert/template", "var t=document.createElement('template'); t.innerHTML='<p>x</p>'; var p=t.content.firstChild; [p.ownerDocument===document, typeof p.ownerDocument.createElement, p.ownerDocument.createElement('i').tagName, document.createElement('i').ownerDocument===document].join()", 0, "false,function,I,true" },
    /* SOMA 188B */
    { "this no estricto", "(function(){return this===globalThis})()", 0, "true" },
    { "this no estricto", "(function(){'use strict';return this===undefined})()", 0, "true" },
    { "this no estricto", "(function(){return (function(){return typeof this})()})()", 0, "object" },
    { "this no estricto", "(function(){'use strict';return (function(){return typeof this})()})()", 0, "undefined" },
    { "this no estricto", "var o={f:function(){return this===o}}; o.f()", 0, "true" },
    { "this no estricto", "(function(){return [1].map(function(){return this===globalThis})[0]})()", 0, "true" },
    { "this no estricto", "class K0{m(){return (function(){return this})()}}; new K0().m()===undefined", 0, "true" },
    { "this no estricto", "(function(){return function(){return this}.call(null)===globalThis})()", 0, "true" },
    { "this no estricto", "(function(){return function(){return this}.call(5)})()", 0, "5" },
    { "this no estricto", "(()=>{'use strict';return (()=>typeof this)()})()", 0, "object" },
    { "huecos de array", "Array(3).concat('a','b').map(function(x){return x.length}).join()", 0, ",,,1,1" },
    { "huecos de array", "[...Array(3)].map(function(_,i){return i}).join()", 0, "0,1,2" },
    { "huecos de array", "Array.from(Array(2)).map(function(_,i){return i}).join()", 0, "0,1" },
    { "huecos de array", "Array.apply(null,Array(2)).map(function(_,i){return i}).join()", 0, "0,1" },
    { "huecos de array", "Object.keys(Array(3)).length+':'+(1 in Array(3))+':'+Array(3).length", 0, "0:false:3" },
    { "huecos de array", "var a1=[1,2,3]; delete a1[1]; var c1=0; a1.forEach(function(){c1++}); c1+':'+(1 in a1)+':'+a1[1]+':'+a1.length", 0, "2:false:undefined:3" },
    { "huecos de array", "var b1=[]; b1[3]=1; Object.keys(b1).join()+':'+b1.indexOf(undefined)+':'+JSON.stringify(b1)", 0, "3:-1:[null,null,null,1]" },
    { "huecos de array", "new Array(3).fill(0).map(function(x){return x+1}).join()", 0, "1,1,1" },
    { "huecos de array", "[1,,3].length+':'+(1 in [1,,3])", 0, "3:false" },
    { "huecos de array", "var m1=new Map; m1.set(Array(1)[0],5); m1.get(undefined)", 0, "5" },
    { "huecos de array", "Array(3).join('-')+':'+[undefined,undefined].map(function(){return 1}).join()", 0, "--:1,1" },
    { "huecos de array", "Array(3).reduce(function(a,b){return a+1},0)+':'+Array(2).some(function(){return true})+':'+Array(2).every(function(){return false})", 0, "0:false:true" },
    { "Function.toString", "String(function(q,C){this.a=q})", 0, "function(q,C){this.a=q}" },
    { "Function.toString", "function g1(a,b){return a} String(g1)", 0, "function g1(a,b){return a}" },
    { "Function.toString", "String((a,b)=>a)", 0, "(a,b)=>a" },
    { "Function.toString", "String({m(a){return a}}.m)", 0, "m(a){return a}" },
    { "Function.toString", "class K1{constructor(a){this.a=a} foo(x){return x}} String(K1)", 0, "class K1{constructor(a){this.a=a} foo(x){return x}}" },
    { "Function.toString", "class K2{foo(x){return x}} String(K2.prototype.foo)", 0, "foo(x){return x}" },
    { "Function.toString", "String(async function(a){})", 0, "async function(a){}" },
    { "BigInt", "function* g2(a){} String(g2)", 0, "function* g2(a){}" },
    { "Function.toString", "String(function   named ( a ) { })", 0, "function   named ( a ) { }" },
    { "Function.toString", "/^function\\s*\\(([^)]*)\\)/.exec(String(function(alpha, beta){}))[1]", 0, "alpha, beta" },
    { "BigInt", "typeof BigInt(5)", 0, "bigint" },
    { "BigInt", "String(BigInt(Number.MAX_SAFE_INTEGER)+BigInt(2))", 0, "9007199254740993" },
    { "BigInt", "String(BigInt(1)<<BigInt(64))", 0, "18446744073709551616" },
    { "BigInt", "String(BigInt.asUintN(64, BigInt(-1)))", 0, "18446744073709551615" },
    { "BigInt", "String(BigInt.asIntN(64, BigInt('18446744073709551615')))", 0, "-1" },
    { "BigInt", "String(BigInt(4294967295)<<BigInt(32)|BigInt(7))", 0, "18446744069414584327" },
    { "BigInt", "String(123n*2n)+':'+String(-7n/2n)+':'+String(-7n%2n)+':'+String(7n>>1n)+':'+String(~5n)+':'+String(-5n)", 0, "246:-3:-1:3:-6:-5" },
    { "BigInt", "[5n==5, 5n===5n, 5n===5, 2n>1, 1<2n, 2n>=2n, 0n?'t':'f', 1n?'t':'f', 'a'+5n, `${5n}`].join()", 0, "true,true,false,true,true,true,f,t,a5,5" },
    { "BigInt", "(function(){try{return 5n+1}catch(e){return e.name}})()", 0, "TypeError" },
    { "BigInt", "Number(2n**53n)", 0, "9007199254740992" },
    { "BigInt", "(function(){try{return BigInt(1.5)}catch(e){return e.name}})()", 0, "RangeError" },
    { "BigInt", "(function(){try{return BigInt('x')}catch(e){return e.name}})()", 0, "SyntaxError" },
    { "BigInt", "(255n).toString(16)+':'+String(BigInt('0xff'))", 0, "ff:255" },
    { "BigInt", "(function(){var x=5n; x++; x+=2n; return String(x)})()", 0, "8" },
    { "BigInt", "String(BigInt(2)**BigInt(100))", 0, "1267650600228229401496703205376" },
    { "BigInt", "String(BigInt.asUintN(32, 0x1ffffffffn))+':'+String(0x10n+0b11n)", 0, "4294967295:19" },
    { "DOM/188B", "new RegExp('('.repeat(100)+'a'+')'.repeat(100)).exec('a').length", 0, "101" },
    { "DOM/188B", "typeof Window+':'+(typeof Window.prototype.addEventListener)+':'+typeof CDATASection+':'+typeof ProcessingInstruction+':'+typeof Range+':'+typeof DOMRect", 0, "function:function:function:function:function:function" },
    { "DOM/188B", "var f1=document.createDocumentFragment(); f1.appendChild(document.createElement('p')); f1.appendChild(document.createTextNode('x')); f1.children.length+':'+f1.firstElementChild.tagName+':'+f1.childElementCount", 0, "1:P:1" },
    { "DOM/188B", "var d1=document.createElement('div'); d1.setAttribute('data-a','1'); d1.id='q'; var at=d1.attributes; [at.length, at.getNamedItem('id').value, at['data-a'].value, at.item(0).name, at[0].ownerElement===d1].join()", 0, "2,q,1,data-a,true" },
    { "DOM/188B", "function P1(){} P1.prototype=Object.create(HTMLElement.prototype); P1.prototype.async=function(){return 7}; typeof P1.prototype.async+':'+P1.prototype.async()", 0, "function:7" },
    { "DOM/188B", "var s1=document.createElement('script'); s1.async=true; s1.async+':'+s1.hasAttribute('async')", 0, "true:true" },
    { "DOM/188B", "new DOMRect(1,2,3,4).right+':'+new AggregateError([1,2],'m').errors.length+':'+(new AggregateError([],'m') instanceof Error)", 0, "4:2:true" },
    { "DOM/188B", "var L1='http://h/'+'a'.repeat(5000)+'/x'; new URL('y',L1).href.length", 0, "5011" },
};
int main(void)
{
    const char *html = "<!doctype html><html><body><div id='p'></div></body></html>";
    struct wjs_host h = {0}; h.log = jlog; h.dirty = dirty; h.url = "https://example.org";
    int n = (int)(sizeof CASES / sizeof CASES[0]), fail = 0;
    g_ju16_off = 0; g_jbin_off = 0; g_jstream_off = 0;
    g_jsco_off = getenv("JSCO_OFF") != 0;   /* con ASan las pilas de corrutina dan falsos positivos (sigaltstack) */
    for (int i = 0; i < n; i++) {
        struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
        struct wjs *j = wjs_new(d, &h); char out[2048] = "";
        int rc = wjs_eval_str(j, CASES[i].src, out, sizeof out);
        if (CASES[i].post && rc == 0) {
            for (int k = 0; k < 200; k++) { int ran = 0; g_now += 50; int nx = wjs_pump(j, g_now, &ran); if (nx < 0 && !ran) break; }
            rc = wjs_eval_str(j, CASES[i].post, out, sizeof out);
        }
        if (rc != 0 || strcmp(out, CASES[i].exp)) { fail++; printf("FAIL [%s] #%d: %s\n   esperado [%s] obtenido [%s] rc=%d\n", CASES[i].grp, i, CASES[i].src, CASES[i].exp, out, rc); }
        wjs_free(j); wd_free(d);
    }
    if (fail) { printf("test_web_js_188: %d fallos de %d\n", fail, n); return 1; }
    printf("test_web_js_188: %d casos OK (Proxy, eval directo, static, web components, DOM, BigInt, this, huecos)\n", n);
    return 0;
}
