#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <nthread.h>

#include "rwlock.h"

#pragma GCC diagnostic ignored "-Wunused-function"

// ----------------------------------------------------
// Funcion que entrega el tiempo transcurrido en milisegundos

static int time0= 0;

static int getTime0() {
    struct timeval Timeval;
    gettimeofday(&Timeval, NULL);
    return Timeval.tv_sec*1000+Timeval.tv_usec/1000;
}

static void resetTime() {
  time0= getTime0();
}

static int getTime() {
  return getTime0()-time0;
}

// ==================================================
// Manejo de las pausas
//

#define TAM_PASO 500

static int ahora() {
  int div= TAM_PASO/10;
  return (getTime()+div/2)/div;
}

static void esperar(int paso) {
  int actual= getTime();
  int hasta= actual+paso*TAM_PASO;
  do {
    usleep((hasta-actual)*1000);
    actual= getTime();
  } while (actual<hasta);
}

// ==================================================
// 

static char *nStrdup(char *s) {
  return strcpy(nMalloc(strlen(s)+1), s);
}

static int verbose= 1;
static pthread_mutex_t m= PTHREAD_MUTEX_INITIALIZER;
static nRWLock *g_rwl, *g_rwl1, *g_rwl2;
static int escribiendo= 0;
static int lectores= 0;

typedef enum { LEC, ESCR } Tipo;

typedef struct {
  Tipo tipo;
  char *nombre;
  int plazo, tiempo_acceso, termino;
  pthread_t t;
  nRWLock *rwl;
} Agente;

static void *agente_fun(void *ptr) {
  Agente *pa= ptr;
  char *desc= pa->tipo==LEC ? "lector" : "escritor";
  char *nombre= pa->nombre;
  nSetThreadName(nombre);
  if (verbose)
    printf("%d: %s %s solicita entrar\n", ahora(), desc, nombre);
  int rc;
  int timeout= pa->plazo<0 ? -1 : pa->plazo*TAM_PASO;
  if (pa->tipo==LEC)
    rc= nEnterRead(pa->rwl, timeout);
  else // pa->tipo==ESCR
    rc= nEnterWrite(pa->rwl, timeout);
  if (!rc) {
    if (verbose)
      printf("%d: %s %s no pudo entrar\n", ahora(), desc, nombre);
    return NULL;
  }
  if (verbose)
    printf("%d: %s %s entra\n", ahora(), desc, nombre);
  if (pa->rwl==g_rwl1) {
    pthread_mutex_lock(&m);
    if (pa->tipo==LEC) {
      if (escribiendo)
        nFatalError(desc, "%s no cumple exclusion mutua\n", nombre);
      lectores++;
    }
    else { // pa->tipo==ESCR
      if (escribiendo || lectores>0)
        nFatalError(desc, "%s no cumple exclusion mutua\n", nombre);
      escribiendo= 1;
    }
    pthread_mutex_unlock(&m);
  }
  esperar(pa->tiempo_acceso);
  pthread_mutex_lock(&m);
  if (pa->tipo==LEC)
    lectores--;
  else
    escribiendo= 0;
  pthread_mutex_unlock(&m);
  if (verbose)
    printf("%d: %s %s sale\n", ahora(), desc, nombre);
  pa->termino= getTime();
  if (pa->tipo==LEC)
    nExitRead(pa->rwl);
  else
    nExitWrite(pa->rwl);
  return 0;
}

static Agente *crearAgente( Tipo tipo, char *nombre,
                            int plazo, int tiempo_acceso ) {
  Agente *pa= malloc(sizeof(Agente));
  Agente ag= {tipo, nombre, plazo, tiempo_acceso};
  *pa= ag;
  pa->rwl= g_rwl;
  pthread_create(&pa->t, NULL, agente_fun, pa);
  return pa;
}

static int esperarAgente(Agente *pa) {
  pthread_join(pa->t, NULL);
  int ret= pa->termino;
  free(pa);
  return ret;
}

void termina(Agente *pa, int ref) {
  char *nombre= pa->nombre;
  int t= esperarAgente(pa);
  int dif= t-ref*TAM_PASO;
  if (dif<0)
    dif= -dif;
  if (dif>TAM_PASO/2)
    nFatalError("termina", "Tiempo de termino %d de %s es incorrecto\n",
                t, nombre);
}
 
int main(int argc, char *argv[]) {
  g_rwl1= nMakeRWLock();
  g_rwl2= nMakeRWLock();
  g_rwl= g_rwl1;

if (argc==1) {
  printf("--- Test: un solo lector ------------------------------\n");
  {
    resetTime();
    Agente *pedro= crearAgente(LEC, "pedro", -1, 1);
    termina(pedro, 1);
  }

  printf("--- Test: un solo escritor ----------------------------\n");
  {
    resetTime();
    Agente *juan= crearAgente(ESCR, "juan", -1, 1);
    termina(juan, 1);
  }

  printf("--- Test: lectores en paralelo ------------------------\n");
  {
    resetTime();
    Agente *pedro= crearAgente(LEC, "pedro", -1, 2);
    esperar(1);
    Agente *juan= crearAgente(LEC, "juan", -1, 2);
    termina(pedro, 2);
    termina(juan, 3);
  }

  printf("--- Test: escritores secuenciales ---------------------\n");
  {
    resetTime();
    Agente *pedro= crearAgente(ESCR, "pedro", -1, 2);
    esperar(1);
    Agente *juan= crearAgente(ESCR, "juan", -1, 1);
    termina(pedro, 2);
    termina(juan, 3);
  }

  printf("--- Test: escritores paralelos en rwlocks distintos ---\n");
  {
    resetTime(); 
    Agente *pedro= crearAgente(ESCR, "pedro", -1, 2);
    esperar(1);
    g_rwl= g_rwl2;
    Agente *juan= crearAgente(ESCR, "juan", -1, 1);
    g_rwl= g_rwl1;
    termina(pedro, 2);
    termina(juan, 2);
  }
  
  printf("--- Test: escritor espera a que salga ultimo lector ---\n");
  {
    // 0: entra L ana, 1: entra L tomas, 2: entra L ximena, 3: llega E jorge
    // 4: sale ana, 5: sale ximena, 6: sale tomas y entra jorge, 7: sale jorge
    resetTime(); 
    Agente *ana= crearAgente(LEC, "ana", -1, 4);
    esperar(1);
    Agente *tomas= crearAgente(LEC, "tomas", -1, 5);
    esperar(1);
    Agente *ximena= crearAgente(LEC, "ximena", -1, 3);
    esperar(1);
    Agente *jorge= crearAgente(ESCR, "jorge", -1, 1);
    termina(ana, 4);
    termina(ximena, 5);
    termina(tomas, 6);
    termina(jorge, 7);
  }

  printf("--- Test: lector espera si llega despues de escritor --\n");
  { 
    // 0: entra L ana, 1: entra L tomas, 2: llega E ximena, 3: llega L jorge
    // 4: sale tomas, 5: sale ana y entra ximena, 6: sale ximena y entra jorge
    // 7: sale jorge
    resetTime(); 
    Agente *ana= crearAgente(LEC, "ana", -1, 5);
    esperar(1);
    Agente *tomas= crearAgente(LEC, "tomas", -1, 3);
    esperar(1);
    Agente *ximena= crearAgente(ESCR, "ximena", -1, 1);
    esperar(1);
    Agente *jorge= crearAgente(LEC, "jorge", -1, 1);
    termina(tomas, 4);
    termina(ana, 5);
    termina(ximena, 6);
    termina(jorge, 7);
  }

  printf("--- Test: entradas alternadas -------------------------\n");
  {
    // 0: llega y entra L ana, 1: llega E tomas, 2: llega E ximena,
    // 3: llega E jorge, 4: llega E veronica, 5: llega L alberto,
    // 6: llega L julio,
    // 7: sale ana y entra tomas, 8: sale tomas y entran alberto y julio,
    // 9: llega carolina, 10: sale alberto, 11 sale julio y entra ximena
    // 12: sale ximena y entra carolina, 13: sale carolina y entra jorge
    // 14: sale jorge y entra veronica, 15: sale veronica
    resetTime();
    Agente *ana= crearAgente(LEC, "ana", -1, 7);
    esperar(1);
    Agente *tomas= crearAgente(ESCR, "tomas", -1, 1);
    esperar(1);
    Agente *ximena= crearAgente(ESCR, "ximena", -1, 1);
    esperar(1);
    Agente *jorge= crearAgente(ESCR, "jorge", -1, 1);
    esperar(1);
    Agente *veronica= crearAgente(ESCR, "veronica", -1, 1);
    esperar(1);
    Agente *alberto= crearAgente(LEC, "alberto", -1, 2);
    esperar(1);
    Agente *julio= crearAgente(LEC, "julio", -1, 3);
    esperar(3);
    Agente *carolina= crearAgente(LEC, "carolina", -1, 1);
    termina(ana, 7);
    termina(tomas, 8);
    termina(alberto, 10);
    termina(julio, 11);
    termina(ximena, 12);
    termina(carolina, 13);
    termina(jorge, 14);
    termina(veronica, 15);
  }
}
else {
#ifdef OPT
#   define NAGENTES 400
#   define NITER    1000
#   define MOD      500
#else
#   define NAGENTES 100
#   define NITER    500
#   define MOD      50
#endif

  printf("\n----------------------------------------------\n");
  printf("Test de esfuerzo\n");
  printf("\n----------------------------------------------\n");
  verbose= 0;
  Agente *agentes[NAGENTES];
  for (int j= 0; j<NAGENTES; j++)
    agentes[j]= NULL;
  int id= 1;
  for (int i= 0; i<NITER; i++) {
    for (int j= 0; j<NAGENTES; j++) {
      int tipo= random()%2;
      if (agentes[j]!=NULL) {
        char *nom= agentes[j]->nombre;
        esperarAgente(agentes[j]);
        free(nom);
      }
      char nom[20];
      sprintf(nom, "%d", id++);
      agentes[j]= crearAgente(tipo, nStrdup(nom), -1, 0);
      if (id % MOD == 0)
        printf("%c", tipo==LEC ? 'L' : 'E');
    }
  }
  for (int j=0; j<NAGENTES; j++) {
    char *nom= agentes[j]->nombre;
    esperarAgente(agentes[j]);
    free(nom);
  }
  printf("\n");
}

  printf("Felicitaciones: Aprobo los tests\n");

  return 0;
}

