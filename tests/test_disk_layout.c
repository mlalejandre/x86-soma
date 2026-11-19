/* PARCHE 122: zonas de disco accesibles con sector_read/sector_write. Cada limite exacto. */
#include <stdio.h>
#include "../src/fs/disk_layout.h"
static int fails, checks;
static void T(unsigned long long s, int want) { checks++; int g = soma_lba_user_zone(s); if (g != want) { fails++; printf("FALLO: LBA %llu -> %d, esperado %d\n", s, g, want); } }
int main(void)
{
    T(0, 0); T(1, 0); T(24, 0); T(25, 0); T(30, 0); T(31, 0); T(1024, 0); T(1025, 0); T(3137, 0); T(3200, 0); T(3234, 0); T(3500, 0); T(3599, 0);   /* arranque, buzon, VFS, diario, scratch */
    T(3600, 1); T(3601, 1); T(3800, 1); T(3961, 1);                                                                                                  /* zona libre (PARCHE 130: acaba en 3961) */
    T(3962, 0); T(4000, 0); T(4094, 0); T(6211, 0); T(6212, 0); T(7000, 0); T(8191, 0);                                                              /* ranura B del checkpoint */
    T(4095, 0); T(4096, 0); T(4400, 0); T(4401, 0); T(5000, 0); T(6209, 0); T(6210, 0);                                                              /* marcador y checkpoint COMPLETO */
    T(8192, 0); T(8255, 0); T(8256, 0); T(16383, 0);                                                                                                 /* SRCFS */
    T(16384, 0); T(16385, 0); T(100000, 0); T(1000000000ULL, 0); T(~0ULL, 0);                                                                         /* BIGFS y mas alla */
    printf("%s: %d comprobaciones, %d fallos\n", fails ? "FALLO" : "OK", checks, fails);
    return fails != 0;
}
