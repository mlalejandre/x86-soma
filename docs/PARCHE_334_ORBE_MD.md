# Parche 334 — El orbe dibuja tablas, LaTeX y código como el chat

## Antes
La respuesta del orbe tenía su propio formateador, mucho más sencillo que el del chat: las tablas salían como texto con
barras (`| a | b |`), el LaTeX se convertía a símbolos sueltos (`\frac` como «(a)/(b)», matrices y entornos sin dibujar) y
los bloques de código eran texto amarillo sin resaltado ni fondo.

## Ahora
La respuesta del orbe usa el mismo maquetador que la aplicación de chat (`chat_md.c`, probado con entradas hostiles en el 321):
- **Tablas** GFM con cabecera, alineación por columna y filas alternas.
- **LaTeX** con el motor de SOMA (`tex.c`): fracciones, raíces, sumatorios e integrales con límites, matrices,
  `\begin{align}`... en línea (`$…$`, `\(…\)`) y en bloque (`$$…$$`, `\[…\]`).
- **Código**: bloques con fondo, nombre del lenguaje y resaltado de sintaxis; `código en línea` con su fondo.
- Títulos, listas anidadas y de casillas, citas, reglas, negrita/cursiva/tachado, enlaces y algo de HTML.

Se conserva lo propio del orbe: el texto se revela de arriba abajo mientras llega (la línea que se está escribiendo con el
color del orbe), sigue solo a lo que se escribe hasta que tomas el control con la rueda, una respuesta de una línea sale
centrada y el razonamiento va detrás, separado y apagado. Los fondos (código, tablas) son semitransparentes sobre el cosmos.
Las imágenes no se cargan en el orbe (se ve su recuadro; en el chat sí).

Sin fuente suavizada o sin memoria (necesita unos 2,4 MB la primera vez), se usa el formateador de antes.

Nueva orden de diagnóstico: `orbe <archivo.md>` pone ese texto en el orbe, para ver cómo queda una respuesta.

## Pruebas
- `python3 tests/prueba_orbe_md_334.py` (QEMU): pone en el orbe una respuesta con tabla, fórmulas y código, comprueba que el
  sistema sigue vivo y que el orbe ha pintado texto, y deja la captura en `build/tests/orbe_md_334.png` para mirarla.
- El maquetador ya tiene sus pruebas (`test_chat_285`, `test_chat_md_hostil_321`).
- Necesita `make`.
