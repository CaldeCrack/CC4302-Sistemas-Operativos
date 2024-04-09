#define _XOPEN_SOURCE 500

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "reservar.h"

#define N_EST 10
#define TRUE 1
#define FALSE 0

static int ocup[N_EST];
static int verbose= TRUE;
static int tiempo_actual;

void fatalError( char *procname, char *format, ... ) {
  va_list ap;

  fprintf(stderr,"Fatal error(%s):", procname);
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);

  exit(1); /* shutdown */
}

//===================================================================
// Manejo de pausas
//

static pthread_mutex_t t_mon= PTHREAD_MUTEX_INITIALIZER;

static void iniciar() {
  pthread_mutex_lock(&t_mon);
  tiempo_actual= 1;
  pthread_mutex_unlock(&t_mon);
}

static int tiempoActual() {
  pthread_mutex_lock(&t_mon);
  int t= tiempo_actual;
  pthread_mutex_unlock(&t_mon);
  return t;
}

static void pausa(int tiempo_espera) {
  /* tiempo_espera en centesimas de segundo */
  pthread_mutex_lock(&t_mon);
  int tiempo_inicio= tiempo_actual;
  pthread_mutex_unlock(&t_mon);
  usleep(tiempo_espera*500000L); // Medio segundo
  pthread_mutex_lock(&t_mon);
  tiempo_actual= tiempo_inicio+tiempo_espera;
  pthread_mutex_unlock(&t_mon);
}

//===================================================================
// Manejo de vehiculos
//

typedef struct {
  pthread_t t;
  char *nom;
  int t_reserva;
} Vehiculo;
 
int vehiculoFun(Vehiculo *v, int iter, int k, int tiempo) {
  // nSetTaskName(v->nom);
  while (iter--) {
    if (verbose)
      printf("%d: %s solicita %d estacionamientos\n",
              tiempoActual(), v->nom, k);
    int e= reservar(k);
    v->t_reserva= tiempoActual();
    for (int j= e; j<e+k; j++) {
      if (j>=N_EST)
        fatalError("estacionarVehiculo", "estacionamiento %d no existe\n",
                    j);
      if (ocup[j])
        fatalError("estacionarVehiculo", "estacionamiento %d esta ocupado\n",
                    j);
      ocup[j]= TRUE;
    }
    if (verbose)
      printf("%d: %s reserva %d estacionamiento desde %d\n",
              tiempoActual(), v->nom, k, e);
    if (tiempo>0)
      pausa(tiempo);
    if (verbose)
      printf("%d: %s libera %d estacionamientos desde %d\n",
              tiempoActual(), v->nom, k, e);
    for (int j= e; j<e+k; j++) {
      ocup[j]= FALSE;
    }
    liberar(e, k);
  }
  return 0;
}

typedef struct {
  Vehiculo *v;
  int iter, k, tiempo;
} Param;

// int vehiculoFun(Vehiculo *v, int iter, int k, int tiempo)
void *threadVehiculo(void *ptr) {
  Param *p= ptr;
  vehiculoFun(p->v, p->iter, p->k, p->tiempo);
  free(p);
  return NULL;
}

Vehiculo *estacionarVehiculo(char *nom, int iter, int k, int tiempo) {
  Vehiculo *v= malloc(sizeof(Vehiculo));
  v->nom= nom;
  Param param= {v, iter, k, tiempo};
  Param *p = malloc(sizeof(Param));
  *p= param;
  pthread_create(&v->t, NULL, threadVehiculo, p);
  return v;
}

void esperarVehiculo(Vehiculo *v, int t_reserva) {
  pthread_join(v->t, NULL);
  if (t_reserva>0 && t_reserva!=v->t_reserva)
    fatalError("esperarVehiculo",
                "Tiempo de otorgamiento de la reserva erroneo de %s. "
                "Es %d y debio ser %d.\n", v->nom, v->t_reserva, t_reserva);
  free(v);
}

//===================================================================
// Test unitarios
//

void testUnitarios() {
  iniciar();
  Vehiculo *renault, *mercedes, *chevrolet, *suzuki, *toyota, *mg, *ford, *bmw;
  printf("--- Test: un solo vehiculo no espera -------------------------\n");
  renault= estacionarVehiculo("renault", 1, 2, 0);
  esperarVehiculo(renault, 1);
  
  printf("--- Test: se ocupan todos los estacionamientos, "
          "pero nadie espera ---\n");
  iniciar();
  renault= estacionarVehiculo("renault", 1, 2, 3);
  pausa(1);
  mercedes= estacionarVehiculo("mercedes", 1, 3, 2);
  pausa(1);
  chevrolet= estacionarVehiculo("chevrolet", 1, 5, 1);
  esperarVehiculo(renault, 1);
  esperarVehiculo(mercedes, 2);
  esperarVehiculo(chevrolet, 3);
  
  printf("--- Test: se ocupan todos los estacionamientos, "
          "ultimo espera ---\n");
  iniciar();
  renault= estacionarVehiculo("renault", 1, 2, 3);
  pausa(1);
  mercedes= estacionarVehiculo("mercedes", 1, 3, 2);
  pausa(1);
  chevrolet= estacionarVehiculo("chevrolet", 1, 6, 1);
  esperarVehiculo(renault, 1);
  esperarVehiculo(mercedes, 2);
  esperarVehiculo(chevrolet, 4);

  printf("--- Test: estacionamientos se otorgan por orden de llegada ----\n");
  iniciar();
  renault= estacionarVehiculo("renault", 1, 2, 4);
  pausa(1);
  mercedes= estacionarVehiculo("mercedes", 1, 3, 3);
  pausa(1);
  chevrolet= estacionarVehiculo("chevrolet", 1, 6, 1);
  pausa(1);
  suzuki= estacionarVehiculo("suzuki", 1, 1, 1);
  esperarVehiculo(renault, 1);
  esperarVehiculo(mercedes, 2);
  esperarVehiculo(chevrolet, 5);
  esperarVehiculo(suzuki, 5);

  printf("--- Test: un test mas completo ----\n");
  iniciar();
  chevrolet= estacionarVehiculo("chevrolet", 1, 6, 5);
  pausa(1);
  toyota= estacionarVehiculo("toyota", 1, 4, 3);
  pausa(1);
  renault= estacionarVehiculo("renault", 1, 2, 4);
  pausa(1);
  mg= estacionarVehiculo("mg", 1, 2, 3);
  pausa(1);
  suzuki= estacionarVehiculo("suzuki", 1, 1, 5);
  pausa(1);
  ford= estacionarVehiculo("ford", 1, 6, 2);
  pausa(1);
  mercedes= estacionarVehiculo("mercedes", 1, 3, 3);
  pausa(1);
  bmw= estacionarVehiculo("bmw", 1, 7, 1);
  pausa(1);
  esperarVehiculo(renault, 5);
  esperarVehiculo(mercedes, 9);
  esperarVehiculo(chevrolet, 1);
  esperarVehiculo(suzuki, 6);
  esperarVehiculo(toyota, 2);
  esperarVehiculo(mg, 5);
  esperarVehiculo(ford, 9);
  esperarVehiculo(bmw, 11);
}

//===================================================================
// Test de robustez
//

#ifdef NSYSTEM
#define NTASKS 100
#define ITER 30000
#else
#define NTASKS 100
#define ITER 500
#endif

void testRobustez() {
  // nSetTimeSlice(1); // No hace nada en pSystem
  Vehiculo *vehiculos[NTASKS];
  char *noms[NTASKS];
  verbose= FALSE;
  printf("Test de robustez -------------------------------------\n");
  for (int i= 0; i<NTASKS; i++) {
    noms[i]= malloc(10);
    sprintf(noms[i], "T%d", i);
    int k= (random() % N_EST)+1;
    vehiculos[i]= estacionarVehiculo(noms[i], ITER, k, 0);
  }
  for (int i= 0; i<NTASKS; i++) {
    esperarVehiculo(vehiculos[i], -1);
    free(noms[i]);
  }
}

int main() {
  initReservar();
  testUnitarios();
  testRobustez();

  cleanReservar();
  printf("Felicitaciones: aprobo todos los tests\n");
  return 0;
}
