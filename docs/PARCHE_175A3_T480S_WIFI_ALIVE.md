# Parche 175A3 · T480s: WiFi, segundo intento de arranque del firmware y diagnóstico completo

Requiere el 175A2.

## Lo que se vio en el portátil (175A2)
- Tarjeta 8086:24FD (subsistema 0010, HW_REV 0x230). El firmware entró entero: **9 secciones, 661 KiB**, con
  `LOAD_STATUS 0xFFFFFFFF` (las dos CPU marcadas como cargadas).
- No llegó la notificación ALIVE en 3 s. `CSR_INT` vacío (ni errores ni recepción) y anillo RX sin usar.

## Qué cambia
- **Inicialización de transmisión**, como `iwl_pcie_tx_init` de Linux, que el 175A se saltaba creyendo que no hacía
  falta para ALIVE:
  - colas del programador desactivadas (`SCD_TXFACT`);
  - página *keep-warm*;
  - un anillo de 256 descriptores (32 KiB) para cada una de las 31 colas (`FH_MEM_CBBC`);
  - programador en modo automático con 31 colas (`SCD_GP_CTRL`).
  Es la única diferencia que quedaba con la secuencia de Linux antes de cargar el firmware.
- El puntero de escritura del anillo RX se vuelve a escribir con los registros sombra ya activos. Se espera ALIVE 5 s.
- **Diagnóstico como el de Linux** cuando no llega ALIVE (orden `wifi`):
  - estado del **arranque seguro** de cada CPU (`SB_CPU_1/2_STATUS`): si el firmware pasó la verificación;
  - `WFPM_GP2`: si el firmware llegó a ejecutarse (lo cambia al arrancar);
  - registros del anillo RX leídos de vuelta, estado de `rb_stts`, `CSR_RESET` y todo lo visto en `CSR_INT`.
- `wifi arrancar sinreset`: repite el arranque sin el reinicio por software, por si fuera el problema.

## En el portátil
`wifi`, y foto. Si sigue sin ALIVE, prueba también `wifi arrancar sinreset` y manda las dos fotos.
