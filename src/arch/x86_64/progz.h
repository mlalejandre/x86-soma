/* SOMA_314_PROGZ: los programas grandes de ring 3 (Python, cc, js, voz, reproductor de video y navegador aislado) van COMPRIMIDOS
 * dentro de la imagen del nucleo (tools/progz.py, formato en src/core/progz_fmt.h): unos 4,2 MB menos de imagen.
 * Al lanzarlos se descomprimen con el servicio aislado zdec (ring 3: el nucleo no gana codigo de descompresion), se comprueban
 * tamano y huella, se lanza el programa (el cargador copia sus segmentos) y se libera la copia.
 *
 *   PROGZ_DECL(python);                         extern de los simbolos de ld -b binary (python.elf.z)
 *   struct progz p; if (progz_abre(PROGZ(python), "python", &p) == 0) { usermode_exec...(p.elf, p.len, ...); progz_cierra(&p); } */
#ifndef SOMA_PROGZ_H
#define SOMA_PROGZ_H
#include <stdint.h>
#include <stddef.h>

#define PROGZ_DECL(n) extern const uint8_t _binary_##n##_elf_z_start[], _binary_##n##_elf_z_end[]
#define PROGZ(n) _binary_##n##_elf_z_start, _binary_##n##_elf_z_end

struct progz { void *elf; size_t len; };

int  progz_abre(const uint8_t *ini, const uint8_t *fin, const char *nombre, struct progz *p);   /* 0 = listo; -1 = no se pudo */
void progz_cierra(struct progz *p);
#endif
