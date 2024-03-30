#define _XOPEN_SOURCE 500

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "maleta.h"

typedef struct {
    double *w, *v;
    int *z, n;
    double maxW;
    int k;
    double best;
} Args;

void *thread_function(void *p) {
    Args *arg = (Args *)p;
    arg->best = llenarMaletaSec(arg->w, arg->v, arg->z, arg->n, arg->maxW, arg->k);
    return NULL;
}

double llenarMaletaPar(double w[], double v[], int z[], int n, double maxW, int k) {
    pthread_t pid[8];
    Args args[8];
    
    for (int i = 0; i < 8; i++) {
        int *z_copy = malloc(sizeof(int) * n);
        args[i] = (Args){w, v, z_copy, n, maxW, k/8};
        pthread_create(&pid[i], NULL, thread_function, &args[i]);
    }

    double best = -1;
    for (int i = 0; i < 8; i++) {
        pthread_join(pid[i], NULL);
        if (args[i].best > best) {
            for (int j = 0; j < n; j++)
                z[j] = args[i].z[j];
            best = args[i].best;
        }
        free(args[i].z);
    }
    return best;
}
