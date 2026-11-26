# Parche 336 — El investigador adapta fuentes e informe al tema

## Lo que fallaba
- Con un modelo que razona, el investigador no recibía búsquedas propuestas (200 fichas, gastadas pensando) y buscaba solo la
  pregunta resumida. El mismo fallo que el buscador de YouTube (335).
- Se buscaba igual para todo (Wikipedia en español y DuckDuckGo) y se leían las páginas en el orden en que llegaban: para un
  resultado de fútbol salía antes la Wikipedia del club que la crónica del partido; para una duda de salud, cualquier blog.
- El informe se pedía igual para todo: sin fechas en deportes, sin prudencia en salud, sin separar consenso de hipótesis en
  ciencia.

## Qué cambia (`src/agent/inv_tema.c`, puro)
- **Tema de la pregunta**: deportes, salud, ciencia, tecnología, actualidad o general (por palabras clave, sin mayúsculas
  ni tildes). Se ve en la barra de fases del orbe y en el chat («tema salud», «tema deportes (de hoy)»).
- **Actualidad**: «hoy», «ayer», «anoche» → resultados del último día; «último», «resultado», «clasificación», «quién
  ganó», noticias → de la última semana (filtro de fecha de DuckDuckGo).
- **Fuentes por tema**: unas 110 fuentes puntuadas por tema. Salud: MedlinePlus, OMS, CDC, NIH, Ministerio de Sanidad,
  AEMPS, Manual MSD, Clínica Universidad de Navarra, AECC…; deportes: prensa deportiva y sitios oficiales (UEFA, LaLiga,
  NBA, ATP, Fórmula 1…); ciencia: NASA, ESA, Nature, Science, CSIC, SINC…; tecnología: documentación oficial (MDN,
  Python, Microsoft Learn, Arch Wiki…); actualidad: agencias (EFE, Reuters, AP), RTVE, prensa y fuentes oficiales (BOE, INE).
  Los organismos públicos y universidades suben; Quora, Brainly, tiendas, Pinterest y foros bajan o no se leen; en salud, lo
  que vende remedios o homeopatía no se lee. Se eligen las mejores de 16 candidatas (antes, las 5 primeras de 10).
- **Wikipedia en inglés** también para ciencia, salud y tecnología.
- **Las búsquedas** las propone el modelo sabiendo el tema (en salud, una con el nombre de una fuente sanitaria; en deportes
  recientes, con el mes o «última jornada»; en ciencia, una en inglés), con 1200 fichas y sin el razonamiento.
- **El informe** lleva instrucciones del tema: fechas y marcadores exactos (y decir si falta el último partido); en salud,
  fuentes oficiales primero, sin dosis que no estén en ellas, síntomas de alarma y aviso final de consultar a un profesional;
  en ciencia, consenso frente a hipótesis y unidades; en tecnología, versiones y código en bloques; en actualidad, fecha de
  cada hecho y hechos frente a opiniones. Del informe se quita el razonamiento del modelo.

## Pruebas
- `test_inv_tema_336` (en `make test`): temas con tildes y mayúsculas, actualidad, puntuación de fuentes (subdominios, rutas
  como bbc.com/sport, «as.com» no es «canvas.com», organismos públicos, foros, remedios), orden estable, instrucciones,
  razonamiento fuera y 20000 entradas al azar.
- Necesita `make`.
