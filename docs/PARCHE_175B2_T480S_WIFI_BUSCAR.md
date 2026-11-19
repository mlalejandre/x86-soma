# Parche 175B2 · T480s: WiFi, imagen de funcionamiento (RUNTIME) y búsqueda de redes

Requiere el 175B1B.

## Lo que se vio en el portátil (175B1B)
`FIRMWARE VIVO Y RADIO CALIBRADA`: 16 resultados de calibración guardados y MAC 04:EA:56:4E:AB:1F.

## Qué cambia
Tras calibrar con la imagen INIT, el driver sigue los pasos de Linux (`iwl_mvm_load_rt_fw` + `iwl_mvm_up`):

1. **Parada limpia de la tarjeta**, como `iwl_trans_stop_device`:
   - colas y DMA detenidos;
   - maestro del bus parado;
   - reinicio.
2. **Carga de la imagen RUNTIME** (8 secciones) y espera de ALIVE.
3. **Paginación de la CPU2**: la página CSS más 6 bloques de 32 KiB (184 KiB) se quedan en la RAM y se comunican al firmware con `FW_PAGING_BLOCK_CMD`.
4. **Configuración**, con estas órdenes en este orden:
   - `TX_ANT`;
   - los resultados de calibración (`PHY_DB_CMD`, en el orden CFG, NCH, PAPD, TXP);
   - `PHY_CONFIGURATION` con los disparadores RUNTIME del firmware;
   - `BT_CONFIG` (convivencia con Bluetooth);
   - `DQA_ENABLE`.
5. **Estación auxiliar** para buscar: la cola 1 se asigna al FIFO 5 (`SCD_QUEUE_CFG`) y después se envía `ADD_STA` de tipo auxiliar.
6. **Región (LAR)**: se pregunta al firmware cuál es la actual (`MCC_UPDATE` con «ZZ» / GET_CURRENT).
7. **Configuración de la búsqueda** (`SCAN_CFG_CMD` v1):
   - 38 canales de 2,4 y 5 GHz;
   - antenas, tasas y MAC.
8. Queda **LISTA**. Un hilo vacía el anillo de recepción cada 250 ms, porque se trabaja sin interrupciones.

## Nueva orden: `wifi buscar`
Hace una búsqueda **pasiva**: escucha las balizas sin enviar sondeos. Usa `SCAN_REQ_UMAC` v7, con *adaptive dwell*, y espera `SCAN_COMPLETE_UMAC` hasta 15 s.

Analiza las balizas y las respuestas de sondeo que llegan en `REPLY_RX_MPDU`, junto con el canal y la señal de `REPLY_RX_PHY`. Para cada red muestra:
- **señal** (dBm), **canal** y **BSSID**;
- **seguridad**: abierta, WEP, WPA, WPA2 o WPA3;
- **nombre** de la red, o «(oculta)».

Las redes salen ordenadas por señal. `iwl_scan()` / `iwl_bss_list()` quedan listas para el panel de Configuración.

## Interruptores
- `soma.wifi_rt=off`: se para tras calibrar, como el 175B1B.
- `soma.wifi_calib=off`: se para en ALIVE.
- `soma.wifi=off`: sin WiFi.

## En el portátil
1. Escribe `wifi`. Lo esperado: `LISTA: firmware RUNTIME en marcha`, la región y la paginación.
2. Escribe `wifi buscar` y espera unos segundos a que salga la lista de redes.

Manda foto de las dos salidas.
