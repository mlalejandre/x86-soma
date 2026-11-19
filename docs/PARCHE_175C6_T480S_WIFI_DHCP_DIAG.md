# Parche 175C6 · T480s: WiFi, diagnóstico del DHCP recibido e IP fija automática

Requiere el 175C5.

## Lo que se vio en el portátil (175C5)
- `wifi prueba`: con cifrado hw, TID 8, secuencia del firmware y sin QoS **el router responde al ARP**. Eran los datos con TID 0: la WiFi ya pasa tráfico en los dos sentidos.
- El DHCP sigue sin respuesta, pero `wifi tramas` muestra que las OFFER del router (192.168.1.1:67 -> 255.255.255.255:68, 355 B) **llegan a SOMA a los pocos décimas de cada DISCOVER**. Fallan entre la tarjeta y el cliente DHCP.

## Qué cambia
- Cada paquete UDP recibido hacia el puerto 68 se pasa por las mismas comprobaciones que `ip_input`/`udp_input`/`dhcp_parse` y se anota: tipo, xid, chaddr, tamaño IP frente a lo recibido, checksum IP y UDP y veredicto de `dhcp_parse` (aceptado, no es para net_mac, mal formado, descartado por tamaño). `wifi tramas` lo muestra, con la cola hacia la pila (puestas/recogidas/pendientes) y `net_mac` frente a la MAC de la WiFi.
- Si `wifi prueba` consigue ARP pero no DHCP, **pone IP fija sola** (misma red /24, puerta y DNS = el router) para poder navegar; `netmgr` no vuelve a pedir DHCP mientras dure (se anula al reconectar).
- Salida de `wifi tramas` arreglada (los saltos de línea salían como `\n`).

## En el portátil
Conecta, `wifi prueba`, y prueba el navegador con una página `http://`. Después `wifi tramas` y foto (las líneas «DHCP recibido»).
