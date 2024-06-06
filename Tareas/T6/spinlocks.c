#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#include "pss.h"
#include "spinlocks.h"

int swapInt(volatile int *psl, int status); // en archivo swap.s
void storeInt(volatile int *psl, int status);

static pthread_mutex_t mtx= PTHREAD_MUTEX_INITIALIZER;
// static pthread_cond_t cond= PTHREAD_COND_INITIALIZER;
static Map *map= NULL;

static int busywaiting= 0;

int setBusyWaiting(int flag) {
  pthread_mutex_lock(&mtx);
  int old= busywaiting;
  busywaiting= flag;
  pthread_mutex_unlock(&mtx);
  return old;
}

void spinLock(volatile int *psl) {
  if (busywaiting) {
    // Implementacion de verdaderos spin-locks que esperan con busy-waiting
    do {
      while (*psl==CLOSED)
        ;
    } while (swapInt(psl, CLOSED)==CLOSED);
  }
  else {
    pthread_mutex_lock(&mtx);
    if (map==NULL)
       map= makeHashMap(10000, hash_ptr, pointer_equals);
    pthread_cond_t *pcond= query(map, (int*)psl);
    if (pcond==NULL) {
      pcond= malloc(sizeof(pthread_cond_t));
      pthread_cond_init(pcond, 0);
      define(map, (int *)psl, pcond);
    }
    while (*psl!=OPEN)
      pthread_cond_wait(pcond, &mtx);
    *psl= CLOSED;
    pthread_mutex_unlock(&mtx);
  }
}

void spinUnlock(int *psl) {
  if (busywaiting)
    swapInt(psl, OPEN);
  else {
    pthread_mutex_lock(&mtx);
    *psl= OPEN;
    if (map==NULL)
       map= makeHashMap(10000, hash_ptr, pointer_equals);
    pthread_cond_t *pcond= query(map, (int *)psl);
    if (pcond!=NULL)
      pthread_cond_broadcast(pcond);
    pthread_mutex_unlock(&mtx);
  }
}
