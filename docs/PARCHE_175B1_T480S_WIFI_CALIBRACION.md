# Parche 175B1 · T480s: WiFi, primeras órdenes al firmware y calibración de la radio

Requiere el 175A3.

## Lo que se vio en el portátil (175A3)
- **FIRMWARE VIVO**: ALIVE 0xCAFE, LMAC 36.2668244328, UMAC 25.0.
- 9 secciones (661 KiB) cargadas en 8 ms; arranque total en 59 ms.
- Tabla de errores en 0x00813A20 y programador (SCD) en 0x00817080.

## Qué cambia
Antes, el driver reiniciaba la tarjeta nada más llegar ALIVE. Ahora sigue los pasos de Linux (`iwl_run_init_mvm_ucode`) con la imagen INIT:

1. **Programador y cola de órdenes**, como `iwl_pcie_tx_start` + `iwl_trans_pcie_txq_enable`:
   - se borra el contexto del SCD en la SRAM;
   - se configura la tabla de recuento de bytes;
   - la cola 0 queda activa en el FIFO 7 (órdenes);
   - se activan los FIFO y los 8 canales DMA de transmisión, con reintento automático.
2. **Envío de órdenes**:
   - descriptor TFD de 128 bytes, con los primeros 20 bytes en TB0 y el resto en TB1;
   - cabecera corta o larga según el grupo;
   - el número de secuencia empareja cada orden con su respuesta, que llega por el anillo RX.
3. **Dirección MAC** leída de los registros WFMP de la tarjeta, como hace Linux en la familia 8000.
4. **Calibración**:
   - `TX_ANT_CONFIGURATION_CMD` con las antenas válidas (de `PHY_SKU`);
   - `PHY_CONFIGURATION_CMD` con los disparadores de calibración INIT, que salen del propio firmware (TLV 22, `DEF_CALIB`);
   - se espera `INIT_COMPLETE_NOTIF` (2 s);
   - los resultados `CALIB_RES_NOTIF_PHY_DB` se guardan para enviarlos a la imagen RUNTIME en la fase B2.
5. Si algo falla después de ALIVE, la orden `wifi` muestra:
   - las órdenes enviadas y cuáles quedaron sin respuesta;
   - la tabla de errores del firmware (válida, código, PC, dato).

La paginación de la CPU2 (184 KiB servidos desde la RAM) **solo la usa la imagen RUNTIME**: el código está listo para la fase B2, pero con INIT no se envía.

Interruptores: `soma.wifi_calib=off` se queda en ALIVE, como el 175A3; `soma.wifi=off` desactiva la WiFi entera.

## En el portátil
Escribe `wifi` y saca una foto. Lo esperado es una línea como esta:

`FIRMWARE VIVO Y RADIO CALIBRADA … CALIBRADA: N resultados de calibracion guardados | MAC xx:xx:…`

Comprueba que la MAC coincide con la de la pegatina o la BIOS.
