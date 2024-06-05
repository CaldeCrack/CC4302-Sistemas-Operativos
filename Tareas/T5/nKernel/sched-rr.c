#define _GNU_SOURCE 1

#include "nthread-impl.h"
#include <sys/resource.h>

static void nth_setCoreTimerAlarm(long long sliceNanos, int coreId);
static long long nth_getCoreNanos();

long long nth_sliceNanos= 0; // Duration of the time slice before preempting
static timer_t *nth_timerSet;

static __thread clockid_t nth_clockid;

static NthQueue *nth_rrReadyQueue;

static void nth_VTimerHandler(int sig, siginfo_t *si, void *uc);


void nth_rrInit(void) {
  nth_timerSet= malloc(nth_totalCores*sizeof(*nth_timerSet));
}

void nth_rrThreadInit(void) {
  if (pthread_getcpuclockid(pthread_self(), &nth_clockid)!=0) {
    perror("pthread_getcpuclockid");
    nFatalError("nth_rrThreadInit", "Cannot continue\n");
  }
  
  struct sigaction sigact;
  sigact.sa_flags= SA_SIGINFO;
  sigact.sa_sigaction= nth_VTimerHandler;
  sigact.sa_mask= nth_sigsetCritical;

  if (sigaction(SIGVTALRM, &sigact, NULL)!=0) {
    perror("sigaction");
    nFatalError("nth_rrThreadInit", "Cannot continue\n");
  }
    
  struct sigevent sigev;
  sigev.sigev_notify= SIGEV_THREAD_ID;
  sigev._sigev_un._tid= gettid();
  sigev.sigev_signo= SIGVTALRM;
  sigev.sigev_value.sival_ptr= &nth_timerSet[nth_coreId()];
  if ( timer_create(CLOCK_THREAD_CPUTIME_ID,
                    &sigev, &nth_timerSet[nth_coreId()])!=0 ) {
    perror("timer_create");
    nFatalError("nth_rrThreadInit", "Cannot continue\n");
  }
}

static long long nth_getCoreNanos() {
  struct timespec ts;
  if (clock_gettime(nth_clockid, &ts)!=0) {
    perror("clock_gettime");
    nFatalError("nth_getCoreNanos", "Cannot continue\n");
  }
  return (long long)ts.tv_sec*1000000000+ts.tv_nsec;
}

static void nth_VTimerHandler(int sig, siginfo_t *si, void *uc) {
  nThread th= nth_selfCritical();
  if (th==NULL)
    return; // It has been called for nth_coreFun()
  
  START_HANDLER
# ifdef TRACESCHED
    printk("SCHED %d:SIGVTALRM\n", nth_coreId());
# endif  

  th->sliceNanos= 0;
  if (!nth_emptyQueue(nth_rrReadyQueue))
    nth_implicitContextChanges++;

  // If this core is waiting in sigsuspend in nth_rrSchedule,
  // do not call recursively nth_rrSchedule

  if ( !nth_coreIsIdle[nth_coreId()] ) {
    schedule();
  }
  
  END_HANDLER
}

static void nth_rrSetReady(nThread th) {
  CHECK_CRITICAL("nth_rrSetReady")
  
  assert(th->status!=READY && th->status!=RUN && th->timequeue==NULL);
  
  int prevStatus= th->status;
  (void)prevStatus;

  th->status= READY;
  if (nth_allocCoreId(th)<0) {
    if (th->sliceNanos>0) {               // it has some slice yet
      nth_putFront(nth_rrReadyQueue, th);
    }
    else {                                // it exhausted its slice
      th->sliceNanos= nth_sliceNanos;     // give it a whole new slice and
      nth_putBack(nth_rrReadyQueue, th);  // put it at the end of the queue
    }
#   ifdef TRACESCHED
      printk( "SCHED %d:setReady %s %s %d us\n", nth_coreId(),
              th->name, nth_stateNames[prevStatus], th->sliceNanos/1000 );
#   endif
  }
  else if (nth_allocCoreId(th)!=nth_coreId()) {
    // Thread th is allocated to nth_allocCoreId(th), wake it up
#   ifdef TRACESCHED
      printk( "SCHED %d:setReady %s %s %d us alloc at %d\n", nth_coreId(),
               th->name, nth_stateNames[prevStatus], th->sliceNanos/1000,
               nth_allocCoreId(th) );
#   endif
    // Not needed: nth_delQueue(nth_rrReadyQueue, th);
    nth_coreWakeUp(nth_allocCoreId(th));
  }
# ifdef TRACESCHED
  else {
    printk("SCHED %d:setReady %s same core\n", nth_coreId(), th->name);
  }
# endif
}

static void nth_rrSuspend(State waitState) {
  CHECK_CRITICAL("nth_rrSuspend")

  nThread th= nSelf();
# ifdef TRACESCHED
    printk( "SCHED %d:suspend(%s,%s)\n",
            nth_coreId(), th->name, nth_stateNames[th->status] );
# endif
  // if (th->status==READY)
  //   nth_delQueue(nth_rrReadyQueue, th);
  assert(th->status==RUN);

  th->status= waitState;
}

static void nth_rrSchedule(void) {
  CHECK_CRITICAL("nth_rrSchedule")

  // int prevCoreId= coreId();

  nThread thisTh= nSelf();

  if (thisTh!=NULL) {
    long long endNanos= nth_getCoreNanos();
    thisTh->sliceNanos -= endNanos-thisTh->startCoreNanos;
  }
  // thisTh->sliceNanos can be negative if the slice expired when
  // signals were disabled

  for (;;) {
    if (thisTh!=NULL && (thisTh->status==READY || thisTh->status==RUN)) {
      if (nth_queryThread(nth_rrReadyQueue, thisTh))
        nFatalError("nth_rrSchedule", "Thread should not be in ready queue\n");
      if (thisTh->sliceNanos>0) {
#       ifdef TRACESCHED
          printk( "SCHED %d:sched %s %s %d us\n", nth_coreId(),
                  thisTh->name, nth_stateNames[thisTh->status],
                  thisTh->sliceNanos/1000 );
#       endif
        break; // Continue running same allocated thread
      }
      else {
        thisTh->sliceNanos= nth_sliceNanos;
        if (nth_emptyQueue(nth_rrReadyQueue)) {
#         ifdef TRACESCHED
            printk( "SCHED %d:sched %s %s %d us\n", nth_coreId(),
                    thisTh->name, nth_stateNames[thisTh->status],
                    thisTh->sliceNanos/1000 );
#         endif
          break;
        }
#       ifdef TRACESCHED
          printk( "SCHED %d:sched %s %s %d us\n", nth_coreId(),
                  thisTh->name, nth_stateNames[thisTh->status],
                  thisTh->sliceNanos/1000 );
#       endif
        thisTh->status= READY;
        nth_putBack(nth_rrReadyQueue, thisTh);
      }
    }
 
    nThread nextTh= nth_getFront(nth_rrReadyQueue);
    if (nextTh!=NULL) {
      if (nth_allocCoreId(nextTh) != -1)
        nFatalError("nth_rrSchedule", "Thread should not be in ready queue\n");

      // The context change: give this core to nextTh
      // it will take a while to return from here
      // Meanwhile thread nextTh and others are being executed
      
#     ifdef TRACESCHED
        printk( "SCHED %d:switch %s %s %d us -> %s %s %d us\n", nth_coreId(),
                thisTh!=NULL ? thisTh->name : NULL,
                thisTh!=NULL ? nth_stateNames[thisTh->status] : NULL,
                thisTh!=NULL ? thisTh->sliceNanos/1000 : 0,
                nextTh->name, nth_stateNames[nextTh->status],
                nextTh->sliceNanos/1000 );
#     endif

      assert( thisTh==NULL || thisTh->status!=READY ||
              nth_queryThread(nth_rrReadyQueue, thisTh) );
      nth_changeContext(thisTh, nextTh);

      // Some time later, at return the scheduler gave back onother core
      // to thisTh so probably previous core != next core
      if (thisTh->status==READY)
        break;
    }
    
    if (thisTh!=nSelf())
      nFatalError("nth_rrSchedule", "nSelf() inconsistency\n");

    // No threads to execute: the core will be parked

    nth_corePark();
  }

  if (thisTh!=nSelf())
    nFatalError("nth_rrSchedule", "nSelf() inconsistency\n");

  CHECK_STACK
  if (thisTh->status!=READY && thisTh->status!=RUN)
    nFatalError("nth_rrSchedule", "Thread is not ready to run\n");
  thisTh->status= RUN;
  thisTh->startCoreNanos= nth_getCoreNanos();

  if (!nth_emptyQueue(nth_rrReadyQueue))
    nth_reviewCores();
  nth_setCoreTimerAlarm(thisTh->sliceNanos, nth_coreId());
}

static void nth_setCoreTimerAlarm(long long sliceNanos, int coreId) {
  struct itimerspec slicespec;
  slicespec.it_value.tv_sec= sliceNanos/1000000000;
  slicespec.it_value.tv_nsec= sliceNanos%1000000000;
  slicespec.it_interval.tv_sec= 0;
  slicespec.it_interval.tv_nsec= 0;
  int rc= timer_settime(nth_timerSet[coreId], 0, &slicespec, NULL);
  if (rc!=0) {
    if (rc==-1)
      perror("timer_settime");
    nFatalError("timer_settime", "Failed to set time alarm, cannot continue\n");
  }
}

static void nth_rrStop(void) {
  CHECK_CRITICAL("nth_rrStop")
  
  // Disarm all timers
  int ncores= nth_totalCores;
  for (int i= 0; i<ncores; i++) {
    nth_setCoreTimerAlarm(0, i);
  }
  
  nth_destroyQueue(nth_rrReadyQueue);
}

Scheduler nth_rrScheduler= { .schedule = nth_rrSchedule,
                               .setReady = nth_rrSetReady,
                               .suspend = nth_rrSuspend,
                               .stop = nth_rrStop };
                               
void nSetTimeSlice(long long sliceNanos) {
  START_CRITICAL
  
  if (nth_verbose)
    printf("Info: setting %d-core round robin scheduling\n", nth_totalCores);
  
  nth_sliceNanos= sliceNanos;

  if (!isRRScheduling()) {
    nth_rrReadyQueue= nth_makeQueue();
    nth_setScheduler(nth_rrScheduler);
    MapIterator *iter= getMapIterator(nth_threadSet);
    void *ptr;
    while (mapNext(iter, &ptr, &ptr)) {
      nThread th= ptr;
      if (th->status==READY) {
        th->sliceNanos= sliceNanos;
        nth_putBack(nth_rrReadyQueue, th);
      }
      else if (th->status==RUN) {
        int coreId= th->allocCoreId;
        th->sliceNanos= sliceNanos;
        // Arm timer for thread th
        nth_setCoreTimerAlarm(sliceNanos, coreId);
      }
    }
    destroyMapIterator(iter);
  }
  else {
    MapIterator *iter= getMapIterator(nth_threadSet);
    void *ptr;
    while (mapNext(iter, &ptr, &ptr)) {
      nThread th= ptr;
      if (th->status==READY) {
        th->sliceNanos= nth_sliceNanos;
      }
      else if (th->status==RUN) {
        int coreId= th->allocCoreId;
        // Arm timer for thread th
        nth_setCoreTimerAlarm(sliceNanos, coreId);
      }
    }
    destroyMapIterator(iter);
  }
  
  END_CRITICAL
}

void allocThreadRR(nThread th, int coreId) {
  START_CRITICAL
  
  if (! (0<=coreId && coreId<nth_totalCores))
    printf("The core %d does not exist\n", coreId);
  int id= nth_allocCoreId(th);
  if (id>=0)
    printf("The thread is already allocated at core %d\n", id);
  if (!nth_coreIsIdle[coreId])
    printf("Core %d is not idle\n", coreId);
  if (nth_queryThread(nth_rrReadyQueue, th))
    nth_delQueue(nth_rrReadyQueue, th);
  nth_putFront(nth_rrReadyQueue, th);
  nth_coreWakeUp(coreId);
  END_CRITICAL
}

int isRRScheduling(void) {
  return nth_scheduler.schedule==nth_rrScheduler.schedule;
}
