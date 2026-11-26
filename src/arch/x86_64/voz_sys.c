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
#include "fs.h"        /* SOMA_246: /etc/voz.conf */
#include "habla_texto.h"   /* SOMA_250_TTS */
#include "unidades.h"      /* SOMA_267_DISCOS: modelos en la unidad MODELOS */
#include "audio_sys.h"     /* SOMA_330: audio_sys_flush */

#include "progz.h"
PROGZ_DECL(voz);                                            /* SOMA_314_PROGZ: voz.elf va comprimido en la imagen */

static struct kmutex lk; static int lk_init;
static volatile int g_idx = -1, g_stopped;
static volatile int g_sp_cancel, g_sp_proc;                /* SOMA_256: parar durante la preparacion / el proceso de lectura esta en marcha */

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

/* SOMA_246_VOZ_MODELOS: tres niveles de modelo, elegidos en Ajustes > Voz (o con "voz modelo rapido|preciso|maximo") y guardados en
 * /etc/voz.conf. El nivel de un .bin sale de su nombre: tiny/base -> Rapido, small/medium -> Preciso, large/turbo -> Maxima precision. */
static const char *const TIER_FILE[VOZ_TIERS] = { "ggml-base-q5_1.bin", "ggml-small-q5_1.bin", "ggml-large-v3-turbo-q5_0.bin" };
static const char *const TIER_NAME[VOZ_TIERS] = { "Rapido", "Preciso", "Maxima precision" };
static const char *const TIER_KEY[VOZ_TIERS]  = { "rapido", "preciso", "maximo" };
static const char *const TIER_FWCFG[VOZ_TIERS] = { "opt/soma/modelo", "opt/soma/modelo_preciso", "opt/soma/modelo_maximo" };   /* QEMU */
static const uint32_t TIER_MB[VOZ_TIERS] = { 60, 190, 575 };
static int g_pref = -1;
static volatile int g_prog = -1;                       /* instalacion en curso: 0..100 */

const char *voz_tier_file(int t) { return t >= 0 && t < VOZ_TIERS ? TIER_FILE[t] : ""; }
const char *voz_tier_name(int t) { return t >= 0 && t < VOZ_TIERS ? TIER_NAME[t] : ""; }
uint32_t voz_tier_mb(int t)      { return t >= 0 && t < VOZ_TIERS ? TIER_MB[t] : 0; }
int voz_install_progress(void)   { return g_prog; }
static int has_sub(const char *s, const char *sub)
{
    for (; s && *s; ++s) {
        uint32_t i = 0;
        for (; sub[i]; ++i) { char c = s[i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != sub[i]) break; }
        if (!sub[i]) return 1;
    }
    return 0;
}
int voz_tier_of(const char *name)
{
    if (has_sub(name, "large") || has_sub(name, "turbo")) return 2;
    if (has_sub(name, "small") || has_sub(name, "medium")) return 1;
    return 0;
}
int voz_tier_from_key(const char *k)
{
    for (int t = 0; t < VOZ_TIERS; ++t) {
        uint32_t i = 0; while (k[i] && TIER_KEY[t][i] && k[i] == TIER_KEY[t][i]) i++;
        if (i >= 4 && (!k[i] || k[i] == ' ')) return t;            /* basta con las 4 primeras letras: "rapi", "prec", "maxi" */
    }
    return -1;
}
int voz_pref(void)
{
    if (g_pref < 0) {
        char b[64]; int n = vfs_read("/etc/voz.conf", b, sizeof b - 1);
        g_pref = 0;
        if (n > 0) { b[n] = 0; for (int i = 0; i + 7 < n; ++i) if (b[i] == 'm' && b[i + 1] == 'o' && b[i + 2] == 'd' && b[i + 6] == '=' ) { int t = voz_tier_from_key(b + i + 7); if (t >= 0) g_pref = t; break; } }
    }
    return g_pref;
}
void voz_set_pref(int t)
{
    if (t < 0 || t >= VOZ_TIERS) return;
    g_pref = t;
    char b[48]; uint32_t p = 0; sput(b, sizeof b, &p, "modelo="); sput(b, sizeof b, &p, TIER_KEY[t]); sput(b, sizeof b, &p, "\n");
    (void)vfs_write("/etc/voz.conf", b, p);
}

/* SOMA_267_DISCOS: un modelo de whisper.cpp en la unidad MODELOS del pendrive (/modelos/voz): se usa DIRECTAMENTE desde alli
 * (voz.elf lo lee con SYS_OPEN), sin copiarlo al almacen de DATOS. Se comprueba la cabecera "lmgg" de ggml. */
static int unid_modelo(int tier, char *path, uint32_t cap, uint64_t *size)
{
    if (!unid_activa(UNID_MODELOS)) return -1;
    struct exfat_dirent *d = (struct exfat_dirent *)kmalloc(32 * sizeof *d);   /* lo llaman a la vez Ajustes y el dictado */
    if (!d) return -1;
    int total = 0, n = unid_listar(VOZ_DIR "/voz", d, 32, &total), pick = -1;
    for (int i = 0; i < n; ++i) {
        if (d[i].is_dir || d[i].name[0] == '.' || d[i].size <= (1u << 20) || !ends_with(d[i].name, ".bin") || voz_tier_of(d[i].name) != tier) continue;
        if (slen(d[i].name) + 16 >= cap) continue;
        if (pick < 0 || ends_with(d[i].name, TIER_FILE[tier])) pick = i;
    }
    int r = -1;
    if (pick >= 0) {
        uint32_t p = 0; sput(path, cap, &p, VOZ_DIR "/voz/"); sput(path, cap, &p, d[pick].name);
        uint8_t mg[4] = { 0 };
        if (unid_leer(path, 0, mg, 4) == 4 && mg[0] == 'l' && mg[1] == 'm' && mg[2] == 'g' && mg[3] == 'g') { if (size) *size = d[pick].size; r = 0; }
    }
    kfree(d);
    return r;
}

/* Modelo instalado de un nivel: el recomendado si esta; si no, cualquier otro .bin de ese nivel. 0 = encontrado.
 * SOMA_267_DISCOS: primero el de la unidad MODELOS (sin copiar); despues el del almacen. */
int voz_installed(int tier, char *path, uint32_t cap, uint64_t *size)
{
    if (tier >= 0 && tier < VOZ_TIERS && unid_modelo(tier, path, cap, size) == 0) return 0;
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs || tier < 0 || tier >= VOZ_TIERS) return -1;
    uint32_t ino, dir; struct bigfs_stat st;
    char rec[96]; uint32_t q = 0; sput(rec, sizeof rec, &q, VOZ_DIR "/"); sput(rec, sizeof rec, &q, TIER_FILE[tier]);
    if (bigfs_lookup(fs, rec, &ino) == (int)BIGFS_T_FILE && bigfs_stat(fs, ino, &st) == 0 && st.size > (1u << 20)) {
        uint32_t p = 0; sput(path, cap, &p, rec); if (size) *size = st.size; return 0;
    }
    if (bigfs_lookup(fs, VOZ_DIR, &dir) != (int)BIGFS_T_DIR) return -1;
    uint32_t cur = 0;
    while (bigfs_readdir(fs, dir, &cur, &st) == 1) {
        if (st.type != BIGFS_T_FILE || st.name[0] == '.' || st.size <= (1u << 20) || !ends_with(st.name, ".bin") || voz_tier_of(st.name) != tier) continue;
        uint32_t p = 0; sput(path, cap, &p, VOZ_DIR "/"); sput(path, cap, &p, st.name);
        if (size) *size = st.size;
        return 0;
    }
    return -1;
}
/* Modelo a usar: el del nivel elegido; si no esta instalado, el del nivel mas cercano que haya (primero los mas rapidos). */
int voz_model_tier(char *path, uint32_t cap, uint64_t *size, int *tier)
{
    int want = voz_pref();
    static const int order[VOZ_TIERS][VOZ_TIERS] = { { 0, 1, 2 }, { 1, 0, 2 }, { 2, 1, 0 } };
    for (int k = 0; k < VOZ_TIERS; ++k) {
        int t = order[want][k];
        if (voz_installed(t, path, cap, size) == 0) { if (tier) *tier = t; return 0; }
    }
    return -1;
}
int voz_model(char *path, uint32_t cap, uint64_t *size) { return voz_model_tier(path, cap, size, 0); }

int voz_ready(const char **why)
{
    static const char *d; if (!why) why = &d;
    if (!usermode_avx2_ok()) { *why = "este procesador no tiene AVX2/FMA (o no esta activo): hace falta para el reconocimiento de voz"; return 0; }
    char m[96];
    if (voz_model(m, sizeof m, 0) != 0) { *why = "no hay modelo de voz (copia ggml-base-q5_1.bin en la carpeta voz de la unidad MODELOS del pendrive; otros modelos en Ajustes > Voz)"; return 0; }
    *why = "";
    return 1;
}

void voz_stop(void) { int i = g_idx; if (i >= 0) { g_stopped = 1; usermode_stop(i); } }
int voz_busy(void) { return g_idx >= 0; }

/* Lanza voz.elf con "args" y una region de heap_mib MiB bajo demanda; espera a que termine. Un proceso a la vez (cerrojo). */
static int run_proc(const char *args, uint64_t heap_mib, char *out, uint32_t cap, int64_t *code_out)
{
    lock();
    struct uregion r;
    r.va = USER_HEAP_VA; r.npages = (uint32_t)(heap_mib << 8); r.used = 0;
    r.flags = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITABLE | VMM_FLAG_NX;
    r.demand = 2;                                            /* bajo demanda, marcos de la reserva alta (ver usermode_demand_fault) */
    r.frames = (uint64_t *)kmalloc((size_t)r.npages * sizeof(uint64_t));
    if (!r.frames) { kmutex_unlock(&lk); if (!out) kprint("voz: sin memoria para preparar el proceso\n"); return -4; }
    for (uint32_t k = 0; k < r.npages; ++k) r.frames[k] = 0;
    int err = 0;
    int idx = -2;                                            /* SOMA_314_PROGZ: se descomprime, se lanza y se libera */
    { struct progz pz; if (progz_abre(PROGZ(voz), "voz", &pz) == 0) { idx = usermode_exec_region("voz", pz.elf, pz.len, 30u * 60u * 1000u, args, USER_SYS_DEFAULT, &r, &err); progz_cierra(&pz); } }
    int64_t code = -1;
    if (idx >= 0) {
        int guest = 0;
        if (out) usermode_redirect(idx, out, cap, "");
        else if (console_capture_active()) guest = console_capture_guest(usermode_proc_tid(idx));
        g_stopped = 0; g_idx = idx;
        /* SOMA_330_RESPUESTA_PRIMERO: una lectura que se mando parar mientras se preparaba (descomprimir y lanzar el programa) ya no
         * llego a ver g_idx: antes sonaba entera aunque la respuesta de verdad ya hubiera llegado. Ahora se para nada mas lanzarla. */
        if (g_sp_proc && g_sp_cancel) { g_stopped = 1; usermode_stop(idx); }
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

int voz_run(const char *user_args, char *out, uint32_t cap, int64_t *code_out)
{
    const char *why;
    if (out && cap) out[0] = 0;
    /* SOMA_243_VOZ_AUTO: sin modelo en el almacen, se instala solo desde la carpeta MODELOS del pendrive (antes habia que
     * escribir "voz instalar" en el terminal y la Grabadora solo decia que faltaba) */
    { char m[96]; int want = voz_pref();
      /* SOMA_246: el modelo del nivel elegido en Ajustes tampoco hace falta instalarlo a mano: si esta en el pendrive, se copia */
      if (usermode_avx2_ok() && voz_installed(want, m, sizeof m, 0) != 0 && (voz_model(m, sizeof m, 0) != 0 || voz_on_stick(want, 0, 0, 0) == 0)) {
          static char msg[256];
          int have_other = voz_model(m, sizeof m, 0) == 0;
          if (!out) kprint("voz: el modelo elegido no esta en el almacen: instalandolo desde el pendrive...\n");
          if (voz_install(msg, sizeof msg) != 0 && !have_other) {
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

    return run_proc(args, heap_mib, out, cap, code_out);
}

/* ---- instalacion del modelo ---- */
static int g_copy_ggml = 1;                               /* SOMA_247: 0 al copiar voces (.onnx/.json), que no son ggml */
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
    g_prog = 0;
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
        if (done == 0 && g_copy_ggml && (n < 4 || buf[0] != 'l' || buf[1] != 'm' || buf[2] != 'g' || buf[3] != 'g')) {
            kfree(buf); bigfs_unlink(fs, dst);
            sput(msg, cap, &p, "el archivo no es un modelo ggml de whisper.cpp (usa los ggml-*.bin de whisper.cpp)");
            return -1;
        }
        int64_t w = bigfs_write(fs, ino, done, buf, n);
        if (w != (int64_t)n) { kfree(buf); bigfs_unlink(fs, dst); sput(msg, cap, &p, "error escribiendo en el almacen: "); sput(msg, cap, &p, w < 0 ? bigfs_strerror((int)w) : "escritura incompleta"); return -1; }
        done += n;
        uint32_t pct = (uint32_t)((uint64_t)done * 100 / size);
        g_prog = (int)pct;                                     /* SOMA_246: progreso para Ajustes > Voz */
        if (pct / 20 != last_pct / 20) { char b[8]; uint32_t q = 0; sdec(b, sizeof b, &q, pct); kprint("voz: "); kprint(b); kprint(" %\n"); last_pct = pct; }
        thread_yield();
    }
    kfree(buf);
    sput(msg, cap, &p, "modelo instalado en "); sput(msg, cap, &p, dst); sput(msg, cap, &p, " ("); sdec(msg, cap, &p, size >> 20); sput(msg, cap, &p, " MiB)");
    return 0;
}

/* Esta el modelo de un nivel en la carpeta MODELOS del pendrive (o, en QEMU, en fw_cfg)? 0 = si (nombre y tamano). */
int voz_on_stick(int tier, char *name, uint32_t cap, uint32_t *size)
{
    if (tier < 0 || tier >= VOZ_TIERS) return -1;
    uint32_t fsz = fwcfg_open(TIER_FWCFG[tier]);
    if (fsz) { if (name) { uint32_t p = 0; sput(name, cap, &p, TIER_FILE[tier]); } if (size) *size = fsz; return 0; }
    struct fat_dirent *d = (struct fat_dirent *)kmalloc(24 * sizeof *d);   /* lo llaman a la vez Ajustes y el dictado */
    if (!d) return -2;
    const char *why;
    int n = usbdisk_bootdir("MODELOS", d, 24, &why);
    int pick = -1;
    /* SOMA_243: macOS deja copias "._nombre" de 4 KB (metadatos AppleDouble) que tambien acaban en .bin: se ignoran */
    for (int i = 0; i < n; ++i) {
        if (d[i].is_dir || d[i].name[0] == '.' || d[i].size <= (1u << 20) || !ends_with(d[i].name, ".bin") || voz_tier_of(d[i].name) != tier) continue;
        if (pick < 0 || ends_with(d[i].name, TIER_FILE[tier])) pick = i;
    }
    if (pick < 0) { kfree(d); return n < 0 ? -2 : -1; }
    if (name) { uint32_t p = 0; sput(name, cap, &p, d[pick].name); }
    if (size) *size = d[pick].size;
    kfree(d);
    return 0;
}

int voz_install_tier(int tier, char *msg, uint32_t cap)
{
    uint32_t p = 0;
    if (cap) msg[0] = 0;
    if (tier < 0 || tier >= VOZ_TIERS) return -1;
    {   char um[160]; uint64_t us = 0;                                 /* SOMA_267_DISCOS: en MODELOS se usa desde alli */
        if (unid_modelo(tier, um, sizeof um, &us) == 0) {
            sput(msg, cap, &p, "el modelo esta en la unidad MODELOS y se usa directamente desde alli (no hace falta copiarlo): "); sput(msg, cap, &p, um);
            return 0;
        } }
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
    char name[FAT_NAME_MAX + 1]; uint32_t size = 0;
    int st = voz_on_stick(tier, name, sizeof name, &size);
    if (st != 0) {
        sput(msg, cap, &p, st == -2 ? "no se puede leer la carpeta MODELOS del pendrive" : "la carpeta MODELOS del pendrive no tiene el modelo ");
        if (st != -2) { sput(msg, cap, &p, TIER_NAME[tier]); sput(msg, cap, &p, " ("); sput(msg, cap, &p, TIER_FILE[tier]); sput(msg, cap, &p, ")"); }
        sput(msg, cap, &p, ". Copialo en la carpeta voz de la unidad MODELOS del pendrive (o en MODELOS de la unidad SOMA si es un pendrive antiguo).");
        kmutex_unlock(&lk);
        return -1;
    }
    char dst[96]; uint32_t q = 0; sput(dst, sizeof dst, &q, VOZ_DIR "/");
    for (const char *c = name; *c && q + 1 < sizeof dst; ++c) dst[q++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c + 32) : *c;
    dst[q] = 0;
    uint32_t fsz = fwcfg_open(TIER_FWCFG[tier]);               /* reabre fw_cfg: la lectura es secuencial */
    if (fsz) r = copy_into(fs, dst, fsz, 1, 0, msg, cap);
    else { char src[96]; q = 0; sput(src, sizeof src, &q, "MODELOS/"); sput(src, sizeof src, &q, name); r = copy_into(fs, dst, size, 0, src, msg, cap); }
    g_prog = -1;
    kmutex_unlock(&lk);
    return r;
}

/* "voz instalar": el modelo del nivel elegido; si no esta en el pendrive y no hay ninguno instalado, cualquiera que haya. */
int voz_install(char *msg, uint32_t cap)
{
    int want = voz_pref();
    int r = voz_install_tier(want, msg, cap);
    if (r == 0) return 0;
    char m[96];
    if (voz_model(m, sizeof m, 0) == 0) return r;              /* ya hay otro: se usa ese (el aviso dice que falta el elegido) */
    for (int t = 0; t < VOZ_TIERS; ++t) if (t != want && voz_on_stick(t, 0, 0, 0) == 0) return voz_install_tier(t, msg, cap);
    if (cap) { uint32_t p = 0; msg[0] = 0; sput(msg, cap, &p, "el pendrive no tiene ningun modelo. Copia ggml-base-q5_1.bin (whisper.cpp, 60 MB) en la carpeta voz de la unidad MODELOS (desde cualquier ordenador; o pon los modelos en la carpeta modelos/ del proyecto y graba con construyeimagen.py)."); }
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

/* ===================================================================== SOMA_247_HABLA: texto a voz (voces de Piper)
 * La voz (un .onnx de Piper y su .onnx.json) se copia de la carpeta MODELOS del pendrive a /voces del almacen (o, en QEMU, de
 * -fw_cfg name=opt/soma/voz,file=x.onnx y name=opt/soma/voz_json,file=x.onnx.json). El texto va a /voz/leer.txt y voz.elf
 * --habla lo lee en voz alta (sintetiza frase a frase y la manda a la tarjeta de sonido). */
#define VOCES_DIR "/voces"
static volatile int g_sp_state;                            /* 0 nada; 1 preparando o leyendo */
static volatile uint32_t g_sp_ver;
static char g_sp_msg[200];
static char *g_sp_text; static uint32_t g_sp_len;

static int is_onnx(const char *n) { return n[0] != '.' && ends_with(n, ".onnx"); }

static void tts_load(void);
static struct voz_tts_conf g_tts; static int g_tts_loaded;
int voz_voice(char *path, uint32_t cap, uint64_t *size)
{
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return -1;
    uint32_t dir; struct bigfs_stat st;
    tts_load();
    if (g_tts.voz[0]) {                                        /* SOMA_250_TTS: la elegida en Ajustes, si sigue instalada */
        uint32_t vi;
        if (bigfs_lookup(fs, g_tts.voz, &vi) == (int)BIGFS_T_FILE && bigfs_stat(fs, vi, &st) == 0 && st.size >= (1u << 20)) {
            uint32_t p = 0; sput(path, cap, &p, g_tts.voz); if (size) *size = st.size; return 0;
        }
    }
    if (bigfs_lookup(fs, VOCES_DIR, &dir) != (int)BIGFS_T_DIR) return -1;
    uint32_t cur = 0;
    while (bigfs_readdir(fs, dir, &cur, &st) == 1) {
        if (st.type != BIGFS_T_FILE || !is_onnx(st.name) || st.size < (1u << 20)) continue;
        uint32_t p = 0; sput(path, cap, &p, VOCES_DIR "/"); sput(path, cap, &p, st.name);
        if (size) *size = st.size;
        return 0;
    }
    return -1;
}

static void lower_into(char *dst, uint32_t cap, const char *prefix, const char *name)
{
    uint32_t q = 0; sput(dst, cap, &q, prefix);
    for (const char *c = name; *c && q + 1 < cap; ++c) dst[q++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c + 32) : *c;
    dst[q < cap ? q : cap - 1] = 0;
}

int voz_voice_install(char *msg, uint32_t cap)
{
    uint32_t p = 0;
    if (cap) msg[0] = 0;
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { sput(msg, cap, &p, "no hay almacen BIGFS (preparalo con: big format yes)"); return -1; }
    uint32_t ino;
    if (bigfs_lookup(fs, VOCES_DIR, &ino) != (int)BIGFS_T_DIR && bigfs_mkdir(fs, VOCES_DIR) < 0) { sput(msg, cap, &p, "no se pudo crear " VOCES_DIR); return -1; }
    lock();
    int r = -1;
    g_copy_ggml = 0;
    uint32_t sz = fwcfg_open("opt/soma/voz");                  /* QEMU */
    if (sz) {
        r = copy_into(fs, VOCES_DIR "/voz.onnx", sz, 1, 0, msg, cap);
        uint32_t js = fwcfg_open("opt/soma/voz_json");
        if (r == 0 && js) { char m2[96]; (void)copy_into(fs, VOCES_DIR "/voz.onnx.json", js, 1, 0, m2, sizeof m2); }
    } else {
        struct fat_dirent *d = (struct fat_dirent *)kmalloc(32 * sizeof *d);
        const char *why = "sin memoria";
        int n = d ? usbdisk_bootdir("MODELOS", d, 32, &why) : -1;
        int found = 0, copied = 0, have = 0;
        for (int pick = 0; pick < n; ++pick) {                 /* SOMA_250_TTS: todas las voces del pendrive (antes solo la primera) */
        if (d[pick].is_dir || !is_onnx(d[pick].name) || d[pick].size <= (1u << 20)) continue;
        found++;
        {
            char chk[FAT_NAME_MAX + 16]; uint32_t ci; struct bigfs_stat cst;
            lower_into(chk, sizeof chk, VOCES_DIR "/", d[pick].name);
            if (bigfs_lookup(fs, chk, &ci) == (int)BIGFS_T_FILE && bigfs_stat(fs, ci, &cst) == 0 && cst.size == d[pick].size) { have++; continue; }   /* ya esta */
        }
        {
            char src[FAT_NAME_MAX + 16], dst[FAT_NAME_MAX + 16]; uint32_t q = 0;
            sput(src, sizeof src, &q, "MODELOS/"); sput(src, sizeof src, &q, d[pick].name);
            lower_into(dst, sizeof dst, VOCES_DIR "/", d[pick].name);
            r = copy_into(fs, dst, d[pick].size, 0, src, msg, cap);
            if (r == 0) {                                      /* su configuracion: nombre.onnx.json (si esta) */
                for (int i = 0; i < n; ++i) {
                    if (d[i].is_dir || d[i].name[0] == '.' || !ends_with(d[i].name, ".json")) continue;
                    uint32_t a = slen(d[pick].name), b = slen(d[i].name);
                    if (b != a + 5) continue;
                    int same = 1; for (uint32_t k = 0; k < a; ++k) { char x = d[i].name[k], y = d[pick].name[k]; if (x >= 'A' && x <= 'Z') x = (char)(x + 32); if (y >= 'A' && y <= 'Z') y = (char)(y + 32); if (x != y) { same = 0; break; } }
                    if (!same) continue;
                    char js[FAT_NAME_MAX + 16], jd[FAT_NAME_MAX + 16], m2[96]; q = 0;
                    sput(js, sizeof js, &q, "MODELOS/"); sput(js, sizeof js, &q, d[i].name);
                    lower_into(jd, sizeof jd, VOCES_DIR "/", d[i].name);
                    (void)copy_into(fs, jd, d[i].size, 0, js, m2, sizeof m2);
                    break;
                }
                copied++;
            } else break;
        }
        }
        if (!found) {
            sput(msg, cap, &p, n < 0 ? why : "la carpeta MODELOS del pendrive no tiene ninguna voz (.onnx)");
            sput(msg, cap, &p, ". Copia una voz de Piper (el .onnx y su .onnx.json) en la carpeta voz de la unidad MODELOS del pendrive.");
        } else if (r == 0 || (copied == 0 && have > 0)) { r = 0; }
        if (r == 0) { p = 0; msg[0] = 0; sput(msg, cap, &p, copied ? "voces instaladas en " VOCES_DIR ": " : "las voces del pendrive ya estaban instaladas ("); sdec(msg, cap, &p, (uint64_t)(copied ? copied : have)); if (!copied) sput(msg, cap, &p, ")"); }
        if (d) kfree(d);
    }
    g_copy_ggml = 1;
    g_prog = -1;
    kmutex_unlock(&lk);
    if (r == 0 && sz) { p = 0; msg[0] = 0; sput(msg, cap, &p, "voz instalada en " VOCES_DIR); }
    return r;
}

/* Lee el texto en voz alta (sincrono). Con out, la salida del programa va ahi; sin out, a la consola. */
int voz_speak_run(const char *text, uint32_t len, char *out, uint32_t cap)
{
    uint32_t p = 0;
    if (out && cap) out[0] = 0;
    if (!usermode_avx2_ok()) { if (out) sput(out, cap, &p, "[habla] este procesador no tiene AVX2: la lectura en voz alta no esta disponible\n"); else kprint("habla: sin AVX2\n"); return -2; }
    char voice[96]; uint64_t vsize = 0;
    if (voz_voice(voice, sizeof voice, &vsize) != 0) {
        static char msg[256];
        if (!out) kprint("habla: no hay voz en el almacen: instalandola desde el pendrive...\n");
        if (voz_voice_install(msg, sizeof msg) != 0 || voz_voice(voice, sizeof voice, &vsize) != 0) {
            if (out) { sput(out, cap, &p, "[habla] no hay voz instalada: "); sput(out, cap, &p, msg); sput(out, cap, &p, "\n"); } else { kprint("habla: "); kprint(msg); kprint("\n"); }
            return -2;
        }
    }
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return -2;
    uint32_t ino;
    if (bigfs_lookup(fs, "/voz", &ino) != (int)BIGFS_T_DIR) (void)bigfs_mkdir(fs, "/voz");
    if (bigfs_lookup(fs, "/voz/leer.txt", &ino) >= 0) (void)bigfs_unlink(fs, "/voz/leer.txt");
    if (bigfs_create(fs, "/voz/leer.txt", &ino) < 0 || bigfs_write(fs, ino, 0, text, len) != (int64_t)len) {
        if (out) { sput(out, cap, &p, "[habla] no se pudo guardar el texto\n"); }
        return -3;
    }
    /* memoria: la voz (x3: el archivo, los pesos y los reordenados) + el generador (frases largas: ~70 MB por buffer) */
    uint64_t heap_mib = 1024 + (vsize >> 20) * 3;
    if (heap_mib > 3072) heap_mib = 3072;
    char args[USER_ARGS_MAX]; uint32_t q = 0;
    sput(args, sizeof args, &q, "--habla -H "); sdec(args, sizeof args, &q, heap_mib);
    sput(args, sizeof args, &q, " -m "); sput(args, sizeof args, &q, voice);
    tts_load();                                                /* SOMA_250_TTS: velocidad y hablante elegidos */
    if (g_tts.velocidad != 100) { sput(args, sizeof args, &q, " -s "); sdec(args, sizeof args, &q, (uint64_t)(g_tts.velocidad / 100)); sput(args, sizeof args, &q, "."); { uint32_t f = (uint32_t)(g_tts.velocidad % 100); if (f < 10) sput(args, sizeof args, &q, "0"); sdec(args, sizeof args, &q, f); } }
    if (g_tts.hablante > 0 && g_tts.hablante < voz_voice_speakers(voice)) { sput(args, sizeof args, &q, " -S "); sdec(args, sizeof args, &q, (uint64_t)g_tts.hablante); }
    sput(args, sizeof args, &q, " /voz/leer.txt");
    if (q + 2 >= sizeof args) { if (out) sput(out, cap, &p, "[habla] el nombre de la voz es demasiado largo\n"); return -3; }
    if (g_sp_cancel) { g_sp_cancel = 0; if (out) sput(out, cap, &p, "[habla] detenido\n"); return -4; }   /* SOMA_256: paro antes de que empezara */
    int64_t code = 0;
    g_sp_proc = 1;
    int rr = run_proc(args, heap_mib, out, cap, &code);
    g_sp_proc = 0; g_sp_cancel = 0;
    return rr;
}

static char g_sp_out[2048];
static void speak_thread(void *arg)
{
    (void)arg;
    int r = voz_speak_run(g_sp_text, g_sp_len, g_sp_out, sizeof g_sp_out);
    /* el motivo, si fallo: la ultima linea "[habla] ..." que no sea de progreso */
    g_sp_msg[0] = 0;
    if (r != 0) {
        for (const char *l = g_sp_out; *l;) {
            const char *e2 = l; while (*e2 && *e2 != '\n') e2++;
            int inf = (e2 - l) > 8 && l[0] == '[' && (l[1] == 'h' || l[1] == 'v');
            if (inf) { const char *b = l; while (b < e2 && *b != ']') b++; if (b < e2) b++; while (b < e2 && *b == ' ') b++;
                if (!(e2 - b > 5 && b[0] == 'f' && b[1] == 'r' && b[2] == 'a' && b[3] == 's' && b[4] == 'e')) { uint32_t k = 0; while (b < e2 && k + 1 < sizeof g_sp_msg) g_sp_msg[k++] = *b++; g_sp_msg[k] = 0; } }
            l = *e2 ? e2 + 1 : e2;
        }
        if (!g_sp_msg[0]) { uint32_t k = 0; sput(g_sp_msg, sizeof g_sp_msg, &k, "No se pudo leer el texto"); }
    }
    if (g_sp_text) { kfree(g_sp_text); g_sp_text = 0; }
    g_sp_cancel = 0;
    g_sp_state = 0; g_sp_ver++;
}

int voz_speak_start(const char *text, uint32_t len)
{
    if (g_sp_state) return -16;
    if (!len) return -22;
    if (len > 200000) len = 200000;
    char *t = (char *)kmalloc(len + 1);
    if (!t) return -12;
    for (uint32_t i = 0; i < len; ++i) t[i] = text[i];
    t[len] = 0;
    g_sp_text = t; g_sp_len = len; g_sp_msg[0] = 0; g_sp_cancel = 0;
    g_sp_state = 1; g_sp_ver++;
    if (!thread_create("habla", speak_thread, 0)) { kfree(t); g_sp_text = 0; g_sp_state = 0; g_sp_ver++; return -11; }
    return 0;
}
void voz_speak_stop(void) { if (g_sp_state || g_sp_proc) { g_sp_cancel = 1; voz_stop(); } }   /* SOMA_256: tambien durante la preparacion y la orden habla */
int voz_speaking(void) { return g_sp_state || g_sp_proc; }
int voz_speak_state(void) { return g_sp_state; }
uint32_t voz_speak_ver(void) { return g_sp_ver; }
const char *voz_speak_msg(void) { return g_sp_msg; }

/* ===================================================================== SOMA_250_TTS: preferencias, varias voces y el orbe que habla */
static int conf_int(const char *b, const char *key, int def)
{
    uint32_t kl = slen(key);
    for (const char *l = b; *l; ) {
        uint32_t k = 0; while (k < kl && l[k] == key[k]) k++;
        if (k == kl && l[k] == '=') { const char *v = l + k + 1; int neg = *v == '-'; if (neg) v++; int n = 0, any = 0; while (*v >= '0' && *v <= '9') { n = n * 10 + (*v++ - '0'); any = 1; } return any ? (neg ? -n : n) : def; }
        while (*l && *l != '\n') l++;
        if (*l) l++;
    }
    return def;
}
static void conf_str(const char *b, const char *key, char *out, uint32_t cap)
{
    uint32_t kl = slen(key); out[0] = 0;
    for (const char *l = b; *l; ) {
        uint32_t k = 0; while (k < kl && l[k] == key[k]) k++;
        if (k == kl && l[k] == '=') { const char *v = l + k + 1; uint32_t n = 0; while (v[n] && v[n] != '\n' && v[n] != '\r' && n + 1 < cap) { out[n] = v[n]; n++; } out[n] = 0; return; }
        while (*l && *l != '\n') l++;
        if (*l) l++;
    }
}
static void tts_load(void)
{
    if (g_tts_loaded) return;
    g_tts_loaded = 1;
    g_tts.leer_orbe = 1; g_tts.velocidad = 100; g_tts.hablante = 0; g_tts.voz[0] = 0;   /* por defecto: el orbe habla */
    char b[256]; int n = vfs_read("/etc/habla.conf", b, sizeof b - 1);
    if (n <= 0) return;
    b[n] = 0;
    g_tts.leer_orbe = conf_int(b, "leer_orbe", 1) ? 1 : 0;
    g_tts.velocidad = conf_int(b, "velocidad", 100);
    if (g_tts.velocidad < 70 || g_tts.velocidad > 160) g_tts.velocidad = 100;
    g_tts.hablante = conf_int(b, "hablante", 0); if (g_tts.hablante < 0 || g_tts.hablante > 255) g_tts.hablante = 0;
    conf_str(b, "voz", g_tts.voz, sizeof g_tts.voz);
}
void voz_tts_get(struct voz_tts_conf *c) { tts_load(); *c = g_tts; }
void voz_tts_set(const struct voz_tts_conf *c)
{
    tts_load(); g_tts = *c;
    if (g_tts.velocidad < 70 || g_tts.velocidad > 160) g_tts.velocidad = 100;
    char b[256]; uint32_t p = 0;
    sput(b, sizeof b, &p, "leer_orbe="); sdec(b, sizeof b, &p, (uint64_t)(g_tts.leer_orbe ? 1 : 0));
    sput(b, sizeof b, &p, "\nvelocidad="); sdec(b, sizeof b, &p, (uint64_t)g_tts.velocidad);
    sput(b, sizeof b, &p, "\nhablante="); sdec(b, sizeof b, &p, (uint64_t)g_tts.hablante);
    sput(b, sizeof b, &p, "\nvoz="); sput(b, sizeof b, &p, g_tts.voz); sput(b, sizeof b, &p, "\n");
    (void)vfs_write("/etc/habla.conf", b, p);
}
int voz_voice_list(char (*paths)[96], uint64_t *sizes, int max)
{
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return 0;
    uint32_t dir; struct bigfs_stat st;
    if (bigfs_lookup(fs, VOCES_DIR, &dir) != (int)BIGFS_T_DIR) return 0;
    uint32_t cur = 0; int n = 0;
    while (n < max && bigfs_readdir(fs, dir, &cur, &st) == 1) {
        if (st.type != BIGFS_T_FILE || !is_onnx(st.name) || st.size < (1u << 20)) continue;
        uint32_t p = 0; paths[n][0] = 0; sput(paths[n], 96, &p, VOCES_DIR "/"); sput(paths[n], 96, &p, st.name);
        if (sizes) sizes[n] = st.size;
        n++;
    }
    return n;
}
int voz_voice_speakers(const char *path)
{
    int e = 0; struct bigfs *fs = bigfs_sys(&e);
    if (!fs || !path) return 1;
    char jp[112]; uint32_t q = 0; sput(jp, sizeof jp, &q, path); sput(jp, sizeof jp, &q, ".json");
    uint32_t ino; struct bigfs_stat st;
    if (bigfs_lookup(fs, jp, &ino) != (int)BIGFS_T_FILE || bigfs_stat(fs, ino, &st) != 0) return 1;
    static char b[8192]; uint64_t n = st.size < sizeof b - 1 ? st.size : sizeof b - 1;
    int64_t r = bigfs_read(fs, ino, 0, b, n);
    if (r <= 0) return 1;
    b[r] = 0;
    for (int64_t i = 0; i + 14 < r; i++) {
        if (b[i] == 'n' && b[i + 1] == 'u' && b[i + 2] == 'm' && b[i + 3] == '_' && b[i + 4] == 's' && b[i + 5] == 'p' && b[i + 6] == 'e' && b[i + 7] == 'a' && b[i + 8] == 'k') {
            const char *v = b + i + 9; while (*v && (*v < '0' || *v > '9')) { if (*v == ',' || *v == '}') return 1; v++; }
            int k = 0; while (*v >= '0' && *v <= '9') k = k * 10 + (*v++ - '0');
            return k > 0 ? k : 1;
        }
    }
    return 1;
}
void voz_voice_label(const char *path, char *out, uint32_t cap)
{
    const char *b = path; for (const char *t = path; *t; t++) if (*t == '/') b = t + 1;
    char nm[64]; uint32_t k = 0; while (b[k] && k + 1 < sizeof nm) { nm[k] = b[k]; k++; } nm[k] = 0;
    if (k > 5 && ends_with(nm, ".onnx")) nm[k - 5] = 0;
    /* idioma-nombre-calidad (es_es-davefx-medium) */
    char part[3][32]; int np = 0; uint32_t j = 0; part[0][0] = part[1][0] = part[2][0] = 0;
    for (const char *t = nm; ; t++) {
        if (*t == '-' || !*t) { if (np < 3) part[np][j < 32 ? j : 31] = 0; np++; j = 0; if (!*t) break; continue; }
        if (np < 3 && j + 1 < 32) part[np][j++] = *t;
    }
    uint32_t p = 0; if (cap) out[0] = 0;
    if (np < 3) { sput(out, cap, &p, nm); return; }
    char who[32]; uint32_t w = 0;
    for (const char *t = part[1]; *t && w + 1 < sizeof who; t++) { char ch = *t == '_' ? ' ' : *t; if (w == 0 && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32); who[w] = ch; w++; }
    who[w] = 0;
    sput(out, cap, &p, who); sput(out, cap, &p, " (");
    static const char *const L[][2] = { { "es_es", "Espa\xC3\xB1" "a" }, { "es_mx", "M\xC3\xA9xico" }, { "es_ar", "Argentina" }, { "es_419", "Latinoam\xC3\xA9rica" },
                                        { "en_us", "ingl\xC3\xA9s, EE. UU." }, { "en_gb", "ingl\xC3\xA9s, Reino Unido" }, { "ca_es", "catal\xC3\xA1n" }, { "fr_fr", "franc\xC3\xA9s" },
                                        { "de_de", "alem\xC3\xA1n" }, { "it_it", "italiano" }, { "pt_pt", "portugu\xC3\xA9s" }, { "pt_br", "portugu\xC3\xA9s, Brasil" } };
    const char *lang = part[0];
    for (uint32_t i = 0; i < sizeof L / sizeof L[0]; i++) { const char *a = L[i][0], *c = part[0]; while (*a && (*c | 32) == *a) { a++; c++; } if (!*a && !*c) { lang = L[i][1]; break; } }
    sput(out, cap, &p, lang);
    const char *ql = part[2][0] == 'x' ? "muy baja" : part[2][0] == 'l' ? "baja" : part[2][0] == 'm' ? "media" : part[2][0] == 'h' ? "alta" : part[2];
    sput(out, cap, &p, ", calidad "); sput(out, cap, &p, ql); sput(out, cap, &p, ")");
}
/* El orbe lee su respuesta: si esta activado y hay voz (o se puede instalar UNA vez desde el pendrive). Corta la lectura anterior.
 * Todo en un hilo: buscar o instalar la voz lee el almacen o el pendrive y el escritorio no debe esperar. */
static int g_reply_noinst;
static volatile uint32_t g_reply_seq;
struct reply_job { char *text; uint32_t len, seq; };
static void reply_thread(void *arg)
{
    struct reply_job *j = (struct reply_job *)arg;
    /* SOMA_330_RESPUESTA_PRIMERO: la respuesta manda. Lo que se este leyendo (el aviso "se lo paso al modelo...") se corta YA: se para el
     * proceso y se tira el sonido que quedaba en la cola de la tarjeta, sin esperar a que termine la frase. */
    if (voz_speaking()) { voz_speak_stop(); (void)audio_sys_flush(); }
    for (int i = 0; i < 3000 && voz_speaking(); i++) {
        thread_sleep(1);
        if (i == 1500) { voz_speak_stop(); (void)audio_sys_flush(); }   /* por si la parada llego en el momento de lanzarlo */
    }
    char vp[96]; uint64_t vs = 0;
    int ok = j->seq == g_reply_seq;                                    /* si llego otra respuesta mientras, gana la nueva */
    if (ok && voz_voice(vp, sizeof vp, &vs) != 0) {
        static char m[200];
        if (g_reply_noinst || voz_voice_install(m, sizeof m) != 0 || voz_voice(vp, sizeof vp, &vs) != 0) {
            if (!g_reply_noinst) { kprint("habla: el orbe no puede leer en voz alta: "); kprint(m); kprint("\n"); }
            g_reply_noinst = 1; ok = 0;
        }
    }
    if (ok && j->seq == g_reply_seq && !g_sp_state) (void)voz_speak_start(j->text, j->len);
    kfree(j->text); kfree(j);
}
uint32_t voz_reply_seq(void) { return g_reply_seq; }      /* SOMA_330: cambia con cada respuesta que el orbe manda leer */
int voz_speak_reply(const char *text)
{
    tts_load();
    if (!g_tts.leer_orbe || !text || !*text || !usermode_avx2_ok()) return -1;
    struct reply_job *j = (struct reply_job *)kmalloc(sizeof *j);
    char *c = (char *)kmalloc(2048);
    if (!j || !c) { if (j) kfree(j); if (c) kfree(c); return -12; }
    j->len = habla_limpia(text, c, 2048, 1500);
    if (!j->len) { kfree(c); kfree(j); return -22; }
    j->text = c; j->seq = ++g_reply_seq;
    if (!thread_create("habla_orbe", reply_thread, j)) { kfree(c); kfree(j); return -11; }
    return 0;
}
