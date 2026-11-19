# Parche 152C — Scripts insertados por JS: asíncronos y con bloqueo de rastreadores

Se aplica encima de los parches 152 y 152B.

## Qué se midió

En la portada de 20minutos.es, tres scripts escritos dentro del HTML tardaban 919, 418 y 362 ms, pese a ocupar menos
de 5 KB cada uno. Su propio código no tarda eso. Ese tiempo lo ocupaban los `<script src>` que esos scripts insertan
con `appendChild` (así se cargan Google Tag Manager, la publicidad y la analítica): el motor los descargaba y ejecutaba
**dentro** de la llamada a `appendChild`, bloqueando la pestaña. Además, esos scripts insertados se saltaban la lista de
bloqueo de rastreadores, que solo se aplicaba a los scripts del HTML.

## Qué cambia

- Un `<script src>` insertado por JS se pone en cola (hasta 64) y se ejecuta después, por turnos de unos 25 ms desde
  `wp_tick`, igual que en los navegadores, donde estos scripts son `async` por defecto. Siguen recibiendo sus eventos
  `load` y `error`, y `document.currentScript` sigue funcionando.
- Pasan por la misma lista de bloqueo que los scripts del HTML (googletagmanager, doubleclick, taboola, criteo…). A un
  script bloqueado se le envía `error`, como haría un bloqueador de anuncios.
- Si la cola se llena, el script se ejecuta en el acto, como antes.
- Con `-DSOMA_WEB_ASYNC_DYN_SCRIPTS=0` se vuelve al comportamiento anterior.
- Con `-DSOMA_WEB_PERF_LOG=1` el registro muestra `[web152] script dinamico <ms> <url>` y
  `[web152] script bloqueado <url>`.

## Prueba

Un script en línea inserta 4 scripts de 400 ms y uno de Google Tag Manager:

- **Antes:** ese script en línea tardaba unos 1,7 s.
- **Ahora:** todos los scripts del HTML terminan en 0,32 s. Los 4 dinámicos se ejecutan después y disparan su `load`.
  El de Google Tag Manager se bloquea y dispara `error`. El título de la página confirma `dyn 4/1`.
