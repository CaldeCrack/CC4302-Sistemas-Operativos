#define _XOPEN_SOURCE 500

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "reservar.h"

// Defina aca las variables globales y funciones auxiliares que necesite
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int display = 0, ticket_dist = 0;
int parking[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

int available_parking(int k) {
    int cnt = 0;
    for(int i = 0; i < N_EST; i++) {
        if(parking[i] == 0)
            cnt++;
        else
            cnt = 0;
        if(cnt >= k)
            return 1;
    }
    return 0;
}

int parking_lot(int k) {
    int cnt = 0, pos = 0;
    for(int i = 0; i < N_EST; i++) {
        if(parking[i] == 0) {
            pos = MIN(pos, i);
            cnt++;
        }
        else {
            pos = 15;
            cnt = 0;
        }
        if(cnt >= k)
            return pos;
    }
    return pos;
}

void parking_usage(int k, int pos, int val) {
    for(int i = pos; i < pos + k; i++)
        parking[i] = val;
}

void initReservar() {}

void cleanReservar() {}

int reservar(int k) {
    pthread_mutex_lock(&m);
    int my_num = ticket_dist++;
    while(display != my_num || !available_parking(k))
        pthread_cond_wait(&cond, &m);
    int pos = parking_lot(k);
    parking_usage(k, pos, 1);
    display++;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&m);
    return pos;
}

void liberar(int e, int k) {
    pthread_mutex_lock(&m);
    parking_usage(k, e, 0);
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&m);
} 
