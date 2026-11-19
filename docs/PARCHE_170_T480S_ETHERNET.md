# Parche 170 · T480s: Ethernet por cable

Requiere el 172. Ficha 170 del plan.

## Qué cambia
- **Controlador `e1000e.c`** para la familia Intel e1000/e1000e, por sondeo y como interfaz `eth0` de netif:
  - el **I219-LM / I219-V** del T480s (la MAC está en el chipset y el PHY va por MDIO);
  - para las pruebas, los 82574L (`-device e1000e`) y 82540EM (`-device e1000`) de QEMU, que usan los mismos descriptores.
  - Anillos de 32 descriptores con buffers de 2 KiB; la MAC sale de la NVM.
- **Particularidades del I219**, como en el e1000e de Linux:
  - **ULP**: modo de ultra bajo consumo en el que Windows o Linux dejan el PHY al apagar sin cable, y en el que no
    responde. Con el firmware de gestión (ME) presente, se le pide que lo quite (H2ME/FWSM). Sin ME, se reinicia la
    alimentación del PHY (LANPHYPC), se quita el modo SMBus y se limpia `ULP_CONFIG1`.
  - **Semáforo con el firmware** (`EXTCNF_CTRL.SWFLAG`) para tocar el PHY: el ME también lo usa.
  - Ajustes de envío de Sunrise Point (`IOSFPC`, `TARC0`).
- **Gestor de red `netmgr`**, que mira cada segundo:
  - usa la primera interfaz real con enlace, por orden de preferencia: cable y, con el 171, móvil por USB;
  - al aparecer el enlace pide dirección por **DHCP**, y si falla lo reintenta cada 10 s;
  - avisa al perder el enlace y vuelve a pedir dirección al recuperarlo;
  - en QEMU con virtio (IP fija) no hace nada.
- **Orden `red`**: estado, interfaz, IP, puerta y DNS. De la tarjeta muestra el enlace con velocidad y dúplex, los
  contadores, el modo ULP, el PHY (identificador, BMCR, BMSR) y los registros CTRL, STATUS y FWSM.
- Interruptores: `soma.ethernet=off` (no toca la tarjeta) y `soma.redauto=off` (sin gestor: a mano con `netif usar` y
  `dhcp`).

## Pruebas (QEMU)
- Con `e1000e` (82574L) y con `e1000` (82540EM), sin virtio:
  - DHCP automático, ping a la puerta de enlace y respuesta del DNS;
  - descarga por TCP de un archivo de 400 KB servido desde el anfitrión, comprobada contra el original.
- Cable desconectado y vuelto a conectar (`set_link`): avisa de la pérdida, vuelve el enlace y DHCP otra vez.
- Sin tarjeta Intel (virtio): todo como antes.

## En el portátil
1. Enchufa el cable al router y arranca. Deben salir `RED: eth0: enlace arriba` y `dhcp: IP 192.168...`.
2. `red`: debe decir `enlace 1000 Mbit/s` (o 100) y `conectado`.
3. `ping 8.8.8.8` y abre una web en el navegador.
4. Si no hay enlace, mándame la salida de `red`: el estado del PHY y del ULP dirá qué falta.
