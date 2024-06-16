Este ejemplo es una adaptacion del tutorial incluido
(archivo "device drivers tutorial.pdf") y bajado de:
http://www.freesoftwaremagazine.com/articles/drivers_linux

---

Guia rapida:

*******************************************************************
Para insertar el modulo necesita que el .ko este en un disco local.
No puede cargar un modulo que vive en un disco ajeno a Linux.
*******************************************************************

Lo siguiente se debe realizar parados en
el directorio en donde se encuentra este README.txt

+ Compilacion (puede ser en modo usuario):
$ make
...
$ ls
... disco.ko ...

+ Instalacion (en modo root)

# mknod /dev/disco c 61 0
# chmod a+rw /dev/disco
# insmod disco.ko
# dmesg | tail
...
[...........] Inserting disco module
#

+ Testing (en modo usuario preferentemente)

Ud. necesitara crear multiples shells independientes.  Luego
siga las instrucciones del enunciado de la tarea 3 de 2017-2

+ Desinstalar el modulo

# rmmod disco.ko
#
