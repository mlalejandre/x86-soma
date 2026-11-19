# Parche 197: servidor web bajo control y opciones de compilacion seguras

Requiere el parche 196.

## 1. El servidor web (httpd) ya no esta siempre encendido

El nucleo arrancaba siempre un servidor web en el puerto 80, sin contrasena, que ensena el panel, la telemetria, la pizarra de los
agentes y lo que haya en /www y /public. En QEMU solo lo ve el anfitrion (localhost:8090). En un portatil real conectado a una
WiFi lo veia cualquier equipo de esa red, y cualquiera que llegase al portatil a traves de un encaminador.

- **Por defecto**: activo en maquina virtual (nada cambia con ejecutar.py), **apagado en un equipo real** (puerto 80 cerrado).
- `httpd on` / `httpd off` lo cambian al momento y lo recuerdan en `/etc/httpd.conf`. `httpd` ensena el estado.
- Un agente puede consultar el estado, pero no encenderlo ni apagarlo.
- Encendido, **solo atiende a equipos de la misma red local** (misma subred que la interfaz). A los demas les cierra la conexion
  sin responder; `httpd` cuenta los rechazos.

## 2. Opciones de compilacion

Se anaden al nucleo (x86_64 y aarch64) y a los servicios de ring 3: `-fno-strict-aliasing -fno-delete-null-pointer-checks -fwrapv`.

El nucleo reinterpreta bufferes de red y de disco como estructuras, hace aritmetica que da la vuelta (numeros de secuencia TCP,
temporizadores) y trabaja con direcciones bajas. El estandar de C permite al optimizador dar por imposible ese codigo y
eliminarlo o reordenarlo; el resultado son fallos que aparecen solo al cambiar de version del compilador. Son las opciones con
las que se compila Linux. El nucleo crece un 0,1 % y no se pierde ninguna funcion.

Al activarlas el compilador aviso de una variable que no podia probar inicializada en `x509.c` (`mont_mul`): ahora se pone a cero
entera. No cambia el resultado (las 3030 comprobaciones de certificados siguen pasando).

## Archivos

`src/net/httpd.c`, `src/net/httpd_policy.h`, `src/net/tcp.c/.h` (`tcp_peer_ip`), `src/agent/shell.c` (ayuda), `src/core/x509.c`,
`Makefile`, `tests/test_httpd_policy.c`.
