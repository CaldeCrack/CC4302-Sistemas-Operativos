#define _XOPEN_SOURCE 500

#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <ucontext.h>

#include "nthread-impl.h"

static void nth_systemEnd(void);
static void nth_systemInit(void);
static intptr_t nth_chkSum(void **sp);

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


// Mutex to ensure mutual exclusion of virtual cores
LLMutex nth_schedMutex;
// Condition to wait for an available core
// LLCond nth_idleCoreCond;
// Condition to wait for am available nthread 
// LLCond nth_nthreadReadyCond;

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

__thread int nth_criticalLvl= 0; // For nested critical sections
sigset_t nth_sigsetCritical;    // Signal to be blocked in a critical section
sigset_t nth_sigsetApp;         // Signals accepted in app mode

// Critical section management for multi-core
                                
void nth_startCritical(void) {
  if (nth_criticalLvl==0) {
    sigset_t sigsetOld;
    pthread_sigmask(SIG_BLOCK, &nth_sigsetCritical, &sigsetOld);
    CHECK_STACK
    if (nth_totalCores>1)
      llLock(&nth_schedMutex);
    DBG(
      if (sigismember(&sigsetOld, SIGVTALRM))
        nFatalError( "nth_startCritical",
                     "SIGVTALRM blocked before critical section\n" );
    );
  }
  nth_criticalLvl++;
}

void nth_endCritical(void) {
  CHECK_STACK
  if (--nth_criticalLvl==0) {
    if (nth_totalCores>1)
      llUnlock(&nth_schedMutex);
    sigset_t sigsetOld;
    pthread_sigmask(SIG_SETMASK, &nth_sigsetApp, &sigsetOld);
    DBG(
      if (nth_criticalLvl<0)
        nFatalError( "nth_endCritical", "Unbalanced critical sections\n");
      if (!sigismember(&sigsetOld, SIGVTALRM))
        nFatalError( "nth_endCritical",
                     "SIGVTALRM not blocked inside critical section\n" );
    );
  }
}

void nth_startHandler(void) {
  CHECK_STACK
  if (nth_criticalLvl++!=0 && !nth_coreIsIdle[nth_coreId()]) {
    sigset_t sigsetCurr;
    pthread_sigmask(SIG_BLOCK, NULL, &sigsetCurr);
    if (sigismember(&sigsetCurr, SIGVTALRM))
      nFatalError( "nth_startHandler",
              "Inconsistency detected: nth_criticalLvl thinks critical\n" );
    nFatalError( "nth_startHandler",
            "Inconsistency detected: handler start while SIGVTALRM enabled\n" );
  }
  if (nth_totalCores>1)
    llLock(&nth_schedMutex);
}

void nth_endHandler(void) {
  CHECK_STACK
  if (--nth_criticalLvl!=0 && !nth_coreIsIdle[nth_coreId()])
    nFatalError( "nth_endHandler",
                 "Inconsistency detected: still in a critical section?\n");
  if (nth_totalCores>1)
    llUnlock(&nth_schedMutex);
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
    sigset_t sigsetOld;
    pthread_sigmask(SIG_BLOCK, &nth_sigsetCritical, &sigsetOld);
    CHECK_STACK
    DBG (
      if (sigismember(&sigsetOld, SIGVTALRM))
        nFatalError( "nth_startCritical",
                     "SIGVTALRM blocked before critical section\n" );
    );
    nThread thisTh= nth_selfCritical();
    pthread_sigmask(SIG_SETMASK, &sigsetOld, NULL);
    return thisTh;
  }
}

void nth_setSelf(nThread th) {
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
static nThread nth_deadList= NULL;

int nSetStackSize(int stackSize) {
  int prev= nth_stackSize;
  nth_stackSize= stackSize;
  return prev;
}

static void nth_threadInit(nThread thisTh, void *(*startFun)(void *),
                                           void *ptr) {
  CHECK_CRITICAL("nth_threadIni");
  if (thisTh->lastTh!=NULL && thisTh->lastTh->status==BURIED)
    nFatalError("nth_threadInit", "Thread leak\n");

  thisTh->status= RUN;
  if (nth_totalCores>1)
    nth_reviewCores();

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
  makecontext(&newTh->uctx, (void (*)(void))nth_threadInit, 3,
              newTh, startFun, ptr);
  setReady(newTh);
  schedule();
  
  *pth= newTh;

  END_CRITICAL

  return 0;
}

char *nth_statusNames[]= { STATE_NAMES };

void dumpThreads(void) {
  MapIterator *iter= getMapIterator(nth_threadSet);
  while (mapHasNext(iter)) {
    void *ptr;
    mapNext(iter, &ptr, &ptr);
    nThread th= ptr;
    char *statusName="unknown";
    if (th->status>=0 && th->status<=WAIT_SLEEP)
      statusName= nth_statusNames[th->status];
    printf("thread=%p %s %s core=%d next=%p join=%p\n",
           (void*)th, th->name==NULL ? "?" : th->name, statusName,
           th->allocCoreId, (void*)th->nextTh, (void*)th->joinTh);
  }
  destroyMapIterator(iter);
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
  if (thisTh->name!=NULL) nFree(thisTh->name);
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
  if (nth_allocCoreId(th)<0) {
    if (th->stack!=NULL)
      free(th->stack);
    free(th);
  }
  else {
    // The thread th is allocated to some core, it cannot be freed yet
    th->status= BURIED;
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
  
  LLMutexInit(&nth_schedMutex);
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
  
  START_CRITICAL
  
  nth_timeInit();
  nth_rrInit();
  nth_rrThreadInit();
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
 * Core wake up
 *************************************************************/

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

// Stack change: in nStack-<arch>.s
// Strongly dependent on the machine

static intptr_t nth_chkSum(void **sp) {
  intptr_t *intSp= (intptr_t*)sp;
  intptr_t chkSum= 0;
  for (int i= 0; i<SAFETY_NET; i++) {
    chkSum += *intSp++;
  }
  return chkSum;
}
 

void nth_cleanBuriedThread(nThread *pth) {
  nThread lastTh= *pth;
  *pth= NULL;
  if (lastTh!=NULL && lastTh->status==BURIED) {
    if (lastTh->stack!=NULL)
      free(lastTh->stack);
    free(lastTh);
  }
}

// __attribute__((no_sanitize_address))
void nth_changeContext(nThread thisTh, nThread nextTh) {
  CHECK_CRITICAL("nth_ChangeContext")
  
  if (thisTh!=NULL) {
    thisTh->allocCoreId= -1;
  }

  if (nextTh->allocCoreId!=-1)
    nFatalError("nth_changeContext", "Next thread is already allocated\n");

  nth_contextChanges++;
  
  DBG(
    if (nextTh->status!=READY)
      nFatalError("nthChangeContext", "This thread is not ready for run\n");
    if ( thisTh!=NULL && thisTh->stack !=NULL &&
         ( thisTh->stack[ADDRMAGIC1] != MAGIC1 ||
           thisTh->stack[ADDRMAGIC2] != MAGIC2 ))
      nFatalError("nth_changeContext", "Stack overflow!\n");
  );
  
  nextTh->allocCoreId= nth_coreId();
  nth_setSelf(nextTh);

  nextTh->lastTh= thisTh;
  if (thisTh==NULL)
    setcontext(&nextTh->uctx);  // This core is bootstrapping at nth_coreFun
  else if (thisTh!=nextTh) {

    swapcontext(&thisTh->uctx, &nextTh->uctx);

    nth_cleanBuriedThread(&thisTh->lastTh);
  }
}


// __attribute__((no_sanitize_address))
void nth_callInNewContext(nThread thisTh, nThread newTh,
                      void (*proc)(void *), void *ptr) {
  int nth_saveCritical= nth_criticalLvl;
  thisTh->allocCoreId= -1;
    
  DBG(
    // Canaries to detect stack overflow
    newTh->stack[ADDRMAGIC1]= MAGIC1;
    newTh->stack[ADDRMAGIC2]= MAGIC2;
  );

  // In assembler
  newTh->allocCoreId= nth_coreId();
  makecontext(&newTh->uctx, (void (*)(void))proc, 1, ptr);

  nth_criticalLvl= nth_saveCritical;
}
