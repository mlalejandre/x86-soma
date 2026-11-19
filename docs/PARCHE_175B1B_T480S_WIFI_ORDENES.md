# Parche 175B1B · T480s: WiFi, corrección del tamaño de las órdenes cortas

Requiere el 175B1.

## Lo que se vio en el portátil (175B1)
- Cola de órdenes funcionando: `TX_ANT_CONFIGURATION` respondió y la MAC se leyó (04:EA:56:4E:AB:1F, de Intel).
- Con `PHY_CONFIGURATION` el firmware se detuvo con el código **0x38 = BAD_COMMAND**.
- El último paquete recibido fue 0x02 (`REPLY_ERROR`): el firmware rechazó la orden.

## Causa
El primer tramo del descriptor (TB0) se declaraba siempre de 20 bytes. Linux usa `min(tamaño, 20)`.

`PHY_CONFIGURATION` ocupa 16 bytes (cabecera de 4 más 12 de datos), así que al firmware le llegaba con 4 bytes de más, y la rechazaba por tamaño incorrecto. `TX_ANT` no comprueba el tamaño, y por eso pasó.

## Qué cambia
- TB0 = `min(tamaño, 20)`, como `tb0_size` en `iwl_pcie_enqueue_hcmd`.
- La orden `wifi` muestra el `REPLY_ERROR` del firmware (tipo, orden y secuencia), si lo hay.
- También muestra el nombre del código de error: BAD_COMMAND, SYSASSERT, NMI_INTERRUPT_WDG o FATAL_ERROR.

## En el portátil
`wifi` y foto. Lo esperado: `FIRMWARE VIVO Y RADIO CALIBRADA`.
