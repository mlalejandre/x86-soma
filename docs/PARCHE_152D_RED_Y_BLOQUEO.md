# Parche 152D — Scripts descargados en segundo plano y lista de bloqueo editable

Se aplica encima de los parches 152, 152B y 152C.

## 1. Descargas de scripts fuera del hilo del navegador

En la portada de 20minutos.es, cada script que inserta la propia página (publicidad, consentimiento, analítica)
tardaba entre 200 y 440 ms, casi todo en la red. Mientras se descargaba, la pestaña no atendía clics ni teclas.

Ahora la descarga de **todos** los scripts externos va a un hilo de red propio (`web_js_io`). Sirve tanto para los
scripts escritos en el HTML como para los que inserta el JavaScript. Usa el mismo protocolo, ya probado, que el hilo de
imágenes del parche 151. El hilo del navegador solo ejecuta el código cuando llega; mientras tanto atiende la entrada,
el hover y las imágenes. El orden de los scripts del HTML se respeta.

| Prueba: 8 scripts de 400 ms | Antes (152C) | Ahora |
|---|---|---|
| Bloqueo más largo del hilo del navegador mientras cargan | 506 ms | **44 ms** |

Solo funciona con `SOMA_WEB_ASYNC_IMAGES=1`, porque usa los hilos y el cerrojo HTTP del 151. Sin esa opción, o con
`-DSOMA_WEB_ASYNC_SCRIPTS=0`, las descargas vuelven a ser síncronas, como antes. Las descargas siguen siendo de una en
una: el cliente HTTP sigue sin admitir varias a la vez.

Con `-DSOMA_WEB_PERF_LOG=1`, el tiempo de `[web152] script` y `script dinamico` pasa a ser solo el de **ejecutar** el
JavaScript; la red ya no cuenta, porque ocurre en paralelo.

## 2. Lista de bloqueo del usuario: `/etc/bloqueo_web.txt`

El archivo se relee al cargar cada página. Formato: una entrada por línea; cada entrada es un dominio o cualquier trozo
de la URL.

```
# analitica y publicidad que no quiero cargar
sensic.net
mrf.io
hb.20m.es
# permitir algo que la lista interna bloquea:
!googletagmanager.com
```

- Las líneas que empiezan por `#` son comentarios.
- Una entrada que empieza por `!` permite esa URL aunque la lista interna la bloquee.
- Se aplica a los scripts del HTML y a los que inserta el JavaScript. A un script bloqueado se le envía su evento
  `error`, como haría un bloqueador de anuncios.

Se puede editar desde SOMA, por ejemplo con `nano /etc/bloqueo_web.txt` en la terminal o con el bloc de notas.
