# libxaac en SOMA

Decodificador AAC (AAC-LC, HE-AAC, xHE-AAC) de Ittiam. Licencia Apache 2.0 (ver `LICENSE` y `NOTICE`).

- Origen: https://github.com/ittiam-systems/libxaac, commit `2fbadd57bca46693b7a077bcf1513131106d960c`.
- Copiado sin modificar: `common/`, `decoder/*.c`, `decoder/*.h`, `decoder/drc_src/` y `decoder/generic/`.
- No se incluyen: el codificador, las rutinas para ARM y x86_64 con intrínsecos, las pruebas ni el fuzzer.
- Lo usa el reproductor nativo `vplay.elf` (ring 3) a través de `src/user/vplay/vp_adec.c`. Ver `docs/PARCHE_252_VIDEO_NATIVO.md`.
