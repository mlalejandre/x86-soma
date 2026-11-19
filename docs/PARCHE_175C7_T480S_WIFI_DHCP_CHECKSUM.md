# Parche 175C7 · T480s: WiFi, el DHCP del router llega con el checksum UDP mal

Requiere el 175C6.

## Lo que se vio en el portátil (175C6)
`wifi tramas` mostró que cada OFFER del router llega íntegra (IP total 355 de 355, checksum IP ok, `dhcp_parse` la acepta) pero con **checksum UDP incorrecto**. `udp_input` descarta esos paquetes, por eso el cliente DHCP nunca las veía. Pasa con routers que mandan las respuestas DHCP sin checksum correcto (dhclient tampoco lo comprueba).

## Qué cambia
- `udp_input`: no se comprueba el checksum UDP de los paquetes 67 -> 68 (DHCP). El resto de UDP sigue verificándose. Seguridad: el paquete ya pasó el checksum IP y `dhcp_parse` valida xid, MAC y formato.
- `dhcp_run`: si falla, ya no pisa con la IP anterior una IP puesta mientras tanto (`wifi ipfija`); antes dejaba 0.0.0.0 y por eso las consultas DNS salían con origen 0.0.0.0.

## En el portátil
Conecta la WiFi y espera unos segundos: el DHCP debería dar IP solo (`wifi` muestra la IP). Prueba el navegador. Si no, `wifi prueba` y `wifi tramas` con foto.
