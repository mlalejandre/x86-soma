#ifndef SOMA_SBX_ABI_H
#define SOMA_SBX_ABI_H
/* PARCHE 138: ABI de los SERVICIOS AISLADOS (kernel <-> programa de ring 3). La comparten src/arch/x86_64/sandbox.c y los
 * programas de src/user/ que hacen de servicio (imgdec, ...).
 *
 * Un servicio aislado es un proceso de ring 3 que:
 *   - solo puede llamar a SYS_EXIT (cualquier otra syscall lo mata: USER_EXIT_DENIED);
 *   - recibe su entrada en SBX_IN_VA (solo lectura, no ejecutable): cabecera struct sbx_in + datos;
 *   - tiene un monton propio en SBX_HEAP_VA (lectura/escritura, no ejecutable) que se reserva BAJO DEMANDA, pagina a pagina, hasta
 *     sbx_in.heap_bytes; pasar de ahi es un fallo de pagina fuera de region (muere);
 *   - deja su respuesta en SBX_OUT_VA (una pagina): struct sbx_out + hasta SBX_OUT_INLINE bytes; los resultados grandes se dejan en
 *     su monton y se indican con (data_va, data_len): el kernel los VALIDA (dentro del monton, ya reservados) antes de copiarlos.
 * Las tres regiones estan FUERA de USER_WINDOW: aunque una syscall estuviese permitida, ningun puntero a ellas se aceptaria.
 * Nada de lo que escribe el proceso se cree sin validar: magic, tamanos y punteros se comprueban en el kernel. */
#include <stdint.h>

#define SBX_OUT_VA   0x8003000000ULL        /* USER_BASE + 48 MiB: 1 pagina de respuesta */
#define SBX_IN_VA    0x8004000000ULL        /* USER_BASE + 64 MiB: entrada, hasta SBX_IN_MAX */
#define SBX_HEAP_VA  0x8010000000ULL        /* USER_BASE + 256 MiB: monton bajo demanda, hasta SBX_HEAP_MAX */
#define SBX_IN_MAX   (48u * 1024u * 1024u)
#define SBX_HEAP_MAX (192u * 1024u * 1024u)
#define SBX_IN_MAGIC  0x4E495853u           /* "SXIN" */
#define SBX_OUT_MAGIC 0x54554F53u           /* "SOUT" */
#define SBX_OUT_INLINE (4096u - 64u)

struct sbx_in {                             /* al principio de SBX_IN_VA; los datos empiezan en SBX_IN_VA + 64 */
    uint32_t magic;
    uint32_t op;                            /* operacion (la define cada servicio) */
    uint32_t len;                           /* bytes de datos */
    uint32_t heap_bytes;                    /* tamano del monton bajo demanda */
    int32_t  arg[8];                        /* argumentos de la operacion */
    uint8_t  pad[64 - 48];
};
_Static_assert(sizeof(struct sbx_in) == 64, "sbx_in debe ocupar 64 bytes");

struct sbx_out {                            /* al principio de SBX_OUT_VA */
    uint32_t magic;
    int32_t  status;                        /* 0 = OK; <0 error del servicio */
    uint64_t data_va;                       /* resultado grande dentro del monton (0 = ninguno) */
    uint64_t data_len;
    int32_t  val[8];                        /* resultados pequenos */
    uint32_t inline_len;                    /* bytes validos en inline_data */
    uint8_t  pad[64 - 60];
    uint8_t  inline_data[SBX_OUT_INLINE];
};
_Static_assert(sizeof(struct sbx_out) == 4096, "sbx_out debe ocupar una pagina");

/* ---- servicio imgdec: decodificadores de imagen (PNG, GIF, JPEG base/progresivo/CMYK, BMP) ---- */
#define SBX_IMG_DECODE   1                  /* wi_decode:          arg = {} */
#define SBX_IMG_FIT      2                  /* wi_decode_fit:      arg = {maxw, maxh} */
#define SBX_IMG_BMPRAW   3                  /* BMP con su alfa tal cual (image_decode) */
#define SBX_IMG_JPEGRAW  4                  /* PARCHE 139: jpeg_decode del reproductor (MJPEG): arg = {stride, max_alto}; val = {codigo jpeg,
                                               ancho, alto, ncomp, hmax, vmax, restart}; data = stride*alto pixeles 0x00RRGGBB (solo si OK) */

/* ---- PARCHE 139: servicio zdec: inflate (deflate crudo, zlib, gzip) ---- */
#define SBX_ZLIB_INFLATE 1                  /* arg = {fmt (0 crudo,1 zlib,2 gzip,3 auto), max_out}; val = {codigo wz_inflate}; data = lo
                                               decodificado (aunque haya error: el PNG y el PDF aprovechan lo parcial) */
/* respuesta: status 0 OK / -1 imagen no valida / -2 sin memoria; val = {w, h, nw, nh, oom}; data = w*h pixeles ARGB */
#define SBX_IMG_ANIM     5                  /* SOMA_180: wi_decode_anim: arg = {max_pixeles}; val = {w, h, nw, nh, oom, fotogramas}; datos = fotogramas*w*h*4;
                                             * inline_data = duracion de cada fotograma (uint16, ms) */
#define SBX_IMG_SVG      6                  /* SOMA_180: wi_decode_svg: arg = {ancho, alto, color}; val y datos como SBX_IMG_FIT */
#define SBX_IMG_MAX_PIXELS (6u * 1024u * 1024u)

/* ---- PARCHE 141: servicio pdfsvc: analizador PDF (pdf.c). Sin estado entre peticiones: cada una trae el PDF entero. ---- */
#define SBX_PDF_OPEN    1     /* -> status 0/-1; val = {paginas}; inline = titulo UTF-8 (o el error si status -1); data = int32 [ancho_pt, alto_pt] x paginas */
#define SBX_PDF_RENDER  2     /* arg = {pagina, escala256} -> data = lista de dibujo serializada (abajo) */
#define SBX_PDF_MAX_PAGES 65536
#define SBX_PDFL_MAGIC 0x4C464450u   /* "PDFL" */
struct sbx_pdf_hdr { uint32_t magic; int32_t w, h, unknown, nops; uint32_t blob_len; };
struct sbx_pdf_op {           /* 52 bytes; los punteros de struct pdf_op pasan a ser (off, n) dentro del blob */
    uint8_t  kind, flags, flip_x, flip_y, font, pad0[3];
    int16_t  size; uint16_t pad1;
    int32_t  x, y, w, h;
    uint32_t color, fill;
    uint32_t off, n;          /* TEXT: bytes; PATH: puntos (pares x,y de int32); IMAGE: off a iw*ih pixeles */
    int32_t  iw, ih;
};
_Static_assert(sizeof(struct sbx_pdf_op) == 52, "sbx_pdf_op debe ocupar 52 bytes");
/* disposicion: hdr | ops[nops] | blob[blob_len]   (off relativos al blob, alineados a 4) */

/* ---- PARCHE 142: servicio netsvc: cliente HTTP (web_net.c) con la capacidad de red. ---- */
#define SBX_NET_FETCH 1
struct sbx_net_req {          /* al principio de los datos; detras: url, metodo, tipo de contenido y cuerpo (sin NUL) */
    uint32_t url_len, method_len, ctype_len, body_len;
    uint32_t timeout_ms, max_body;
    uint32_t flags;           /* bit 0 want_binary, bit 1 no_redirect, bit 2 via_gateway */
};
/* respuesta: status 0 = hubo respuesta HTTP (val[0] = codigo, val[1] = redirecciones, val[2] = via_gateway), -1 = error de red;
 * inline = ctype NUL charset NUL final_url NUL err NUL; data = cuerpo */

/* ---- servicio sbxevil (solo pruebas): ataques al propio mecanismo ---- */
#define SBX_EVIL_SYSCALL    1   /* intenta write/open/fb: debe morir DENIED */
#define SBX_EVIL_LOOP       2   /* bucle infinito: plazo */
#define SBX_EVIL_KREAD      3   /* lee memoria del kernel: #PF */
#define SBX_EVIL_WRITE_IN   4   /* escribe en la entrada (solo lectura): #PF */
#define SBX_EVIL_HEAP_PAST  5   /* escribe mas alla del monton: #PF */
#define SBX_EVIL_HEAP_ALL   6   /* toca todo el monton: limite propio -> NOMEM si el sistema va justo, si no OK */
#define SBX_EVIL_BAD_PTR    7   /* responde con data_va fuera del monton */
#define SBX_EVIL_BAD_LEN    8   /* responde con data_len enorme */
#define SBX_EVIL_UNTOUCHED  9   /* responde con datos en paginas del monton nunca tocadas */
#define SBX_EVIL_BAD_MAGIC 10   /* respuesta sin magic */
#define SBX_EVIL_EXEC_HEAP 11   /* salta a codigo escrito en el monton (NX): #PF */
#define SBX_EVIL_ECHO      12   /* devuelve la entrada invertida (prueba de datos) */
#define SBX_EVIL_PLANT     13   /* PARCHE 139: deja un "secreto" por todo lo que usa del monton (1 MiB) */
#define SBX_EVIL_SNOOP     14   /* PARCHE 139: busca restos de la peticion anterior en su monton: val[0] = bytes no nulos encontrados */
#define SBX_EVIL_STATIC    15   /* PARCHE 139: cuenta peticiones atendidas por este proceso (variable global): val[0] */
#define SBX_EVIL_NET_NOCAP 16   /* PARCHE 142: sin capacidad de red intenta abrir un socket: debe morir DENIED */
#define SBX_EVIL_NET_STEAL 17   /* PARCHE 142: arg0 = descriptor de OTRO; prueba send/recv/poll/close en todos: val[0] = respuestas que no fueron EBADF */
#define SBX_EVIL_NET_FLOOD 18   /* PARCHE 142: arg0 = ip, arg1 = puerto; abre 6: val[0] = abiertos, val[1] = codigo del 5o */
#define SBX_EVIL_NET_DNS   19   /* PARCHE 142: nombres maliciosos: val[0] = cuantos NO fueron rechazados con EINVAL */

#endif
