#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <nthread.h>

#include "subasta.h"

#pragma GCC diagnostic ignored "-Wunused-function"

static void msleep(int delay) {
  usleep(delay*1000);
}

// ==================================================
// Funcion para reportar un error y detener la ejecucion

void fatalError( char *procname, char *format, ... ) {
  va_list ap;

  fprintf(stderr,"Error Fatal en la rutina %s y la tarea \n", procname);
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);

  exit(1); /* shutdown */
}

// ==================================================
// Funciones para reportar nombre de los threads

__thread char *threadName= NULL;

static void setThreadName(char *name) {
  threadName= name;
}

static char *getThreadName(void) {
  return threadName;
}

// print_msg:
//   1   => con mensajes y delay
//   0   => sin mensajes, con delay
//   -1  => sin mensajes, sin delay

typedef struct {
  Subasta s;
  int print_msg;
  char *nom;
  int oferta, ret;
} Param;

static void *aleatorio(void *ptr) {
  Param *pparm= ptr;
  Subasta s= pparm->s;
  int print_msg= pparm->print_msg;
  char *nom= pparm->nom;
  int oferta= pparm->oferta;
  if (print_msg>=0) {
    setThreadName(nom);
    int delay= random()%1000;
    msleep(delay);
  }
  if (print_msg>0)
    printf("%s ofrece %d\n", nom, oferta);
  int ret= ofrecer(s, oferta);
  if (print_msg>0) {
    if (ret)
      printf("%s se adjudico un item a %d\n", nom, oferta);
    else
      printf("%s fallo con su oferta de %d\n", nom, oferta);
  }
  pparm->ret= ret;
  return pparm;
}

static pthread_t crearAleatorio(Subasta s, int print_msg, char *nom, int oferta)
{
  Param parm= { s, print_msg, nom, oferta };
  Param *pparm= malloc(sizeof(Param));
  *pparm= parm;
  pthread_t thr;
  if (pthread_create(&thr, NULL, aleatorio, pparm)!=0) {
    perror("nom");
    exit(1);
  }
  return thr;
}

static int esperar(pthread_t thr) {
  void *pparm;
  pthread_join(thr, &pparm);
  int ret= ((Param*)pparm)->ret;
  free(pparm);
  return ret;
}

static void *oferente(void *ptr) {
  Param *pparm= ptr;
  Subasta s= pparm->s;
  int print_msg= pparm->print_msg;
  char *nom= pparm->nom;
  int oferta= pparm->oferta;
  setThreadName(nom);
  if (print_msg>0)
    printf("%s ofrece %d\n", nom, oferta);
  int ret= ofrecer(s, oferta);
  if (print_msg>0) {
    if (ret)
      printf("%s se adjudico un item a %d\n", nom, oferta);
    else
      printf("%s fallo con su oferta de %d\n", nom, oferta);
  }
  pparm->ret= ret;
  return pparm;
}

static pthread_t crearOferente(Subasta s, int print_msg, char *nom, int oferta)
{
  Param parm= { s, print_msg, nom, oferta };
  Param *pparm= malloc(sizeof(Param));
  *pparm= parm;
  pthread_t thr;
  if (pthread_create(&thr, NULL, oferente, pparm)!=0) {
    perror("nom");
    exit(1);
  }
  return thr;
}

typedef struct {
  int (*testFun)(int print_msg);
  int print_msg;
} ParamTest;

static void *genTest(void *ptr) {
  ParamTest *pparm= ptr;
  (*pparm->testFun)(pparm->print_msg);
  free(pparm);
  return NULL;
}

static pthread_t crearTest(int (*testFun)(int), int print_msg) {
  ParamTest parm= {testFun, print_msg};
  ParamTest *pparm= malloc(sizeof(ParamTest));
  *pparm= parm;
  pthread_t thr;
  if (pthread_create(&thr, NULL, genTest, pparm)!=0) {
    perror("pthread_create");
    exit(1);
  }
  return thr;
}

static int test1(int print_msg) {
  Subasta s= nuevaSubasta(2);
  pthread_t pedro= crearAleatorio(s, print_msg, "pedro", 1);
  pthread_t juan= crearAleatorio(s, print_msg, "juan", 3);
  pthread_t diego= crearAleatorio(s, print_msg, "diego", 4);
  pthread_t pepe= crearAleatorio(s, print_msg, "pepe", 2);
  if (esperar(pedro))
    fatalError("test1", "pedro debio perder con 1\n");
  if (esperar(pepe))
    fatalError("test1", "pepe debio perder con 2\n");
  int u;
  int recaud= adjudicar(s, &u);
  if (recaud!=7)
    fatalError("test1", "La recaudacion debio ser 7 y no %d\n", recaud);
  if (u!=0)
    fatalError("test1", "Quedaron %d unidades sin vender\n", u);
  if (!esperar(juan))
    fatalError("nMain", "juan debio ganar con 3\n");
  if (!esperar(diego))
    fatalError("nMain", "diego debio ganar con 4\n");
  if (print_msg>0)
    printf("El monto recaudado es %d y quedaron %d unidades sin vender\n",
            recaud, u);
  destruirSubasta(s);
  return 0;
}

static int test2(int print_msg) {
  Subasta s= nuevaSubasta(3);
  pthread_t ana= crearOferente(s, print_msg, "ana", 7);
  if (print_msg>=0)
    msleep(1000);
  pthread_t maria= crearOferente(s, print_msg, "maria", 3);
  if (print_msg>=0)
    msleep(1000);
  pthread_t ximena= crearOferente(s, print_msg, "ximena", 4);
  if (print_msg>=0)
    msleep(1000);
  pthread_t erika= crearOferente(s, print_msg, "erika", 5);
  if (print_msg>=0)
    msleep(1000);
  if (esperar(maria))
    fatalError("nMain", "maria debio perder con 3\n");
  pthread_t sonia= crearOferente(s, print_msg, "sonia", 6);
  if (print_msg>=0)
    msleep(1000);
  if (esperar(ximena))
    fatalError("nMain", "ximena debio perder con 4\n");
  int u;
  int recaud= adjudicar(s, &u);
  if (recaud!=18)
    fatalError("test1", "La recaudacion debio ser 7 y no %d\n", recaud);
  if (u!=0)
    fatalError("test1", "Quedaron %d unidades sin vender\n", u);
  if (!esperar(ana))
    fatalError("nMain", "ana debio ganar con 7\n");
  if (!esperar(erika))
    fatalError("nMain", "erika debio ganar con 5\n");
  if (!esperar(sonia))
    fatalError("nMain", "sonia debio ganar con 6\n");
  if (print_msg>0)
    printf("El monto recaudado es %d y quedaron %d unidades sin vender\n",
            recaud, u);
  destruirSubasta(s);
  return 0;
}

static int test3(int print_msg) {
  Subasta s= nuevaSubasta(5);
  pthread_t tomas= crearOferente(s, print_msg, "tomas", 2);
  if (print_msg>=0)
    msleep(1000);
  pthread_t monica= crearOferente(s, print_msg, "monica", 3);
  if (print_msg>=0)
    msleep(1000);
  int u;
  int recaud= adjudicar(s, &u);
  if (recaud!=5)
    fatalError("test3", "La recaudacion debio ser 5 y no %d\n", recaud);
  if (u!=3)
    fatalError("test3", "Quedaron %d unidades sin vender\n", u);
  if (print_msg>0)
    printf("El monto recaudado es %d y quedaron %d unidades sin vender\n",
            recaud, u);
  if (!esperar(tomas))
    fatalError("nMain", "tomas debio ganar con 2\n");
  if (!esperar(monica))
    fatalError("nMain", "monica debio ganar con 3\n");
  destruirSubasta(s);
  return 0;
}


#define N 30

#ifndef OPT
#define M 150
#else
#define M 5000
#endif

int main() {
  printf("una sola subasta con tiempos aleatorios\n");
  test1(1);
  printf("test aprobado\n");
  printf("-------------\n");

  printf("una sola subasta con tiempos deterministicos\n");
  test2(1);
  printf("test aprobado\n");
  printf("-------------\n");

  printf("una sola subasta con menos oferentes que unidades disponibles\n");
  test3(1);
  printf("test aprobado\n");
  printf("-------------\n");

  // nSetTimeSlice(1);
  printf("%d subastas en paralelo\n", N);
  pthread_t *tasks1= malloc(M*sizeof(pthread_t));
  pthread_t *tasks2= malloc(M*sizeof(pthread_t));
  pthread_t *tasks3= malloc(M*sizeof(pthread_t));
  for (int k=1; k<N; k++) {
    tasks1[k]= crearTest(test1, 0);
    tasks2[k]= crearTest(test2, 0);
    tasks3[k]= crearTest(test3, 0);
  }
  tasks1[0]= crearTest(test1, 1);
  tasks2[0]= crearTest(test2, 1);
  tasks3[0]= crearTest(test3, 1);
  for (int k=0; k<N; k++) {
    pthread_join(tasks1[k], NULL);
    pthread_join(tasks2[k], NULL);
    pthread_join(tasks3[k], NULL);
  }
  printf("test aprobado\n");
  printf("-------------\n");
  printf("%d subastas en paralelo\n", M*2);
  for (int k=1; k<M; k++) {
    tasks1[k]= crearTest(test1, -1);
    tasks2[k]= crearTest(test2, -1);
  }
  tasks1[0]= crearTest(test1, 1);
  tasks2[0]= crearTest(test2, 1);
  pthread_join(tasks1[0], NULL);
  pthread_join(tasks2[0], NULL);
  printf("Enterrando tareas.  Cada '.' son 30 tareas enterradas.\n");
  for (int k=1; k<M; k++) {
    pthread_join(tasks1[k], NULL);
    pthread_join(tasks2[k], NULL);
    if (k%10==0) printf(".");
  }
  free(tasks1);
  free(tasks2);
  free(tasks3);
  printf("\ntest aprobado\n");
  printf(  "-------------\n");
  printf("Felicitaciones: paso todos los tests\n");
  return 0;
}
