#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "nthread.h"
#include "pss.h"

//==========================================
// Implementacion de HashMap
//==========================================

typedef struct entry {
  void *key, *val;
  struct entry *next;
} Entry;

struct map {
  int capacity;
  Entry **entries;
  HashFun hash;
  EqualsFun equals;
  int hasNext;
};

struct map_iterator {
  Map *map;
  int idx;
  Entry *entry;
};

Map *makeHashMap(int capacity, HashFun hash, EqualsFun equals) {
  Map *map= malloc(sizeof(Map));
  map->capacity= capacity;
  map->hash= hash;
  map->equals= equals;
  map->entries= malloc(capacity*sizeof(Entry*));
  for (int i= 0; i<capacity; i++) {
    map->entries[i]= NULL;
  }
  return map;
}

void destroyHashMap(Map *map) {
  for (int i= 0; i<map->capacity; i++) {
    Entry *entry= map->entries[i];
    while (entry!=NULL) {
      Entry *next= entry->next;
      free(entry);
      entry= next;
    }
  }
  free(map->entries);
  free(map);
}

static Entry **queryEntry(Map *map, int idx, void *key) {
  Entry **pentry= & map->entries[idx];
  Entry *entry= *pentry;
  while (entry!=NULL) {
    if ((*map->equals)(key, entry->key))
      return pentry;
    pentry= &entry->next;
    entry= entry->next;
  }
  return NULL;
}
    
void *query(Map *map, void *key) {
  int idx= (*map->hash)(key) % map->capacity;
  Entry **pentry= queryEntry(map, idx, key);
  if (pentry==NULL)
    return NULL;
  else
    return (*pentry)->val;
}
  
int define(Map *map, void *key, void *val) {
  int idx= (*map->hash)(key) % map->capacity;
  Entry **pentry= queryEntry(map, idx, key);
  if (pentry!=NULL) {
    (*pentry)->val= val;
    return 1;
  }
  else {
    Entry *entry= malloc(sizeof(Entry));
    entry->key= key;
    entry->val= val;
    entry->next= map->entries[idx];
    map->entries[idx]= entry;
    return 0;
  }
}

void *del(Map *map, void *key) {
  int idx= (*map->hash)(key) % map->capacity;
  Entry **pentry= queryEntry(map, idx, key);
  if (pentry==NULL) {
    return NULL;
  }
  else {
    Entry *entry= *pentry;
    void *val= entry->val;
    *pentry= entry->next;
    free(entry);
    return val;
  }
} 
  
int contains(Map *map, void *key) {
  int idx= (*map->hash)(key) % map->capacity;
  Entry **pentry= queryEntry(map, idx, key);
  return pentry!=NULL;
}

static void advance(MapIterator *iter) {
  Entry *entry= iter->entry;
  while (entry==NULL) {
    if (iter->idx >= iter->map->capacity) {
      break;
    }
    entry= iter->map->entries[iter->idx++];
  }
  iter->entry= entry;
}

MapIterator *getMapIterator(Map *map) {
  MapIterator *iter= malloc(sizeof(MapIterator));
  iter->idx= 1;
  iter->entry= map->entries[0];
  iter->map= map;
  advance(iter);
  return iter;
}
  
void destroyMapIterator(MapIterator *iter) {
  free(iter);
}

int mapNext(MapIterator *iter, void **pkey, void **pval) {
  Entry *entry= iter->entry;
  if (entry==NULL) {
    return 0;
  }
  else {
    *pkey= entry->key;
    *pval= entry->val;
    iter->entry= entry->next;
    advance(iter);
    return 1;
  }
}
 
int mapHasNext(MapIterator *iter) {
  return iter->entry!=NULL;
}

void resetMapIterator(MapIterator *iter) {
  iter->entry= iter->map->entries[0];
  iter->idx= 1;
  advance(iter);
}

//==========================================
// Funciones de hashing
//==========================================

unsigned hash_ptr(void *ptr) {
    unsigned h = (uintptr_t) ptr / 16;
    return h;
}


int pointer_equals(void *ptr1, void* ptr2) {
    return ptr1 == ptr2;
}

unsigned hash_string(void *str) {
    unsigned total = 2;
    for (char *reader = str; *reader; ++reader) {
        total += *reader;
        total *= 5;
    }
    return total;
}

int equals_strings(void *ptrX, void *ptrY) {
    return strcmp((char *)ptrX, (char *)ptrY) == 0;
}

//==========================================
// Implementacion de Queue
//==========================================

typedef struct node {
    void *val;
    struct node *next;
} QueueNode;

struct queue {
    QueueNode *first, **plast;
    int len;
};

Queue *makeQueue() {
    Queue *q = malloc(sizeof(Queue));
    q->first = NULL;
    q->plast = &q->first;
    q->len = 0;
    return q;
}

void put(Queue *q, void *ptr) {
    QueueNode *node = malloc(sizeof(QueueNode));
    node->val = ptr;
    node->next = NULL;
    *q->plast = node;
    q->plast = &node->next;
    q->len++;
}

void *get(Queue *q) {
    if (q->first == NULL) {
        return NULL;
    }
    void *val = q->first->val;
    QueueNode *next = q->first->next;
    free(q->first);
    q->first = next;
    if (next == NULL) {
        q->plast = &q->first;
    }
    q->len--;
    return val;
}

void *peek(Queue *q) {
    if (q->first == NULL) {
        return NULL;
    } else {
        return q->first->val;
    }
}

int emptyQueue(Queue *q) {
    return q->first == NULL;
}

int queueLength(Queue *q) {
    return q->len;
}

void destroyQueue(Queue *q) {
    QueueNode *node = q->first;
    while (node != NULL) {
        QueueNode *next = node->next;
        free(node);
        node = next;
    }
    free(q);
}

//==========================================
// Implementacion de sort generico
//==========================================

void sort(void *ptr, int left, int right,
          Comparator compare, Swapper swap) {
  int i, last;

  if (left>=right)
    return;

  (*swap)(ptr, left, (left+right)/2);
  last= left;

  /*
    +--+-----------+--------+--------------+
    |  |///////////|\\\\\\\\|              |
    +--+-----------+--------+--------------+
    left        last         i         right
  */

  for (i= left+1; i<=right; ++i)
    if ((*compare)(ptr, i, left)<0)
      (*swap)(ptr, ++last, i);
  (*swap)(ptr, left, last);

  sort(ptr, left, last-1, compare, swap);
  sort(ptr, last+1, right, compare, swap);
}

//==========================================
// Implementacion de Cola de Prioridad general
//==========================================

// Adapted from https://www.geeksforgeeks.org/priority-queue-using-binary-heap/

struct priqueue {
  PriComparator compare;
  int maxsize, size;
  void **ar;
};
  
PriQueue *makeFullPriQueue(int iniSize, PriComparator compare) {
  PriQueue *q= malloc(sizeof(PriQueue));
  q->compare= compare;
  q->maxsize= iniSize;
  q->size= -1;
  q->ar= malloc(iniSize*sizeof(void*));
  return q;
}

void destroyPriQueue(PriQueue *q) {
  free(q->ar);
  free(q);
}

void *fullPriPeek(PriQueue *q) {
  return q->size==-1 ? NULL : q->ar[0];
}

int emptyPriQueue(PriQueue *q) {
  return q->size==-1;
}

int priLength(PriQueue *q) {
  return q->size+1;
}

static void shiftUp(PriQueue *q, int i) {
  void **ar= q->ar;
  PriComparator compare= q->compare;
  int parent_i= (i-1)/2;
  while (i>0 && compare(ar[parent_i],ar[i])<0) {
    void *tmp= ar[parent_i]; // swap ar[parent_i] and ar[i]
    ar[parent_i]= ar[i];
    ar[i]= tmp;
    i= parent_i;
    parent_i= (i-1)/2;
  }
}

static void shiftDown(PriQueue *q, int i) {
  void **ar= q->ar;
  int size= q->size;
  PriComparator compare= q->compare;

  for (;;) {
    int maxIndex= i;
    int left= 2*i+1;
    if (left<=size && compare(ar[left], ar[maxIndex])>0)
      maxIndex= left;

    int right= 2*i+2;
    if (right<=size && compare(ar[right],ar[maxIndex])>0)
      maxIndex= right;
    if (i==maxIndex)
      break;
    void *tmp= ar[i]; // swap ar[i] and ar[maxIndex]
    ar[i]= ar[maxIndex];
    ar[maxIndex]= tmp;
    i= maxIndex;
  }
}

void fullPriPut(PriQueue *q, void *elem) {
  if (q->size+1==q->maxsize) {
    void **newAr= malloc(q->maxsize*2*sizeof(void*));
    for (int i= 0; i<=q->size; i++)
      newAr[i]= q->ar[i];
    free(q->ar);
    q->ar= newAr;
    q->maxsize= q->maxsize*2;
  }
  q->size++;
  q->ar[q->size] = elem;
  shiftUp(q, q->size);
}

void *fullPriGet(PriQueue *q) {
  void *result= q->ar[0];
  q->ar[0]= q->ar[q->size];
  q->size--;
  shiftDown(q, 0);
  return result;
}

//==========================================
// Implementacion de Cola de Prioridad simplificada
//==========================================

typedef struct {
  double pri;
  void *elem;
} PriObj;

static int priCmpFun(void *elemA, void *elemB) {
  PriObj *a= elemA;
  PriObj *b= elemB;
  double cmp= b->pri-a->pri;
  return cmp>0 ? 1 : (cmp<0 ? -1 : 0);
}

PriQueue *makePriQueue(void) {
  return makeFullPriQueue(16, priCmpFun);
}

void *priPeek(PriQueue *q) {
  PriObj *priobj= fullPriPeek(q);
  return priobj==NULL ? NULL : priobj->elem;
}
  
double priBest(PriQueue *q) {
  PriObj *priobj= fullPriPeek(q);
  return priobj==NULL ? 0 : priobj->pri;
}

void *priGet(PriQueue *q) {
  PriObj *priobj= fullPriGet(q);
  if (priobj==NULL)
    return NULL;
  else {
    void *res= priobj->elem;
    free(priobj);
    return res;
  }
}

void priPut(PriQueue *q, void *elem, double pri) {
  PriObj *priobj= malloc(sizeof(PriObj));
  priobj->pri= pri;
  priobj->elem= elem;
  fullPriPut(q, priobj);
}
