# Parche 175C4 · T480s: WiFi, la tarjeta deja de dormirse y se recogen las tramas que el AP guarda

Requiere el 175C3.

## Lo que se vio en el portátil (175C3)
- Las claves están en la tarjeta (estado 1 las dos) y la tarjeta descifra: 80 tramas, ninguna fallida.
- Las tramas que enviamos llegan al AP: 21 enviadas, 0 sin ACK.
- Pero en 12 s solo llegó **una** trama dirigida a nosotros, frente a 79 de grupo, y el DHCP seguía sin respuesta.

## Causa probable
Es lo que pasa cuando el AP cree que la estación está **dormida**, en ahorro de energía:
- guarda sus tramas individuales y lo anuncia en el TIM de cada baliza;
- las de grupo las sigue mandando tras el DTIM.

SOMA nunca enviaba `POWER_TABLE_CMD`, la orden de ahorro de energía de todo el dispositivo, que Linux sí envía en `iwl_mvm_up`. Sin ella, el firmware queda con su ahorro por defecto.

## Qué cambia
- **`POWER_TABLE_CMD` con flags 0** (CAM, sin ahorro) al poner en marcha la imagen RUNTIME.
- **Las balizas del AP se siguen recibiendo después de asociarse** (`MAC_FILTER_IN_BEACON`), y SOMA lee el TIM.
  - Si nuestro AID aparece marcado (el AP nos guarda tramas), se envía una trama nula sin el bit de ahorro (PM = 0), como mucho cada 150 ms, para que el AP sepa que estamos despiertos y las suelte.
- Al terminar de conectar se manda una nula sin PM, para anunciar desde el principio que estamos despiertos.
- La orden `wifi` muestra las balizas recibidas del AP, cuántas traían nuestro bit en el TIM y cuántas nulas se mandaron para recoger las tramas.

## En el portátil
Conecta y prueba `dhcp` y el navegador (una página `http://`). Si algo falla, haz foto de `wifi`.
