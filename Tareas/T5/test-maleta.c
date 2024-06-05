#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <math.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include <nthread.h>

#include "maleta.h"

#define N_INTENTOS 4
#define TOLERANCIA 1.5

// ----------------------------------------------------
// Funcion que entrega el tiempo transcurrido desde el lanzamiento del
// programa en milisegundos

long long getUSecsOfDay() {
    struct timeval Timeval;
    gettimeofday(&Timeval, NULL);
    return (long long)Timeval.tv_sec*1000000+Timeval.tv_usec;
}

static int time0= 0;

static int getTime0() {
    return getUSecsOfDay()/1000;
}

void resetTime() {
  time0= getTime0();
}

int getTime() {
  return getTime0()-time0;
}

// ----------------------------------------------------
// Generacion de numeros aleatorios
//
// Es correcto usar la funcion estandar random, que genera buenos
// numeros aleatorios.  Pero es ineficiente.  La funcion random pide un
// mutex para evitar dataraces.  Pero este problema invoca random
// demasiado frecuentemente, provocando asi tanta contencion en la solucion
// paralela que hasta se podria demorar mas que la secuencial.
// Por eso tuve que programar mi propia version de la generacion de
// numeros aleatorios, que no sea compartida por los distintos threads.
// Es decir cada thread genera su propia secuencia de numeros.  El
// estado necesario para la generacion de los numeros se almacena en la
// estructura RandGen.  Cada thread maneja su propia instancia de RandGen
// para evitar dataraces.  Aun asi la semilla inicial para cada instancia
// de RandGen se genera con random.

#define MAXBITS 31

// Sacado de: http://en.wikipedia.org/wiki/Multiply-with-carry

#define PHI 0x9e3779b9

typedef struct {
  uint32_t Q[4096], c; // Para rand_cmwc
  int32_t cur;         // Para random0or1
  int bit;
} RandGen;

void init_rand(RandGen *gen) {
    uint32_t x= random();
    int i;

    gen->c= 362436;
    gen->Q[0] = x;
    gen->Q[1] = x + PHI;
    gen->Q[2] = x + PHI + PHI;

    for (i = 3; i < 4096; i++)
            gen->Q[i] = gen->Q[i - 3] ^ gen->Q[i - 2] ^ PHI ^ i;
}

uint32_t rand_cmwc(RandGen *gen) {
    uint64_t t, a = 18782LL;
    uint32_t i = 4095;
    uint32_t x, r = 0xfffffffe;
    i = (i + 1) & 4095;
    t = a * gen->Q[i] + gen->c;
    gen->c = (t >> 32);
    x = t + gen->c;
    if (x < gen->c) {
            x++;
            gen->c++;
    }
    return (gen->Q[i] = r - x);
}

RandGen *makeRandGen() {
  RandGen *gen= malloc(sizeof(RandGen));
  init_rand(gen);
  gen->bit= MAXBITS;
  return gen;
}

void destroyRandGen(RandGen *gen) {
  free(gen);
}

int random0or1(RandGen *gen) {
  // Codigo copiado de srand, pero sin soporte para threads
  if (gen->bit>=MAXBITS) {
    gen->cur = rand_cmwc(gen) & 0x7fffffff;
  }
  int res= gen->cur & 1;
  gen->cur >>= 1;
  gen->bit++;
  return res;
}

// ----------------------------------------------------

double llenarMaletaSec(double w[], double v[], int z[], int n,
                    double maxW, int k) {
  RandGen *gen= makeRandGen();
  double best= -1;
  while (k--) {
    int x[n];
    double sumW= 0, sumV= 0;
    for (int i=0; i<n; i++) {
      x[i]= (random0or1(gen) && sumW+w[i]<=maxW) ? 1 : 0;
      if (x[i]==1) {
        sumW += w[i];
        sumV += v[i];
    } }
    if (sumV>best) {
      best= sumV;
      for(int i=0; i<n; i++) {
        z[i]= x[i];
  } } }
  destroyRandGen(gen);
  return best;
}

int contarArticulos(int z[], int n) {
  int cnt= 0;
  for (int j=0; j<n; j++) {
    if (z[j])
      cnt++;
  }
  return cnt;
}

int main() {

  printf("Test 1: Un ejemplo pequeño\n");
  {
    int n= 5;
    double maxW= 10;
    int k= 8000000;
    double w[]= {5, 4, 2, 4, 5};
    double v[]= {100, 110, 200, 150, 180};
    int zsec[n], zpar[n];
    printf("Calculando secuencialmente\n");
    double bestsec= llenarMaletaSec(w, v, zsec, n, maxW, k);
    printf("k= %d  numero de articulos seleccionados= %d  valor= %g\n",
           k, contarArticulos(zsec, n), bestsec);
    printf("Calculando en paralelo\n");
    double bestpar= llenarMaletaPar(w, v, zpar, n, maxW, k);
    printf("k= %d  numero de articulos seleccionados= %d  valor= %g\n",
           k, contarArticulos(zpar, n), bestpar);
    if (bestsec!=bestpar) {
      fprintf(stderr, "La solucion debio ser %g, no %g\n", bestsec, bestpar);
      exit(1);
    }
    printf("Test 1 aprobado\n");
    printf("\n--------------------------------------------------\n\n");
  }

  printf("Test 2: Uno ejemplo grande con n=32\n");
  {
    srandom(getUSecsOfDay());
    int n= 32;
    double maxW= 300;
    int k= 10000000;
    double w[]= { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };

//    double v[]= {32.1, 31.5, 16.0, 27.8, 28.3, 29.7, 30.2, 25.3, 24.9, 23.6, 22.5,
//                 21.0, 20.6, 19.4, 15.9, 17.3, 16.8, 18.1, 14.7, 13.5, 9.2, 8.8,
//                 10.3, 12.1, 11.7, 7.3, 6.1, 5.9, 4.2, 3.6, 2.8, 1.7};

    double v[]= {32.1, 31.5, 30.0, 29.8, 28.3, 27.7, 26.2, 25.3, 24.9, 23.6, 22.5,
                 21.0, 20.6, 19.4, 18.9, 17.3, 16.8, 15.1, 14.7, 13.5, 12.2, 11.8,
                 10.3, 9.1, 8.7, 7.3, 6.1, 5.9, 4.2, 3.6, 2.8, 1.7};
 
    int zsec[n], zpar[n];

    double bestsec, bestpar;

    int tiempo_sec, tiempo_par;
    double speedUp= 0;
    int i;
    printf("Precalentando secuencialmente\n");
    bestsec= llenarMaletaSec(w, v, zsec, n, maxW, k/100);
    printf("k= %d  numero de articulos seleccionados= %d  valor= %g\n",
            k/100, contarArticulos(zsec, n), bestsec);
 
    printf("Calculando secuencialmente con k= %d combinaciones\n", k);
    resetTime();
    bestsec= llenarMaletaSec(w, v, zsec, n, maxW, k);
    tiempo_sec= getTime();
    printf("Tiempo= %d, numero de articulos seleccionados= %d  valor= %g\n",
           tiempo_sec, contarArticulos(zsec, n), bestsec);
 
    for (i= 1; i<=5; i++) {
      printf("\n-----\n");
      printf("Calculando en paralelo con k= %d combinaciones, intento %d\n", k, i);
      resetTime();
      bestpar= llenarMaletaPar(w, v, zpar, n, maxW, k);
      tiempo_par= getTime();
      speedUp= (double)tiempo_sec/tiempo_par;
      printf("Tiempo= %d  speedup= %g  numero de articulos seleccionados= %d  valor= %g\n",
           tiempo_par, speedUp, contarArticulos(zpar, n), bestpar);
 
      double delta= bestpar>bestsec ? bestpar-bestsec : bestsec-bestpar;
      if (delta>20) {
        fprintf(stderr, "La solucion debio rondar %g, no %g\n", bestsec, bestpar);
        exit(1);
      }
      double best= 0;
      for (int j=0; j<n; j++)  {
        if (zpar[j])
          best += v[j];
      }
      if (best!=bestpar) {
        fprintf(stderr, "El valor entregado %g no coincide con el calculado %g\n",
                bestpar, best);
        exit(1);
      }
      if (speedUp>=TOLERANCIA)
        break;
    }
    if (i>=N_INTENTOS) {
      fprintf(stderr, "Despues de %d intentos no obtuvo un speedup de %gx\n",
              N_INTENTOS, TOLERANCIA);
      fprintf(stderr, "Revise la paralelizacion.\n");
      exit(1);
    }
    printf("Test 2 aprobado: speedup >= %gx\n\n", TOLERANCIA);
  }

  printf("Felicitaciones: su tarea funciona\n");
  
  return 0;
}
