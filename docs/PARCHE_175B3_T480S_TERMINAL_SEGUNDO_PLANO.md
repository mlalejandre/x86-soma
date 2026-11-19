# Parche 175B3 · T480s: las órdenes lentas de la WiFi ya no congelan el escritorio

Requiere el 175B2.

## Lo que se vio en el portátil (175B2)
- **La búsqueda funciona**: 24 redes en 2,4 y 5 GHz, con nombre, canal, seguridad y BSSID.
- La región que da el firmware es «00» (mundial), con 51 canales.
- Problema: durante los segundos que dura `wifi buscar`, el ordenador dejaba de responder.

## Causa
El terminal ejecuta cada orden **en el hilo del escritorio** y espera a que termine. Mientras la tarjeta escuchaba los 38 canales, no se dibujaba nada ni se atendían el ratón ni el teclado.

## Qué cambia
- `wifi buscar` y `wifi arrancar` se ejecutan en un **hilo aparte**:
  - el terminal avisa de que la orden está en marcha y el sistema sigue respondiendo;
  - la salida se captura en ese hilo y aparece en el terminal al terminar.
- Solo una orden lenta a la vez. Si escribes otra mientras tanto, el terminal pide esperar.
- **Señal honesta**: cada medida de la radio (`REPLY_RX_PHY`) se usa para una sola trama.
  - Antes, una trama sin medida propia heredaba la de la anterior; por eso muchas redes salían con -79 dBm.
  - Ahora, una red sin medida sale con «?».
  - La cabecera muestra cuántas medidas llegaron.

## En el portátil
Escribe `wifi buscar` y mueve el ratón o abre una ventana mientras busca. El resultado sale en el terminal unos segundos después.
