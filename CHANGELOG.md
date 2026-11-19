# Historial de cambios

## v0.3.0-prebeta — fase pre-beta

Primera versión que se publica como **pre-beta**: el sistema es usable a diario en el emulador y en el ThinkPad T480s, pero
todavía es software experimental (todo el núcleo, las aplicaciones y el navegador corren en ring 0). Esta fase sirve para
encontrar fallos con más gente y más hardware antes de una beta.

### Arranque desde pendrive USB (x86_64)
- SOMA arranca en un PC real desde un pendrive (probado en el ThinkPad T480s). Instrucciones para Windows, Linux y
  macOS en el README.

### Escritorio
- **Animación al minimizar**: la ventana se desintegra en un mosaico y vuela en espiral hasta su sitio en la órbita (223).
- Órbita estable de las ventanas minimizadas, nombres al pasar el ratón y animación al cerrar en el agujero negro (206, 208).
- Iconos y gadget del reloj arrastrables, con la disposición guardada en `/etc/escritorio.conf` (`escritorio reiniciar`
  vuelve a la de fábrica) (208).
- **Cuadrícula virtual**: al soltar un icono se alinea a la celda más cercana; si está ocupada, intercambia el sitio (218).
- Botones de **Apagar** y **Reiniciar** rediseñados con iconos vectoriales, estados al pasar el ratón y cuenta atrás
  de confirmación (218).
- Iconos vectoriales del sistema (`sysicon`) y Grabadora, Cámara y Captura con aspecto profesional (210).

### Varias ventanas por aplicación
- Notas, Imágenes, Lector PDF, Archivos, Paint y SOMA Studio abren tantas ventanas como se quiera (hasta 6–8 según la
  aplicación), cada una con su documento y su estado (211–217).
- La X minimiza (la ventana orbita con su número), un clic la restaura y arrastrarla al agujero negro la cierra.
- Monitor, Ajustes, Cámara, Grabadora, Captura y Agente siguen siendo de una sola ventana a propósito.

### Rendimiento
- Capa de fondo, cachés de iconos y de contenido de ventanas, borrado y volcado por zonas (207, 209).
- Con varias ventanas abiertas el orbe va fluido y el Monitor del sistema ya no se redibuja sin cambios: en el T480s, de
  40–50 % de CPU en reposo a ~22 % (216).

### Estabilidad
- **Aviso de cambios sin guardar** al cerrar Notas, Paint o Studio en el agujero negro; hay que soltarlo otra vez para
  cerrar sin guardar (219).
- **Prueba de esfuerzo** `escritorio prueba [n]`: abre y cierra cada aplicación n veces y comprueba que la memoria vuelve
  exactamente a su valor y que el heap está íntegro (220).
- **Rescate de aplicaciones**: si una aplicación hace un acceso a memoria no válido (u otra excepción de CPU) o se queda
  10 s sin soltar el procesador, se cierra su ventana con un aviso y el sistema sigue, en vez de un pánico o un cuelgue
  total (221). Pruebas: `escritorio fallo memoria`, `escritorio fallo cuelgue`.
- Ya existían y siguen activos: guardado atómico con diario y CRC en SOMAFS02, decodificadores de imagen y PDF en
  ring 3 con fuzzing (ASan/UBSan), contención de fallos del motor del navegador y vigilante del núcleo.

### Limitaciones conocidas
- Contención, no aislamiento: una aplicación averiada puede haber dañado memoria del núcleo antes de fallar.
- El rescate solo existe en x86_64 (en la Raspberry Pi 5 un fallo de una aplicación sigue siendo un pánico).
- El orbe animado gasta ~20 % de un núcleo en el T480s con una ventana abierta.
