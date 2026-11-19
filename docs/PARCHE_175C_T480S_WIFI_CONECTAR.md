# Parche 175C · T480s: conectar a la WiFi (WPA2), Internet por WiFi y Ajustes > Redes

Requiere el 175B3.

## Qué hace
### Conexión WPA2-PSK
La tarjeta Intel 8265 es *SoftMAC*: el firmware solo transmite y recibe, y el resto lo hace el driver (en Linux, mac80211 + iwlmvm). SOMA hace una versión mínima:

1. **Firmware limpio**: se rearranca (unos 0,3 s), así no hace falta deshacer nada de una conexión anterior.
2. **Contextos del firmware**, con las mismas órdenes que Linux:
   - `PHY_CONTEXT`: el canal de la red, a 20 MHz;
   - `MAC_CONTEXT`: una estación;
   - `BINDING`;
   - la cola 5 (`SCD_QUEUE_CFG`, FIFO VO);
   - `ADD_STA` del punto de acceso.
3. **Tiempo reservado en el canal** (`TIME_EVENT`, como `iwl_mvm_protect_session`).
4. **Autenticación abierta y asociación**, con tramas 802.11 construidas por SOMA y enviadas con `TX_CMD`.
   - SOMA se presenta como estación 11a/g, sin HT ni WMM: velocidad máxima de 54 Mbit/s, pero mucho más sencillo y robusto.
   - La velocidad se ajusta sola entre 6 y 54 Mbit/s según los ACK.
5. **Asociados**:
   - `MAC_CONTEXT` con el AID y la sincronización DTIM tomada de la baliza;
   - `TIME_QUOTA` con todo el tiempo de radio para nuestro binding;
   - `MAC_PM_POWER_TABLE` sin ahorro de energía y con *keep-alive*.
6. **Saludo de 4 pasos EAPOL**, con el mismo suplicante que la Raspberry (`src/core/wpa.c`). También se renueva la clave de grupo.
7. **Cifrado CCMP en el procesador** (`src/core/ccmp.c`, nuevo):
   - el firmware no necesita claves;
   - probado con el vector oficial de 802.11 y con 40 vectores de AES-CCM de referencia;
   - se rechazan las repeticiones (PN).

Si una red tiene varios puntos de acceso, o 2,4 y 5 GHz con el mismo nombre, se prueban por orden: primero los canales que no son DFS, y entre ellos el de mejor señal.

Redes soportadas: abiertas, WPA2-PSK y WPA2/WPA3 mixtas (se usa la parte WPA2), siempre con CCMP. No se admiten WPA3 puro, Enterprise, WEP ni TKIP; en esos casos se explica el motivo.

### Internet por WiFi
- Nueva interfaz `wlan0`.
- El gestor de red usa la primera interfaz con enlace: **cable > móvil por USB > WiFi**. Al conectar la WiFi pide IP por DHCP.
- Cada 25 s sin tráfico se envía una trama nula para que el AP no nos dé por perdidos.
- Si se pierde la conexión (desautenticación o balizas perdidas), se reconecta sola.

### Región
El firmware deduce el país de las balizas (`MCC_CHUB_UPDATE`) y SOMA se lo confirma, como Linux. Así se habilitan los canales de 5 GHz de España.

### Redes guardadas
- Se guardan en `/etc/wifi.conf` con la clave PMK derivada; la contraseña no se guarda en claro.
- Al arrancar, y cada 30 s sin conexión, SOMA busca y se conecta sola a la mejor red guardada visible.
- Después de `wifi desconectar` no se reconecta sola hasta que conectes a mano.

### Ajustes > Redes (nuevo)
- **Conexiones**: estado del cable, el móvil por USB y la WiFi, cuál está en uso, IP y estado del gestor.
- **WiFi**:
  - interruptor para activar o desactivar;
  - estado: conectando, conectada a… o el motivo del fallo;
  - botones **Buscar redes** y **Desconectar**;
  - lista de redes con barras de señal, seguridad y si está *guardada* o *conectada*.
- Al pulsar una red sale el campo de **contraseña** (con «ver»), y los botones **Conectar** y **Olvidar** si estaba guardada. Intro también conecta.
- La página se actualiza sola mientras conecta o busca.

### Terminal
- `wifi conectar <red> [clave]` (comillas si el nombre tiene espacios): muestra cada paso, y después la IP.
- `wifi desconectar`
- `wifi guardadas`
- `wifi olvidar <red>`
- `wifi activar` / `wifi desactivar`
- **`wifi registro`**: los últimos 48 sucesos con hora, cada paso de la conexión y el motivo de cualquier fallo. **Es lo que hay que fotografiar si algo falla.**
- `wifi` muestra además el estado de la conexión: red, canal, tasa, tramas enviadas y recibidas, MIC erróneos e IP.

### Otros
- `cpus prueba` reserva su búfer de 256 KiB al usarse, en lugar de llevarlo fijo en la imagen del kernel. La imagen se acercaba al límite de 14 MiB.

## En el portátil
1. Abre **Ajustes > Redes** y pulsa **Buscar redes**. Elige tu red, escribe la contraseña y pulsa **Conectar**.
2. O en el terminal: `wifi conectar "Mi red" micontrasena`.
3. Si conecta: desenchufa el cable y prueba el navegador o el agente.
4. Si no conecta: haz foto de `wifi registro` y de `wifi`.
