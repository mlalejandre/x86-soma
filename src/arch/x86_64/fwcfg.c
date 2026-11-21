/* PARCHE 138: QEMU fw_cfg por E/S (selector 16 bits en 0x510, datos en 0x511). Sin QEMU la firma no coincide y no se lee nada. */
#include "fwcfg.h"
#include "io.h"

static void sel(uint16_t k) { outw(0x510, k); }
static uint8_t rd8(void) { return inb(0x511); }
static uint32_t rdbe32(void) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | rd8(); return v; }
static uint16_t rdbe16(void) { uint16_t v = (uint16_t)(rd8() << 8); return (uint16_t)(v | rd8()); }

uint32_t fwcfg_read_file(const char *name, char *out, uint32_t max)
{
    if (!out || max == 0) return 0;
    out[0] = 0;
    sel(0x0000);
    if (rd8() != 'Q' || rd8() != 'E' || rd8() != 'M' || rd8() != 'U') return 0;
    sel(0x0019);
    uint32_t n = rdbe32();
    if (n > 4096) return 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t size = rdbe32(); uint16_t key = rdbe16(); (void)rdbe16();
        char fn[56]; for (int k = 0; k < 56; ++k) fn[k] = (char)rd8();
        fn[55] = 0;
        int eq = 1; for (int k = 0; k < 56; ++k) { if (fn[k] != name[k]) { eq = 0; break; } if (!name[k]) break; }
        if (!eq) continue;
        if (size + 1 > max) return 0;
        sel(key);
        for (uint32_t k = 0; k < size; ++k) out[k] = (char)rd8();
        out[size] = 0;
        return size;
    }
    return 0;
}



/* SOMA_241_VOZ: lectura por partes de un archivo grande (el modelo de voz en QEMU: -fw_cfg name=opt/soma/modelo,file=...).
 * fwcfg_open selecciona el archivo y devuelve su tamano (0 si no esta); despues fwcfg_read_seq lee los bytes en orden. */
uint32_t fwcfg_open(const char *name)
{
    sel(0x0000);
    if (rd8() != 'Q' || rd8() != 'E' || rd8() != 'M' || rd8() != 'U') return 0;
    sel(0x0019);
    uint32_t n = rdbe32();
    if (n > 4096) return 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t size = rdbe32(); uint16_t key = rdbe16(); (void)rdbe16();
        char fn[56]; for (int k = 0; k < 56; ++k) fn[k] = (char)rd8();
        fn[55] = 0;
        int eq = 1; for (int k = 0; k < 56; ++k) { if (fn[k] != name[k]) { eq = 0; break; } if (!name[k]) break; }
        if (!eq) continue;
        sel(key);
        return size;
    }
    return 0;
}
void fwcfg_read_seq(void *buf, uint32_t n)
{
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t k = 0; k < n; ++k) p[k] = rd8();
}
