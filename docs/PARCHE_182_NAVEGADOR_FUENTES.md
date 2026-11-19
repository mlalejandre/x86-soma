# Parche 182: Navegador (III) — fuente con anchos de Arial

Requiere el parche 181. Las páginas web usaban DejaVu Sans, ~10 % más ancha que Arial/Helvetica (la fuente de la
mayoría de sitios), lo que cambiaba los saltos de línea, los botones y las cabeceras. Ahora el contenido web se
dibuja con **Liberation Sans** (anchos idénticos a Arial; licencia SIL OFL 1.1, ver `docs/LICENSE_LIBERATION_SANS.txt`).

## Qué cambia

* Dos caras nuevas incrustadas (subconjunto sin hinting, 2327 caracteres cada una): `src/apps/web_libsans.ttf` (regular) y
  `src/apps/web_libsansb.ttf` (negrita real; antes la negrita era un trazo doble sintético). Total +312 KB en el kernel.
* DejaVu Sans se conserva como **respaldo de glifos**: cualquier carácter que Liberation no tenga (flechas, símbolos raros…)
  sale de DejaVu. La cursiva sigue siendo inclinación sintética (mismos anchos que la regular, como en Arial).
* Solo el contenido web cambia (marca `WF_WEB`): escritorio, terminal, visor de PDF y editor siguen con DejaVu.
* Métricas de línea de Liberation: ascenso 0,92 em, descenso 0,23 em, `line-height: normal` = 1,15 (antes 1,18).
* Los espacios miden fracciones de píxel (16 px: 4,44 px). Se arrastra el resto en 1/64 px por línea (y en la anchura
  intrínseca) en lugar de redondear cada espacio: una frase de 43 caracteres mide 316 px (teórico 316,6; antes 313).
* La caché de glifos de `vfont.h` distingue las caras (`vf_face` en la clave).

## Interruptor y modo seguro

`soma.webfont=off` (o modo seguro): el contenido web vuelve a DejaVu y a las métricas del parche 181.

## Pruebas

`tests/test_web_font_182.c` (anfitrión, ASan/UBSan): anchos de 6 cadenas (regular, negrita, acentos, euro, tamaños 12–24)
contra las tablas `hmtx` de Liberation Sans, respaldo DejaVu, aislamiento del escritorio, interruptor y caché de caras.
`python3 parche_182_navegador_fuentes.py --test`.

## Límites

* No hay *kerning* (pares como "AV", "To"): las líneas largas pueden salir ~0,5 % más anchas que en Chrome.
* Sin cara serif (Times/Georgia se dibujan con Liberation Sans) ni cursiva real; `font-family` aún no elige cara.
* Chino/japonés/coreano siguen sin fuente.
