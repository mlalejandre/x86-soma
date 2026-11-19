# SOMA 146 — Integridad del DOM e insercion de HTML

El parche actua en la implementacion de C/DOM del navegador. NO depende de
Node.js ni introduce bibliotecas de Chromium en el kernel.

## Correcciones

1. `replaceChild(x, x)` conserva el hijo y no ejecuta una eliminacion espuria.
2. `wd_remove` marca al padre como `WNF_DIRTY` incluso si desaparece el ultimo
   hijo: layout y estilos pueden recalcularse tras `innerHTML = ''`,
   `textContent = ''` y otras eliminaciones de nodos.
3. Los elementos `<script>` creados mediante `innerHTML`, `outerHTML` y
   `insertAdjacentHTML` se marcan como inertes, incluso cuando se mueven luego
   a otro nodo. Esto aproxima el comportamiento de los navegadores modernos.
   Los scripts creados con `document.createElement('script')` siguen activos.

## Alcance y limites

- No implementa `MutationObserver`, colecciones DOM vivas ni la asincronia del
  event loop; tampoco mueve el navegador a Ring 3.
- Los atributos HTML de eventos como `onclick` no se deshabilitan: HTML
  no confiable sigue necesitando sanitizacion/capacidades y aislamiento.
- `wd_parse_fragment` no expone aun errores atomicos de parsing/OOM; cambios
  en su politica requieren pruebas aparte.
- Es necesaria una prueba adicional de navegacion en QEMU y la comprobacion
  visual de paginas complejas antes de dar por validada la integracion.

## Uso

```sh
python3 parche_146_dom.py --plan
python3 parche_146_dom.py --self-test
python3 parche_146_dom.py --apply
python3 parche_146_dom.py --test
# Probar en QEMU/una copia de SOMA; despues, si se desea:
python3 parche_146_dom.py --undo
```

`--test` compila temporalmente las fuentes reales del motor JS/DOM con
ASan+UBSan y ejecuta 13 verificaciones nativas. No modifica el build del
proyecto. Si no hay compilador compatible, la prueba falla de forma explicita.
