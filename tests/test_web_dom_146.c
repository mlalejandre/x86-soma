/* SOMA 146 native JS/DOM regression suite; standalone against actual interpreter. */
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
static int errors=0, dirties=0;
static void jlog(void *ud,int level,const char*msg) {(void)ud; fprintf(stderr,"JS log [%d]: %s\n",level,msg); if(level>=2)errors++;}
static void dirty(void*ud,int v){(void)ud;(void)v;dirties++;}
static void check(struct wjs *j, const char*label,const char*js,const char*expected) {
    char out[1024]; int rc=wjs_eval_str(j,js,out,sizeof out);
    fprintf(stderr,"CASE %-32s %d [%s]\n",label,rc,out);
    if(rc || strcmp(out,expected)) { fprintf(stderr,"FAIL %s expected [%s]\n",label,expected); exit(1); }
}
static void require(int v,const char*msg) {if(!v){fprintf(stderr,"FAIL %s\n",msg);exit(1);}}
int main(void) {
    struct wdoc *d=wd_new(); require(d!=NULL,"new doc");
    const char*html="<!doctype html><html><body><div id='p'><span id='x'>hello</span><b id='y'>B</b></div></body></html>";
    wd_parse(d,html,(uint32_t)strlen(html));
    struct wnode *p=wd_by_id(d->root,"p"); require(p!=NULL,"p");
    struct wjs_host h={0};h.log=jlog;h.dirty=dirty;h.url="https://example.org";
    struct wjs*j=wjs_new(d,&h);require(j!=NULL && !errors,"create wjs");
    check(j,"replaceChild identity","(function(){var p=document.getElementById('p'), x=document.getElementById('x');return p.replaceChild(x,x)===x && p.firstChild===x && p.lastChild.id==='y';})()","true");
    check(j,"insertBefore identity","(function(){var p=document.getElementById('p'), x=document.getElementById('x');p.insertBefore(x,x);return p.firstChild===x;})()","true");
    check(j,"move existing sibling","(function(){var p=document.getElementById('p'), x=document.getElementById('x'), y=document.getElementById('y'); p.replaceChild(y,x);return p.firstChild===y && !x.isConnected && y.isConnected;})()","true");
    p->flags &= (uint16_t)~WNF_DIRTY;
    check(j,"empty innerHTML","(function(){var p=document.getElementById('p');p.innerHTML='';return p.firstChild===null && p.childNodes.length===0;})()","true");
    require((p->flags&WNF_DIRTY)!=0,"empty innerHTML must invalidate layout");
    check(j,"textContent","(function(){var p=document.getElementById('p');p.textContent='Hola';return p.textContent==='Hola' && p.firstChild.nodeType===3;})()","true");
    p->flags &= (uint16_t)~WNF_DIRTY;
    check(j,"empty textContent","(function(){var p=document.getElementById('p');p.textContent='';return p.textContent==='' && p.firstChild===null;})()","true");
    require((p->flags&WNF_DIRTY)!=0,"empty textContent invalidates layout");
    check(j,"insertAdjacentHTML scripts inert","(function(){var p=document.getElementById('p');window.executed=0;p.insertAdjacentHTML('beforeend','<script>executed=1</script>');return executed;})()","0");
    check(j,"inert stays inert on move","(function(){var p=document.getElementById('p'),s=p.firstChild;document.body.appendChild(s);return executed;})()","0");
    check(j,"dynamic script executes","(function(){var p=document.getElementById('p'),s=document.createElement('script');s.textContent='executed=2';p.appendChild(s);return executed;})()","2");
    check(j,"createElement script with innerHTML executes","(function(){var p=document.getElementById('p'),s=document.createElement('script');s.innerHTML='executed=6';p.appendChild(s);return executed;})()","6");
    check(j,"innerHTML script remains inert","(function(){var p=document.getElementById('p');p.innerHTML='<script>executed=3</script>';document.body.appendChild(p.firstChild);return executed;})()","6");
    check(j,"outerHTML script remains inert","(function(){var p=document.getElementById('p');p.innerHTML='<span id=replace>hello</span>'; document.getElementById('replace').outerHTML='<script>executed=4</script>';document.body.appendChild(p.firstChild);return executed;})()","6");
    check(j,"fragment moves and order","(function(){var p=document.getElementById('p'), a=document.createElement('a'), b=document.createElement('b'), c=document.createElement('i'), f=document.createDocumentFragment();p.innerHTML='';p.appendChild(c);f.appendChild(a);f.appendChild(b);p.replaceChild(f,c);return p.firstChild===a && p.lastChild===b && f.firstChild===null && !c.isConnected;})()","true");
    require(!errors && dirties>0,"no prelude errors, DOM invalidations emitted");
    puts("SOMA 146: 13 pruebas nativas DOM/JS OK");
    wjs_free(j); wd_free(d);return 0;
}
