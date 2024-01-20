# vitextserver / vitextclient

Este proyecto implementa una aplicación de Sistema de VOD (Video On Demand) sobre UDP, con envío y recepción a una dirección IP multicast. La aplicación puede ejecutarse en el mismo equipo y permite el flujo de datos desde el servidor al cliente.

## Configuración

- Cualquiera puede arrancar primero (cliente o servidor).
- Configuración explícita en línea de comando para:
  - Dirección IP multicast (rango 239.0.0.0/8, para uso local dentro de un dominio).
  - Fichero de video en el servidor.

## Ejemplos de Uso

En el equipo1:
```bash
./vitextserver 1.vtx 239.0.0.1
En el equipo2:

bash
Copy code
./vitextclient 239.0.0.1
Prácticas de SMA
Escenario Inicial
Supongamos que el cliente se inicia primero:

P1-P4: El servidor espera 100ms (buffer) y comienza la reproducción.
P5-P7: El cliente se inicia con un buffer de 100 y espera a que el servidor envíe datos.
Comportamiento del Servidor y Cliente
El servidor se comporta como si estuviera generando contenido en tiempo real.
Manda paquetes de un frame según el espaciado indicado en el archivo .vtx.
El cliente implementa buffering con un valor seleccionable en la línea de comandos.
Manejo de Frames y Paquetes
Un frame puede transmitirse en varios paquetes.
Un frame solo se reproduce si han llegado todos los paquetes que lo componen.
Interpretación de Cabeceras
El cliente interpreta la cabecera RTP en el envío de paquetes.
Lee cabecera RTP.
Lee cabecera VTX.
Muestra los datos por la pantalla en el tiempo apropiado.
Desacoplamiento de Tareas en el Cliente
Es necesario desacoplar las siguientes tareas:

Recepción de paquetes de datos.
Espera para reproducir el próximo frame.
Atención a señal (Ctrl-C).
Generación y envío de mensajes RTCP.
Recepción y tratamiento de mensajes RTCP.
Para lograr esto se utiliza select junto con un buffer de paquetes.

packet_buffer
El componente packet_buffer está específicamente desarrollado para vtx y:

Guarda información relevante como número de secuencia, timestamp, tamaño del frame, etc.
pbuf_insert: Rellena un paquete con parte de cabecera RTP, cabecera vtx y datos.
pbuf_retrieve: Devuelve un puntero a los datos y parámetros relevantes (RTP, vtx).
Escenarios Avanzados en vitextserver
vitextserver permite especificar patrones de:

Pérdidas de paquetes.
Reordenamiento de paquetes.
Tiempo sin mandar.
css
Copy code
./vitextserver --packet-loss 0.1 --packet-reorder 0.05 --time-without-s
