/**
   \file
   \author lcs10
   \brief test tabella hash

 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcheck.h>

#include "genHash.h"

#define SIZE1 999149
#define SIZE2 3
static char *strings[] = {
  "Statistics prove, prove that you've one birthday,",
  "one birthday ev'ry year.",
  "But there are three hundred and sixty four",
  "unbirthdays.",
  "That is why we're gathered here to cheer.",
  "A very merry unbirthday to you, to you.",
  "A very merry unbirthday to you,",
  "It's great to drink to someone and I guess that you will do.",
  "A very merry unbirthday to you",
  NULL
};
int compare_int(void *a, void *b) {
    int *_a, *_b;
    _a = (int *) a;
    _b = (int *) b;
    return ((*_a) - (*_b));
}
int compare_string(void *a, void *b) {
    char *_a, *_b;
    _a = (char *) a;
    _b = (char *) b;
    return strcmp(_a,_b);
}
/* funzione di copia di un intero */
void * copy_int(void *a) {
  int * _a;

  if ( ( _a = malloc(sizeof(int) ) ) == NULL ) return NULL;

  *_a = * (int * ) a;

  return (void *) _a;
}
/* funzione di copia di una stringa */
void * copy_string(void * a) {
  char * _a;

  if ( ( _a = strdup(( char * ) a ) ) == NULL ) return NULL;

  return (void *) _a;
}

int main (void) {
  hashTable_t * tbs, *tbi;
  int i;
  void * p;

  mtrace();

  /*** inizio test creazione hash **/

  /* hash di stringhe */
  if ( ( tbs = new_hashTable (SIZE1,compare_string,copy_string,copy_int,hash_string) ) == NULL ) {
    fprintf(stderr,"new_Hash: impossibile creare 1\n");
    exit(EXIT_FAILURE);
  }

  /* hash di interi */
  if ( ( tbi = new_hashTable (SIZE1,compare_int,copy_int,copy_string,hash_int) ) == NULL ) {
    fprintf(stderr,"new_Hash: impossibile creare 2\n");
    exit(EXIT_FAILURE);
  }

  /*** fine test creazione ***/

  /*** inizio test cancellazione hash vuoto ***/
  free_hashTable(&tbs);
  free_hashTable(&tbi);

  if ( tbi != NULL || tbs != NULL ) {
        fprintf(stderr,"free_hashTable: puntatore non a NULL\n");
    exit(EXIT_FAILURE);
  }

   /*** fine test cancellazione hash vuoto ***/

  /*** inizio test add/find ***/
  /* creo hash di test */
  /* hash di stringhe */
  if ( ( tbs = new_hashTable (SIZE1,compare_string,copy_string,copy_int,hash_string) ) == NULL ) {
    fprintf(stderr,"new_Hash: impossibile creare 1\n");
    exit(EXIT_FAILURE);
  }
  
  /* hash di interi */
  if ( ( tbi = new_hashTable (SIZE1,compare_int,copy_int,copy_string,hash_int) ) == NULL ) {
    fprintf(stderr,"new_Hash: impossibile creare 2\n");
    exit(EXIT_FAILURE);
  }
  
  /* trovo/inserisco stringhe ed interi nelle tabelle */
  for( i=0; strings[i]!=NULL; i++) {
    if ( find_hashElement(tbs,strings[i]) != NULL ) {
      fprintf(stderr,"find_hashElement: %s gia' presente\n",strings[i]);
      exit(EXIT_FAILURE);
    }
    
    if ( add_hashElement(tbs,strings[i],&i) == -1 ) {
      fprintf(stderr,"add_hashElement: %s",strings[i]);
      perror("");
      exit(EXIT_FAILURE);
    }

    if ( add_hashElement(tbi,&i,strings[i]) == -1 ) {
      fprintf(stderr,"add_hashElement: %d",i);
      perror("");
      exit(EXIT_FAILURE);
    }
    
    if ( ( p = find_hashElement(tbi,&i) ) == NULL ||  compare_string(p,strings[i])!= 0 ) {
      fprintf(stderr,"find_hashElement: %s : NON presente\n",strings[i]);
      exit(EXIT_FAILURE);
    }
    
    free(p);   
  }


  /* rimuovo tutti gli elementi */
  for( i=0; strings[i]!=NULL; i++) {
    if ( remove_hashElement(tbs,strings[i]) != 0 ) {
      fprintf(stderr,"remove_hashElement: %s NON rimosso\n",strings[i]);
      exit(EXIT_FAILURE);
    }
    /* e' ancora presente ? */
    if ( find_hashElement(tbs,strings[i]) != NULL ) {
      fprintf(stderr,"find_ListElement: %s NON rimosso\n",strings[i]);
      exit(EXIT_FAILURE);
    }
  }

  for(i-- ; i>=0 ; i--) {
    if ( remove_hashElement(tbi,&i) != 0 ) {
      fprintf(stderr,"remove_hashElement: %d NON rimosso\n",i);
      exit(EXIT_FAILURE);
    } 
    if ( find_hashElement(tbi,&i) != NULL ) {
      fprintf(stderr,"find_hashElement: %d NON rimosso\n",i);
      exit(EXIT_FAILURE);
    }
  }


  free_hashTable(&tbs);
  free_hashTable(&tbi);
  /*** fine test add/find ***/

  /*** test collisioni ***/
  

  /* hash di stringhe */
  if ( ( tbs = new_hashTable (SIZE2,compare_string,copy_string,copy_int,hash_string) ) == NULL ) {
    fprintf(stderr,"new_Hash: impossibile creare 1\n");
    exit(EXIT_FAILURE);
  }

  /* hash di interi */
  if ( ( tbi = new_hashTable (SIZE2,compare_int,copy_int,copy_string,hash_int) ) == NULL ) {
    fprintf(stderr,"new_Hash: impossibile creare 2\n");
    exit(EXIT_FAILURE);
  }

  for( i=0; strings[i]!=NULL; i++) {
    if ( add_hashElement(tbs,strings[i],&i) == -1 ) {
      fprintf(stderr,"add_hashElement: %s",strings[i]);
      perror("");
      exit(EXIT_FAILURE);
    }

    if ( add_hashElement(tbi,&i,strings[i]) == -1 ) {
      fprintf(stderr,"add_hashElement: %d",i);
      perror("");
      exit(EXIT_FAILURE);
    }

    if ( ( p = find_hashElement(tbs,strings[i]) ) == NULL ||  compare_int(p,&i)!= 0 ) {
      fprintf(stderr,"find_hashElement: %s : NON presente\n",strings[i]);
      exit(EXIT_FAILURE);
    }
    free(p);
  }
  free_hashTable(&tbs);
  free_hashTable(&tbi);
  /*** fine test collisioni ***/


  return 0;
}

