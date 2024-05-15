#define _XOPEN_SOURCE 500

#include "nKernel/nthread-impl.h"
#include "rwlock.h"

int reading = 0;

struct rwlock {
    int writing;
    NthQueue *writers, *readers;
    State status;
};

nRWLock *nMakeRWLock() {
    NthQueue *r = nth_makeQueue(), *w = nth_makeQueue();
    nRWLock *rwl = nMalloc(sizeof(nRWLock));
    *rwl = (nRWLock) {0, r, w};
    return rwl;
}

void nDestroyRWLock(nRWLock *rwl) {
    free(rwl);
}

int nEnterRead(nRWLock *rwl, int timeout) {
    START_CRITICAL

    if(!rwl->writing && nth_emptyQueue(rwl->writers))
        reading++;
    else {
        nThread thisTh = nSelf();
        nth_putBack(rwl->readers, thisTh);
        suspend(WAIT_RWLOCK);
        schedule();
    }

    END_CRITICAL
    return 1;
}

int nEnterWrite(nRWLock *rwl, int timeout) {
    START_CRITICAL

    if(!rwl->writing && !reading)
        rwl->writing = 1;
    else {
        nThread thisTh = nSelf();
        nth_putBack(rwl->writers, thisTh);
        suspend(WAIT_RWLOCK);
        schedule();
    }

    END_CRITICAL
    return 1;
}

void nExitRead(nRWLock *rwl) {
    START_CRITICAL
    reading--;

    if(!reading && !nth_emptyQueue(rwl->writers)) {
        rwl->writing = 1;
        nThread th = nth_getFront(rwl->writers);
        setReady(th);
        schedule();
    }

    END_CRITICAL
}

void nExitWrite(nRWLock *rwl) {
    START_CRITICAL
    rwl->writing = 0;

    if(!nth_emptyQueue(rwl->readers)) {
        while(!nth_emptyQueue(rwl->readers)) {
            reading++;
            nThread th = nth_getFront(rwl->readers);
            setReady(th);
        }
        schedule();
    } else if(!nth_emptyQueue(rwl->writers)) {
        rwl->writing = 1;
        nThread th = nth_getFront(rwl->writers);
        setReady(th);
        schedule();
    }

    END_CRITICAL
}
