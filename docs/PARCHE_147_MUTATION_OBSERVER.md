# SOMA 147 — MutationObserver (primera fase)

Implementacion en JavaScript integrado (prelude) sobre el DOM nativo de SOMA.
No incluye ni vincula codigo de Chromium/Blink.

## Cobertura
- `new MutationObserver(callback)`, `observe`, `disconnect`, `takeRecords`.
- `childList`, `attributes`, `subtree`, `attributeFilter`, `attributeOldValue`.
- Agrupacion y entrega **asincrona mediante microtareas Promise**.
- Observacion de `appendChild`, `removeChild`, `insertBefore`, `replaceChild` y
  cambios mediante `setAttribute` / `removeAttribute`.
- Captura de `innerHTML` / `textContent` donde el motor nativo expone setters
  configurables; ver pruebas de integracion del navegador.
- Limites: 64 observadores activos, 64 nodos por observador, 256 registros por
  observador. Los registros nuevos se descartan al alcanzar el limite.

## Limitaciones importantes
- SUBCONJUNTO, no implementacion completa de la especificacion WHATWG.
- No detecta necesariamente las mutaciones ejecutadas directamente desde C,
  por los setters nativos de `id`, `className`, `style`, `dataset`, `nodeValue`,
  ni las realizadas fuera de estos metodos JS. La ruta nativa de `innerHTML`
  depende de los descriptores de propiedad del motor.
- Los movimientos entre distintos padres no generan todavia todos los
  registros de eliminacion exigidos por DOM Standard.
- `characterData` requiere validacion adicional para setters nativos; no se
  incluyen registros de `nodeValue` ni de `data`.
- Los registros tienen arrays convencionales, no NodeList de solo lectura.
- La cola de microtareas se procesa despues de ejecutar scripts/dispatch en
  SOMA; los detalles de orden frente a otros microtasks requieren evolucion.
- No aisla el navegador en Ring 3.
- La cabecera es generada: si regeneras `web_js_prelude.h`, incorpora los
  cambios tambien a tu generador o este parche se perdera.

## Uso
```
python3 parche_147_mutation_observer.py --plan
python3 parche_147_mutation_observer.py --self-test
python3 parche_147_mutation_observer.py --apply
python3 parche_147_mutation_observer.py --test
python3 parche_147_mutation_observer.py --undo
```

Pruebas nativas con ASan/UBSan en el host y adaptacion solo temporal de los
builtins setjmp/longjmp para Apple Clang. Probar adicionalmente QEMU y sitios
web reales antes de considerar validado el navegador completo.
