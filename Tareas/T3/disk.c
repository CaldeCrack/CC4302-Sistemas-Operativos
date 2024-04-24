#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#include "disk.h"
#include "pss.h"

typedef struct {
    int ready;
    pthread_cond_t c;
} Request;

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
PriQueue *mayores, *menores;
int busy = 0, actual_track = 0;

void iniDisk(void) {
    mayores = makePriQueue();
    menores = makePriQueue();
}

void cleanDisk(void) {
    destroyPriQueue(mayores);
    destroyPriQueue(menores);
}

void requestDisk(int track) {
    pthread_mutex_lock(&m);
    if(!busy) {
        busy = 1;
        actual_track = track;
    }
    else {
        Request req = {0, PTHREAD_COND_INITIALIZER};
        if (track >= actual_track)
            priPut(mayores, &req, track);
        else
            priPut(menores, &req, track);
        while(!req.ready)
            pthread_cond_wait(&req.c, &m);
    }
    pthread_mutex_unlock(&m);
}

void releaseDisk() {
    pthread_mutex_lock(&m);
    if (emptyPriQueue(mayores) && emptyPriQueue(menores)) {
        actual_track = 0;
        busy = 0;
    } else {
        if(emptyPriQueue(mayores) || (!emptyPriQueue(menores) && priBest(menores) < priBest(mayores) && priBest(menores) > actual_track)){
            actual_track = priBest(menores);
            Request *req = priGet(menores);
            req->ready = 1;
            pthread_cond_signal(&req->c);
        } else {
            actual_track = priBest(mayores);
            Request *req = priGet(mayores);
            req->ready = 1;
            pthread_cond_signal(&req->c);
        }
    }
    pthread_mutex_unlock(&m);
}
