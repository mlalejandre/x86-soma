# Parche 196: arrancar llama-server desde SOMA

Requiere el parche 195.

## El problema

`ejecutar.py` (con `tools/llm_server.py`) arranca llama.cpp en el servidor por SSH eligiendo un modelo. SOMA en un equipo real no
tiene SSH: si el servidor estaba apagado, desde Ajustes > IA no habia forma de encenderlo.

## La solucion

Un servicio pequeno en el servidor, `tools/soma_lanzador.py`, que hace lo mismo que `llm_server.py` (misma orden y parametros de
llama-server, mismas variables de entorno, mismos archivos de PID y de registro) cuando SOMA se lo pide por HTTP.

### En el servidor (Linux), una sola vez

    scp tools/soma_lanzador.py mlalejandre@192.168.1.200:~/
    ssh mlalejandre@192.168.1.200
    python3 ~/soma_lanzador.py --instalar-servicio      # escribe el servicio de systemd y ensena la clave
    systemctl --user daemon-reload
    systemctl --user enable --now soma-lanzador
    sudo loginctl enable-linger mlalejandre              # que arranque con el equipo

Escucha en el puerto 8086 (`SOMA_LANZADOR_PORT`). Si usas variables como `MODELS_DIR` o `LLAMA_SERVER_BIN`, tenlas definidas al
ejecutar `--instalar-servicio`: se copian al servicio. `python3 soma_lanzador.py --clave` vuelve a ensenar la clave.

### En SOMA

- **Ajustes > IA**, proveedor Local / red: campo **Lanzador** (la clave) y boton **Arrancar servidor**. Lista los modelos .gguf del
  servidor; al pulsar uno lo arranca, espera a que cargue (se ve el tiempo), lo deja elegido y hace la prueba.
- **Terminal**:

      llm-config lanzador                      estado
      llm-config lanzador clave <clave>        (no queda en el historial)
      llm-config lanzador servidor ip:puerto   (por defecto: el equipo de servidor.local, puerto 8086)
      llm-config lanzador modelos
      llm-config lanzar <numero o nombre>
      llm-config parar                         para llama-server y libera la VRAM

## Seguridad

- El lanzador solo atiende a equipos de la red local y exige la clave en cada peticion (16 caracteres al azar, guardada en
  `~/.config/soma/lanzador.clave`, solo legible por el usuario). Un segundo de espera por cada clave incorrecta.
- Solo puede hacer tres cosas: listar los .gguf de `MODELS_DIR`, lanzar `LLAMA_SERVER_BIN` con uno de ellos (elegido por nombre de
  la lista, nunca por ruta) y pararlo. No ejecuta ordenes arbitrarias.
- La clave viaja por http dentro de la red local (sin cifrar): quien pueda escuchar tu red podria arrancar o parar el modelo.
  SOMA se niega a enviarla a un equipo de fuera de la red local por http.
- En SOMA la clave vive en `/etc/llm.conf`, ilegible para los agentes; `llm-config` no es una orden de agente.

## Archivos

Nuevo: `tools/soma_lanzador.py`. Cambiados: `src/agent/llm_proto.c/.h`, `src/agent/llm.c/.h`, `src/agent/shell.c`,
`src/apps/settings.c`, `src/ui/console.c`, `tests/test_llm_proto_195.c`.
