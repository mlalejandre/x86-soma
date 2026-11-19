/* SOMA151: protocolo SPSC sin kernel, con sanitizadores y cambios de pagina. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include "web_async_151.h"
static unsigned long checked;
static void ck(int ok,const char *w){if(!ok){fprintf(stderr,"SOMA151 ERROR: %s\n",w);exit(1);}++checked;}
static uint32_t seed=0x1ac3917u;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void test_tickets(void){
 for(int i=0;i<100000;i++){
  struct soma151_ticket t={rnd(),rnd(),(int)(rnd()%128)};
  ck(soma151_same_ticket(t,t.generation,t.index,t.hash),"ticket identico");
  ck(!soma151_same_ticket(t,t.generation+1,t.index,t.hash),"pagina antigua");
  ck(!soma151_same_ticket(t,t.generation,t.index+1,t.hash),"indice cambiado");
  ck(!soma151_same_ticket(t,t.generation,t.index,t.hash^1u),"URL cambiada");
 }
 ck(soma151_running(SOMA151_QUEUED)&&soma151_running(SOMA151_RUNNING),"ocupado");
 ck(!soma151_running(SOMA151_READY)&&!soma151_running(SOMA151_IDLE),"libre");
}
struct mailbox {volatile int state,cancel;struct soma151_ticket ticket;uint64_t payload;};
static struct mailbox box;
static volatile int done;
static void *worker(void *arg){
 (void)arg;
 while(!__atomic_load_n(&done,__ATOMIC_ACQUIRE)){
  if(__atomic_load_n(&box.state,__ATOMIC_ACQUIRE)!=SOMA151_QUEUED){sched_yield();continue;}
  __atomic_store_n(&box.state,SOMA151_RUNNING,__ATOMIC_RELEASE);
  uint64_t x=box.payload;
  box.payload=__atomic_load_n(&box.cancel,__ATOMIC_ACQUIRE)?0:x^0x31415926abcdef78ULL;
  __atomic_store_n(&box.state,SOMA151_READY,__ATOMIC_RELEASE);
 }
 return 0;
}
static void test_mailbox(void){
 pthread_t t;ck(pthread_create(&t,0,worker,0)==0,"hilo");
 int stale=0,accepted=0,cancelled=0;
 for(int i=0;i<20000;i++){
  while(__atomic_load_n(&box.state,__ATOMIC_ACQUIRE)!=SOMA151_IDLE)sched_yield();
  box.ticket.generation=(uint32_t)(i+1);box.ticket.index=i%67;box.ticket.hash=(uint32_t)i*7919u;
  uint64_t val=(uint64_t)(unsigned)(i+1)*1234567u;box.payload=val;
  int cancel=i%13==0;__atomic_store_n(&box.cancel,cancel,__ATOMIC_RELEASE);
  __atomic_store_n(&box.state,SOMA151_QUEUED,__ATOMIC_RELEASE);
  while(__atomic_load_n(&box.state,__ATOMIC_ACQUIRE)!=SOMA151_READY)sched_yield();
  int same=soma151_same_ticket(box.ticket,(uint32_t)(i+(i%17==0?2:1)),i%67,(uint32_t)i*7919u);
  if(!same)stale++;
  else if(cancel){ck(box.payload==0,"cancelada");cancelled++;}
  else{ck(box.payload==(val^0x31415926abcdef78ULL),"datos intactos");accepted++;}
  __atomic_store_n(&box.state,SOMA151_IDLE,__ATOMIC_RELEASE);
 }
 __atomic_store_n(&done,1,__ATOMIC_RELEASE);
 ck(pthread_join(t,0)==0,"join");
 ck(stale>500&&accepted>10000&&cancelled>1000,"cobertura");
 printf("SOMA151: %lu comprobaciones; aceptadas=%d antiguas=%d canceladas=%d\n",checked,accepted,stale,cancelled);
}
int main(void){test_tickets();test_mailbox();return 0;}
