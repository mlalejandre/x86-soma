# Parche 332 — Zona horaria: el reloj ya no va una hora atrasado

## El fallo
El reloj del equipo (RTC) guarda la hora en UTC y el reloj del escritorio la pintaba tal cual: en España, en invierno, una hora
menos (dos en verano). Cada parte del sistema lo arreglaba a su manera: `date` sumaba 2 horas todo el año y ponía «CEST»,
el agente también sumaba 2 (y podía dar el día 32 al pasar la medianoche), la agenda usaba las reglas de Madrid y el
programador, Python y los archivos del pendrive recibían UTC.

## Qué cambia
- **Una sola hora del sistema** (`src/core/hora.c`): todo lo que se enseña a una persona sale en la zona elegida y con su
  horario de verano: reloj del escritorio y de la barra, `date`, la fecha y hora que ve el agente, la agenda (también las
  horas en UTC de los `.ics`), `sys.hour()` del programador, `datetime.now()` de Python, las fechas de los archivos que se
  escriben en el pendrive (FAT/exFAT guardan la hora local) y el informe de arranque.
- **Ajustes > Fecha y hora**: la hora en grande, la zona con su desfase y si es horario de verano o de invierno; elección
  de zona por regiones (Europa y África, América, Asia y Oceanía) y qué guarda el reloj del equipo.
- **Orden `hora`**: `hora` (local y UTC), `hora zona <ciudad, país, nombre IANA o UTC+2>`, `hora zonas`, `hora reloj utc|local`.
  `ajustes hora` abre la página. Se guarda en `/etc/hora.conf`. Por defecto, España peninsular.
- **Equipos con Windows**: Windows guarda la hora local en el reloj del equipo. Con `hora reloj local` SOMA la interpreta
  así; `rtc_get_datetime()` sigue dando UTC a todo lo demás (certificados TLS, memoria, almacén).

## Zonas
55 zonas (toda España, Europa, América, Asia y Oceanía) con las reglas vigentes: UE, EE. UU./Canadá, Cuba, Chile, Australia
y Nueva Zelanda; los países que ya no cambian la hora (México, Brasil, Paraguay, Turquía, Rusia...) van sin regla.
Marruecos no descuenta la hora del Ramadán. También vale un desfase fijo (`UTC+3`, `-04:30`).

## Pruebas
- `test_zona_332` (en `make test`): calendario, cambios de hora de 2025 y 2026 **exactos al minuto** para cada regla
  (comprobados contra tzdata de Python), búsqueda por ciudad/país/IANA/con tildes, desfases fijos y 20000 entradas raras.
- `python3 tests/prueba_hora_332.py` (QEMU con el reloj fijado): zona por defecto en invierno y en verano, cambio por
  ciudad, IANA, tildes y desfase, hemisferio sur, reloj en hora local, `/etc/hora.conf` y que se conserva al reiniciar.
- Necesita `make`.
