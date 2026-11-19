# Parche 171B · T480s: el pendrive como disco aunque haya lector de tarjetas, y arranque sin errores de ACPI

Requiere el 171. Corrige dos fallos vistos en el portátil.

## 1. «DISCO USB: no hay disco USB»
El T480s tiene un **lector de tarjetas interno** conectado por USB (Realtek 0BDA:0316) que se presenta como
almacenamiento masivo. SOMA usaba el **primer** dispositivo de almacenamiento que encontraba. Si el lector estaba antes
que el pendrive (según el puerto donde se enchufa), lo elegía; vacío, fallaba, y el pendrive no se probaba. Los
archivos se guardaban entonces solo en memoria.

Ahora se prueban **todos** por orden hasta que uno funciona, y la consola explica cada uno:
```
USB: almacenamiento 0BDA:0316: sin medio o no responde (lector de tarjetas vacio?)
USB: almacenamiento 0781:5583: en uso como disco
```
Probado en QEMU con un dispositivo de almacenamiento inservible (un CD USB vacío) en el puerto 1 y el pendrive en el 2.

## 2. 93 «ACPI error» al arrancar
Al activar las teclas del ThinkPad, el 168 llamaba al método `MHKM` del firmware una vez por tecla. En el T480s ese
método hace `Decrement(Arg0)` y el intérprete lo rechaza, con 3 líneas de error por llamada. Ya no se llama: la máscara
que deja el firmware incluye las teclas Fn que se usan (brillo, volumen y micrófono funcionan en el portátil).
