# Parche 339 · Listo para publicar en GitHub (parches 329 a 338)

Solo documentación, exportadores, `.gitignore` y CI: no cambia el núcleo ni las aplicaciones, así que **no hace falta `make`**.
Requiere el 338.

## README

* La introducción dice qué se ejecuta aislado en ring 3.
* La sección de red pasa a llamarse **«Red: el núcleo no habla con Internet»**, con la tabla de servicios de ring 3: `netsvc`,
  `tlstun`, `leesvc`, `mimesvc`, `imgdec`, `pdfsvc`, `websvc` y `zdec`. Antes decía «Servicios Web en Ring 0», que ya no era
  cierto desde el 311.
* Secciones nuevas:
  * **IA local**: el motor de IA del 329, la orden `ia` y la partición MODELOS.
  * **Noticias**, **Discos** y **Fecha y hora**.
* Las aplicaciones Noticias, Agentes y Discos, la página Fecha y hora y el panel Salida del Studio, en la lista de aplicaciones.
* `investiga` por temas y el panel Agentes, en la lista de agentes.
* `src/user/` en la estructura del proyecto.
* Qué hace falta para compilar sin Docker (`g++` con su `libstdc++` estática, para el motor de IA).

## CHANGELOG

* La introducción de la pre-beta ya no dice que todo corre en ring 0.
* En la entrada del 329, el arreglo del enlace en Debian.
* La entrada de este parche. Como siempre, de ahí saca `subir_github.py` las notas de la release.

## Exportadores: nunca un `.conf`

`exportar_txt.py` trataba `.conf` como texto y solo se saltaba una lista de nombres conocidos (`llm.conf`, `correo.conf`,
`wifi.conf`...). Un `.conf` con otro nombre se habría exportado entero. Ahora no se exporta ninguno, ni los `.env*`.
`tools/exporta_para_claude.py` ya los excluía todos.

Temas nuevos:

| Exportador | Temas |
|---|---|
| `exportar_txt.py` | `servicios`, `iallocal`, `noticias`, `agentes`, `discos`, `hora`; archivos nuevos en `investiga`, `lenguaje` y `youtube` |
| `tools/exporta_para_claude.py` | `noticias`, `agentes`, `investiga`, `servicios`, `youtube`; `coder_run` en `programador` y `chat_md` en `orbe` |

## `.gitignore`

No se sube ningún `*.conf` ni `.env*`. Ningún archivo del repositorio queda oculto por estas reglas
(`git ls-files -ci --exclude-standard` sale vacío).

## CI: compilar en el contenedor del Dockerfile

`ejecutar.py` compila dentro de `soma-toolchain`, que es Debian bookworm (gcc 12, glibc 2.36). La CI de GitHub compilaba en
Ubuntu (gcc 13, glibc 2.39), donde la libstdc++ está hecha con `_FORTIFY_SOURCE` y glibc redirige `sscanf` a `__isoc23_sscanf`.

El motor de IA del 329 enlazaba en Ubuntu y no en Debian (faltaban `mbsrtowcs` y `__isoc99_sscanf`), y la CI no podía verlo.
Ahora hay un trabajo `docker-build` que construye la imagen del `Dockerfile` y compila x86_64 con `-Werror` igual que
`ejecutar.py`. El trabajo de Ubuntu instala además `g++` de forma explícita.

## Antes de subir

```
python3 subir_github.py --comprobar
```

Entre los parches nuevos tienen que aparecer del 329 al 339. La imagen de `imagen/` es de antes de estos parches: si quieres que la opción rápida
del README lleve esta versión, regenérala antes con `python3 construyeimagen.py --carpeta`.
