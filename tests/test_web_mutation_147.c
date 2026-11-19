/* SOMA 147 - prueba de MutationObserver con el interprete/DOM real */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_js.h"
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
uint64_t web_now_ms(void) { return 10000; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
static int errors=0, count=0;
static void jlog(void *ud, int level, const char *msg) {
    (void)ud; fprintf(stderr, "JS [%d] %s\n", level, msg); if (level >= 2) errors++;
}
static void dirty(void *ud, int val) { (void)ud; (void)val; }
static void chk(struct wjs *j, const char *name, const char *src, const char *expected) {
    char out[1024]; int rc = wjs_eval_str(j, src, out, sizeof out);
    fprintf(stderr, "CASE %-28s rc=%d got=[%s] expected=[%s]\n", name, rc, out, expected);
    if (rc || strcmp(out, expected)) exit(1);
    count++;
}
int main(void) {
    struct wdoc *doc = wd_new(); if (!doc) return 2;
    const char *html = "<html><body><div id='p'><span id='x'>x</span></div><div id='other'></div></body></html>";
    wd_parse(doc, html, (uint32_t)strlen(html));
    struct wjs_host host = {0}; host.log = jlog; host.dirty = dirty; host.url = "https://example.org";
    struct wjs *j = wjs_new(doc, &host); if (!j || errors) { fprintf(stderr, "Error inicializando JS\n"); return 2; }
    chk(j, "constructor", "typeof MutationObserver", "function");
    chk(j, "options required", "(function(){try{new MutationObserver(function(){}).observe(document.body,{});return false;}catch(e){return e instanceof TypeError;}})()", "true");
    chk(j, "callback required", "(function(){try{new MutationObserver(0);return false;}catch(e){return e instanceof TypeError;}})()", "true");
    chk(j, "childList append async", "var p=document.getElementById('p'), log=[]; var ob=new MutationObserver(function(r){log.push(r[0].type+':'+r[0].addedNodes.length);}); ob.observe(p,{childList:true}); p.appendChild(document.createElement('b')); 'pending'", "pending");
    chk(j, "childList callback", "log.join(',')", "childList:1");
    chk(j, "remove child", "p.removeChild(p.lastChild); log.join(',')", "childList:1");
    chk(j, "no-op replace", "p.replaceChild(p.firstChild,p.firstChild); log.length", "2");
    chk(j, "attr oldValue", "var attrs=[];var ab=new MutationObserver(function(r){attrs.push(r[0].attributeName+':'+r[0].oldValue);});ab.observe(p,{attributes:true,attributeOldValue:true,attributeFilter:['data-x']});p.setAttribute('data-x','A'); p.setAttribute('data-x','B');'ok'", "ok");
    chk(j, "attr callbacks", "attrs.join('|')", "data-x:null");
    chk(j, "attribute filter", "p.setAttribute('title','ignored');attrs.length", "1");
    chk(j, "attr remove", "p.removeAttribute('data-x'); attrs.length", "1");
    chk(j, "attr remove delivered", "attrs.join('|')", "data-x:null|data-x:B");
    chk(j, "subtree", "var z=[],so=new MutationObserver(function(r){z.push(r[0].target.id);});so.observe(document.body,{childList:true,subtree:true}); p.appendChild(document.createElement('i'));'queued'", "queued");
    chk(j, "subtree callback", "z.join(',')", "p");
    chk(j, "takeRecords", "var n=0,t=new MutationObserver(function(r){n++;});t.observe(document.getElementById('other'),{childList:true});var other=document.getElementById('other');other.appendChild(document.createElement('u'));var q=t.takeRecords();q.length", "1");
    chk(j, "takeRecords cleared", "n", "0");
    chk(j, "disconnect", "t.disconnect();other.appendChild(document.createElement('b'));n", "0");
    chk(j, "batching", "var calls=0,lens=[];var bt=new MutationObserver(function(r){calls++;lens.push(r.length);});bt.observe(other,{childList:true});other.appendChild(document.createElement('i'));other.appendChild(document.createElement('em'));'sync'", "sync");
    chk(j, "one delivery", "calls+':'+lens[0]", "1:2");
    chk(j, "fragment adds", "var f=document.createDocumentFragment(); f.appendChild(document.createElement('i')); f.appendChild(document.createElement('em')); other.appendChild(f); calls", "1");
    chk(j, "fragment delivered", "lens.join(',')", "2,1");
    chk(j, "innerHTML descriptor", "typeof Object.getOwnPropertyDescriptor(Element.prototype,'innerHTML').set", "function");
    chk(j, "textContent descriptor", "typeof Object.getOwnPropertyDescriptor(Node.prototype,'textContent').set", "function");
    chk(j, "innerHTML queued", "var el=document.createElement('section'),ml=[];var m2=new MutationObserver(function(rs){ml.push(rs[0].addedNodes.length+':'+rs[0].removedNodes.length)});m2.observe(el,{childList:true});el.innerHTML='<span>ok</span>';'queued'", "queued");
    chk(j, "innerHTML delivered", "ml.join(',')", "1:0");
    chk(j, "textContent queued", "el.textContent='abc';'queued'", "queued");
    chk(j, "textContent delivered", "ml.join(',')", "1:0,1:1");
    chk(j, "characterData queued", "var text=el.firstChild,changes=[];var mch=new MutationObserver(function(rs){changes.push(rs[0].oldValue)});mch.observe(text,{characterData:true,characterDataOldValue:true});text.textContent='abcd';'queued'", "queued");
    chk(j, "characterData delivered", "changes.join(',')", "abc");
    chk(j, "characterData options", "(function(){var ob2=new MutationObserver(function(){});ob2.observe(p,{characterData:true,subtree:true});return true;})()", "true");
    if (errors) { fprintf(stderr, "Errores JS no esperados: %d\n", errors); return 1; }
    fprintf(stderr, "SOMA 147: %d pruebas nativas OK\n", count);
    wjs_free(j); wd_free(doc); return 0;
}
