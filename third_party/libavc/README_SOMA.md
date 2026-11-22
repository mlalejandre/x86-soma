# libavc en SOMA

Decodificador H.264 de Ittiam (el decodificador por software de Android). Licencia Apache 2.0 (ver `LICENSE` y `NOTICE`).

- Origen: https://github.com/ittiam-systems/libavc, commit `e4fb9e993f2b25d3a9cdbf8e3fff1c3a65e4489a`.
- Copiado sin modificar: `common/*.c`, `common/*.h`, `decoder/*.c`, `decoder/*.h` y `common/x86/ih264_platform_macros.h` (aquí en `common/`).
- No se incluyen: el codificador, MVC/SVC, las rutinas en ensamblador o con intrínsecos (ARM, SSSE3, SSE4.2, AVX2), `ithread.c` ni los ejemplos.
- SOMA usa solo las rutas en C genérico y un único hilo. La selección de funciones y los hilos vacíos están en
  `src/user/vplay/vp_vdec.c` y `src/user/vplay/vp_libc.c`.
- Lo usa el reproductor nativo `vplay.elf` (ring 3). Ver `docs/PARCHE_252_VIDEO_NATIVO.md`.
