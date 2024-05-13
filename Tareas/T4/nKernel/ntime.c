#include "nthread-impl.h"

#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>

static void nth_RTimerHandler(int sig, siginfo_t *si, void *uc);
static void nth_wakeThreads(void);

static NthTimeQueue *nth_timeQueue;
static long long nth_iniTime= 0;
static long long nth_last_time= 0;
static long long nth_last_coreId= 0;
static timer_t nth_realTimer;

/*************************************************************
 * Prolog and epilog
 *************************************************************/

void nth_timeInit(void) {

  struct timespec ts;
  int rc= clock_gettime(CLOCK_REALTIME, &ts);
  if (rc!=0) {
    if (rc==-1)
      perror("clock_gettime");
    nFatalError("clock_gettime", "Failed to get time, cannot continue\n");
  }
  nth_iniTime= (long long)ts.tv_sec*1000000000LL+ts.tv_nsec;

  nth_timeQueue= nth_makeTimeQueue();

  struct sigaction sigact;
  sigact.sa_flags= SA_SIGINFO;
  sigact.sa_sigaction= nth_RTimerHandler;
  sigact.sa_mask= nth_sigsetCritical;

  if (sigaction(SIGALRM, &sigact, NULL)!=0) {
    perror("sigaction");
    nFatalError("nth_timeInit", "Failed to register signal, cannot continue\n");
  }
    
  struct sigevent sigev;
  sigev.sigev_notify= SIGEV_SIGNAL;
  sigev.sigev_signo= SIGALRM;
  sigev.sigev_value.sival_ptr= &nth_realTimer;
  if ( timer_create(CLOCK_REALTIME,
                    &sigev, &nth_realTimer)!=0 ) {
    perror("timer_create");
    nFatalError("nth_timeInit", "Failed to create timer, cannot continue\n");
  }
}

void nth_timeEnd(void) {
  if (!nth_emptyTimeQueue(nth_timeQueue))
    nFPrintf(stderr, "*** There are pending threads in the time queue\n");
}

/*************************************************************
 * Timeout management
 *************************************************************/

long long nGetTimeNanos(void) {
  START_CRITICAL
  struct timespec ts;
  int rc= clock_gettime(CLOCK_REALTIME, &ts);
  if (rc!=0) {
    if (rc==-1)
      perror("clock_gettime");
    nFatalError("clock_gettime", "Failed to get time, cannot continue\n");
  }
  long long curr_time= (long long)ts.tv_sec*1000000000LL+ts.tv_nsec-nth_iniTime;
  if (curr_time<nth_last_time)
#if 0
    nFatalError("nGetTimeNanos", "Previous time newer than current time "
                "(delta=%lld cores= %d %d)\n", curr_time-nth_last_time,
                 nth_last_coreId, nth_coreId());
#else
     curr_time= nth_last_time+1;
#endif
  nth_last_time= curr_time;
  nth_last_coreId= nth_coreId();
  END_CRITICAL
  return curr_time;
}

int nGetTime(void) {
  return nGetTimeNanos()/1000000LL;
}

static void nth_setRealTimerAlarm(long long nanos) {
  struct itimerspec spec;
  spec.it_value.tv_sec= nanos/1000000000;
  spec.it_value.tv_nsec= nanos%1000000000;
  spec.it_interval.tv_sec= 0;
  spec.it_interval.tv_nsec= 0;
  int rc= timer_settime(nth_realTimer, 0, &spec, NULL);
  if (rc!=0) {
    if (rc==-1)
      perror("timer_settime");
    nFatalError("timer_settime", "Failed to set time alarm, cannot continue\n");
  }
  nth_alarmArmed= nanos==0 ? 0 : 1;
}

void nth_programTimer(long long nanos, void (*wakeUpFun)(nThread th)) {
  CHECK_CRITICAL("nth_programTimer")
  nThread thisTh= nSelf();
  if (nanos<=0) {
#   ifdef TRACESCHED
      printk( "SCHED %d:timer not programmed %s %s\n", nth_coreId(),
              thisTh->name, nth_stateNames[thisTh->status]);
#   endif
    setReady(thisTh);
  }
  else {
    long long currTime= nGetTimeNanos();
    long long wakeTime= currTime+nanos;
    if ( nth_emptyTimeQueue(nth_timeQueue) ||
         wakeTime-nth_nextTime(nth_timeQueue)<0 ) {
#     ifdef TRACESCHED
        printk( "SCHED %d:timer set %s %s after %lld us at %lld us\n",
                nth_coreId(),
                thisTh->name, nth_stateNames[thisTh->status],
                (wakeTime-currTime)/1000, currTime/1000 );
#     endif
      nth_setRealTimerAlarm(wakeTime-currTime);
    }
#   ifdef TRACESCHED
    else {
      printk( "SCHED %d:timer programmed %s %s after %lld us at %lld us\n",
              nth_coreId(),
              thisTh->name, nth_stateNames[thisTh->status],
              (wakeTime-currTime)/1000, currTime/1000 );
    }
#   endif
    thisTh->wakeUpFun= wakeUpFun;
    nth_putTimed(nth_timeQueue, thisTh, wakeTime);
  }
}

void nth_cancelThread(nThread th) {
  CHECK_CRITICAL("nth_cancelTask")
# ifdef TRACESCHED
    long long currTime= nGetTimeNanos();
    nThread thisTh= nth_selfCritical();
    printk( "SCHED %d:%s cancels %s %s at %lld us\n", nth_coreId(),
            thisTh->name, th->name, nth_stateNames[th->status],
            currTime/1000 );
# endif
  nth_delTimed(nth_timeQueue, th);
  nth_wakeThreads();
}

static void nth_wakeThreads(void) {
  long long currTime= nGetTimeNanos();
  // Wake up all threads with wake time <= currTime
  while ( !nth_emptyTimeQueue(nth_timeQueue) &&
          nth_nextTime(nth_timeQueue)-currTime<=0 ) {
    nThread th= nth_getTimed(nth_timeQueue);
    if (th->wakeTime-currTime>0)
      nFatalError("nth_wakeThreads", "Inconsistent wake time");
#   ifdef TRACESCHED
      nThread thisTh= nth_selfCritical();
      printk( "SCHED %d: %s wakes up %s %s at %lld us ", nth_coreId(),
              thisTh->name, th->name, nth_stateNames[th->status],
              currTime/1000 );
#   endif
    if (th->wakeUpFun!=NULL)
      (*th->wakeUpFun)(th);
    setReady(th);
  }
  
  if (nth_emptyTimeQueue(nth_timeQueue)) {
    nth_setRealTimerAlarm(0);
#   ifdef TRACESCHED
      printk("timer unarmed\n");
#   endif
  }
  else {
#   ifdef TRACESCHED
      printk( "timer armed after %lld us\n",
              (nth_nextTime(nth_timeQueue)-currTime)/1000 );
#   endif
    nth_setRealTimerAlarm(nth_nextTime(nth_timeQueue)-currTime);
  }
}

static void nth_RTimerHandler(int sig, siginfo_t *si, void *uc) {
  // If this core is waiting in nth_rrSchedule, a return to nth_rrSchedule
  // is mandatory because calling recursively nth_rrSchedule is unsafe
    
  START_HANDLER

#   ifdef TRACESCHED
      printk( "SCHED %d:SIGVTALRM %s\n", nth_coreId(),
              nth_coreIsIdle[nth_coreId()] ? "idle" : "busy" );
#   endif
   
  nth_wakeThreads();
  // Buggy: if (! nth_coreIsIdle[nth_coreId()] && th!=NULL)
  if (! nth_coreIsIdle[nth_coreId()])
    schedule();
  
  END_HANDLER
}

/*************************************************************
 * nSleepNanos
 *************************************************************/

int nSleepNanos(long long nanos) {
  START_CRITICAL

  suspend(WAIT_SLEEP);
  nth_programTimer(nanos, NULL);
  schedule();

  END_CRITICAL
  return 0;
}

int nSleepMillis(long long millis) {
  nSleepNanos(millis*1000000LL);
  return 0;
}

int nSleepMicros(long long micros) {
  nSleepNanos(micros*1000LL);
  return 0;
}

int nSleepSeconds(unsigned int seconds) {
  nSleepNanos(seconds*1000000000LL);
  return 0;
}
