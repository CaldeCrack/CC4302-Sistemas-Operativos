#define _XOPEN_SOURCE 500

#include "nthread-impl.h"

int nMutexInit(nMutex *m, void *ptr) {
  nth_initQueue(&m->wq);
  m->ownerTh= NULL;
  m->cond= NULL; // For compatibility with nSystem
  return 0;
}

int nMutexDestroy(nMutex *m) {
  return 0;
}

int nCondInit(nCond *c, void *ptr) {
  nth_initQueue(&c->wq);
  c->m= NULL;
  return 0;
}

int nCondDestroy(nCond *c) {
  return 0;
}

void nLock(nMutex *m) {
  START_CRITICAL
  
  nThread thisTh= nSelf();
  if (m->ownerTh==NULL && nth_emptyQueue(&m->wq)) {
    m->ownerTh= thisTh;
  }
  else {
    nth_putBack(&m->wq, thisTh);
    suspend(WAIT_LOCK);
    schedule();
  }
  
  END_CRITICAL
}

void nUnlock(nMutex *m) {
  START_CRITICAL
  
  if (m->ownerTh!=nSelf())
    nFatalError("nUnlock", "This thread does not own this mutex\n");
  if (nth_emptyQueue(&m->wq))
    m->ownerTh= NULL;
  else {
    nThread w= nth_getFront(&m->wq);
    m->ownerTh= w;
    setReady(w);
    schedule();
  }
  
  END_CRITICAL
}

void nCondWait(nCond *c, nMutex *m) {
  START_CRITICAL
  
  nThread thisTh= nSelf();
  DBG(
    if (m->ownerTh!=thisTh)
      nFatalError("nCondWait", "This thread does not own this mutex\n");
    if (c->m!=NULL && c->m!=m)
      nFatalError("nCondWait", "The mutex is not the same registered before\n");
  );
  c->m= m;
  nth_putBack(&c->wq, thisTh);
  suspend(WAIT_COND);
  if (nth_emptyQueue(&m->wq))
    m->ownerTh= NULL;
  else {
    nThread w= nth_getFront(&m->wq);
    m->ownerTh= w;
    setReady(w);
  }
  schedule();
  
  END_CRITICAL
}

void nCondSignal(nCond *c) {
  START_CRITICAL
  
  nThread w= nth_getFront(&c->wq);
  if (w!=NULL) {
    nMutex *m= c->m;
    if (m!=NULL) {
      if (m->ownerTh!=nSelf())
        nFatalError("nCondSignal", "This thread does not own this mutex\n");
      nth_putBack(&m->wq, w);
    }
  }
  
  END_CRITICAL
}

void nCondBroadcast(nCond *c) {
  START_CRITICAL
  
  // move all threads in c->wl to their mutex
  nMutex *m= c->m;
  if (m!=NULL) {
    if (m->ownerTh!=nSelf())
      nFatalError("nCondSignal", "Thread does not own this mutex\n");
    while (!nth_emptyQueue(&c->wq)) {
      nThread w= nth_getFront(&c->wq);
      nth_putBack(&m->wq, w);
    }
  }
  END_CRITICAL
}
