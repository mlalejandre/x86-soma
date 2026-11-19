# Parche 190: navegador fluido en maquinas lentas y busquedas de Google

Requiere el 189 (y por tanto 188B).

## Lo que se veia

1. Tras buscar en Google el navegador se quedaba en "Ejecutando scripts..." con la pagina de "trafico inusual" y dejaba de responder.
2. Mientras el navegador cargaba, todo el sistema iba lento y el vigilante avisaba: `Ningun hilo cedio la CPU en 5 s`, con la traza
   en el dibujo del escritorio (`cmd_vsoma` > ... > `ui_rr_stroke` / `fb_present`), no en el motor JavaScript.

## Causas

- **Google /sorry.** Google responde a las peticiones automaticas (cabecera de navegador no reconocido, IP con muchas peticiones) con HTTP 429 y una
  pagina que exige un reCAPTCHA. Este navegador no puede resolverlo, asi que la busqueda no tenia salida.
- **El escritorio no cedia la CPU.** Con un solo nucleo los hilos cooperan: el hilo del escritorio solo dormia cuando el fotograma anterior habia
  sido mas rapido que su plazo. Con fotogramas caros (maquina lenta, pagina grande) encadenaba un fotograma tras otro y el hilo que carga la
  pagina (red, scripts) casi no corria. La animacion de carga hacia que la ventana se repintase sin parar.
- **Esquinas redondeadas O(r^2).** `ui_rr_stroke` y el relleno recorrian los r*r pixeles de cada esquina, con una raiz cuadrada por pixel; una caja
  grande con `border-radius` enorme (las "pastillas" de las paginas modernas) costaba segundos en una maquina lenta.

## Cambios

- `src/apps/web_util.h` (`wu_google_sorry_alt`) y `web_page.c`: si la pagina final es `google.*/sorry/...` (o una busqueda que contesta 429), la
  busqueda se repite en DuckDuckGo (version HTML, sin JavaScript) y se anota en `about:log`. `soma.websorry=off` lo desactiva.
- `src/ui/gui.c`, `src/apps/browser.c` (`browser_loading`): tras cada fotograma el escritorio duerme tanto como tardo (el triple mientras la pagina
  carga sola y el usuario no la toca) y la animacion de carga se limita a 10 fotogramas por segundo. El bucle siempre cede la CPU.
- `src/drivers/fb.c` (`ui_corner_first`): busqueda binaria del primer pixel con cobertura; solo se recorre el anillo de la esquina. El resultado es
  identico pixel a pixel (comprobado para radios 1..420: 24,8 M de iteraciones antes, 0,36 M ahora).

## Medidas (QEMU con -icount, un nucleo, maquina muy lenta)

| pagina de prueba (44 cajas con borde y radio enorme) | 189 | 190 |
|---|---|---|
| carga | 10,0 s | 5,1 s |

## Pruebas

`tests/test_web_sorry_190.c` (8 casos). El resto de pruebas del 189 siguen igual.
