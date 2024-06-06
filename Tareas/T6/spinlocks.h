enum { OPEN, CLOSED};
int setBusyWaiting(int flag);
void spinLock(volatile int *psl) ;
void spinUnlock(int *psl);
