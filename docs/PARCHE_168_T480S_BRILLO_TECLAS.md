# Parche 168 · T480s: brillo de la pantalla y teclas Fn del ThinkPad

Requiere el 167. Ficha 168 del plan.

## Qué cambia
- **Brillo del panel**: el T480s ilumina la pantalla con la PWM del chipset Intel, que vive en la gráfica (00:02.0, registros
  `BLC_PWM_PCH_CTL1/CTL2`, 0xC8250/0xC8254). El firmware la deja encendida; SOMA solo cambia el ciclo de trabajo, como
  Linux (`lpt_set_backlight`).
  - Curva cuadrática (el ojo no percibe el brillo de forma lineal), 16 pasos para las teclas.
  - Mínimo del 2 %: desde el teclado o la orden nunca se deja la pantalla negra.
  - Antes de tocar nada se comprueba que los registros tienen sentido: si la PWM está apagada, se controla desde la CPU o
    la gráfica no responde, SOMA no escribe y la orden `brillo` dice por qué.
  - El último brillo se guarda en `/etc/brillo.conf` (2 s después de la última pulsación) y se recupera al arrancar.
- **Teclas Fn**:
  - **Volumen** (Fn+F1 silencio, F2 bajar, F3 subir): el teclado manda los códigos E0 20/2E/30. El silencio recuerda el
    volumen anterior. Aviso en pantalla con una barra.
  - **Brillo** (Fn+F5/F6): el EC levanta `SCI_EVT`. SOMA hace la consulta `QR_EC` (0x84) y ejecuta el método `_Qxx` del
    firmware, que avisa al panel (`Notify 0x86/0x87`) y al dispositivo **HKEY** del ThinkPad (eventos 0x1010/0x1011).
    Las dos vías son la misma pulsación: si llegan con menos de 60 ms se cuenta una vez.
  - **Micrófono** (Fn+F4, HKEY 0x101B): aviso (SOMA aún no graba sonido).
  - El HKEY se activa como `thinkpad_acpi` de Linux (`MHKV`, `MHKA`, `MHKM`, `MHKC 1`); al panel se le pide `_DOS(4)`, para
    que el firmware no cambie el brillo por su cuenta.
- **Tapa**: al cerrarla se apaga la luz del panel; al abrirla vuelve como estaba (antes solo se contaba el aviso).
- **Consultas del EC**: además de las teclas, ahora se atienden todos los avisos del EC (batería, temperatura...), que
  antes se quedaban pendientes.
- **Orden `brillo`**: porcentaje, ciclo y periodo de la PWM, registros, estado del EC/HKEY/video, volumen y los últimos
  eventos (`EC _Qxx`, `HKEY 0x10xx`, `video 0x86`). `brillo 0-100`, `brillo +`, `brillo -`.

## Interruptores
- `soma.brillo=off`: no toca la PWM (las teclas de volumen siguen).
- `soma.hkey=off`: no activa el HKEY del ThinkPad.
- `soma.tapa=off`: cerrar la tapa no apaga la pantalla.
- El modo seguro desactiva los tres.

## Pruebas
- `tests/test_brillo_168.c` (anfitrión): curva, mínimo, inverso, validación de registros y pasos de las teclas.
- En QEMU con `tests/acpi_168_teclas.asl` (un panel con `_BCL/_BCM`, un HKEY `LEN0268` y una tapa; se compila con `iasl`
  y se carga con `-acpitable`): `_DOS(4)` aplicado, HKEY activo (versión 0x200), una pulsación de brillo que llega por las
  dos vías cuenta una vez, silencio del micrófono y aviso de la tapa.
- Teclas de volumen en QEMU con `sendkey volumedown/audiomute/volumeup`: 100 → 85 → 0 → 85 → 90.
- QEMU no tiene gráfica Intel ni EC: el brillo y las consultas del EC solo se prueban en el T480s.

## En el portátil
1. `brillo`: debe decir `PWM del chipset: ciclo N de M` y `HKEY activo`.
2. Fn+F5/F6 (brillo) y Fn+F1/F2/F3 (volumen): sale el aviso con la barra.
3. Cierra la tapa unos segundos y ábrela: la pantalla vuelve.
4. Si el brillo no cambia, mándame la salida de `brillo` después de pulsar Fn+F6 (lista de últimos eventos y registros).
