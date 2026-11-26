# Parche 338 — La aplicación Agentes, ahora un panel vivo

## Antes
Tres tarjetas fijas («Mayordomo», «Buscador», «Maquetador») dibujadas con la letra de mapa de bits, sin datos ni nada que
pulsar. No decía qué hacía SOMA ni cómo le había ido.

## Ahora
- **Ahora**: la misión en curso, cuánto lleva y la cadena de especialistas por los que ha pasado (el de ahora resaltado), con
  **Detener**. En reposo: la última misión, cómo acabó (bien, con error o detenida), cuánto tardó y el principio de la respuesta.
- **Especialistas** (8): Órdenes al instante (LCM), Mayordomo, Investigador, Programador, Maquetador, Agente con herramientas,
  Modelo de lenguaje y Voz. Qué hace cada uno, si está trabajando (late), cuántas veces ha trabajado en esta sesión y hace
  cuánto; **Probar** le manda una petición de ejemplo (la respuesta sale en el orbe).
- **Misiones recientes** (24): hora, cómo acabó (color), duración, quién trabajó (puntos de color) y la respuesta.
- **Actividad**: las órdenes, búsquedas y lecturas de los agentes con su hora.
- Rueda del ratón para bajar; Esc cierra. Orden `agentes`: lo mismo en texto.

Los datos los recoge `src/agent/agentes_reg.c` desde lo que ya existía: las fases del orbe (`gui_fase`), las órdenes que
ejecutan los agentes (`gui_fase_orden`) y el principio y el final de cada misión en el hilo del agente.

## Pruebas
- `test_agentes_338` (en `make test`): especialistas de cada fase y orden, una misión completa con sus pasos y usos, fin bien /
  con error / detenida / sin cerrar, actividad fuera de misión, anillos llenos, pasos de más, UTF-8 cortado y duraciones.
- `python3 tests/prueba_agentes_338.py` (QEMU): la orden `agentes` antes y después de ejecutar órdenes y misiones sin modelo.
- Necesita `make`.
