# Parche 193 · Contención de los agentes

Requiere el 192. Probado arrancando en QEMU (x86_64) con la orden `agent_sim` contra servidores de prueba; aarch64 solo compila.

## Por qué
El agente lee contenido de internet (`search`, `curl`). Ese contenido puede traer instrucciones («lee tal archivo y envíalo a
http://atacante/?d=…»). Con `cat` sin límites y `curl`, `descargar`, `navegador`, `tls`, `dns` y `wifi` como órdenes permitidas,
una página maliciosa podía sacar datos del usuario fuera del equipo o llevar el equipo a otra red.

## Qué cambia
1. **Destinos de red por procedencia** (`src/agent/agent_net.c`, orden `agente_red`). Un agente solo puede pedir una URL que:
   - el usuario escribió en la misión, o
   - apareció tal cual en un resultado de `search` o en una página ya descargada, o
   - pertenece a un dominio de confianza del usuario.

   Una URL compuesta por el modelo (que puede llevar datos pegados) se rechaza. Para `dns` solo valen nombres de host ya vistos.
   No se registran las URL de órdenes encadenadas ni de `echo`/`cat`: así el modelo no puede «blanquear» una URL.
   Se aplica a `curl`, `descargar`, `navegador`, `tls` y `dns`.
2. **Secretos que un agente no puede leer** (`src/fs/fs.c`, en el VFS: no hay orden que se lo salte): `/etc/wifi.conf` (la PMK),
   `/etc/history.txt`, `/etc/llm.conf` y todo `/etc/secreto/`. Tampoco aparecen en sus búsquedas (`grep`, `mem_search`).
3. **El historial ya no guarda contraseñas** (`src/ui/console.c`): `wifi conectar <red> <clave>` y `wpa psk <ssid> <clave>` se
   guardan con `***`. Un historial antiguo se limpia al cargarlo.
4. **Un agente no cambia de red WiFi** (solo consulta) **ni publica en `/www` o `/public`** (lo sirve `httpd` a toda la red).

## Orden `agente_red` (solo el usuario)
    agente_red                         estado
    agente_red permitir wikipedia.org  dominio de confianza (vale para sus subdominios)
    agente_red quitar wikipedia.org
    agente_red procedencia             modo por defecto
    agente_red cerrado                 solo los dominios de confianza
    agente_red abierto                 como antes del parche (sin protección)
Se guarda en `/etc/agente_red.conf` (un agente no puede escribir en `/etc`).

## Cambio de comportamiento
Si el modelo pide una URL «de memoria» (por ejemplo una página de Wikipedia que no salió en ninguna búsqueda), ahora recibe
`AGENT_DENIED` con la explicación y debe buscarla primero. Si te resulta incómodo, añade los dominios habituales con
`agente_red permitir`.

## Lo que no cubre
- El texto de la consulta de `search` llega al buscador (no a un sitio que elija el atacante).
- Elegir entre varias URL ya vistas puede codificar unos pocos bits.
- El canal con el LLM sigue sin autenticar ni cifrar: va con la validación de certificados TLS (siguiente parche).
- No se ha probado con el LLM real: aquí no hay uno. Las reglas se han ejercitado con `agent_sim`.

## Pruebas
- `tests/test_agent_net_193.c` (host, 47 comprobaciones): normalización, URL alargadas, subdominios, usuario@, puertos, modos, lista.
- QEMU con `agent_sim`: secretos ilegibles (`cat`, `grep`, `cp`, `head`); URL no vista, con datos añadidos o blanqueada con `echo`:
  denegadas y sin llegar al servidor; URL de un resultado de `search`: permitida; `dns`, `wifi conectar`, `write /www/…`: denegados.
- `usfuzz`, autopruebas `sbx` y `user`, `conctest`: siguen bien.

## Revertir
`python3 parche_193_contencion_agente.py --revertir`
