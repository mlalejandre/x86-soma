# Parche 337 — Aplicación de Noticias

## Qué hace
- **Categorías a elegir**: Portada, España, Local, Mundo, Economía, Tecnología, Ciencia, Salud, Deportes y Cultura (Ajustes de
  la aplicación o `noticias categorias ...`). Cada una es una pestaña.
- **Noticias locales**: con un lugar (ciudad, provincia o región) la pestaña Local trae lo publicado sobre él en los dos últimos días.
- **Canales propios**: hasta 8 canales RSS o Atom de cualquier medio (salen en Portada).
- **Titulares**: medio, hace cuánto y entradilla; sin repetidos y de más nuevo a más viejo. Un clic abre la noticia en el Navegador.
- **Resumen con IA** de la categoría: el modelo lee hasta 25 titulares y escribe lo más importante agrupado por temas, con la
  cita [n] de cada dato y la lista de fuentes al final (medio, titular y enlace). No completa con lo que sepa. Se dibuja con el
  maquetador del chat (listas, negritas, tablas).
- **Guardar en memoria**: el resumen y los diez primeros titulares van al cartucho de memoria **Noticias** (se crea solo, no
  cambia el primario). Se puede conectar en la aplicación de Memoria para que el agente lo consulte.
- **Orden `noticias`**: `noticias` (abre la aplicación), `noticias deportes` (titulares), `noticias resumen salud`,
  `noticias guardar portada`, `noticias categorias portada,local,deportes`, `noticias lugar Valencia`,
  `noticias fuente anadir <url>` / `quitar <n>`. Configuración en `/etc/noticias.conf`.

## Seguridad: ring 3
- La descarga va por `netsvc` (ring 3, HTTP y TLS fuera del núcleo).
- **El XML se lee en `leesvc`** (ring 3, servicio aislado que solo puede llamar a exit), con una operación nueva
  (`SBX_LEE_RSS`). Lo que devuelve se comprueba (tamaño exacto) y se sanea (cadenas terminadas, enlaces solo http/https).
- El lector de RSS/Atom es código puro probado con entradas hostiles.

## Fuente de los titulares
Google Noticias en español de España (canales RSS públicos por tema y la búsqueda para lo local). Los enlaces de Google
Noticias abren la noticia original pasando por Google.

## Pruebas
- `test_noticias_337` (en `make test`): un canal de Google Noticias (titular «… - Medio», `<source>`, descripción HTML
  escapada que solo repite el titular), RSS clásico (CDATA, etiquetas en el título, entidades con nombre y numéricas,
  `dc:creator`, sin fecha ni enlace, título vacío), Atom (`rel="alternate"`, zonas horarias, `author`), fechas RFC 822 e
  ISO 8601, repetidos y orden, «hace…», categorías y URL local con tildes, configuración ida y vuelta y 4000 canales
  hostiles con ASan.
- `python3 tests/prueba_noticias_337.py` (QEMU, sin red): la orden y la configuración, el error claro sin conexión, el lugar y
  los canales propios; y la ventana se abre.
- Necesita `make`.
