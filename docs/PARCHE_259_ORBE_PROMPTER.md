# Parche 259 · El orbe como teleprompter, con formato y barra de fases

Requiere el 258. Se aplica desde la raíz de SOMA:

```
python3 parche_259_orbe_prompter.py
python3 parche_259_orbe_prompter.py --comprobar
```

Para quitarlo: `python3 parche_259_orbe_prompter.py --revertir` (antes que el 258 y el 257).

## Qué cambia en pantalla

**Respuesta.** La respuesta va en una zona fija bajo el orbe, de 980 px de ancho como mucho. Ocupa desde debajo de la barra de fases hasta 40 px del borde inferior.

- **Letra.** La letra es siempre la misma, sea larga o corta la respuesta; antes, una respuesta de más de 150 caracteres pasaba a letra diminuta. Se elige en Ajustes > LCM > «Letra de las respuestas en el orbe»: Compacta 16 px, Mediana 20 px o Grande 26 px. Grande es la que viene. Se guarda en `/etc/lcm.conf` como `letra=`.
- **Fuente.** Se usa el texto suavizado de `web_font`, el mismo del navegador: Liberation Sans con negrita y cursiva de verdad. Los símbolos matemáticos salen de DejaVu Sans y el código, en mono. Si no hay fuente, se usa el mapa de bits de siempre a doble tamaño.
- **Desplazamiento automático.** Mientras SOMA escribe, el texto sube solo para que siempre se vea la línea que se está escribiendo.
- **Desplazamiento manual.** Si mueves el ratón arriba o abajo, o usas la rueda, tomas el control; en el modo orbe no hay puntero. Hacia arriba ves lo anterior. Cuando bajas hasta el final, vuelve a ir solo; también al hacer una pregunta nueva.
- **Indicadores.** Arriba y abajo hay un difuminado cuando queda texto por ver. A la derecha, una barra fina dice dónde estás: gris si va solo y azul si lo llevas tú.
- **Razonamiento.** El razonamiento del agente va detrás de la respuesta, más tenue y separado por una línea. Se ve bajando y ya no le quita sitio a la respuesta.
- **Límites.** Antes, la respuesta se cortaba a 1536 caracteres y 40 líneas. Ahora caben 16 KB de respuesta y 4 KB de razonamiento.

**Formato que entiende.** Es lo que suelen escribir los modelos de lenguaje:

| Escrito | Se ve |
|---|---|
| `**negrita**`, `__negrita__`, `<b>` | negrita |
| `*cursiva*`, `_cursiva_`, `<i>` | cursiva (solo en bordes de palabra: `mi_archivo_2.txt` o `2*3*4` no cambian) |
| `<u>subrayado</u>`, `[texto](url)` | subrayado (el enlace, en azul) |
| `` `código` `` y bloques ```` ``` ```` | mono, color ámbar, con fondo |
| `# Título`, `## Subtítulo` | más grande, en negrita |
| `- punto`, `* punto` | `•` con sangría colgante |
| `> cita` | con raya a la izquierda |
| `---` | línea horizontal |
| `$...$`, `\(...\)` | fórmula en línea, en color turquesa |
| `$$...$$`, `\[...\]` | fórmula centrada en su línea |

**LaTeX que traduce:**

- Letras griegas.
- Símbolos: `\times \cdot \pm \le \ge \neq \approx \infty \sum \prod \int \partial \nabla \in \forall \exists \to \Rightarrow …`.
- `\frac{a}{b}` se ve como a/b, y `\frac{a+b}{2}` como (a+b)/2.
- `\sqrt{x}` se ve como √x.
- `^` y `_` dan superíndice y subíndice de verdad: más pequeños y desplazados.
- `\mathbb{R}` se ve como ℝ.
- `90^\circ` se ve como 90°.
- `\text{...}`, `\mathbf{...}`, y las funciones `\sin`, `\log` y similares.

Un precio como «cuesta $5 y $10» no se toma por fórmula.

**Barra de fases.** Va en su propia franja, entre el orbe y la respuesta, así que no tapa ni la pregunta ni la respuesta. Cada paso de la petición es un icono vectorial dentro de un círculo:

| Fase | Icono | Quién la marca |
|---|---|---|
| LCM | insignia «LCM» | el LCM contesta o ejecuta una orden |
| Mayordomo | pajarita | agent.c, antes de `mayordomo_run` |
| Búsqueda en internet | lupa | órdenes `search`, `descargar`, `navegador`, `fetch`, `web`, o un agente investigador o buscador |
| Documento | hoja | el maquetador |
| Programador | `</>` | el programador |
| Otro agente | dos nodos | cualquier agente delegado |
| Orden | terminal | una orden que ejecuta el agente o el LCM |
| Pensando | chispa | el agente piensa (`gui_thought_update`) |
| Escribiendo | lápiz | llega la respuesta en trozos |
| Voz | altavoz | el orbe está leyendo la respuesta (x86_64) |
| Hecho | ✓ verde | fin del turno |

La fase actual late con el color del orbe y las anteriores quedan unidas por una línea. La barra se ve mientras SOMA trabaja o habla, y hasta 12 s después de la última fase. Se vacía con cada pregunta. Cualquier parte de SOMA puede añadir una fase llamando a `gui_fase("nombre")`, desde cualquier hilo.

## Archivos

- `src/ui/gui_prompter.c` (nuevo). Lo incluye `gui.c` justo después de `gui_vortice.c` y contiene el formato, la maquetación, el desplazamiento, el dibujo y la barra.
- `src/ui/gui.c`. Lo incluye y envía al prompter el ratón y la rueda cuando no hay puntero.
- `src/ui/gui_cosmos.c`. `her_draw_agent_zone` deja que dibuje el prompter. Si este no puede (pantalla de menos de 240×300 o sin memoria), queda el dibujo de antes.
- `src/ui/gui_vortice.c`. Le pasa la pregunta nueva, el pensamiento, la escritura en trozos y la respuesta final. Los búferes de antes siguen igual (el orbe los usa para su animación).
- `src/agent/lcm_soma.inc`. Añade `lcm_cfg_get(6)` y `lcm_cfg_set(6)`, que dan la letra sin cargar el modelo, y las fases «lcm» y «orden». `gui_fase` es débil, así que las pruebas sin GUI siguen enlazando.
- `src/agent/agent.c`. Avisa de las fases. Las de mayordomo, documento, programador y agente son opcionales: si tu `agent.c` es distinto, se avisa y se sigue.
- `src/agent/shell.c` (opcional). Avisa de las órdenes del agente.
- `src/apps/settings.c`. La página LCM tiene un selector más.
- `Makefile`. `gui.o` depende de `gui_prompter.c`.
- `tests/test_orbe_259.c`. Prueba en el anfitrión: 57 comprobaciones de formato, maquetación, desplazamiento, fases y límites (pasa también con ASan y UBSan).
- `tests/test_orbe_259_nucleo.c`. Comprueba que compila como en el núcleo.

## Qué está medido y qué no

- **Probado en el anfitrión.**
  - Las 54 comprobaciones de `tests/test_orbe_259.c`, con imitaciones de la pantalla y las fuentes.
  - Que compila con `-ffreestanding -mgeneral-regs-only -Werror` para x86_64 y aarch64.
  - Que el banco del 257 y el LCM en modo núcleo siguen pasando con el `lcm_soma.inc` cambiado.
  - Aplicar y revertir sobre un árbol de SOMA con 253, 257 y 258.
- **No probado en tu equipo.**
  - El núcleo entero no lo he podido compilar: me faltan cabeceras como `fb.h`.
  - Las firmas de `fb_*` las he sacado de cómo las usa el resto del código.
  - Lo que hay que mirar al arrancar:
    1. El texto no se pisa: el avance de `wf_cp_adv64` y el dibujo con `wf_draw` son los del navegador.
    2. La barra no roza el halo del orbe.
    3. El sentido del ratón y la rueda: ratón arriba = ver lo anterior; rueda como en Ajustes.
