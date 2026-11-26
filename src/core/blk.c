/* PARCHE 045: ver blk.h */
#include "blk.h"

static struct blkdev devs[BLK_MAX_DEV];
static int ndev;

void blk_reset(void) { ndev = 0; }

static void copy_name(char *d, const char *s) { int i = 0; for (; s[i] && i < (int)sizeof(((struct blkdev *)0)->name) - 1; i++) d[i] = s[i]; d[i] = 0; }

int blk_register(const struct blkdev *d)
{
    if (!d || !d->read || !d->sectors) return BLK_E_ARG;
    for (int i = 0; i < ndev; i++) { int same = 1; for (int k = 0; k < 12; k++) { if (devs[i].name[k] != d->name[k]) { same = 0; break; } if (!d->name[k]) break; } if (same) { devs[i] = *d; return i; } }
    if (ndev >= BLK_MAX_DEV) return BLK_E_ARG;
    devs[ndev] = *d; copy_name(devs[ndev].name, d->name);
    return ndev++;
}

int blk_count(void) { return ndev; }
const struct blkdev *blk_get(int i) { return (i >= 0 && i < ndev) ? &devs[i] : 0; }

int blk_find(const char *name)
{
    for (int i = 0; i < ndev; i++) { int k = 0; while (name[k] && name[k] == devs[i].name[k]) k++; if (!name[k] && !devs[i].name[k]) return i; }
    return -1;
}

static int check(int dev, uint64_t lba, uint64_t n)
{
    if (dev < 0 || dev >= ndev || n == 0) return BLK_E_ARG;
    if (lba >= devs[dev].sectors || n > devs[dev].sectors - lba) return BLK_E_RANGE;
    return 0;
}

int blk_read(int dev, uint64_t lba, uint32_t n, void *buf)
{
    int r = check(dev, lba, n); if (r) return r;
    uint8_t *p = buf;
    while (n) {
        uint32_t c = n > BLK_CHUNK ? BLK_CHUNK : n;
        if (devs[dev].read(lba, c, p) != 0) return BLK_E_IO;
        lba += c; n -= c; p += (uint64_t)c * 512;
    }
    return 0;
}

int blk_write(int dev, uint64_t lba, uint32_t n, const void *buf)
{
    int r = check(dev, lba, n); if (r) return r;
    if (!devs[dev].write || !devs[dev].test_disk) return BLK_E_RO;
    const uint8_t *p = buf;
    while (n) {
        uint32_t c = n > BLK_CHUNK ? BLK_CHUNK : n;
        if (devs[dev].write(lba, c, p) != 0) return BLK_E_IO;
        lba += c; n -= c; p += (uint64_t)c * 512;
    }
    return 0;
}

int blk_desbloquea(int dev)
{
    if (dev < 0 || dev >= ndev || !devs[dev].write) return BLK_E_RO;
    devs[dev].test_disk = 1;
    return 0;
}

int blk_copy(int src, uint64_t slba, int dst, uint64_t dlba, uint64_t n, int verify)
{
    static uint8_t a[BLK_CHUNK * 512], b[BLK_CHUNK * 512];
    int r = check(src, slba, n); if (r) return r;
    if ((r = check(dst, dlba, n)) != 0) return r;
    if (!devs[dst].write || !devs[dst].test_disk) return BLK_E_RO;
    if (src == dst) {                                          /* mismo dispositivo: los intervalos no pueden solaparse */
        if (slba < dlba + n && dlba < slba + n) return BLK_E_ARG;
    }
    while (n) {
        uint32_t c = n > BLK_CHUNK ? BLK_CHUNK : (uint32_t)n;
        if ((r = blk_read(src, slba, c, a)) != 0) return r;
        if ((r = blk_write(dst, dlba, c, a)) != 0) return r;
        if (verify) {
            if ((r = blk_read(dst, dlba, c, b)) != 0) return r;
            for (uint32_t i = 0; i < c * 512; i++) if (a[i] != b[i]) return BLK_E_VERIFY;
        }
        slba += c; dlba += c; n -= c;
    }
    return 0;
}

const char *blk_err_str(int e)
{
    switch (e) {
    case 0: return "OK";
    case BLK_E_ARG: return "argumentos invalidos";
    case BLK_E_RANGE: return "fuera de rango";
    case BLK_E_IO: return "error de E/S del dispositivo";
    case BLK_E_RO: return "dispositivo protegido contra escritura (solo discos de prueba)";
    case BLK_E_VERIFY: return "la verificacion fallo";
    default: return "error";
    }
}
