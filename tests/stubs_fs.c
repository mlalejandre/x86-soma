/* Sustitutos para probar fs.c en host: RAM-disk de 16 MiB, mutex nulos, red/RTC/imagenes. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "host_stubs.h"
#include "mutex.h"
#include "rtc.h"
#include "net.h"
#include "image.h"
#include "virtio_net.h"

#define DISK_SECTORS 32768u
static uint8_t g_disk[DISK_SECTORS * 512u];
static int g_writes;
static int g_fail_after = -1;

/* PARCHE 087: almacen de archivos grandes simulado con archivos del host (persiste entre procesos, como el disco) */
#include <sys/stat.h>
#include "fs.h"
static void spill_name(int slot, char *p) { snprintf(p, 96, "build/tests/spill/%03d", slot); }
int fs_spill_ready(void) { mkdir("build", 0755); mkdir("build/tests", 0755); mkdir("build/tests/spill", 0755); return 1; }
int fs_spill_put(int slot, const char *name, const char *data, uint32_t len)
{
    char p[96]; spill_name(slot, p); FILE *f = fopen(p, "wb"); if (!f) return -1;
    char h[64]; memset(h, 0, 64); memcpy(h, "RFS1", 4); strncpy(h + 4, name, 47); memcpy(h + 52, &len, 4);
    int ok = fwrite(h, 1, 64, f) == 64 && (len == 0 || fwrite(data, 1, len, f) == len);
    fclose(f); return ok ? 0 : -1;
}
int fs_spill_stat(int slot, char *name_out, uint32_t *len_out)
{
    char p[96]; spill_name(slot, p); FILE *f = fopen(p, "rb"); if (!f) return -1;
    char h[64]; int ok = fread(h, 1, 64, f) == 64 && !memcmp(h, "RFS1", 4); fclose(f); if (!ok) return -1;
    if (name_out) { memcpy(name_out, h + 4, 48); name_out[47] = 0; }
    if (len_out) memcpy(len_out, h + 52, 4);
    return 0;
}
int fs_spill_get(int slot, char *buf, uint32_t len)
{
    char p[96]; spill_name(slot, p); FILE *f = fopen(p, "rb"); if (!f) return -1;
    int ok = fseek(f, 64, SEEK_SET) == 0 && fread(buf, 1, len, f) == len; fclose(f); return ok ? 0 : -1;
}
void fs_spill_del(int slot) { char p[96]; spill_name(slot, p); remove(p); }
void host_disk_reset(void) { memset(g_disk, 0, sizeof(g_disk)); g_writes = 0; g_fail_after = -1; for (int i = 0; i < 256; i++) fs_spill_del(i); }
int  host_disk_writes(void) { return g_writes; }
void host_disk_fail_after(int n) { g_fail_after = n; }

int host_disk_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(g_disk, 1, sizeof(g_disk), f);
    fclose(f);
    return n == sizeof(g_disk) ? 0 : -1;
}
int host_disk_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(g_disk, 1, sizeof(g_disk), f);
    fclose(f);
    return n == sizeof(g_disk) ? 0 : -1;
}

static int g_reads;                                  /* SOMA_269_RAM */
int host_disk_reads(void) { return g_reads; }
int sysdisk_read(uint64_t sector, void *buf)
{
    g_reads++;
    if (sector >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk + sector * 512u, 512);
    return 0;
}
uint64_t sysdisk_capacity(void) { return DISK_SECTORS; }   /* PARCHE 083 */
int sysdisk_flush(void) { return 0; }   /* PARCHE 115 */
int sysdisk_write(uint64_t sector, const void *buf)
{
    if (sector >= DISK_SECTORS) return -1;
    if (g_fail_after == 0) return -1;
    if (g_fail_after > 0) g_fail_after--;
    memcpy(g_disk + sector * 512u, buf, 512);
    g_writes++;
    return 0;
}

void kmutex_init(struct kmutex *m, const char *name) { (void)m; (void)name; }
void kmutex_lock(struct kmutex *m) { (void)m; }
void kmutex_unlock(struct kmutex *m) { (void)m; }

void rtc_get_datetime(struct rtc_time *t)
{
    t->year = 2026; t->month = 11; t->day = 3; t->hour = 12; t->min = 0; t->sec = 0;
}
int  arp_format_cache(char *out, uint32_t max) { if (max) out[0] = 0; return 0; }
void virtio_net_stats(uint32_t *a, uint32_t *b, uint64_t *c, uint64_t *d) { *a = *b = 0; *c = *d = 0; }
int  image_create_demo_bmp(uint8_t *o, size_t m, uint32_t w, uint32_t h) { (void)o;(void)m;(void)w;(void)h; return 0; }
int  image_create_chip_bmp(uint8_t *o, size_t m, uint32_t w, uint32_t h) { (void)o;(void)m;(void)w;(void)h; return 0; }

/* PARCHE 089: lo define mem.c, que estas pruebas no enlazan */
volatile unsigned char kheap_owner;
