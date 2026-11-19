# SOMA 144 — compatibilidad CSS Selectors Level 4

## Objetivo

Mejorar la representacion de paginas modernas sin reemplazar el motor web,
sin introducir bibliotecas de terceros y sin modificar la frontera Ring 3.

## Cambios

- `:is(a, b)`, `:where(a, b)`, `:not(a, b)` y alias `:matches(a, b)`
  aceptan **listas de hasta 8 selectores compuestos simples** (clases, ids,
  etiquetas, atributos y pseudoclases basicas).
- Se permite combinarlas con un compuesto externo: `div.card:is(.active, #main)`.
- La especificidad de `:is()` y `:not()` es la del argumento mas especifico;
  la de `:where()` es cero.
- Se rechazan de forma segura argumentos vacios o expresiones de selector
  complejo en un brazo (`div > span`) o anidadas: **no estan implementadas**.
- No cambia el formato de CSS ni las API publicas del navegador.

## Validacion

```sh
python3 parche_144_webcss.py --plan
python3 parche_144_webcss.py --self-test
python3 parche_144_webcss.py --apply
python3 parche_144_webcss.py --test
```

El modo --test compila y ejecuta pruebas de selectores/cascada en host.
Por seguridad, no compila ni arranca QEMU automaticamente.
En el sistema real, comparar tambien la huella visual `sbx_webref` del parche
142 y verificar manualmente paginas existentes.

## Reversion

`python3 parche_144_webcss.py --undo`

Rechaza la reversion si se alteraron archivos instalados o el manifiesto
es invalido. Conserva una copia exacta de los bytes originales.

**Limites:** Esto NO instala Blink, V8, Skia ni garantiza compatibilidad
con Chromium. Aun faltan aislamiento del HTML/CSS/JS, DOM y APIs web,
HTTP/TLS propios y muchas funciones de renderizado.
