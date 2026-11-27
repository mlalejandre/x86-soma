# Parche 358 — Hoja de cálculo: motor y formatos

Primera de tres partes de la hoja de cálculo de SOMA (359: la ventana; 360: gráficos, IA e integración con las demás apps).

## Qué hay
| Archivo | Qué hace |
|---|---|
| `src/apps/hoja_core.[ch]` | Modelo (libros, hojas, celdas en tabla hash, estilos), entrada en castellano, formatos de número, CSV y formato propio `SOMA-HOJA 1` |
| `src/apps/hoja_formula.inc` | Analizador de fórmulas (castellano/inglés, `;`/`,`), compilador a código RPN y evaluador con pila explícita; unas 130 funciones |
| `src/apps/hoja_ops.inc` | Insertar y borrar filas o columnas (moviendo referencias), copiar, rellenar series, ordenar, filtrar, buscar y reemplazar |
| `src/apps/hoja_zip.c`, `hoja_xlsx.c`, `hoja_ods.c` | Escritores ZIP, XLSX y ODS (en el núcleo) |
| `src/apps/hoja_zip_lee.c`, `hoja_xlsx_lee.c`, `hoja_ods_lee.c` | Lectores (en x86_64 solo dentro de `mimesvc.elf`, ring 3; en aarch64 en el núcleo) |
| `src/apps/hoja.[ch]` | Libros abiertos (8 como mucho), abrir y guardar por extensión, la orden `hoja` |
| `src/agent/shell_hoja.c` | Registro de la orden |

## Seguridad
- El XLSX/ODS/CSV de fuera se descomprime y se analiza en `mimesvc` (op `SBX_MIME_HOJA 5`). El servicio devuelve el libro en
  texto `SOMA-HOJA 1` y el núcleo lo vuelve a validar con `hj_carga` (límites de hojas, filas, columnas, longitud de textos y
  fórmulas; cada fórmula se recompila).
- Límites: 48 MB de entrada, 96 MB por parte descomprimida, 400.000 celdas, fórmulas de 8 KB, anidamiento 64. Sin ZIP64 ni cifrado;
  `.xls` antiguo (OLE) se rechaza con un mensaje.
- CSV: nunca se importan fórmulas; al exportar, el texto que empieza por `=`, `+` o `@` sale con `'` delante.
- `hoja abrir` solo acepta extensiones de hoja de cálculo (no sirve para leer claves ni `.conf`); `hoja guardar` solo escribe en
  `/usuario/` o `/tmp/`, sin `..`, y nunca `.xlsm`. Un nombre sin ruta va a `/usuario/`.
- Fuzz con ASan/UBSan de los tres lectores (`tests/hoja_cli_358.c fuzz`).

## Orden `hoja`
```
hoja nueva Gastos
hoja pon A1 Concepto
hoja pon B2 45,30
hoja pon B4 =SUMA(B2:B3)
hoja pon C4 =SI(B4>50;"mucho";"poco")
hoja ver A1:D10
hoja guardar /usuario/gastos.xlsx      (o .ods, .csv, .hoja)
hoja abrir /usuario/informe.ods
```
El intérprete de órdenes ya no parte la línea por un `;` que esté entre paréntesis.

## Pruebas
- `tests/test_hoja_358.c`: 247 comprobaciones (fórmulas, errores, ciclos, cadenas de 3.000 celdas, traducción, filas/columnas,
  hojas, ordenar/filtrar, CSV, serialización, fuzz).
- `tests/test_hoja_fmt_358.py`: 37 comprobaciones; si hay LibreOffice u openpyxl, ida y vuelta con ellos.

## Límites conocidos
- Los formatos de `TEXTO()` dependen del idioma (se usan los códigos de Excel en castellano).
- Gráficos: llegan en el 360 (el modelo ya los guarda).
