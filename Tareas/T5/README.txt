==========================================================
Esta es la documentación para compilar y ejecutar su tarea
==========================================================

Se está ejecutando el comando: less README.txt

***************************
*** Para salir: tecla q ***
***************************

Para avanzar a una nueva página: tecla <page down>
Para retroceder a la página anterior: tecla <page up>
Para avanzar una sola línea: tecla <enter>
Para buscar un texto: tecla / seguido del texto (/...texto...)
         por ejemplo: /ddd

-----------------------------------------------

¡Nuevo! Esta distribución de nThreads sí es compatible con procesadores
Arm, como los procesadores M1 y M2 de los Mac de Apple.

La implementación de nThreads está en el directorio nKernel.

-----------------------------------------------

Breve introducción a nThreads

Este es nThreads (nano threads).  Es un sistema operativo de
juguete que implementa threads ultra livianos a partir de un número fijo
de p-threads (livianos, pero no ultra livianos).

Para compilar sus tareas previas copie maleta.c, reservar.c y disk.c a este
directorio y cambie:

#include <pthread.h>

por:

#include "nthreads.h"

No todas las funciones de Unix/Debian se pueden invocar.  Además las
funciones tienen nombres distintos.  Al final del archivo nKernel/nthread.h
hay macros del tipo #define pthread_create nThreadCreate que traducen
los nombres oficiales de la API de Unix/Debian a los nombres de nThreads.
Por eso Ud. sí puede usar los nombres tradicionales en sus programas.
Pero considere que el debugger le mostrará los nombres traducidos.
Para saber qué funciones puede invocar, vea si hay un #define para su
función al final del archivo nKernel/nthread.h.  Si no lo hay es
probable que no puede usar esa función porque puede producir dataraces
difíciles de diagnosticar, *especialmente* si esa función invoca malloc/free
o usa variables globales.  Funciones como strlen, strcpy, etc. sí se
pueden usar porque no invocan malloc/free.

Para compilar por ejemplo reservar.c ejecute alguno de estos comandos:

make PROB=reservar run-san
make PROB=reservar run-g
make PROB=reservar run

El primer comando genera el binario reservar.bin-san, el segundo
reservar.bin-g y el tercero reservar.bin.  Ejecútelos con la opción -h
para recibir explicaciones sobre las opciones para ejecutar con
distintos schedulings.  Por ejemplo:

./reservar.bin-san -h

Use PROB=maleta para probar su tarea 1.  Considere que no pasará el test
de eficiencia con make PROB=integral run, porque su tarea se ejecutará con
un solo core virtual (un solo pthread).  Ejecútelo manualmente con varios
cores.  Por ejemplo:

./maleta.bin -ncores 4

Use PROB=disk para su tarea 3.

-----------------------------------------------

Instrucciones para probar su tarea 4

Programe las funciones solicitadas en el archivo rwlock.c.

Pruebe su tarea bajo Debian 12.  Estos son los requerimientos
para aprobar su tarea:

+ make run-san debe felicitarlo y no reportar ningún incidente en el
  manejo de memoria.

+ make run debe felicitarlo.

+ make run-g debe felicitarlo.

(Lamentablemente make run-thr no funciona.)

Cuando pruebe su tarea con make run en su computador asegúrese de que
está configurado en modo alto rendimiento y que no estén corriendo
otros procesos intensivos en uso de CPU al mismo tiempo.

Invoque el comando make zip para ejecutar todos los tests y generar un
archivo rwlock.zip que contiene rwlock.c, con su solución, y
resultados.txt, con la salida de make run, make run-g y make run-san.

Para depurar use: make ddd

Video con ejemplos de uso de ddd: https://youtu.be/FtHZy7UkTT4
Archivos con los ejemplos: https://www.u-cursos.cl/ingenieria/2020/2/CC3301/1/novedades/r/demo-ddd.zip

-----------------------------------------------

Entrega de la tarea

Ejecute: make zip

Entregue por U-cursos el archivo rwlock.zip

A continuación es muy importante que descargue de U-cursos el mismo
archivo que subió, luego descargue nuevamente los archivos adjuntos y
vuelva a probar la tarea tal cual como la entregó.  Esto es para
evitar que Ud. reciba un 1.0 en su tarea porque entregó los archivos
equivocados.  Creame, sucede a menudo por ahorrarse esta verificación.

-----------------------------------------------

Limpieza de archivos

make clean

Hace limpieza borrando todos los archivos que se pueden volver
a reconstruir a partir de los fuentes: *.o binarios etc.

-----------------------------------------------

Acerca del comando make

El comando make sirve para automatizar el proceso de compilación asegurando
recompilar el archivo binario ejecutable cuando cambió uno de los archivos
fuentes de los cuales depende.

A veces es útil usar make con la opción -n para que solo muestre
exactamente qué comandos va a ejecutar, sin ejecutarlos de verdad.
Por ejemplo:

   make -n ddd

También es útil usar make con la opción -B para forzar la recompilación
de los fuentes a pesar de que no han cambiado desde la última compilación.
Por ejemplo:

   make -B run

