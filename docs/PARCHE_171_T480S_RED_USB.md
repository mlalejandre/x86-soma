# Parche 171 · T480s: internet por el móvil (USB) o un adaptador USB-Ethernet

Requiere el 170. Ficha 171 del plan: el puente hasta tener WiFi.

## Qué cambia
- **Red por USB** como interfaz `usb0`, con los tres protocolos estándar:
  - **RNDIS**: Android (la mayoría) y Windows;
  - **CDC-NCM**: Android moderno, Pixel;
  - **CDC-ECM**: adaptadores USB-Ethernet y algunos móviles.
- La función de red se busca **en todas las configuraciones** del dispositivo: un móvil con ADB y anclaje a la vez, o un
  adaptador que ofrece ECM solo en su segunda configuración.
- **RNDIS**: `INIT`, consulta de la MAC, filtro de paquetes y estado del medio. Las respuestas se piden sin esperar a la
  interrupción, y se contesta a los `KEEPALIVE` del dispositivo.
- **NCM**: se leen los parámetros NTB y se fija el tamaño de entrada a 16 KiB. Las tramas viajan en bloques NTB16 (varias
  por transferencia al recibir).
- **ECM / NCM**: la MAC sale de la cadena del descriptor Ethernet (la que el dispositivo reserva para el ordenador).
- **USB**:
  - transferencias de control con datos de salida y búfer propio;
  - 4 transferencias bulk de 16 KiB siempre en cola, recogidas sin bloquear;
  - un byte de relleno si la longitud es múltiplo del tamaño de paquete, como `usbnet` de Linux;
  - si un extremo se detiene, se reinicia.
- **En caliente**: al enchufar el móvil, el USB se vuelve a explorar, aparece `usb0` y el gestor de red cambia a ella y
  pide DHCP. Al quitarlo avisa. Si hay cable con enlace, se prefiere el cable.
- Interruptores: `soma.usbred=off` y `soma.usbnet=rndis|ecm|ncm` (forzar el protocolo si el dispositivo ofrece varios).
- **iPhone**: usa un protocolo propio de Apple que exige emparejar el teléfono. **No está incluido.**

## Pruebas
- `tests/test_usbnet_171.c` (anfitrión), con descriptores reales:
  - Android con ADB y RNDIS, Pixel con NCM, adaptador ECM de USB 3; un disco USB no se toma por red;
  - mensajes RNDIS (incluidos los rechazos);
  - dos tramas por transferencia;
  - NTB16 de ida y vuelta con divisor y resto, NTB con dos NDP encadenados;
  - 300 000 entradas al azar con ASan.
- QEMU, `usb-net` (un dispositivo con configuración RNDIS y configuración CDC-ECM):
  - **RNDIS** (elegida sola) y **ECM** (forzada): DHCP, ping, DNS y descarga TCP de 400 KB comprobada.
  - **Enchufar en caliente**: lo detecta, cambia a `usb0`, DHCP. Al quitarlo, avisa.

## En el portátil
1. Conecta el móvil Android con el cable y activa **Ajustes > Conexiones > Compartir conexión > Anclaje de USB**
   (el nombre varía según la marca).
2. Deben salir `USB: red RNDIS ... interfaz usb0 lista`, `RED: se usa usb0` y `dhcp: IP ...`.
3. `red` muestra el estado; `ping 8.8.8.8` y el navegador.
4. Si no aparece, mándame la salida de `usb` y de `red`.
