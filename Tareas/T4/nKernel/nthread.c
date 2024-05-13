#include "nthread-impl.h"

#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <ucontext.h>

static void nth_systemEnd(void);
static void nth_systemInit(void);
static void nth_cleanBuriedThread(nThread *pth);

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

__thread int _pc= 0;

/*************************************************************
 * Scheduler management
 *************************************************************/

int nth_idleCores= 0;     // Number of idle virtual cores
int nth_totalCores= 1;    // Number of total virtual cores available
int nth_alarmArmed= 0;    // 1 if the realtime alarm is armed
Map *nth_threadSet= NULL; // A hash map keeping a set of all nthreads
int nth_threadCount;      // Number of living threads
int nth_zombieCount;      // Number of threads waiting a join
int nth_verbose= 1;
int nth_fcfs1= 0;
int nth_pri1= 0;
int nth_implicitContextChanges= 0;
int nth_contextChanges= 0;
pthread_t *nth_nativeCores;
int *nth_reviewStatus;

Scheduler nth_scheduler;

void nth_setScheduler(Scheduler scheduler) {
  CHECK_CRITICAL("nth_setScheduler")
  
  if (nth_scheduler.stop!=NULL)
    (*nth_scheduler.stop)();
  nth_scheduler= scheduler;
}

/*************************************************************
 * Critical section management
 *************************************************************/

static volatile int nth_schedStatus= -1;

#ifdef NTHSPINLOCKS
static pthread_spinlock_t nth_schedSpinLock;
#else
static pthread_mutex_t nth_schedMutex= PTHREAD_MUTEX_INITIALIZER;
#endif

void nth_schedLock(void) {
#ifdef NTHSPINLOCKS
  if (pthread_spin_lock(&nth_schedSpinLock)!=0) {
    perror("pthread_spin_lock");
    nFatalError("nth_schedLock", "Failed\n");
  }
#else
  if (pthread_mutex_lock(&nth_schedMutex)!=0) {
    perror("pthread_mutex_lock");
    nFatalError("nth_schedLock", "Failed\n");
  }
#endif

  assert(nth_schedStatus<0);
  nth_schedStatus= nth_thisCoreId;
}

void nth_schedUnlock(void) {
  assert(nth_schedStatus==nth_thisCoreId);
  nth_schedStatus= -1;
#ifdef NTHSPINLOCKS
  if (pthread_spin_unlock(&nth_schedSpinLock)!=0) {
    perror("pthread_spin_unlock");
    nFatalError("nth_schedUnlock", "Failed\n");
  }
#else
  if (pthread_mutex_unlock(&nth_schedMutex)!=0) {
    perror("pthread_mutex_unlock");
    nFatalError("nth_schedUnlock", "Failed\n");
  }
#endif
}

__thread int nth_criticalLvl= 0; // For nested critical sections
sigset_t nth_sigsetCritical;    // Signal to be blocked in a critical section
sigset_t nth_sigsetApp;         // Signals accepted in app mode

// Critical section management for multi-core

// Disable signals
static void nth_sigDisable(void) {
  sigset_t sigsetOld;
  if (pthread_sigmask(SIG_BLOCK, &nth_sigsetCritical, &sigsetOld)!=0) {
    perror("pthread_sigmask");
    nFatalError("nth_startCritical", "pthread_sigmask error\n");
  }
  assert( (nth_criticalLvl==0) == (sigismember(&sigsetOld, SIGVTALRM)==0) );
}

// Enable signals
static void nth_sigEnable(void) {
  sigset_t sigsetOld;
  if (pthread_sigmask(SIG_SETMASK, &nth_sigsetApp, &sigsetOld)!=0) {
    perror("pthread_sigmask");
    nFatalError("nth_startCritical", "pthread_sigmask error\n");
  }
  // SIGVTALRM should be blocked before calling nth_endCritical
  assert(sigismember(&sigsetOld, SIGVTALRM));
}
                                
void nth_startCritical(void) {
  nth_sigDisable();
  assert(nth_criticalLvl>=0); // starts and ends must be balanced
  CHECK_STACK
  nth_criticalLvl++;
  // When opening a critical section and is a multicore
  if (nth_criticalLvl==1 && nth_totalCores>1)
    nth_schedLock();          // Lock scheduler
}

void nth_endCritical(void) {
  CHECK_STACK
  assert(nth_criticalLvl>=1);  // This thread must be in a critical section
  if (--nth_criticalLvl==0) { // This thread is closing the critical section
    if (nth_totalCores>1)
      nth_schedUnlock();      // Unlock scheduler when multicore
    nth_sigEnable();
  }
}

void nth_startHandler(void) {
  CHECK_STACK
  // This thread is not in a critical section or the core is idle
  assert(nth_criticalLvl==0 || nth_coreIsIdle[nth_coreId()]);
  nth_criticalLvl++;
  sigset_t sigsetCurr;
  (void)sigsetCurr; // To avoid a warning when assertions are disabled
  assert( pthread_sigmask(SIG_BLOCK, NULL, &sigsetCurr)==0 &&
          sigismember(&sigsetCurr, SIGVTALRM) );
  // When opening a critical section and is a multicore
  if ((nth_criticalLvl==1 || nth_coreIsIdle[nth_coreId()]) && nth_totalCores>1)
    nth_schedLock();          // Lock scheduler
  assert(nth_totalCores==1 || nth_schedStatus==nth_coreId());
}

void nth_endHandler(void) {
  // This thread is closing a critical section or
  // the core is idle and the thread remains in a critical section
  nth_criticalLvl--;
  assert( nth_criticalLvl==0 ||
          (nth_criticalLvl>0 && nth_coreIsIdle[nth_coreId()]) );
  CHECK_STACK
  // When closing a critical section and is a multicore
  if ((nth_criticalLvl==0 || nth_coreIsIdle[nth_coreId()]) && nth_totalCores>1)
    nth_schedUnlock();        // Unlock scheduler
}

/*************************************************************
 * Thread and core id management
 *************************************************************/

__thread int nth_thisCoreId; // The virtual core id of this pthread
nThread *nth_coreThreads;    // Array of running pthreads w/virtual cores
int *nth_coreIsIdle;         // Which cores wait for a thread to run

nThread nSelf() {                 // The id of the running nthread
  if (nth_criticalLvl>0)
    return nth_selfCritical();
  else {
    nth_sigDisable();
    nThread thisTh= nth_selfCritical();
    nth_sigEnable();
    return thisTh;
  }
}

static void nth_setSelf(nThread th) {
  nth_coreThreads[nth_coreId()]= th;
}

/*************************************************************
 * Thread creation
 *************************************************************/

// The number of reserved words at the end of the stack area for
// safety reasons.  Sanitize can safely read them without overflowing
// to another malloc'ed memory.
#define SAFETY_NET 16

static int nth_stackSize= 16384;
static int nth_threadId= 1;

int nSetStackSize(int stackSize) {
  int prev= nth_stackSize;
  nth_stackSize= stackSize;
  return prev;
}

static void nth_threadInit(void *(*startFun)(void *), void *ptr) {
  CHECK_CRITICAL("nth_threadIni");
  nThread thisTh= nth_selfCritical();
  nth_cleanBuriedThread(&thisTh->prevTh);
  thisTh->status= RUN;
  char buf[20];
  sprintf(buf, "%d", nth_threadId++);
  thisTh->name= strdup(buf);
# ifdef TRACESCHED
    printk("SCHED %d:starting %s\n", nth_coreId(), buf);
# endif
  
  schedule();

  END_CRITICAL

  void *retPtr= (*startFun)(ptr);
  // The thread finished (startFun returned)
  nThreadExit(retPtr);
}

#define MAGIC1 ((void *)(intptr_t)0x184572358a82efb7)
#define ADDRMAGIC1 0
#define MAGIC2 ((void *)(intptr_t)0xc395f91da9283e8b)
#define ADDRMAGIC2 32

static nThread nth_makeThread(int stackSize) {
  nThread newTh= malloc(sizeof(*newTh));
  if (newTh==NULL)
    nFatalError("nth_makeThread", "Not enough memory\n");

  newTh->status= CREATED;
  
  newTh->allocCoreId= -1;
  newTh->name= NULL;
  newTh->joinTh= NULL;
  newTh->queue= newTh->timequeue= NULL;
  newTh->nextTh= newTh->nextTimeTh= NULL;
  newTh->startCoreNanos= 0;
  newTh->sliceNanos= 0;
  newTh->pri= MAXPRI/2;
  newTh->sendQueue= nth_makeQueue();
  
  getcontext(&newTh->uctx);
  if (stackSize==0) {
    newTh->stack= NULL;
    newTh->stackSize= 0xffffffffffffffffULL;
  }
  else {
    newTh->uctx.uc_link= NULL;
    void *stack= malloc(stackSize);
    newTh->stack= stack;
    newTh->stackSize= stackSize;
    newTh->uctx.uc_stack.ss_sp= stack;
    newTh->uctx.uc_stack.ss_size= stackSize;
    newTh->uctx.uc_stack.ss_flags= 0;
    newTh->stack[ADDRMAGIC1]= MAGIC1; // Canaries to detect stack overflow
    newTh->stack[ADDRMAGIC2]= MAGIC2;
  }

  return newTh;
}

int nThreadCreate(nThread *pth, void *attr,
                  void *(*startFun)(void *), void *ptr) {
  START_CRITICAL
  
  nThread thisTh= nSelf();
  nThread newTh= nth_makeThread(nth_stackSize);
 
  nth_threadCount++;
  define(nth_threadSet, newTh, newTh);
  makecontext(&newTh->uctx, (void (*)(void))nth_threadInit, 2, startFun, ptr);
  setReady(newTh);

  schedule();
  
  *pth= newTh;

  END_CRITICAL

  return 0;
}

const char *const nth_stateNames[]= { STATE_NAMES };

void dumpThreads(char *name) {
  FILE *fil= stdout;
  if (name!=NULL) {
    fil= fopen(name, "w");
    if (fil==NULL) {
      perror(name);
      fil= NULL;
    }
  }
    
  MapIterator *iter= getMapIterator(nth_threadSet);
  while (mapHasNext(iter)) {
    void *ptr;
    mapNext(iter, &ptr, &ptr);
    nThread th= ptr;
    const char *statusName="unknown";
    if (th->status>=0 && th->status<=WAIT_SLEEP)
      statusName= nth_stateNames[th->status];
    fprintf(fil, "thread=%p %s %s core=%d next=%p join=%p\n",
           (void*)th, th->name==NULL ? "?" : th->name, statusName,
           th->allocCoreId, (void*)th->nextTh, (void*)th->joinTh);
  }
  destroyMapIterator(iter);
  fflush(fil);
  if (fil!=stdout)
    fclose(fil);
}

nThread findSp(void *sp) {
  MapIterator *iter= getMapIterator(nth_threadSet);
  nThread thRet= NULL;
  while (mapHasNext(iter)) {
    void *ptr;
    mapNext(iter, &ptr, &ptr);
    nThread th= ptr;
    if (th->uctx.uc_stack.ss_sp < sp &&
        sp < (void*)((char*)th->uctx.uc_stack.ss_sp +
                      th->uctx.uc_stack.ss_size)) {
      thRet= th;
      break;
    }
  }
  destroyMapIterator(iter);
  return thRet;
}

#define MAXNAMESIZE 80

void nSetThreadName(char *format, ... ) { // Thread name for debugging purposes
  va_list ap;

  START_CRITICAL

  va_start(ap, format);
  char name[MAXNAMESIZE+1];
  int len= vsnprintf(name, MAXNAMESIZE, format, ap);
  name[len]= 0;
  va_end(ap);

  nThread thisTh= nSelf();
# ifdef TRACESCHED
    printk("SCHED %d:%s renames %s\n", nth_coreId(), thisTh->name, name);
# endif
  if (thisTh->name!=NULL)
    nFree(thisTh->name);
  thisTh->name=strcpy(malloc(len+1), name);

  END_CRITICAL
}

char* nGetThreadName(void) {
  return nSelf()->name;
}

/*************************************************************
 * Thread exit
 *************************************************************/

void nThreadExit(void *retPtr) {
  START_CRITICAL

  nThread th= nSelf();
  th->retPtr= retPtr;

  th->status= ZOMBIE;
  nth_zombieCount++;
# ifdef TRACESCHED
    printk( "SCHED %d:%s exits %s\n", nth_coreId(), th->name,
            th->joinTh==NULL ? "none" : th->joinTh->name );
# endif
  if (th->joinTh!=NULL) {
    setReady(th->joinTh);
  }

  nth_threadCount--;
  // if this is the last thread, terminate the whole process
  if (nth_threadCount==0) {
    del(nth_threadSet, th);
    nth_systemEnd();
  }

  schedule();
  nFatalError("nThreadExit", "Should not come back here\n");
}

/*************************************************************
 * Thread join
 *************************************************************/

int nJoin(nThread th, void **pRetPtr) {
  START_CRITICAL

  if (th->joinTh!=NULL)
    nFatalError("nJoin", "Thread joined twice\n");

  nThread thisTh= nSelf();
# ifdef TRACESCHED
    printk( "SCHED %d:%s joins %s %s\n", nth_coreId(), thisTh->name,
            th->name, nth_stateNames[th->status] );
# endif
  th->joinTh= thisTh;
  if (th->status!=ZOMBIE) {
    thisTh->status= WAIT_JOIN;
    schedule();
  }

  if (!nth_emptyQueue(th->sendQueue))
    fprintf(stderr, "Thread finished with pending message queue\n");
  else
    nth_destroyQueue(th->sendQueue);
  nth_zombieCount--;
  if (pRetPtr!=NULL)
    *pRetPtr= th->retPtr;
  del(nth_threadSet, th);
  th->status= BURIED;
  // If the thread th is allocated to some core, it is waiting in schedule
  // to resume another thread.  It cannot be freed yet.  It will be freed
  // by the thread resumed in that core.
  if (nth_allocCoreId(th)<0) {
    nth_cleanBuriedThread(&th);
  }

  END_CRITICAL

  return 0;
}

/*************************************************************
 * Core functions: the pthreads executing de nthreads
 *************************************************************/

static void *nth_coreFun(void *ptr) {
  int ncores= nth_totalCores;
  int id= 0;
  while (id<ncores) {
    if (nth_nativeCores[id]==pthread_self())
      break;
    id++;
  }
  if (id>=ncores)
    nFatalError("nth_coreFun", "Did not find pthread id.  Cannot continue.\n");
  nth_thisCoreId= id;
  nth_criticalLvl= 0;
  pthread_sigmask(SIG_SETMASK, &nth_sigsetApp, NULL);
  START_CRITICAL

  nth_rrThreadInit();
  
  // Start executing threads
  schedule();

  nFatalError("nth_coreFun", "Should not come back here\n");

  return NULL;
}

static void nth_startCores(void) {
  START_CRITICAL
  
  int ncores= nth_totalCores;
  
  nth_nativeCores= malloc(ncores*sizeof(pthread_t));
  nth_reviewStatus= malloc(ncores*sizeof(int));
  nth_reviewStatus[0]= 0;
  nth_nativeCores[0]= pthread_self();

  for (int id= 1; id<ncores; id++) {
    nth_reviewStatus[id]= 0;
    if (pthread_create(&nth_nativeCores[id], NULL, nth_coreFun, NULL)!=0) {
      perror("pthread_create");
      nFatalError("nth_startCores", "Could not create pthread %d\n", id);
    }
  }

  END_CRITICAL
}

/*************************************************************
 * Process initialization and finalization
 *************************************************************/

static void nth_checkNumber(char *num) {
  if (num==NULL) {
    fprintf(stderr, "main: Missing numeric parameter\n");
    exit(1);
  }
  while  (*num!='\0') {
    if (! ('0'<= *num && *num<='9')) {
      fprintf(stderr, "main: Invalid non numeric option %s\n", num);
      exit(1);
    }
    num++;
  }
}
      
int main(int argc, char *argv[]) {
  int rc;

  // Parameter analyzing
  int out=1;
  int count=1;
  for (int in=1; in<argc; in++) {
    if (strcmp(argv[in],"-slice")==0 && ++in<argc) {
      char *slice= argv[in];
      nth_checkNumber(slice);
      nth_sliceNanos= (long long)atoi(slice)*1000000;
    }
    else if (strcmp(argv[in],"-ncores")==0 && ++in<argc) {
      char *ncores= argv[in];
      nth_checkNumber(ncores);
      nth_totalCores= atoi(ncores);
    }
    else if (strcmp(argv[in],"-verbose")==0) {
      nth_verbose= 1;
    }
    else if (strcmp(argv[in],"-silent")==0) {
      nth_verbose= 0;
    }
    else if (strcmp(argv[in],"-pri1c")==0) {
      nth_pri1= 1;
    }
    else if (strcmp(argv[in],"-h")==0) {
      printf("%s  [-h] | [-silent] ( [-pri1c | -slice millis ] | "
             " -ncores n [ -slice millis ] ) ...\n",
             argv[0]);
      printf("The default scheduling is FCFS for single-core\n");
      printf("Choose -pri1c for single core priority scheduling\n");
      printf("Choose -slice <time> for single core round robin scheduling\n");
      printf("Choose -ncores <n>, with n>=2 for multi-core FCFS scheduling\n");
      printf("Choose -ncores <n> -slice <time>, for round robin scheduling\n");
      exit(0);
    }
    else {
      argv[out++]= argv[in];
      count++;
    }
  }
  
  argc= count;
  
  nth_systemInit();
  sigset_t startSigmask;
  pthread_sigmask(SIG_UNBLOCK, NULL, &startSigmask);
  if (sigismember(&startSigmask, SIGVTALRM))
    nFatalError("main", "SIGVTALRM blocked in app mode\n");

  rc= nMain(argc, argv);

  nShutdown(rc);
  return rc; // Does not get here
}

int *nth_rewiewStatus;

static void nth_Usr1Handler(int sig, siginfo_t *si, void *uc) {
  // Do nothing
  // This signal is just to wake up a core waiting for a thread to execute
#ifdef WAKEDUP
  DBG(
    int saveErrno= errno;
    printf("Core %d waked up\n", nth_coreId());
    errno= saveErrno;
  );
#endif
}

static void nth_systemInit() {
  int ncores= nth_totalCores;
  sigemptyset(&nth_sigsetCritical);
  // Add here other signals required to be catched
  sigaddset(&nth_sigsetCritical, SIGALRM);   // for timeouts as nSleep
  sigaddset(&nth_sigsetCritical, SIGVTALRM); // for time slicing
  sigaddset(&nth_sigsetCritical, SIGIO);     // for async thread read
  sigaddset(&nth_sigsetCritical, SIGUSR1);   // for waking cores up
  pthread_sigmask(SIG_UNBLOCK, &nth_sigsetCritical, NULL);

  // nth_sigsetApp is the set of signals normally blocked in app mode
  pthread_sigmask(SIG_UNBLOCK, NULL, &nth_sigsetApp);
  
  struct sigaction sigact;
  sigact.sa_flags= SA_SIGINFO;
  sigact.sa_mask= nth_sigsetCritical;
  sigact.sa_sigaction= nth_Usr1Handler;

  if (sigaction(SIGUSR1, &sigact, NULL)!=0) {
    perror("sigaction");
    nFatalError("nth_systemInit", "Cannot continue\n");
  }
  
  llMutexInit(&nth_schedMutex);
  // LLCondInit(&nth_idleCoreCond);
  // LLCondInit(&nth_nthreadReadyCond);
  nth_coreThreads= malloc(ncores*sizeof(nThread));
  nth_coreIsIdle= malloc(ncores*sizeof(int));
  for (int i=0; i<ncores; i++) {
    nth_coreIsIdle[i]= 0;
    nth_coreThreads[i]= NULL;
  }
  nth_thisCoreId= 0;
  nth_threadSet= makeHashMap(100, hash_ptr, pointer_equals);
  nth_threadCount= 1;
  nThread mainThread= nth_makeThread(0);
  define(nth_threadSet, mainThread, mainThread);
  mainThread->status= RUN;
  nth_setSelf(mainThread);
  mainThread->allocCoreId= 0;
  
  nth_criticalLvl= 0;
  mainThread->name=strdup("main");
  
  START_CRITICAL
  
  nth_timeInit();
  nth_rrInit();
  nth_rrThreadInit();
  printk("bootstrap\n");
  if (nth_pri1)
    setPri1Scheduling();
  else if (nth_sliceNanos==0 && ncores<=1)
    setFcfs1Scheduling();
  else if (nth_sliceNanos==0)
    setFcfsScheduling();
  else
    nSetTimeSlice(nth_sliceNanos);
  END_CRITICAL

  nth_startCores();
}

static void nth_systemEnd(void) {
  CHECK_CRITICAL("nth_systemEnd")

  nth_timeEnd();  
  if (nth_verbose) {
    printf("Info: Number of cores = %d\n", nth_totalCores);
    printf("Info: total context changes = %d\n", nth_contextChanges);
    printf("Info: Implicit context changes = %d\n", nth_implicitContextChanges);
  }
  MapIterator *iter= getMapIterator(nth_threadSet);
  int unfinishedThreads= 0;
  int runCnt= 0;
  int readyCnt= 0;
  int zombieCnt= 0;
  while (mapHasNext(iter)) {
    void *ptr;
    mapNext(iter, &ptr, &ptr);
    nThread th= ptr;
    unfinishedThreads++;
    if (th->status==RUN)
      runCnt++;
    else if (th->status==READY)
      readyCnt++;
    else if (th->status==ZOMBIE)
      zombieCnt++;
  }
  destroyMapIterator(iter);
  if (unfinishedThreads>1) {
    fprintf(stderr, "The system exited with %d nthreads unfinished "
            "(%d run %d ready %d zombie)\n",
            unfinishedThreads, runCnt, readyCnt, zombieCnt);
  }
}

void nShutdown(int rc) {
  START_CRITICAL
#ifdef TRACERR
#ifdef PRINTLOG
  nth_printLog(80);
#endif
#endif
  nth_systemEnd();
  exit(rc);
  // Does not come back here
}

/*************************************************************
 * Core park and wake up
 *************************************************************/

void nth_corePark(void) {
  int coreId= nth_coreId();
# ifdef TRACESCHED
    printk("SCHED %d:PARK\n", coreId);
# endif
  nth_coreIsIdle[coreId]= 1; // To prevent a signal handler to call
  CHECK_STACK                // recursively this scheduler
  int id= 0;
  while (id<nth_totalCores) {
    if ( !nth_coreIsIdle[id] || nth_reviewStatus[id] ||
         (nth_coreThreads[id]!=NULL && nth_coreThreads[id]->status==READY) )
      break;
    id++;
  }

  if (id>=nth_totalCores && !nth_alarmArmed)
    nFatalError("nth_rrSchedule", "Deadlock\n");

  if (nth_totalCores>1)
    nth_schedUnlock();
  sigsuspend(&nth_sigsetApp);
  if (nth_totalCores>1)
    nth_schedLock();
  nth_coreIsIdle[coreId]= 0;
  nth_reviewStatus[coreId]= 0;
# ifdef TRACESCHED
    printk("SCHED %d:UNPARK\n", coreId);
# endif
}

void nth_coreWakeUp(int id) {
  nth_reviewStatus[id]= 1;
  pthread_kill(nth_nativeCores[id], SIGUSR1);
}

void nth_reviewCores(void) {
  int ncores= nth_totalCores;         // look for an idle core
# ifndef OPT
  // If debugging, choose first a core not allocated to any thread
  for (int id= 0; id<ncores; id++) {
    if (nth_coreThreads[id]==NULL) {
      pthread_kill(nth_nativeCores[id], SIGUSR1);
      break;
    }
  }
# endif
  for (int id= 0; id<ncores; id++) {
    if (nth_coreIsIdle[id] && nth_reviewStatus[id]==0) {
      if (nth_coreThreads[id]==NULL || nth_coreThreads[id]->status!=READY) {
        // wake the core up
        nth_reviewStatus[id]= 1;
        pthread_kill(nth_nativeCores[id], SIGUSR1);
        break;
      }
    }
  }
}

/*************************************************************
 * Context changes
 *************************************************************/

void nth_cleanBuriedThread(nThread *pth) {
  // The stack and thread descriptor (struct nthread) are normally
  // freed in nJoin, but sometimes the thread stack is trapped in a
  // parked core.  In such a case the stack can't be freed in nJoin
  // and the thread passes to state BURIED.
  // When such a core resumes another thread and the previous thread
  // is BURIED, the stack and thread descriptor are freed.
  nThread prevTh= *pth;
  *pth= NULL;
  if (prevTh!=NULL && prevTh->status==BURIED) {
#   ifdef TRACESCHED
      nThread thisTh= nth_selfCritical();
      printk( "SCHED %d:%s bury %s %s\n", nth_coreId(), thisTh->name,
              prevTh->name, nth_stateNames[prevTh->status] );
#   endif
    if (prevTh->stack!=NULL)
      free(prevTh->stack);
    if (prevTh->name!=NULL)
      free(prevTh->name);
    free(prevTh);
  }
}

// __attribute__((no_sanitize_address))
void nth_changeContext(nThread thisTh, nThread nextTh) {
  CHECK_CRITICAL("nth_ChangeContext")
  
  DBG(
    if ( thisTh!=NULL && thisTh->stack !=NULL &&
         ( thisTh->stack[ADDRMAGIC1] != MAGIC1 ||
           thisTh->stack[ADDRMAGIC2] != MAGIC2 ))
      nFatalError("nth_changeContext", "Stack overflow!\n");
  );
  
  assert(nth_totalCores==1 || nth_schedStatus==nth_thisCoreId);
  if (thisTh==nextTh)
    return;
  assert(nextTh->status==READY);
  assert(nextTh->allocCoreId==-1);

  nth_contextChanges++;
  
  int coreId= nth_coreId();
  nextTh->allocCoreId= coreId;
  assert( thisTh==NULL ||
          (thisTh->allocCoreId==coreId && nth_coreThreads[coreId]==thisTh) );
  nth_setSelf(nextTh);

  nextTh->prevTh= thisTh;
  if (thisTh==NULL) {
    setcontext(&nextTh->uctx);  // This core is bootstrapping at nth_coreFun
  }
  else {
    thisTh->allocCoreId= -1;
    swapcontext(&thisTh->uctx, &nextTh->uctx);
    nth_cleanBuriedThread(&thisTh->prevTh);
  }
}
