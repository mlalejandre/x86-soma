#include <stdint.h>
#include "console.h"
#include "io.h"
#include "pci.h"
#include "mem.h"
#include "mutex.h"
#include "virtio_blk.h"
#include "arch.h"

#define VIRTIO_VENDOR_ID       0x1AF4
#define VIRTIO_BLK_LEGACY_ID   0x1001

#define REG_DEVICE_FEATURES    0x00
#define REG_DRIVER_FEATURES    0x04
#define REG_QUEUE_ADDRESS      0x08
#define REG_QUEUE_SIZE         0x0C
#define REG_QUEUE_SELECT       0x0E
#define REG_QUEUE_NOTIFY       0x10
#define REG_STATUS             0x12
#define REG_CONFIG_CAPACITY    0x14   /* SOMA_PARCHE_018_BIGFS: config del dispositivo (legacy, sin MSI-X): capacidad en sectores */

#define ST_ACKNOWLEDGE         0x01
#define ST_DRIVER              0x02
#define ST_DRIVER_OK           0x04
#define ST_FAILED              0x80

#define VIRTIO_BLK_T_IN        0
#define VIRTIO_BLK_T_OUT       1

#ifdef __aarch64__
#define VIRTIO_MMIO_MAGIC_VALUE     0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c
#define VIRTIO_MMIO_QUEUE_PFN       0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_CONFIG          0x100

#define VIRTIO_MMIO_BASE_START      0x0a000000ULL
#define VIRTIO_MMIO_STRIDE          0x200
#define VIRTIO_MMIO_MAX_DEVICES     32
#define VIRTIO_DEV_ID_BLOCK         2
#endif

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
};

struct vq {
    volatile struct virtq_desc  *desc;       /* offset 0 */
    volatile struct virtq_avail *avail;      /* offset 8 */
    volatile struct virtq_used  *used;       /* offset 16 */
    uint32_t size;                           /* offset 24 (4-byte aligned) */
    uint32_t avail_idx;                      /* offset 28 (4-byte aligned) */
    uint32_t last_used;                      /* offset 32 (4-byte aligned) */
    uint32_t pad;                            /* offset 36 -> total 40 bytes */
} __attribute__((aligned(16)));

struct virtio_blk_req {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} __attribute__((aligned(16)));

#ifndef __aarch64__
static uint16_t io_base = 0;
#endif
#ifdef __aarch64__
static uintptr_t mmio_base = 0;
#endif

static struct vq blkq __attribute__((aligned(16)));
static struct kmutex blk_mutex __attribute__((aligned(16)));
static int blk_ready = 0;
static int blk_broken = 0;

static uint8_t blk_memory[16384] __attribute__((aligned(4096)));
static volatile struct virtio_blk_req req __attribute__((aligned(16)));
static volatile uint8_t status_byte __attribute__((aligned(16)));
/* SOMA_PARCHE_018_BIGFS: bufer de rebote de hasta 128 sectores (64 KiB): una sola peticion mueve hasta 64 KiB
 * (antes, un sector por peticion: la lectura de archivos grandes era ~60 veces mas lenta). */
#define BLK_MAX_SECTORS 128
static volatile uint8_t buffer[BLK_MAX_SECTORS * 512] __attribute__((aligned(4096)));

#ifdef __aarch64__
static inline uint32_t mmio_read(uint32_t reg)
{
    volatile uint32_t *p = (volatile uint32_t *)(mmio_base + reg);
    return *p;
}
static inline void mmio_write(uint32_t reg, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)(mmio_base + reg);
    *p = val;
    arch_memory_barrier();
}
#endif

int virtio_blk_init(void)
{
    kmutex_init(&blk_mutex, "virtio_blk_lock");
    kprint("\nVIRTIO-BLK INIT\n");
    kprint("---------------\n");

#ifdef __aarch64__
#include "platform.h"
#if !SOMA_HAVE_VIRTIO
    kprint("virtio: no existe en esta plataforma\n");
    return 0;
#endif
    mmio_base = 0;
    for (int i = 0; i < VIRTIO_MMIO_MAX_DEVICES; ++i) {
        uintptr_t base = VIRTIO_MMIO_BASE_START + (uint64_t)i * VIRTIO_MMIO_STRIDE;
        volatile uint32_t *p = (volatile uint32_t *)base;
        if (p[VIRTIO_MMIO_MAGIC_VALUE / 4] != 0x74726976) continue;
        if (p[VIRTIO_MMIO_DEVICE_ID / 4] == VIRTIO_DEV_ID_BLOCK) {
            mmio_base = base;
            kprint("Encontrado blk en slot ");
            kprint_dec((uint32_t)i);
            kprint("\n");
            break;
        }
    }
    if (!mmio_base) {
        kprint("virtio-blk-mmio no encontrado.\n");
        return 0;
    }

    mmio_write(VIRTIO_MMIO_STATUS, 0);
    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE);
    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE | ST_DRIVER);
    mmio_write(VIRTIO_MMIO_DRIVER_FEATURES, 0);
    mmio_write(VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
    mmio_write(VIRTIO_MMIO_QUEUE_SEL, 0);

    uint32_t max = mmio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0 || max > 256) {
        kprint("ERROR tamano cola\n");
        return 0;
    }

    uint16_t qsize = 8;
    if (qsize > max) qsize = (uint16_t)max;
    mmio_write(VIRTIO_MMIO_QUEUE_NUM, qsize);

    uint32_t addr = (uint32_t)(uintptr_t)blk_memory;
    for (uint32_t i = 0; i < sizeof(blk_memory); i++) blk_memory[i] = 0;
    arch_memory_barrier();

    blkq.desc  = (volatile struct virtq_desc *)(uintptr_t)addr;
    blkq.avail = (volatile struct virtq_avail *)(uintptr_t)(addr + 16 * qsize);
    uint32_t used_off = 4096;
    blkq.used  = (volatile struct virtq_used *)(uintptr_t)(addr + used_off);
    blkq.size  = qsize;
    blkq.avail_idx = 0;
    blkq.last_used = 0;
    blkq.pad = 0;

    mmio_write(VIRTIO_MMIO_QUEUE_ALIGN, 4096);
    mmio_write(VIRTIO_MMIO_QUEUE_PFN, addr / 4096);

    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE | ST_DRIVER | ST_DRIVER_OK);
    uint32_t st = mmio_read(VIRTIO_MMIO_STATUS);

    if (st & ST_FAILED) {
        kprint("virtio-blk ERROR: FAILED status\n");
        return 0;
    }

    blk_ready = 1;
    kprint("virtio-blk ready (Disco persistente conectado)\n");
    return 1;

#else
    uint32_t bar0 = 0;
    int found = 0;
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t function = 0; function < 8; ++function) {
                if (pci_vendor_id(bus, slot, function) == VIRTIO_VENDOR_ID &&
                    pci_device_id(bus, slot, function) == VIRTIO_BLK_LEGACY_ID) {
                    bar0 = pci_bar0(bus, slot, function);
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (found) break;
    }
    if (!found || !(bar0 & 1)) {
        kprint("virtio-blk no encontrado o BAR0 invalido.\n");
        return 0;
    }
    io_base = (uint16_t)(bar0 & 0xFFFC);
    kprint("I/O base: 0x"); kprint_hex16(io_base); kprint("\n");
    outb(io_base + REG_STATUS, 0);
    outb(io_base + REG_STATUS, ST_ACKNOWLEDGE | ST_DRIVER);
    outl(io_base + REG_DRIVER_FEATURES, 0);
    outw(io_base + REG_QUEUE_SELECT, 0);
    uint16_t max = inw(io_base + REG_QUEUE_SIZE);
    kprint("Queue 0 max size: "); kprint_dec(max); kprint("\n");
    if (max == 0 || max > 256) return 0;
    uint32_t addr = (uint32_t)(uintptr_t)blk_memory;
    for (uint32_t i = 0; i < sizeof(blk_memory); i++) blk_memory[i] = 0;
    blkq.desc = (void*)(uintptr_t)addr;
    blkq.avail = (void*)(uintptr_t)(addr + 16 * max);
    blkq.used = (void*)(uintptr_t)((addr + 16 * max + 6 + 2 * max + 4095) & ~4095);
    blkq.size = max;
    blkq.avail_idx = 0;
    blkq.last_used = 0;
    blkq.pad = 0;
    outl(io_base + REG_QUEUE_ADDRESS, addr / 4096);
    outb(io_base + REG_STATUS, ST_ACKNOWLEDGE | ST_DRIVER | ST_DRIVER_OK);
    uint8_t status = inb(io_base + REG_STATUS);
    if (status & ST_FAILED) {
        kprint("virtio-blk ERROR: FAILED status.\n");
        return 0;
    }
    blk_ready = 1;
    kprint("virtio-blk ready (Disco duro conectado)\n");
    return 1;
#endif
}

static int virtio_blk_opn(uint64_t sector, void *buf, uint32_t nsec, int write)
{
    if (!blk_ready || blk_broken) return -1;
    if (nsec == 0 || nsec > BLK_MAX_SECTORS) return -1;
    kmutex_lock(&blk_mutex);
    req.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req.ioprio = 0;
    req.sector = sector;
    status_byte = 0xFF;
    if (write) memcpy((void*)buffer, buf, (size_t)nsec * 512);
    
    blkq.desc[0].addr = (uint64_t)(uintptr_t)&req;
    blkq.desc[0].len = 16;
    blkq.desc[0].flags = 1;
    blkq.desc[0].next = 1;
    
    blkq.desc[1].addr = (uint64_t)(uintptr_t)buffer;
    blkq.desc[1].len = nsec * 512;
    blkq.desc[1].flags = 1 | (write ? 0 : 2);
    blkq.desc[1].next = 2;
    
    blkq.desc[2].addr = (uint64_t)(uintptr_t)&status_byte;
    blkq.desc[2].len = 1;
    blkq.desc[2].flags = 2;
    blkq.desc[2].next = 0;
    
    blkq.avail->ring[blkq.avail_idx % blkq.size] = 0;
    arch_memory_barrier();
    blkq.avail_idx++;
    blkq.avail->idx = (uint16_t)blkq.avail_idx;
    arch_memory_barrier();
    
#ifdef __aarch64__
    mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
#else
    outw(io_base + REG_QUEUE_NOTIFY, 0);
#endif
    
    uint64_t start = arch_cycle_counter();
    extern uint64_t tsc_freq_mhz;
    uint64_t limit_ticks = (tsc_freq_mhz > 0 ? tsc_freq_mhz * 1000000ULL : 62500000ULL) * 2;
    
    while (blkq.used->idx == (uint16_t)blkq.last_used) {
        if ((arch_cycle_counter() - start) > limit_ticks) {
            blk_broken = 1;
            kprint("\nvirtio-blk ERROR: timeout en sector ");
            kprint_dec((uint32_t)sector);
            kprint("\n");
            kmutex_unlock(&blk_mutex);
            return -1;
        }
        arch_pause();
    }
    
    blkq.last_used++;
    arch_memory_barrier();
    
    if (status_byte != 0) {
        kprint("\nvirtio-blk ERROR: status_byte=");
        kprint_hex8(status_byte);
        kprint("\n");
        kmutex_unlock(&blk_mutex);
        return -1;
    }
    
    if (!write) memcpy(buf, (void*)buffer, (size_t)nsec * 512);
    kmutex_unlock(&blk_mutex);
    return 0;
}

static const struct sysdisk_ops *g_alt;                     /* SOMA_162_T480S */
int virtio_blk_set_alt(const struct sysdisk_ops *o) { if (blk_ready) return -1; g_alt = o; return 0; }
const char *virtio_blk_alt_name(void) { return g_alt ? g_alt->name : 0; }
/* SOMA_162_T480S: una linea para la portada y 'disco' (antes era un texto fijo "RamFS (16 MiB virtio-blk)") */
const char *sysdisk_describe(void)
{
    static char b[96]; uint32_t k = 0;
    const char *t; uint64_t mib = virtio_blk_capacity() / 2048;
    if (g_alt) t = "SOMAFS en el pendrive (SOMADATA, ";
    else if (mib) t = "SOMAFS en virtio-blk (";
    else t = "solo en RAM: se pierde al apagar";
    while (*t && k < sizeof b - 24) b[k++] = *t++;
    if (g_alt || mib) {
        char d[20]; int n = 0; uint64_t v = mib; do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n) b[k++] = d[--n];
        t = " MiB)"; while (*t) b[k++] = *t++;
    }
    b[k] = 0;
    return b;
}

int virtio_blk_flush(void)
{
    if (g_alt) return g_alt->flush ? g_alt->flush() : 0;
    return (blk_ready && !blk_broken) ? 0 : -1;     /* write-through por no negociar F_FLUSH (ver virtio_blk.h) */
}

static int virtio_blk_op(uint64_t sector, void *buf, int write)
{
    return virtio_blk_opn(sector, buf, 1, write);
}

/* E/S de varios sectores (hasta BLK_MAX_SECTORS por peticion; trocea lo que pase de ahi) */
int virtio_blk_read_n(uint64_t sector, void *buf, uint32_t nsec)
{
    if (g_alt) return g_alt->read(sector, buf, nsec);
    uint8_t *p = (uint8_t *)buf;
    while (nsec) {
        uint32_t n = nsec > BLK_MAX_SECTORS ? BLK_MAX_SECTORS : nsec;
        if (virtio_blk_opn(sector, p, n, 0) != 0) return -1;
        sector += n; p += (size_t)n * 512; nsec -= n;
    }
    return 0;
}

int virtio_blk_write_n(uint64_t sector, const void *buf, uint32_t nsec)
{
    if (g_alt) return g_alt->write(sector, buf, nsec);
    const uint8_t *p = (const uint8_t *)buf;
    while (nsec) {
        uint32_t n = nsec > BLK_MAX_SECTORS ? BLK_MAX_SECTORS : nsec;
        if (virtio_blk_opn(sector, (void *)p, n, 1) != 0) return -1;
        sector += n; p += (size_t)n * 512; nsec -= n;
    }
    return 0;
}

/* Capacidad del disco en sectores de 512 B (0 si no se conoce) */
uint64_t virtio_blk_capacity(void)
{
    if (g_alt) return g_alt->sectors;
    if (!blk_ready) return 0;
#ifdef __aarch64__
    return (uint64_t)mmio_read(VIRTIO_MMIO_CONFIG) | ((uint64_t)mmio_read(VIRTIO_MMIO_CONFIG + 4) << 32);
#else
    return (uint64_t)inl(io_base + REG_CONFIG_CAPACITY) | ((uint64_t)inl(io_base + REG_CONFIG_CAPACITY + 4) << 32);
#endif
}

int virtio_blk_read(uint64_t sector, void *buf)
{
    if (g_alt) return g_alt->read(sector, buf, 1);
    return virtio_blk_op(sector, buf, 0);
}

int virtio_blk_write(uint64_t sector, const void *buf)
{
    if (g_alt) return g_alt->write(sector, buf, 1);
    return virtio_blk_op(sector, (void *)buf, 1);
}
