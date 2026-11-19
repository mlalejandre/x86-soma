# Parche 178 — T480s: webcam UVC, foto y grabacion de video

Requiere 177D. Interruptor: `soma.cam=off` (tambien el modo seguro).

## Que anade
- Driver UVC (Chicony 04F2:B604 u otra camara USB 2.0 HS) sobre xHCI con transferencias isocronas.
- Motor `camera.c`: vista previa (MJPEG decodificado o YUY2), foto JPEG/PNG y video AVI (MJPG + audio PCM 16 kHz del microfono).
- Aplicacion CAMARA (dock) y comandos `cam` / `camara`.
- Archivos: `/fotos/foto_NNN.jpg`, `/videos/vid_NNN.avi`.

## Comandos
`cam info`, `cam estado`, `cam iniciar [WxH] [fps]`, `cam parar`, `cam foto [ruta]`, `cam video [seg]`, `cam abrir`.
Teclas en la app: espacio = foto, V = video, C = encender/apagar.

## Limites
Video en RAM (max 48 MiB / 20000 tramas) y se escribe al parar. Solo camaras HS. Sin camara real en QEMU: el camino de hardware solo se prueba en el portatil.

## Diagnostico
`cam info` y `cam estado` muestran bytes del probe, TD encolados/completados, histograma de codigos de finalizacion y errores.
