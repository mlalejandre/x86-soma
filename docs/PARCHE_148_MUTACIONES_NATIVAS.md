# SOMA 148 — Mutaciones de atributos desde el puente DOM C

## Alcance
Extiende `MutationObserver` del parche 147 a operaciones que pasan por el
puente nativo C, no por `Element.setAttribute()` de JavaScript:
- Propiedades reflejadas: `id`, `className`, `href` y atributos de `HTMLElement`
  (incluidos booleanos); `hidden`, `tabIndex`, `contentEditable`, `type`.
- `dataset.prop = ...` y `delete dataset.prop`.
- `classList.add/remove/toggle/replace/value` (los cambios simples sin efecto no notifican).
- `style.color = ...`, `style.setProperty`, `removeProperty`, `cssText`.
- `nodeValue` y `data` de nodos de texto/comentario (`characterData`).

Las operaciones invocan un puente JS que encola registros sobre la implementacion
existente de `MutationObserver`; el callback se ejecuta en microtarea, no en el
momento de la escritura. Se respetan filtros y oldValue definidos en el 147.

## No cubre
- Cambios realizados fuera del contexto del interprete JavaScript desde otras
  partes del kernel, el parser HTML o los decodificadores. Es necesario un diseño
  de cola nativa separado para instrumentarlos sin notificar durante el parseo.
- `document.title`, `outerHTML`, cambios de hijos hechos directamente desde C.
- Compatibilidad completa WHATWG ni navegacion al nivel de Chromium.
- Aislamiento del motor web en Ring 3.

## Procedimiento
```
python3 parche_148_dom_observadores.py --plan
python3 parche_148_dom_observadores.py --self-test
python3 parche_148_dom_observadores.py --apply
python3 parche_148_dom_observadores.py --test
python3 parche_148_dom_observadores.py --undo
```

El parche modifica solo `web_js_dom.c`, `web_js_prelude.h` y crea prueba y
este documento. No toca Makefile, kernel, imagenes de disco ni parches antiguos.
No se recomienda regenerar `web_js_prelude.h` sin trasladar antes los cambios
al generador correspondiente.
