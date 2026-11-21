/* SOMA_241_VOZ: reconocimiento de voz (voz a texto) con whisper.cpp, en un proceso de ring 3.
 *
 * El programa (src/user/voz/, compilado con AVX2) va dentro del nucleo como python.elf. Aqui:
 *   - voz_run: lo lanza con una region de memoria GRANDE bajo demanda en USER_HEAP_VA (marcos de la reserva alta: en el T480s hay
 *     ~15 GiB que el resto del sistema casi no usa) y la libera al terminar. Solo un reconocimiento a la vez.
 *   - voz_install: copia el modelo ggml al almacen BIGFS (/modelos) desde la particion de arranque del pendrive (carpeta MODELOS)
 *     o, en QEMU, desde -fw_cfg name=opt/soma/modelo,file=ggml-base-q5_1.bin.
 *   - voz_last_recording: la ultima grabacion de la Grabadora (/grabaciones/grab_N.wav) para "voz ultima".
 * El modelo recomendado es ggml-base-q5_1.bin (60 MB, multilingue: espanol incluido). */
#include <stdint.h>
#include <stddef.h>
#include "voz_sys.h"
#include "usermode.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "mutex.h"
#include "thread.h"
#include "console.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "fwcfg.h"
#include "usbdisk.h"
#include "fat.h"

extern const uint8_t _binary_voz_elf_start[], _binary_voz_elf_end[];

static struct kmutex lk; static int lk_init;
static volatile int g_idx = -1, g_stopped;

static uint32_t slen(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static void sput(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void sdec(char *o, uint32_t cap, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } sput(o, cap, p, b + i); }
static int ends_with(const char *s, const char *e)
{
    uint32_t a = slen(s), b = slen(e);
    if (a < b) return 0;
    for (uint32_t i = 0; i < b; ++i) { char c = s[a - b + i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != e[i]) return 0; }
    return 1;
}
static void lock(void) { if (!lk_init) { kmutex_init(&lk, "voz"); lk_init = 1; } kmutex_lock(&lk); }

/* Modelo a usar: el recomendado si esta; si no, el primer .bin de /modelos. 0 = encontrado (ruta y tamano). */
int voz_model(char *path, uint32_t cap, uint64_t *size)
{
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return -1;
    uint32_t ino; struct bigfs_stat st;
    if (bigfs_lookup(fs, VOZ_MODEL_DEFAULT, &ino) == (int)BIGFS_T_FILE && bigfs_stat(fs, ino, &st) == 0) {
        uint32_t p = 0; sput(path, cap, &p, VOZ_MODEL_DEFAULT); if (size) *size = st.size; return 0;
    }
    if (bigfs_lookup(fs, VOZ_DIR, &ino) != (int)BIGFS_T_DIR) return -1;
    uint32_t cur = 0;
    while (bigfs_readdir(fs, ino, &cur, &st) == 1) {
        if (st.type != BIGFS_T_FILE || !ends_with(st.name, ".bin")) continue;
        uint32_t p = 0; sput(path, cap, &p, VOZ_DIR "/"); sput(path, cap, &p, st.name);
        if (size) *size = st.size;
        return 0;
    }
    return -1;
}

int voz_ready(const char **why)
{
    static const char *d; if (!why) why = &d;
    if (!usermode_avx2_ok()) { *why = "este procesador no tiene AVX2/FMA (o no esta activo): hace falta para el reconocimiento de voz"; return 0; }
    char m[96];
    if (voz_model(m, sizeof m, 0) != 0) { *why = "no hay modelo de voz instalado (copia ggml-base-q5_1.bin a la carpeta MODELOS del pendrive y ejecuta: voz instalar)"; return 0; }
    *why = "";
    return 1;
}

void voz_stop(void) { int i = g_idx; if (i >= 0) { g_stopped = 1; usermode_stop(i); } }
int voz_busy(void) { return g_idx >= 0; }

int voz_run(const char *user_args, char *out, uint32_t cap, int64_t *code_out)
{
    const char *why;
    if (out && cap) out[0] = 0;
    /* SOMA_243_VOZ_AUTO: sin modelo en el almacen, se instala solo desde la carpeta MODELOS del pendrive (antes habia que
     * escribir "voz instalar" en el terminal y la Grabadora solo decia que faltaba) */
    { char m[96];
      if (usermode_avx2_ok() && voz_model(m, sizeof m, 0) != 0) {
          static char msg[256];
          if (!out) kprint("voz: no hay modelo en el almacen: instalandolo desde el pendrive...\n");
          if (voz_install(msg, sizeof msg) != 0) {
              if (out) { uint32_t p = 0; sput(out, cap, &p, "[voz] no hay modelo instalado: "); sput(out, cap, &p, msg); sput(out, cap, &p, "\n"); }
              else { kprint("voz: "); kprint(msg); kprint("\n"); }
              return -2;
          }
          if (!out) { kprint("voz: "); kprint(msg); kprint("\n"); }
      } }
    if (!voz_ready(&why)) { if (out) { uint32_t p = 0; sput(out, cap, &p, "[voz] "); sput(out, cap, &p, why); sput(out, cap, &p, "\n"); } else { kprint("voz: "); kprint(why); kprint("\n"); } return -2; }
    char model[96]; uint64_t msize = 0;
    (void)voz_model(model, sizeof model, &msize);
    /* memoria: el modelo + los bufers de calculo (base ~250 MB en total, small ~700 MB). La region es virtual: solo gasta lo que se toca. */
    uint64_t heap_mib = 1024 + (msize >> 20) * 6;
    if (heap_mib > 4096) heap_mib = 4096;
    char args[USER_ARGS_MAX]; uint32_t p = 0;
    sput(args, sizeof args, &p, "-H "); sdec(args, sizeof args, &p, heap_mib);
    sput(args, sizeof args, &p, " -m "); sput(args, sizeof args, &p, model);
    sput(args, sizeof args, &p, " ");
    if (p + slen(user_args) + 1 >= sizeof args) { if (!out) kprint("voz: la ruta del audio es demasiado larga\n"); return -3; }
    sput(args, sizeof args, &p, user_args);

    lock();
    struct uregion r;
    r.va = USER_HEAP_VA; r.npages = (uint32_t)(heap_mib << 8); r.used = 0;
    r.flags = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITABLE | VMM_FLAG_NX;
    r.demand = 2;                                            /* bajo demanda, marcos de la reserva alta (ver usermode_demand_fault) */
    r.frames = (uint64_t *)kmalloc((size_t)r.npages * sizeof(uint64_t));
    if (!r.frames) { kmutex_unlock(&lk); if (!out) kprint("voz: sin memoria para preparar el proceso\n"); return -4; }
    for (uint32_t k = 0; k < r.npages; ++k) r.frames[k] = 0;
    int err = 0;
    int idx = usermode_exec_region("voz", _binary_voz_elf_start, (size_t)(_binary_voz_elf_end - _binary_voz_elf_start),
                                   30u * 60u * 1000u, args, USER_SYS_DEFAULT, &r, &err);
    int64_t code = -1;
    if (idx >= 0) {
        int guest = 0;
        if (out) usermode_redirect(idx, out, cap, "");
        else if (console_capture_active()) guest = console_capture_guest(usermode_proc_tid(idx));
        g_stopped = 0; g_idx = idx;
        code = usermode_wait(idx);
        g_idx = -1;
        if (guest) (void)console_capture_guest(0);
    }
    uint32_t used = r.used;
    for (uint32_t k = 0; k < r.npages; ++k) if (r.frames[k]) pmm_free_frame((uintptr_t)r.frames[k]);
    kfree(r.frames);
    kmutex_unlock(&lk);
    if (idx < 0) { if (!out) kprint("voz: no se pudo lanzar el proceso (sin procesos libres o memoria)\n"); return -5; }
    if (code_out) *code_out = code;
    if (code < 0) {
        const char *m = g_stopped ? "detenido" : code == USER_EXIT_TIMEOUT ? "se agoto el tiempo (30 min)" : code == USER_EXIT_NOMEM ? "sin memoria" :
                        code <= USER_EXIT_FAULT_BASE && code > USER_EXIT_FAULT_BASE - 256 ? "fallo de la CPU" : "terminado a la fuerza";
        if (out) { uint32_t q = slen(out); sput(out, cap, &q, "[voz] "); sput(out, cap, &q, m); sput(out, cap, &q, "\n"); }
        else { kprint("voz: "); kprint(m); kprint("\n"); }
    }
    (void)used;
    return code == 0 ? 0 : 1;
}

/* ---- instalacion del modelo ---- */
static int copy_into(struct bigfs *fs, const char *dst, uint32_t size, int from_fwcfg, const char *fat_path, char *msg, uint32_t cap)
{
    uint32_t p = 0;
    uint32_t ino;
    if (bigfs_lookup(fs, dst, &ino) >= 0) bigfs_unlink(fs, dst);
    int e = bigfs_create(fs, dst, &ino);
    if (e < 0) { sput(msg, cap, &p, "no se pudo crear "); sput(msg, cap, &p, dst); sput(msg, cap, &p, ": "); sput(msg, cap, &p, bigfs_strerror(e)); return -1; }
    const uint32_t CH = 1u << 20;
    uint8_t *buf = (uint8_t *)kmalloc(CH);
    if (!buf) { bigfs_unlink(fs, dst); sput(msg, cap, &p, "sin memoria"); return -1; }
    uint32_t done = 0, last_pct = 0;
    kprint("voz: copiando el modelo (");
    { char b[16]; uint32_t q = 0; sdec(b, sizeof b, &q, size >> 20); kprint(b); }
    kprint(" MiB)...\n");
    while (done < size) {
        uint32_t n = size - done < CH ? size - done : CH;
        if (from_fwcfg) fwcfg_read_seq(buf, n);
        else {
            const char *why; int r = usbdisk_bootfile_read(fat_path, done, buf, n, 0, &why);
            if (r <= 0) { kfree(buf); bigfs_unlink(fs, dst); sput(msg, cap, &p, "error leyendo el pendrive: "); sput(msg, cap, &p, why); return -1; }
            n = (uint32_t)r;
        }
        if (done == 0 && (n < 4 || buf[0] != 'l' || buf[1] != 'm' || buf[2] != 'g' || buf[3] != 'g')) {
            kfree(buf); bigfs_unlink(fs, dst);
            sput(msg, cap, &p, "el archivo no es un modelo ggml de whisper.cpp (usa ggml-base-q5_1.bin)");
            return -1;
        }
        int64_t w = bigfs_write(fs, ino, done, buf, n);
        if (w != (int64_t)n) { kfree(buf); bigfs_unlink(fs, dst); sput(msg, cap, &p, "error escribiendo en el almacen: "); sput(msg, cap, &p, w < 0 ? bigfs_strerror((int)w) : "escritura incompleta"); return -1; }
        done += n;
        uint32_t pct = (uint32_t)((uint64_t)done * 100 / size);
        if (pct / 20 != last_pct / 20) { char b[8]; uint32_t q = 0; sdec(b, sizeof b, &q, pct); kprint("voz: "); kprint(b); kprint(" %\n"); last_pct = pct; }
        thread_yield();
    }
    kfree(buf);
    sput(msg, cap, &p, "modelo instalado en "); sput(msg, cap, &p, dst); sput(msg, cap, &p, " ("); sdec(msg, cap, &p, size >> 20); sput(msg, cap, &p, " MiB)");
    return 0;
}

int voz_install(char *msg, uint32_t cap)
{
    uint32_t p = 0;
    if (cap) msg[0] = 0;
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { sput(msg, cap, &p, "no hay almacen BIGFS (preparalo con: big format yes)"); return -1; }
    uint32_t ino;
    if (bigfs_lookup(fs, VOZ_DIR, &ino) != (int)BIGFS_T_DIR && bigfs_mkdir(fs, VOZ_DIR) < 0) { sput(msg, cap, &p, "no se pudo crear " VOZ_DIR); return -1; }
    lock();
    int r;
    uint32_t asize = fwcfg_open("opt/soma/audio");             /* QEMU, para las pruebas: un WAV a /grabaciones/voz_prueba.wav */
    if (asize) {
        uint32_t a2;
        if (bigfs_lookup(fs, "/grabaciones", &a2) != (int)BIGFS_T_DIR) (void)bigfs_mkdir(fs, "/grabaciones");
        if (bigfs_lookup(fs, "/grabaciones/voz_prueba.wav", &a2) >= 0) bigfs_unlink(fs, "/grabaciones/voz_prueba.wav");
        uint8_t *b = (uint8_t *)kmalloc(asize);
        if (b && bigfs_create(fs, "/grabaciones/voz_prueba.wav", &a2) >= 0) { fwcfg_read_seq(b, asize); (void)bigfs_write(fs, a2, 0, b, asize); kprint("voz: audio de prueba en /grabaciones/voz_prueba.wav\n"); }
        if (b) kfree(b);
    }
    uint32_t size = fwcfg_open("opt/soma/modelo");             /* QEMU */
    if (size) r = copy_into(fs, VOZ_MODEL_DEFAULT, size, 1, 0, msg, cap);
    else {                                                     /* pendrive: carpeta MODELOS de la particion de arranque */
        static struct fat_dirent d[16];
        const char *why;
        int n = usbdisk_bootdir("MODELOS", d, 16, &why);
        int pick = -1;
        /* SOMA_243: macOS deja copias "._nombre" de 4 KB (metadatos AppleDouble) que tambien acaban en .bin: se ignoran */
        for (int i = 0; i < n; ++i) if (!d[i].is_dir && d[i].name[0] != '.' && d[i].size > (1u << 20) && ends_with(d[i].name, ".bin") && (pick < 0 || ends_with(d[i].name, "base-q5_1.bin"))) pick = i;
        if (pick < 0) {
            sput(msg, cap, &p, n < 0 ? why : "la carpeta MODELOS del pendrive no tiene ningun .bin");
            sput(msg, cap, &p, ". Copia ggml-base-q5_1.bin (whisper.cpp, 60 MB) en la carpeta MODELOS de la unidad SOMA del pendrive y repite 'voz instalar'.");
            kmutex_unlock(&lk);
            return -1;
        }
        char src[96], dst[96]; uint32_t q = 0; sput(src, sizeof src, &q, "MODELOS/"); sput(src, sizeof src, &q, d[pick].name);
        q = 0; sput(dst, sizeof dst, &q, VOZ_DIR "/");
        for (const char *c = d[pick].name; *c && q + 1 < sizeof dst; ++c) dst[q++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c + 32) : *c;
        dst[q] = 0;
        r = copy_into(fs, dst, d[pick].size, 0, src, msg, cap);
    }
    kmutex_unlock(&lk);
    return r;
}

/* Ultima grabacion de la Grabadora: el .wav mas reciente de /grabaciones. */
int voz_last_recording(char *path, uint32_t cap)
{
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return -1;
    uint32_t ino;
    if (bigfs_lookup(fs, "/grabaciones", &ino) != (int)BIGFS_T_DIR) return -1;
    uint32_t cur = 0; struct bigfs_stat st; uint64_t best = 0; int found = 0;
    while (bigfs_readdir(fs, ino, &cur, &st) == 1) {
        if (st.type != BIGFS_T_FILE || !ends_with(st.name, ".wav")) continue;
        if (!found || st.mtime >= best) { best = st.mtime; uint32_t p = 0; sput(path, cap, &p, "/grabaciones/"); sput(path, cap, &p, st.name); found = 1; }
    }
    return found ? 0 : -1;
}
