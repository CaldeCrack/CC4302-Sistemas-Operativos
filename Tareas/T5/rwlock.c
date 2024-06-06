#define _XOPEN_SOURCE 500

#include "nKernel/nthread-impl.h"
#include "rwlock.h"

struct rwlock {
    int writing, reading;
    NthQueue *writers, *readers;
    State status;
};

nRWLock *nMakeRWLock() {
    NthQueue *r = nth_makeQueue(), *w = nth_makeQueue();
    nRWLock *rwl = nMalloc(sizeof(nRWLock));
    *rwl = (nRWLock) {0, 0, r, w};
    return rwl;
}

void nDestroyRWLock(nRWLock *rwl) {
    free(rwl);
}

void f(nThread th) {
    nth_delQueue(th->ptr, th);
    th->ptr = NULL;
}

int nEnterRead(nRWLock *rwl, int timeout) {
    START_CRITICAL

    if(!rwl->writing && nth_emptyQueue(rwl->writers))
        rwl->reading++;
    else {
        nThread th = nSelf();
        nth_putBack(rwl->readers, th);
        suspend(WAIT_RWLOCK);
        schedule();
    }

    END_CRITICAL
    return 1;
}

int nEnterWrite(nRWLock *rwl, int timeout) {
    START_CRITICAL
    int expired = 1;
    nThread th = nSelf();

    if(!rwl->writing && !rwl->reading)
        rwl->writing = 1;
    else {
        nth_putBack(rwl->writers, th);
        if(timeout > 0) {
            th->ptr = rwl->writers;
            suspend(WAIT_RWLOCK_TIMEOUT);
            nth_programTimer(timeout * 1000000LL, f);
            schedule();
            if(th->ptr == NULL)
                expired = 0;
        } else {
            suspend(WAIT_RWLOCK);
            schedule();
        }
    }

    END_CRITICAL
    return expired;
}

void nExitRead(nRWLock *rwl) {
    START_CRITICAL
    rwl->reading--;

    if(!rwl->reading && !nth_emptyQueue(rwl->writers)) {
        rwl->writing = 1;
        nThread th = nth_getFront(rwl->writers);
        if(th->status == WAIT_RWLOCK_TIMEOUT)
            nth_cancelThread(th);
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
            rwl->reading++;
            nThread th = nth_getFront(rwl->readers);
            setReady(th);
        }
        schedule();
    } else if(!nth_emptyQueue(rwl->writers)) {
        rwl->writing = 1;
        nThread th = nth_getFront(rwl->writers);
        if(th->status == WAIT_RWLOCK_TIMEOUT)
            nth_cancelThread(th);
        setReady(th);
        schedule();
    }

    END_CRITICAL
}
