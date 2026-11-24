# Parche 286 · YouTube con el LLM de verdad, pestaña IA nueva y vídeos desde el orbe

## 1. «No está conectado» (el arreglo de fondo)

YouTube (y también el chat del 285 y el Maquetador) llamaban al modelo con `llm_chat_ex`, que iba por el cliente HTTP del navegador.
El agente, en cambio, usa otro camino (`llm_request`) para el servidor local `http://IP:puerto` sin clave: el que funciona en la red de casa.
Por eso el agente respondía y YouTube decía «LLM: sin conexión con el servidor».

Ahora `llm_chat_hist` (y con él `llm_chat_ex`) usa **el mismo camino que el agente** cuando el modelo es el local sin clave. Con OpenAI, Claude o Gemini
todo sigue igual. Los tiempos de espera de YouTube también suben: 120 s para entender la búsqueda y 5 min para responder, porque un modelo local tarda en cargar.

## 2. Hablar con el vídeo (pestaña IA)

- **Conversación de varios turnos**: el modelo recibe la ficha y la transcripción como contexto, más lo hablado antes, como mensajes de verdad.
- **Respuestas en Markdown** (listas, negritas, tablas, código con Copiar, LaTeX), dibujadas con el maquetador del chat.
- **Los minutos se pulsan**: cada `[3:25]` de una respuesta es un enlace que salta a ese momento del vídeo.
- **Atajos**: *Puntos clave*, *Este momento* (explica lo que se ve en el minuto actual) y *¿De qué trata?*.
- **Sin transcripción**: antes de resumir o responder, SOMA pide los subtítulos de YouTube (modo servicio). Si no hay, usa el título y la descripción, como antes.
- El panel lateral se ensancha a 420 px en las pestañas IA y Texto si la ventana es ancha.

## 3. Barras de desplazamiento

- **YouTube**: todas las barras se arrastran (resultados, lista, Info, Texto, IA). La de los resultados estaba pegada al borde, donde el clic lo cogía el redimensionado de la ventana.
- **Chat (285)**: la barra se arrastra y está separada del borde. La lista de conversaciones también tiene barra. La rueda iba al revés.
- **Memoria (283)**: la rueda también iba al revés.

En `mouse.c`, un valor positivo es rueda hacia arriba, y así lo usaban ya el navegador y el explorador de archivos.

## 4. Vídeos desde el orbe

El LCM entiende peticiones de vídeo antes de pasarlas a su red (`lcm_video` en `lcm_soma.inc`) y las convierte en la orden `youtube`:

| Lo que dices | Orden |
|---|---|
| «quiero ver un vídeo sobre agujeros negros», «pon música relajante», «¿me pones un tutorial de Python?», «busca la última canción de Rosalía» | `youtube <búsqueda>` |
| «abre youtube» | `youtube` |
| «pon el segundo», «el tercero», «reproduce el número 4», «me pones el último vídeo» | `youtube ver N` |
| «resume el vídeo», «transcribe el vídeo», «¿de qué trata el vídeo?», «pregúntale al vídeo…», «¿qué dice el vídeo sobre…?» | `youtube resumen` / `transcribir` / `pregunta …` |
| «pausa el vídeo», «continúa el vídeo», «siguiente vídeo», «vídeo anterior» (solo con YouTube abierto; si no, son de SOMA Media) | `youtube pausa` / `sigue` / `siguiente` / `anterior` |

Se abre la ventana con los resultados **numerados** para elegir de palabra. Si la petición vino del orbe, **lo encontrado se lee en voz alta**:
- **Con el LCM**: una frase hecha («He encontrado 12 vídeos de gatos. El primero es «…», de …, dura 4 minutos…»).
- **Con el LLM**: si el buscador con IA está activado, se lee su resumen.

El resumen y las respuestas sobre el vídeo también se leen en voz alta. Todo esto se oye solo si está activado *Ajustes › Voz › leer las respuestas del orbe*.

Órdenes nuevas: `youtube pausa | sigue | siguiente | anterior` y `youtube ver ultimo`.

## Pruebas

- `tests/test_lcm_286.c` (153, dentro de `make pruebas-lcm`): 22 frases de vídeo con su orden, y 128 frases que no son de vídeo y no deben tocarse. Entre ellas, 120 sacadas al azar del catálogo de entrenamiento del LCM. Sobre el catálogo entero (2344 frases de 99 órdenes), la capa nueva no roba ninguna orden de otra aplicación.
- `tests/test_chatapp_285.c` (115): rueda, barras arrastradas (la principal y la lateral) y parar al momento.
- `tests/test_memview_283.c`: la rueda en el sentido nuevo.
