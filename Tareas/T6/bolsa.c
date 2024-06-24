#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "pss.h"
#include "bolsa.h"
#include "spinlocks.h"

// Declare aca sus variables globales
int mutex = OPEN;
int bestPrice = 0;
int *globalState, *globalWait;
char *globalVendedor, *globalComprador;
enum {RECHAZADO = 0, ADJUDICADO = 1, PENDIENTE = 2};

int vendo(int precio, char *vendedor, char *comprador) {
  spinLock(&mutex);
  int state = PENDIENTE;

  if(bestPrice && bestPrice <= precio)
    state = RECHAZADO;
  else {
    if(bestPrice) {
      *globalState = RECHAZADO;
      spinUnlock(globalWait);
    }
    int w = CLOSED;
    globalVendedor = vendedor;
    globalComprador = comprador;
    globalState = &state;
    globalWait = &w;
    bestPrice = precio;
    spinUnlock(&mutex);
    spinLock(&w);
    return state;
  }

  spinUnlock(&mutex);
  return state;
}

int compro(char *comprador, char *vendedor) {
  spinLock(&mutex);
  if(!bestPrice) {
    spinUnlock(&mutex);
    return 0;
  }
  int precio = bestPrice;

  strcpy(vendedor, globalVendedor);
  strcpy(globalComprador, comprador);

  *globalState = ADJUDICADO;
  spinUnlock(globalWait);

  bestPrice = 0;

  spinUnlock(&mutex);
  return precio;
}
