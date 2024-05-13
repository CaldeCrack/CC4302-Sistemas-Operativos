typedef struct rwlock nRWLock;

nRWLock *nMakeRWLock(void);
void nDestroyRWLock(nRWLock *rwl);
int nEnterRead(nRWLock *rwl, int timeout);
void nExitRead(nRWLock *rwl);
int nEnterWrite(nRWLock *rwl, int timeout);
void nExitWrite(nRWLock *rwl);
