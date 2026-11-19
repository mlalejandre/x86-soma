# Parche 175C5 · T480s: WiFi, el TID de los datos y un paquete completo de diagnóstico

Requiere el 175C4.

## Lo que se vio en el portátil (175C4 + `wifi cifrado sw`)
- Con el cifrado de envío en el procesador, `dhcp` sigue sin respuesta: el cifrado de la tarjeta no era la causa.
- El router no manda **ninguna** trama individual (individuales ok 0, 57 de grupo); nuestras tramas se confirman (fallos 0).

## Qué cambia
- **TID de los datos = 8** (`IWL_TID_NON_QOS`, como Linux). Antes iba en 0 aunque la cola está configurada para el TID 8 (las tramas de gestión sí usaban 8). `wifi tid 0` devuelve el comportamiento anterior.
- **EAPOL con `pm_frame_timeout` de gestión** (como Linux con `IEEE80211_TX_CTRL_PORT_CTRL_PROTO`).
- **Registro de tramas** (96 entradas): cada trama de datos enviada o recibida con su dirección, tipo (ARP con IP, IPv4 con protocolo y puertos, EAPOL), si iba cifrada, las descartadas con el motivo (repetición, MIC, otra clave) y las de gestión dirigidas a nosotros (desautenticación...). `wifi tramas` lo muestra junto con las IP vistas en la red (para deducir el router), las tramas dirigidas a nuestra MAC y la última respuesta cruda de TX. `wifi tramas limpiar` lo vacía.
- **Interruptores en caliente**: `wifi cifrado sw|hw`, `wifi tid 0|8`, `wifi seq fw|sw` (numeración de secuencia del firmware o del driver) y `wifi qos on|off` (tramas QoS).
- **`wifi prueba`** (en segundo plano): con la WiFi conectada, envía un ARP al router (deducido del tráfico visto; si no, 192.168.1.1) con 9 combinaciones de esos ajustes y se queda con la primera que obtenga respuesta; luego prueba el DHCP. Si ninguna responde, restaura los ajustes.
- **`wifi ipfija`**: pone IP fija (misma red /24, puerta y DNS = el router deducido) por si el ARP funciona y el DHCP no.

## En el portátil
1. Conecta la WiFi. 2. `wifi prueba` (tarda hasta ~15 s). 3. `dhcp`. 4. Foto de `wifi prueba` y de `wifi tramas`.
Interruptores: `soma.X=off` no aplica (todo es de diagnóstico y no cambia la conexión salvo `tid`).
