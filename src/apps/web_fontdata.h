#ifndef SOMA_WEB_FONTDATA_H
#define SOMA_WEB_FONTDATA_H
/* DejaVu Sans (subconjunto Latin/Griego/Cirilico, sin hinting). Licencia Bitstream Vera / DejaVu: redistribucion libre.
 * Los bytes viven en src/apps/web_dejavu.ttf y se incrustan con .incbin (la ruta es relativa a la raiz del proyecto, desde donde corre make). */
#include <stdint.h>
__asm__(".section .rodata\n"
        ".balign 4\n"
        ".globl web_ttf_blob\n.globl web_ttf_blob_end\n"
        "web_ttf_blob:\n"
        ".incbin \"src/apps/web_dejavu.ttf\"\n"
        "web_ttf_blob_end:\n"
        ".byte 0\n"
        ".previous\n");
extern const uint8_t web_ttf_blob[];
extern const uint8_t web_ttf_blob_end[];
#define WEB_TTF_PTR web_ttf_blob
#define WEB_TTF_LEN ((uint32_t)(web_ttf_blob_end - web_ttf_blob))
/* SOMA_182: Liberation Sans regular y negrita (SIL OFL 1.1; anchos identicos a Arial). Subconjunto sin hinting. */
__asm__(".section .rodata\n"
        ".balign 4\n"
        ".globl web_ttf_sans\n.globl web_ttf_sans_end\n.globl web_ttf_sansb\n.globl web_ttf_sansb_end\n"
        "web_ttf_sans:\n"
        ".incbin \"src/apps/web_libsans.ttf\"\n"
        "web_ttf_sans_end:\n"
        ".balign 4\n"
        "web_ttf_sansb:\n"
        ".incbin \"src/apps/web_libsansb.ttf\"\n"
        "web_ttf_sansb_end:\n"
        ".byte 0\n"
        ".previous\n");
extern const uint8_t web_ttf_sans[], web_ttf_sans_end[], web_ttf_sansb[], web_ttf_sansb_end[];
#define WEB_TTF_SANS_PTR web_ttf_sans
#define WEB_TTF_SANS_LEN ((uint32_t)(web_ttf_sans_end - web_ttf_sans))
#define WEB_TTF_SANSB_PTR web_ttf_sansb
#define WEB_TTF_SANSB_LEN ((uint32_t)(web_ttf_sansb_end - web_ttf_sansb))
/* SOMA_347_NAVEGADOR_SERIF: Liberation Serif regular y negrita (SIL OFL 1.1, docs/LICENSE_LIBERATION_SERIF.txt; anchos de Times), mismo subconjunto
 * que la Sans y sin hinting: titulares y textos con font-family serif (Georgia, Times, Source Serif...) como en Chrome. */
__asm__(".section .rodata\n"
        ".balign 4\n"
        ".globl web_ttf_serif\n.globl web_ttf_serif_end\n.globl web_ttf_serifb\n.globl web_ttf_serifb_end\n"
        "web_ttf_serif:\n"
        ".incbin \"src/apps/web_libserif.ttf\"\n"
        "web_ttf_serif_end:\n"
        ".balign 4\n"
        "web_ttf_serifb:\n"
        ".incbin \"src/apps/web_libserifb.ttf\"\n"
        "web_ttf_serifb_end:\n"
        ".byte 0\n"
        ".previous\n");
extern const uint8_t web_ttf_serif[], web_ttf_serif_end[], web_ttf_serifb[], web_ttf_serifb_end[];
#define WEB_TTF_SERIF_PTR web_ttf_serif
#define WEB_TTF_SERIF_LEN ((uint32_t)(web_ttf_serif_end - web_ttf_serif))
#define WEB_TTF_SERIFB_PTR web_ttf_serifb
#define WEB_TTF_SERIFB_LEN ((uint32_t)(web_ttf_serifb_end - web_ttf_serifb))
#endif
