# Parche 185 — Motor JavaScript (II): cadenas UTF-16

Requiere el parche 184. Interruptor: `soma.jsutf16=off` (y el modo seguro) vuelve a la semántica por bytes anterior.

## Qué arregla

Hasta ahora el motor guardaba las cadenas en UTF-8 y contaba **bytes**: `'é'.length` valía 2, `'😀'.length` valía 4 y
`'日本語'[1]` devolvía un trozo de carácter. Los sitios reales (editores, contadores de caracteres, `TextEncoder`,
rutas con acentos, expresiones regulares con `index`/`lastIndex`) suponen unidades UTF-16, como Chrome y Safari.

| Expresión | Antes | Ahora |
|---|---|---|
| `'héllo'.length` | 6 | 5 |
| `'😀'.length` | 4 | 2 |
| `'😀'.charCodeAt(0)` / `(1)` | 55357 / 65533 | 55357 / 56832 |
| `'a😀b'.codePointAt(1)` | 128512 | 128512 |
| `'a😀b'.indexOf('b')` | 5 | 3 |
| `/b/g` con `'a😀b'`: `lastIndex` tras `test` | 6 | 4 |
| `escape('é')`, `escape('日')` | `%C3%A9` (bytes UTF-8) | `%E9`, `%u65E5` |
| `new TextEncoder().encode('é').length` | 4 (doble codificación) | 2 |

## Diseño

* El almacenamiento sigue siendo UTF-8 (sin coste de memoria ni de conversión para el resto del sistema).
* Los **sustitutos sueltos** (mitades de un par) se guardan como WTF-8 (`ED A0..BF xx`, 3 bytes). Al concatenar una
  mitad alta con una baja se unen en un único carácter de 4 bytes (`js_str_concat`, `js_wb_unit`).
* `jstr.u16` cachea la longitud en unidades (+1; 0 = sin calcular). Las cadenas ASCII, casi todas, cumplen
  unidades == bytes y no pagan nada (recorrido de 8 bytes por vuelta).
* Un cursor por motor (`rt->u16c_*`) hace O(1) amortizado los recorridos secuenciales `for (i<s.length) s[i]`.
* Funciones nuevas en `web_js_u16.c`: `js_u16len`, `js_u16len_n`, `js_u16_to_byte`, `js_byte_to_u16`, `js_u16_unit`,
  `js_u16_slice`, `js_u16_char`, `js_wb_unit`.
* El motor de expresiones regulares sigue trabajando en bytes; solo se convierten sus fronteras visibles
  (`lastIndex`, `match.index`, posición pasada al callback de `replace`, `search`).

## Dónde se aplica

`length`, índices `s[i]`, `for…in`, `Object.keys` de cadenas, `charAt`, `at`, `charCodeAt`, `codePointAt`, `indexOf`,
`lastIndexOf`, `includes`, `startsWith`, `endsWith`, `slice`, `substring`, `substr`, `padStart`, `padEnd`, `split('')`,
`replace` con callback, `String.fromCharCode`, `String.fromCodePoint` (con `RangeError` fuera de rango), `escape`,
`unescape` y `Text.length` del DOM.

## Pruebas

* `tests/test_web_js_185.c`: 76 casos de semántica + 15 del modo `off`, con ASan/UBSan.
* Los 75 casos del contraste se verificaron además contra Node (misma salida).
* Compilación x86_64 y aarch64 con `WERROR=1`. Núcleo: el tamaño sigue por debajo de 14 MiB.

## Límites conocidos

* Los `ArrayBuffer`/arrays tipados reales llegan en el parche 186 (`TextEncoder`/`TextDecoder` sobre bytes reales).
* `normalize` y `localeCompare` siguen siendo básicos.
