# PARCHE 270 · Menú de GRUB: T480s y PC genérico, desde USB o desde RAM

`grub/grub-t480s.cfg` (el menú que lleva `BOOTX64.EFI`) queda así. El menú espera 5 s y arranca la primera entrada.

| # | Entrada | Opciones del núcleo |
|---|---|---|
| 1 (por defecto) | ThinkPad T480s (desde USB) | `soma.maquina=t480s` |
| 2 | ThinkPad T480s (desde RAM) | `soma.maquina=t480s soma.ram=1` |
| 3 | PC x86 genérico (desde USB) | ninguna |
| 4 | PC x86 genérico (desde RAM) | `soma.ram=1` |

«Desde RAM» es el modo del parche 269 (fuentes en memoria; los archivos de SOMA se guardan al apagar o con `disco sync`).

Las variantes de siempre (resolución segura 1024x768, resolución nativa, un solo núcleo, modo seguro, teclado US) están en el
submenú **Opciones avanzadas**, junto con una nueva: PC genérico con teclado español (`soma.teclado=es`).

Nota: el PC genérico no lleva `soma.maquina=t480s`, así que arranca con teclado US (como antes). Si tu PC es español, usa la
entrada del submenú o pulsa `e` y añade ` soma.teclado=es`.

Para volver al menú anterior: `python3 parche_270_grub.py --revertir`.
