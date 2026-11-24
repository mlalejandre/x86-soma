# Parche 288 · Investigación y búsquedas en internet de verdad

## Qué pasaba

- `search` solo devolvía los títulos y las dos líneas de resumen de Wikipedia y DuckDuckGo. El modelo respondía con esos fragmentos y poco
  más, así que las respuestas «de internet» se quedaban cortas o se las inventaba.
- Pedir «investiga…» o «busca información sobre…» iba al bucle general del agente, que a veces hacía una sola búsqueda y otras ninguna.
  Nunca leía las páginas.
- La voz del orbe leía las citas («corchete uno») y las listas de enlaces.

## Qué cambia

### Nueva orden `investiga <pregunta>` (`src/agent/shell_investiga.c`)

1. **Las búsquedas.** El modelo propone de 2 a 3 búsquedas cortas y distintas (en español o inglés, según convenga). Se añade la propia
   petición limpia: «búscame información sobre agujeros negros en internet» se busca como «agujeros negros». Si el modelo no contesta, se
   busca solo la petición limpia.
2. **Buscar.** Cada búsqueda va a la API de Wikipedia en español y a DuckDuckGo ligero.
3. **Leer las páginas.** Se leen hasta 5 páginas distintas, con dos como mucho del mismo sitio. Quedan fuera las redes sociales, los vídeos,
   las tiendas y los archivos (PDF, ZIP…). De Wikipedia se pide solo el artículo (`action=render`), que pesa mucho menos.
4. **El informe.** El modelo escribe la respuesta en Markdown:
   - un primer párrafo que contesta directamente;
   - secciones y listas o tablas si hacen falta;
   - una cita `[n]` detrás de cada dato;
   - al final, `## Fuentes` con los títulos y las URL.

   Se le pide que no invente nada que no esté en las fuentes y que diga si se contradicen. Lleva la fecha de hoy, para que «lo último» tenga
   sentido. El texto que se le pasa se ajusta al tamaño de prompt configurado en `llm-limites`.
5. **Sin modelo**, devuelve los resultados y los extractos de las dos primeras páginas, con el motivo del fallo.

Cada fase tiene su propio tope de tiempo:

| Fase | Tope |
| --- | --- |
| Pedir las búsquedas al modelo | 2 min |
| Las búsquedas | 45 s |
| Leer las páginas | 50 s, y 9 s por página |
| Escribir el informe | 5 min |

Los pasos se ven en la barra de fases del orbe y en el chat: «buscar», una línea por cada búsqueda y por cada página que lee
(`leer es.wikipedia.org`), «pensar» y «escribir».

### Texto legible de una página (`src/agent/web_lee.c`)

- Quita `script`, `style`, `nav`, `header`, `footer`, `aside`, formularios, menús y comentarios.
- Si la página tiene `<article>` o `<main>` con texto de verdad, se queda solo con eso.
- Convierte los títulos en `##` y las listas en viñetas, y decodifica las entidades (`&aacute;`, `&#8212;`, `&#xED;`…).
- Conserva solo las líneas con frases de verdad (fuera los «Ver más», las cookies y los botones), sin repetir líneas seguidas.
- Elige las URL útiles de unos resultados. Entiende las de Wikipedia con paréntesis, como `Mercurio_(planeta)`.

### SOMA la usa sola (`src/agent/agent_ruta.c`, `ar_investiga`)

Desde el orbe o el chat, la petición va directa a la investigación, antes que el LCM y el bucle del agente:

| Petición | Qué hace |
| --- | --- |
| «investiga…», «busca información sobre…», «averigua…», «infórmate…», «últimas noticias de…», «qué se sabe de…» | Investigación a fondo: el modelo propone las búsquedas y se leen 5 páginas |
| «busca X en internet / en la web / en Google / online» | Búsqueda rápida: la petición limpia, 3 páginas y una llamada al modelo menos |

Nada cambia para lo que es de archivos, carpetas, disco, YouTube o vídeos, música, la IP o el DNS, la RAM o la memoria de SOMA, o programar.
Se ha probado con las 2344 frases del catálogo del LCM. Solo cambian de camino 20, todas peticiones de búsqueda en internet que antes iban a
`search` y ahora reciben un informe. Las dos de DNS («averigua la ip de…») siguen yendo a `dns`.

El modelo también tiene `investiga` en su lista de herramientas. Las URL de su resultado cuentan como de un resultado de búsqueda para las
reglas de red del agente (SOMA_193).

### `search` mejorada (`src/agent/shell_red.c`)

Además de los resultados, `search` devuelve el texto limpio (unos 1800 caracteres) de la primera página útil. El modelo ya tiene algo más
que dos líneas de resumen.

### La voz (`src/core/habla_texto.h`)

La voz del orbe ya no lee las citas `[1]`, `[2, 3]` ni `[1-4]`, y deja de leer al llegar a `## Fuentes` o a «Fuentes:».

## Pruebas

`tests/test_investiga_288.c` tiene 88 comprobaciones, entre ellas:

- el texto de una página con cabecera, menú, scripts, estilos, publicidad, pie, comentarios y entidades;
- el título, los topes y el HTML roto;
- las URL útiles y los dominios;
- qué peticiones son de investigar y cuáles no, y la consulta limpia, que conserva tildes y eñes;
- la voz sin citas ni fuentes.

Siguen pasando `test_rutas_251`, `test_busca_251` y `test_tts_250`.

Hace falta `make`.
