# Parche 164D · T480s: arrastrar ventanas con fluidez

Requiere el 164C. Responde a «al arrastrar las ventanas no es todo lo fluido que cabría esperar».

## La causa
Mientras arrastras, la ventana solo se redibuja con el latido del fondo. El 164B lo dejaba a unos 15 fps «mientras se
interactúa», para que el ratón fuera primero. Pero al arrastrar, redibujar es precisamente la respuesta.

## Qué cambia
- **Arrastre y cambio de tamaño**: el fotograma se repite tan rápido como cueste componerlo, entre 16 y 33 ms (hasta
  60 fps). En tu portátil, con unos 16 ms por fotograma, son unos 50 fps durante el arrastre.
- **Sombras de las ventanas, cajas redondeadas y rellenos grandes** repartidos por franjas de filas entre los núcleos.
  El resultado es idéntico: comprobado píxel a píxel con 1 y con 8 núcleos sobre la ventana y su sombra.
- **Ventanas translúcidas**: lo que hay debajo de la ventana se copiaba píxel a píxel en cada fotograma. Ahora se copia
  por filas y repartido. Las ventanas de detrás también.
- Los rellenos simples solo se reparten si son muy grandes (más de 256 K píxeles); en los medianos no compensa despertar
  a los demás núcleos.

## Sobre la gráfica
Todo se dibuja con la CPU, en un búfer que luego se copia a la pantalla del firmware. La GPU Intel UHD 620 no se usa:
manejarla (cambio de modo, aceleración 2D/3D) es un proyecto enorme, como un controlador de Linux. Lo que sí está en el
plan (169) es pedir al firmware el modo de vídeo y el ritmo de refresco nativos, y presentar sin cortes con doble búfer.

## En el portátil
Arrastra el terminal por encima del orbe. Después, `orbe` con el terminal abierto.

## Revertir
`python3 parche_164D_t480s_arrastre.py --undo`.
