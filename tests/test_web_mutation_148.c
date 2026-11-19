/* SOMA 148: regresiones de atributos nativos + MutationObserver */
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
static int errors, count;
static void logmsg(void *ud, int level, const char *msg) {
 (void)ud; fprintf(stderr,"JS [%d] %s\n",level,msg); if (level>=2) errors++;
}
static void dirty(void *ud,int a) { (void)ud; (void)a; }
static void chk(struct wjs *j,const char *name,const char *src,const char *want) {
 char out[1024]={0}; int rc=wjs_eval_str(j,src,out,sizeof out);
 fprintf(stderr,"CASE %-31s rc=%d got=[%s] expected=[%s]\n",name,rc,out,want);
 if(rc || strcmp(out,want)) exit(1); count++;
}
int main(void) {
 struct wdoc *d=wd_new(); if(!d) return 2;
 const char *html="<html><body><div id='p'>hola</div></body></html>";
 wd_parse(d,html,(uint32_t)strlen(html));
 struct wjs_host host={0};host.log=logmsg;host.dirty=dirty;host.url="https://example.org";
 struct wjs *j=wjs_new(d,&host); if(!j || errors) return 2;
 chk(j,"MutationObserver disponible","typeof MutationObserver","function");
 chk(j,"bridge no visible a la web","typeof __soma148MutationNotify","undefined");
 chk(j,"observador de atributos","var p=document.getElementById('p'), l=[];var o=new MutationObserver(function(rs){for(var i=0;i<rs.length;i++)l.push(rs[i].attributeName+':'+rs[i].oldValue);});o.observe(p,{attributes:true,attributeOldValue:true});'ok'","ok");
 chk(j,"id property write queued","p.id='nuevo';'pending'","pending");
 chk(j,"id property notified","l.join('|')","id:p");
 chk(j,"className write queued","p.className='uno';'pending'","pending");
 chk(j,"className notified","l.join('|')","id:p|class:null");
 chk(j,"classList add queued","p.classList.add('dos');'pending'","pending");
 chk(j,"classList add notified","l.join('|')","id:p|class:null|class:uno");
 chk(j,"classList no-op queued","p.classList.add('dos');'pending'","pending");
 chk(j,"classList no-op ignored","l.length","3");
 chk(j,"classList remove absent", "var q=document.createElement('i'),qc=0,qo=new MutationObserver(function(){qc++;});qo.observe(q,{attributes:true});q.classList.remove('no-existe');qo.takeRecords().length", "0");

 chk(j,"classList value setter","p.classList.value='tres';'pending'","pending");
 chk(j,"classList value notified","l.join('|')","id:p|class:null|class:uno|class:uno dos");
 chk(j,"dataset put queued","p.dataset.userId='A';'pending'","pending");
 chk(j,"dataset put notified","l.join('|')","id:p|class:null|class:uno|class:uno dos|data-user-id:null");
 chk(j,"dataset del queued","delete p.dataset.userId;'pending'","pending");
 chk(j,"dataset del notified","l.join('|')","id:p|class:null|class:uno|class:uno dos|data-user-id:null|data-user-id:A");
 chk(j,"style put queued","p.style.color='red';'pending'","pending");
 chk(j,"style put notified","l[l.length-1].slice(0,6)","style:");
 chk(j,"style setProperty queued","p.style.setProperty('color','blue');'pending'","pending");
 chk(j,"style setProperty notified","l[l.length-1].slice(0,6)","style:");
 chk(j,"setAttribute not duplicated","var pre=l.length;p.setAttribute('title','hola');'pending'","pending");
 chk(j,"single attribute record","l.length-pre","1");
 chk(j,"reflected href queued","p.href='/url';'pending'","pending");
 chk(j,"reflected href notified","l[l.length-1]","href:null");
 chk(j,"boolean hidden queued","p.hidden=true;'pending'","pending");
 chk(j,"boolean hidden notified","l[l.length-1]","hidden:null");
 chk(j,"boolean hidden false queued","p.hidden=false;'pending'","pending");
 chk(j,"boolean hidden false notified","l[l.length-1]","hidden:");
 chk(j,"observe text","var t=p.firstChild,cd=[];var tc=new MutationObserver(function(rs){cd.push(rs[0].oldValue);});tc.observe(t,{characterData:true,characterDataOldValue:true});'ok'","ok");
 chk(j,"nodeValue write queued","t.nodeValue='adios';'pending'","pending");
 chk(j,"nodeValue oldValue","cd.join('|')","hola");
 chk(j,"data write queued","t.data='nuevo';'pending'","pending");
 chk(j,"data oldValue","cd.join('|')","hola|adios");
 chk(j,"disconnect ignores changes","o.disconnect();p.id='fin';l.length","12");
 if(errors) { fprintf(stderr,"Unexpected JS errors %d\n",errors);return 1; }
 fprintf(stderr,"SOMA 148: %d pruebas nativas OK\n",count);
 wjs_free(j);wd_free(d);return 0;
}
