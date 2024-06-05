# Para revisar las opciones de compilacion y ejecucion,
# ingrese en el terminal el comando: make
#
#

PROB=rwlock

ARCH=$(shell arch)

NTHSRC=nKernel/nthread.c nKernel/nutil.c nKernel/ntime.c nKernel/pss.c \
       nKernel/sched-fcfs.c nKernel/sched-rr.c \
       nKernel/sched-pri-1core.c nKernel/sched-fcfs-1core.c \
       nKernel/sem.c nKernel/mutex-cond.c nKernel/nmsgs.c \
       nKernel/nsystem.c

NTHHDR=nKernel/nthread.h nKernel/nthread-impl.h nKernel/pss.h

SRCS= test-$(PROB).c $(PROB).c $(NTHSRC)
HDRS= $(PROB).h $(NTHHDR)

#DEFS=-DNTHSTOP -DTRACESCHED
DEFS=

SHELL=bash -o pipefail

INCLUDE=-InKernel
CFLAGS=-Wall -Werror -pedantic -DNTHREADS -std=c18 $(INCLUDE) $(DEFS)
#CFLAGS=-Wall -Werror -pedantic -std=c18 $(INCLUDE)
LDLIBS=-lm -lpthread -lrt

MAK=make --no-print-directory

readme:
	@less README.txt

$(PROB).bin $(PROB).bin-g $(PROB).bin-san $(PROB).bin-thr: $(SRCS) $(HDRS)

run-san: $(PROB).bin-san
	@if grep -P '\t' $(PROB).c ; then echo "Su archivo $(PROB).c contiene tabs.  Reemplacelos por espacios en blanco con el comando expand!" ; false ; else true; fi
	@echo --------------------------------------------
	./$(PROB).bin-san
	@echo --------------------------------------------
	./$(PROB).bin-san 1

run-thr: $(PROB).bin-thr
	@echo "No tiene sentido ejecutar sanitize para threads"
	@echo "porque no esta diseñado para nThreads"
	@echo "No se extrañe por los errores que detecta"
	./$(PROB).bin-thr

run-g: $(PROB).bin-g
	@echo --------------------------------------------
	./$(PROB).bin-g
	@echo --------------------------------------------
	./$(PROB).bin-g -slice 1 -ncores 4 1

run: $(PROB).bin
	@echo --------------------------------------------
	./$(PROB).bin -ncores 4 1
	@echo --------------------------------------------
	./$(PROB).bin -slice 1 -ncores 4 1

run-par: $(PROB).bin
	@echo --------------------------------------------
	./$(PROB).bin -ncores 4
	@echo --------------------------------------------
	./$(PROB).bin -slice 1 -ncores 4

ddd: $(PROB).ddd

ddd-san: $(PROB).ddd-san

zip:
	@if grep -P '\t' $(PROB).c ; then echo "Su archivo $(PROB).c contiene tabs.  Reemplacelos por espacios en blanco!" ; false ; else true; fi
	@rm -f resultados.txt $(PROB).zip
	@echo "Sistema operativo utilizado" > resultados.txt
	@uname -a >> resultados.txt
	@cat /etc/debian_version
	@cat resultados.txt
	@echo ==== run-san ==== | tee -a resultados.txt
	@$(MAK) -B run-san | tee -a resultados.txt
	@echo ==== run-g ==== | tee -a resultados.txt
	@$(MAK) -B run-g | tee -a resultados.txt
	@echo ==== run ==== | tee -a resultados.txt
	@$(MAK) -B run | tee -a resultados.txt
	@echo ==== zip ====
	zip -r $(PROB).zip resultados.txt $(PROB).c
	@echo "Entregue por u-cursos el archivo $(PROB).zip"
	@echo "Descargue de u-cursos lo que entrego, descargue nuevamente los"
	@echo "archivos adjuntos y vuelva a probar la tarea tal cual como"
	@echo "la entrego.  Esto es para evitar que Ud. reciba un 1.0 en su"
	@echo "tarea porque entrego los archivos equivocados.  Creame, sucede"
	@echo "a menudo por ahorrarse esta verificacion."

%.bin: %.c
	gcc -g -O -DOPT=1 $(CFLAGS) $(SRCS) $(LDLIBS) -o $@

%.bin-g: %.c
	gcc -g $(CFLAGS) $(SRCS) $(LDLIBS) -o $@

%.bin-san: %.c
	gcc -g -DSAN=1 -fsanitize=address -fsanitize=undefined $(CFLAGS) $(SRCS) $(LDLIBS) -o $@

%.bin-thr: %.c
	gcc -g -DSAN=1 -fsanitize=thread $(CFLAGS) $(INCLUDE) $(SRCS) $(LDLIBS) -o $@

%.ddd: %.bin-g
	ddd $(*F).bin-g &

%.ddd-san: %.bin-san
	ddd $(*F).bin-san &

clean:
	rm -rf *.o *.log *.bin* core bin obj resultados.txt $(PROB).zip
