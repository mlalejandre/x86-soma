# Parche 203: lo que le faltaba al navegador aislado

El parche 202 saco el motor web del nucleo a un proceso de ring 3. Quedaron cuatro cosas pendientes; este parche las cierra.
Solo x86_64. Requiere el 202.

## 1. Guardar imagen / Copiar imagen

Los pixeles de las imagenes viven en el proceso. Ahora, al elegir la opcion del menu, el nucleo pide la imagen por su direccion
(`W3C_GET_IMG`); el proceso contesta con sus medidas y con donde estan los pixeles dentro de su monton, y el nucleo los copia de
ahi a un buffer propio. Comprobaciones del nucleo: solo copia si el lo pidio con esa orden, el rango tiene que caer dentro del
monton prestado, como mucho 32 MiB, y las medidas declaradas tienen que cuadrar con los bytes copiados. Al abrir el menu solo se
pregunta si la imagen esta (sin pixeles); que hay bajo el puntero (enlace, imagen) se pregunta con `W3C_HIT`.

## 2. Rueda sobre cajas con desplazamiento propio

El proceso publica si la pagina tiene alguna caja `overflow: auto/scroll` con contenido que no cabe. Si la hay, la rueda se manda
al proceso (`W3C_INNER_SCROLL`): mueve la caja bajo el puntero o, si no hay caja ahi o ya llego al tope, devuelve el
desplazamiento a la pagina. Si no hay ninguna, la rueda mueve la pagina en el nucleo sin esperar a nadie, como antes.

De paso: los desplazamientos que pide el motor (AvPag, espacio, flechas) pasan a ser una suma acumulada. Antes el proceso dejaba
la ultima peticion escrita en el estado y podia volver a aplicarse con una publicacion posterior.

## 3. Cookies, localStorage y sessionStorage sobreviven a una caida

Cuando cambian (como mucho una vez por segundo) el proceso entrega al nucleo una copia (`W3P_SAVE`, hasta 5 MiB). El nucleo la
guarda sin interpretarla y se la da al proceso siguiente (`SYS_WEB_FS` con argumento 1). El proceso nuevo no se fia de ella: cada
longitud se comprueba y cada cadena se termina a la fuerza (la pudo escribir un proceso ya comprometido).

## 4. Desplazamiento sin repintar en cada paso

El fotograma se pinta el doble de alto que la vista (margen por arriba y por abajo, tres cuartos hacia donde se mueve la pagina)
y el nucleo lo muestra corrido mientras la vista quepa dentro. Solo se repinta cuando cambia el contenido o cuando la vista se
acerca a menos de 1/8 de su alto del borde de lo pintado.

- Con elementos `position: fixed` o `sticky` no se puede (correr el fotograma los moveria): se pinta la vista exacta en cada paso,
  y el nucleo ya no muestra corrido el fotograma viejo mientras llega el nuevo.
- Con un campo de texto activo (el cursor obliga a repintar dos veces por segundo) el margen solo se usa mientras se desplaza.
- El buffer del fotograma en el nucleo pasa a ser el doble de grande (unos 16 MiB con una vista de 1920x1000).

Dos gastos del 202 que salieron al medir:
- Cada orden terminada (cada movimiento del raton) repintaba la pagina entera. Ahora solo publica el estado.
- Con dos vistas vivas de distinto tamano (la ventana y la vista heredada que abre la orden `navegador`), la pagina se
  remaquetaba dos veces cada 1,5 s. Ahora la vista menor solo se acepta cuando la mayor lleva 400 ms sin pedirse.

`navegador-aislado` muestra ahora los fotogramas pintados, lo que tardo el ultimo y la media, y las copias de cookies guardadas.

## Probado (QEMU)

- Rueda, 30 muescas (2400 px) en una pagina de 139 parrafos: 8 fotogramas en vez de uno por paso (unos 190).
- En reposo: 6 fotogramas en total tras cargar la pagina; antes del arreglo de las dos vistas, unos 3 por segundo.
- Rueda sobre una caja `overflow: auto`: se mueve la caja; fuera de ella, la pagina.
- Copiar imagen: los pixeles que llegan al nucleo tienen la misma huella que el PNG original. Guardar imagen abre su dialogo.
- `panic web 1` y volver a cargar: la pagina recibe la cookie, el sessionStorage y el localStorage de antes de la caida.
- Pagina con cabecera fija, AvPag y espacio, enlace a un ancla, escribir en un campo y desplazar despues.
- `usfuzz`, `conctest`, `oomtest`, `webr3`, `panic web 2..4`; `make fuzz-http` (ahora tambien importa tarros de cookies corruptos).
