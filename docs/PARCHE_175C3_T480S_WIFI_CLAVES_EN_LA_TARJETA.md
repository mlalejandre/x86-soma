# Parche 175C3 · T480s: WiFi, las claves WPA2 se instalan en la tarjeta (las tramas individuales ya llegan)

Requiere el 175C2.

## Lo que se vio en el portátil (175C2)
- Conectada a Livebox6-7E21, con el saludo de 4 pasos completo y sin repeticiones del mensaje 3.
- **Recepción**: 65 tramas protegidas, todas **de grupo** (difusión del router: 27 ARP y 26 IPv4), descifradas bien en el procesador. **Cero tramas individuales**, ni bien ni mal descifradas: la tarjeta no las entregaba.
- **Envío**: 12 tramas cifradas confirmadas por el AP. Pero `dhcp` mandó 4 DISCOVER y no contestó nadie, y el navegador no resolvía nombres.

## Causa
El firmware de las tarjetas *mvm* no admite que el cifrado se haga fuera de la tarjeta (Linux lo dice: «iwlmvm doesn't allow to disable HW crypto»). Sin la clave del enlace instalada, la tarjeta descarta las tramas individuales cifradas que le llegan. Las de grupo sí pasaban, y por eso se veían el ARP y el IPv4 de difusión.

## Qué cambia (como Linux en las tarjetas de esta generación)
- **Instalación de claves**: al terminar el saludo de 4 pasos, después de que salga el mensaje 4, se instalan en la tarjeta con `ADD_STA_KEY` (v1, como `iwl_mvm_send_sta_key`):
  - la clave individual (posición 0);
  - la de grupo (posición 1, con su índice y la marca de multidifusión).
- Cuando el AP renueva la clave de grupo, se reinstala.
- **Recepción**: si la tarjeta descifró la trama (CCM con el MIC correcto), SOMA solo quita la cabecera CCMP y el MIC y comprueba que el PN no se repita. Si no la descifró, se sigue intentando en el procesador, como antes.
- **Envío**: SOMA pone la cabecera CCMP con su PN y la tarjeta cifra y añade el MIC (`TX_CMD_SEC_CCM` con la clave en la orden), igual que iwlmvm en las 8000. El recuento de bytes incluye esos 8 del MIC.
  - Para diagnosticar, `wifi cifrado sw` vuelve a cifrar en el procesador y `wifi cifrado hw` deja el modo normal.
- La orden `wifi` muestra el estado de las claves en la tarjeta (1 = bien), cuántas tramas descifró la tarjeta y cuántas fallaron.

## En el portátil
Conecta, espera unos segundos y prueba `dhcp` y el navegador. Si algo falla, haz foto de `wifi` y `wifi registro`.
