# Parche 175C2 · T480s: WiFi, el escritorio ya no se ralentiza con Ajustes abierto, y diagnóstico de la IP

Requiere el 175C.

## Lo que se vio en el portátil (175C)
- **Conectada a Livebox6-7E21** (WPA2, canal 11, 54 Mbit/s): autenticación, asociación y saludo de 4 pasos completos.
- Pero el gestor de red se quedaba en «el router no dio dirección»: el DHCP no recibía respuesta.
- Con Ajustes abierto, el resto del sistema apenas respondía.

## Qué cambia
- **Ajustes**: la página Redes comprobaba su estado en cada vuelta del bucle de la GUI, unas 1000 veces por segundo. Cuando algo cambiaba, recomponía el escritorio entero. Ahora:
  - lo comprueba como mucho dos veces por segundo;
  - solo vuelve a pintar su propia ventana, y solo si está delante.
- **Mensaje 3 repetido**: si el AP repite el mensaje 3 del saludo (porque no le llegó nuestro 4), ahora se le reenvía el 4, en claro, como hace wpa_supplicant. Antes se ignoraba. Si el AP no recibe el 4, no abre el puerto y descarta nuestros datos: es una de las causas posibles del fallo de DHCP.
- **Sin ahorro de energía de verdad** (`flags = 0`, como Linux con el ahorro desactivado). Antes se dejaba activado el ahorro del propio dispositivo.
- **Diagnóstico del tráfico** en la orden `wifi`:
  - tramas enviadas cifradas y en claro, y el código del último fallo de transmisión;
  - tramas recibidas protegidas, individuales y de grupo descifradas, con MIC incorrecto y con otro índice de clave;
  - recibidas en claro después de las claves;
  - ARP, IPv4 y otras;
  - mensajes 1 y 3 del saludo y cuántos 3 se repitieron, y el índice de la clave de grupo.
- En `wifi registro` quedan anotados también:
  - la primera trama individual y la primera de grupo descifradas;
  - el primer MIC incorrecto;
  - las tramas que el AP no confirmó;
  - las tramas de grupo con otro índice de clave.

## En el portátil
Conecta a la WiFi y espera unos 20 s. Después:
1. `wifi` (foto)
2. `wifi registro` (foto)
3. `dhcp` (foto): pide la IP a mano y dice en qué paso se queda.
