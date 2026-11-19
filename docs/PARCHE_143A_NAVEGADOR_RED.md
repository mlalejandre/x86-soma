# SOMA — Parche 143A: transporte HTTP del navegador mediante netsvc (opt-in)

Este parche NO completa el aislamiento del navegador. Su objetivo es comprobar el
primer límite de proceso de forma controlada, sin cambiar la versión predeterminada.

## Cambios
- `web_page.c`: sus **tres** puntos de descarga de red pasan por `wb_network_fetch`.
  Incluye páginas, scripts, CSS, imágenes, conversión SVG por pasarela y peticiones fetch.
- `web_browser_transport.h`: selecciona la ruta del kernel de siempre (modo 0) o
  `wn_fetch_isolated` del parche 142 (modo 1) en **x86_64**.
- El modo 1 es **fail-closed**: si `netsvc` falla, NO se ejecuta el cliente HTTP en Ring 0.
- AArch64 no admite activar el modo 1 hasta tener su infraestructura equivalente.
- Pruebas de selección de transporte y fallos sin fallback, ejecutables en Linux/macOS.

## Estado y límites importantes

**Por defecto: BROWSER_NET_RING3=0.** La funcionalidad del navegador no cambia.

Con `BROWSER_NET_RING3=1`, el servicio HTTP de la etapa 142 todavía no conserva
correctamente toda la semántica web del navegador: cookies compartidas con JavaScript,
`Referer`, encabezado `Accept`, estado de capacidades de la pasarela y ciertos
comportamientos de sesión. Por tanto, el modo 1 es una **prueba experimental** y
puede producir diferencias visuales o funcionales, aunque no caiga el kernel.
No usar como sustituto de una solución completa, especialmente con sitios autenticados.

HTML, CSS, el DOM, JavaScript, el renderizador y la GUI **siguen en Ring 0**.
No se debe anunciar este parche como «navegador totalmente aislado».

## Uso

Desde la raíz (el script no toca nada por defecto):

```sh
python3 parche_143a.py --plan
python3 parche_143a.py --self-test
python3 parche_143a.py --apply
make browser-boundary-test
make ARCH=x86_64               # ruta normal, no aislada
make clean && make ARCH=x86_64 BROWSER_NET_RING3=1  # opt-in, solo en COPIA/QEMU
python3 tools/sbx_check.py --web --srcfs       # prueba de sistema, requiere QEMU y dependencias
```

**Importante:** `make clean` elimina compilados; solo hacerlo sobre una copia y
con las imágenes de disco de prueba. NO ejecutar automáticamente sobre discos reales.
Para volver al modo original: compilar con `BROWSER_NET_RING3=0`, o:

```sh
python3 parche_143a.py --undo
```

La reversión se niega a sobrescribir un archivo que haya cambiado después de la
instalación, y exige que el manifiesto y las copias estén intactos.

## Validación antes del 143B

1. Ejecutar `make browser-boundary-test` y compilación normal.
2. En una copia, comparar visualmente `sbx_webref` con `0x3EE95F6D`, repetido en
   el mismo entorno y con los mismos recursos de prueba (la huella depende del entorno).
3. Probar HTTP/HTTPS, POST, imágenes y SVG; verificar además cookies, `Referer`,
   scripts, redirecciones y errores de red sin regresar a Ring 0.
4. Aislar a continuación HTML/CSS/DOM/JS y la superficie de pintura por IPC, con
   validación estricta, reinicio y cuotas. **Esto NO forma parte de 143A.**

Las pruebas incorporadas son unitarias/de compilación: **no garantizan** por sí
mismas que el navegador en QEMU conserve la funcionalidad ni que el kernel esté seguro.
