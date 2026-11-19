/* SOMA_159_T480S: modelo en el anfitrion del protocolo de reparto de src/arch/x86_64/smp.c.
 * Cada "nucleo" secundario es un hilo con el mismo bucle: espera activa corta, luego marca sleeping, barrera, vuelve a mirar la
 * secuencia y "duerme" (hlt) hasta una "IPI". La IPI se modela como en el LAPIC: un bit pendiente que se enclava aunque el
 * nucleo no este dormido todavia, y que hace volver de hlt en cuanto se duerme. El principal reparte N trabajos seguidos y
 * comprueba que todos los trozos se ejecutan exactamente una vez y que ningun despertar se pierde (si se perdiera, se
 * colgaria: hay un tiempo limite). Uso: test_smp_159 [trabajos] */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NC 8
#define SPIN 50

typedef void (*smp_fn)(int idx, int n, void *arg);
struct slot {
    _Atomic(smp_fn) fn; _Atomic(void *) arg; atomic_int idx, n;
    atomic_uint seq, done; atomic_int sleeping;
    /* "LAPIC": IPI pendiente */
    pthread_mutex_t m; pthread_cond_t cv; int pending;
    unsigned long jobs, wakes;
};
static struct slot S[NC];
static atomic_int stop;

static void ipi(int c) { pthread_mutex_lock(&S[c].m); S[c].pending = 1; pthread_cond_signal(&S[c].cv); pthread_mutex_unlock(&S[c].m); }
static void hlt(int c)    /* "sti; hlt; cli": vuelve si hay una IPI pendiente (aunque llegara antes) */
{
    pthread_mutex_lock(&S[c].m);
    while (!S[c].pending && !atomic_load(&stop)) pthread_cond_wait(&S[c].cv, &S[c].m);
    S[c].pending = 0;
    pthread_mutex_unlock(&S[c].m);
}

static void *ap(void *p)
{
    int c = (int)(intptr_t)p; struct slot *s = &S[c];
    unsigned last = 0;
    while (!atomic_load(&stop)) {
        unsigned q = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (q == last) {
            for (int i = 0; i < SPIN && q == last; i++) q = atomic_load_explicit(&s->seq, memory_order_acquire);
            if (q == last) {
                atomic_store(&s->sleeping, 1);                               /* seq_cst = la mfence del nucleo */
                if (atomic_load(&s->seq) == last) { hlt(c); s->wakes++; }
                atomic_store_explicit(&s->sleeping, 0, memory_order_release);
                continue;
            }
        }
        smp_fn fn = atomic_load(&s->fn);
        fn(atomic_load(&s->idx), atomic_load(&s->n), atomic_load(&s->arg));
        s->jobs++;
        last = q;
        atomic_store_explicit(&s->done, q, memory_order_release);
    }
    return 0;
}

static int parallel_n(smp_fn fn, void *arg, int n)
{
    unsigned seq[NC];
    for (int c = 1; c < n; c++) {
        atomic_store(&S[c].fn, fn); atomic_store(&S[c].arg, arg); atomic_store(&S[c].idx, c); atomic_store(&S[c].n, n);
        seq[c] = atomic_load(&S[c].seq) + 1;
        atomic_store(&S[c].seq, seq[c]);
    }
    for (int c = 1; c < n; c++) if (atomic_load(&S[c].sleeping)) ipi(c);
    fn(0, n, arg);
    for (int c = 1; c < n; c++) {
        time_t t0 = time(0);
        while (atomic_load_explicit(&S[c].done, memory_order_acquire) != seq[c])
            if (time(0) - t0 > 10) { fprintf(stderr, "FALLO: el nucleo %d no termino (despertar perdido)\n", c); exit(1); }
    }
    return n;
}

/* trabajo: cada trozo marca su parte de un vector; se comprueba que cada casilla se toca exactamente una vez */
#define LEN 4096
struct job { atomic_int hits[LEN]; };
static void part(int idx, int n, void *arg)
{
    struct job *j = arg;
    int a = LEN * idx / n, b = LEN * (idx + 1) / n;
    for (int i = a; i < b; i++) atomic_fetch_add_explicit(&j->hits[i], 1, memory_order_relaxed);
}

int main(int argc, char **argv)
{
    long jobs = argc > 1 ? atol(argv[1]) : 20000;
    pthread_t th[NC];
    for (int c = 0; c < NC; c++) { pthread_mutex_init(&S[c].m, 0); pthread_cond_init(&S[c].cv, 0); }
    for (int c = 1; c < NC; c++) pthread_create(&th[c], 0, ap, (void *)(intptr_t)c);
    static struct job j;
    unsigned rnd = 12345;
    for (long k = 0; k < jobs; k++) {
        for (int i = 0; i < LEN; i++) atomic_store_explicit(&j.hits[i], 0, memory_order_relaxed);
        rnd = rnd * 1103515245u + 12345u;
        int n = 1 + (int)((rnd >> 16) % NC);                              /* repartos de 1..8 trozos, como fb.c y crypto */
        parallel_n(part, &j, n);
        for (int i = 0; i < LEN; i++) if (atomic_load(&j.hits[i]) != 1) { fprintf(stderr, "FALLO: casilla %d tocada %d veces (trabajo %ld)\n", i, atomic_load(&j.hits[i]), k); return 1; }
        if ((rnd >> 8) % 97 == 0) { struct timespec ts = { 0, 200000 }; nanosleep(&ts, 0); }   /* a veces se duermen de verdad */
    }
    atomic_store(&stop, 1);
    unsigned long w = 0, jb = 0;
    for (int c = 1; c < NC; c++) { ipi(c); pthread_join(th[c], 0); w += S[c].wakes; jb += S[c].jobs; }
    printf("reparto SMP: %ld repartos, %lu trozos en secundarios, %lu despertares, ningun despertar perdido: OK\n", jobs, jb, w);
    return 0;
}
