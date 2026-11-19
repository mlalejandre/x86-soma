# Parche 177D · T480s: la Grabadora normaliza el volumen al reproducir

Requiere el 177C.

La salida del códec ya estaba al 100 % del amplificador; lo que sonaba bajo era la propia grabación (el micrófono digital llega con rms ~1400 y pico ~38 %). Al reproducir, la Grabadora busca el pico de la grabación y sube la señal hasta ~88 % de la escala (máximo x32, nunca baja el volumen). El archivo WAV guardado no se modifica. Prueba: una grabación con pico ~8000 se reproduce con pico >26000.
