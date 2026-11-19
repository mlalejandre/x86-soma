# Parche 164 (+165) · T480s: ACPI de verdad — apagar, batería, cargador, tapa y botón de encendido

Requiere el 162. Junta las fichas 164 (intérprete ACPI) y 165 (batería, tapa, botón y apagado) del plan.

## Qué cambia
- **uACPI** (`third_party/uacpi`, licencia MIT, sin modificar): intérprete AML pensado para núcleos propios. Lee la DSDT y
  las SSDT del firmware, ejecuta `_INI`/`_STA` y responde a `_OSI` como Windows (igual que Linux).
  Unos 190 KB de código y compila sin un solo aviso. La imagen pasa a 12,8 MiB (el límite es 14).
- **`src/arch/x86_64/acpi_sys.c`**: la capa de adaptación, con:
  - memoria del firmware con escritura: RAM con caché y MMIO/NVS sin caché;
  - puertos, PCI (mecanismo 1), temporizador TSC, semáforos, cerrojos y trabajo diferido;
  - **controlador embebido (EC)** de ThinkPad por los puertos de la tabla ECDT (0x62/0x66). Sin él, la batería y la tapa
    no pueden leer nada. Cada byte espera como mucho 20 ms: un EC que no responda ralentiza, pero nunca cuelga;
  - **eventos (SCI) por sondeo**: un hilo llama al manejador de uACPI cada 100 ms. No se enruta la interrupción por nivel
    del SCI, porque una tormenta de interrupciones colgaría el sistema si algo fallase. Para la tapa, el botón y la batería
    sobra.
- **Apagado de verdad**: `poweroff` (y `shutdown`) vacían el disco, vacían la caché del pendrive y entran en **S5** por
  ACPI: el portátil se apaga. Antes solo funcionaba en QEMU.
- **Reinicio**: `reboot` usa el registro RESET de la FADT y, si falla, el 8042 de siempre.
- **Botón de encendido**: pulsarlo apaga SOMA limpiamente, igual que `poweroff`. Funciona como evento fijo y como
  dispositivo `PNP0C0C`.
- **Orden `bateria`**:
  - carga total y por batería (el T480s tiene dos: interna y extraíble);
  - cargando o descargando, consumo en mW, tiempo restante o hasta carga completa;
  - capacidad actual frente a la de fábrica (**salud**), ciclos, modelo y fabricante;
  - cargador enchufado o no, tapa abierta o cerrada.
- **Indicador en la barra del escritorio** junto al reloj: «Bateria 87% cargando». En rojo con el 10 % o menos y
  descargando. Lo renueva un hilo cada 10 s, así que la interfaz nunca espera al EC.
- **Aviso de batería baja** (10 %) en la consola.
- **Orden `acpi`**: estado del intérprete, del EC y de los eventos, y los dispositivos conocidos. `acpi todos` lista
  todos los que tienen `_HID`.

## Interruptores
- `soma.acpi=off` (o el modo seguro): como antes, sin intérprete.

## Pruebas realizadas
- **QEMU q35 (UEFI) y PC clásico (BIOS)**:
  - el intérprete arranca en 30–100 ms sin avisos ni errores;
  - aparecen los 30 dispositivos de la DSDT de QEMU.
- **`poweroff`** apaga QEMU por S5 en los dos modelos; **`reboot`** reinicia por la FADT.
- **Botón de encendido** (`system_powerdown` de QEMU): SOMA guarda los discos y se apaga solo en 2 s.
- **Batería, cargador y tapa**: QEMU no tiene, así que se probaron con una SSDT propia compilada con `iasl`. Los valores
  de la batería simulada son:
  - `_BIX` (57/50 Wh, 123 ciclos) y `_BST` que se descarga en cada lectura;
  - resultado: 78 %, 4 h 06 min, salud 87 %, «SANYO 01AV478», cargador desenchufado, tapa abierta.
- Todo junto: pendrive con SOMADATA + 8 núcleos + ACPI, sin regresiones. Raspberry Pi 5: compila.
- **No probado en emulación: el EC**, porque QEMU no tiene uno. El protocolo es el estándar (`RD_EC` 0x80 / `WR_EC`
  0x81, esperando IBF/OBF) y es lo que se verá en el portátil.

## En el portátil
1. En la pantalla de arranque: `ACPI: interprete activo en N ms | EC por la ECDT | SCI 9 (sondeo)`.
2. `bateria`: compara el porcentaje con el de Debian/Windows (±2 %). Prueba también a desenchufar y enchufar el
   cargador.
3. `acpi`: debe listar 2 baterías, el cargador, la tapa, el botón, el EC y «ThinkPad ACPI».
4. `poweroff`: el portátil se apaga del todo. Después, enciéndelo y pulsa el botón de encendido con SOMA en marcha:
   también debe apagarse.
5. Si el arranque se queda parado en «ACPI»: entrada «modo seguro» de GRUB, y foto.

## Revertir
`python3 parche_164_t480s_acpi_energia.py --undo` y `make clean`.
