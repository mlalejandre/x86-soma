/* SOMA_265_CC: valida ejecutables con el validador del nucleo (src/core/elf.c) y las mismas reglas que usermode_exec:
 * base USER_BASE, ventana de 12 MiB, como mucho 3072 paginas. Lo usa tests/test_cc_265.py. */
#include <stdio.h>
#include <stdlib.h>
#include "elf.h"
int main(int argc, char **argv) {
    int bad = 0;
    for (int i = 1; i < argc; ++i) {
        FILE *f = fopen(argv[i], "rb"); if (!f) { perror(argv[i]); return 2; }
        fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
        unsigned char *b = malloc(n ? (size_t)n : 1); if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 2; } fclose(f);
        struct elf_limits lim = { 0x8000000000ULL, 12u << 20, 3072 };
        struct elf_image im;
        int r = elf_parse(b, n, &lim, &im);
        unsigned long pages = 0; for (int k = 0; k < im.nsegs; ++k) pages += (im.seg[k].memsz + 4095) / 4096;
        printf("%s: %s (%d seg, ~%lu pag)\n", argv[i], r ? elf_strerror(r) : "OK", r ? 0 : im.nsegs, r ? 0 : pages);
        bad |= r != 0; free(b);
    }
    return bad;
}
