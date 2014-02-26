/**
   \file
   \author lcs10
   
   \brief Test liste generiche
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcheck.h>

#include "genList.h"

/** stringhe di test */
static char *strings[] = {
    "Hallo world!",
    "She sells sea shells by the sea shore.",
    "Trentatre' trentini entrarono in Trento tutti e trentatre' trotterellando",
    "Visita guidata speciale ......",
    "... museo d'arte moderna",
    "Jose Saramago",
    "e^(i*pi) + 1 = 0",
    "P = NP",
    NULL
};

/** funzione di confronto per lista di interi 
    \param a puntatore intero da confrontare
    \param b puntatore intero da confrontare
    
    \retval 0 se sono uguali
    \retval p (p!=0) altrimenti
*/
int compare_int(void *a, void *b) {
    int *_a, *_b;
    _a = (int *) a;
    _b = (int *) b;
    return ((*_a) - (*_b));
}

/** funzione di confronto per lista di stringhe 
    \param a puntatore prima stringa da confrontare
    \param b puntatore seconda stringa da confrontare
    
    \retval 0 se sono uguali
    \retval p (p!=0) altrimenti
*/
int compare_string(void *a, void *b) {
    char *_a, *_b;
    _a = (char *) a;
    _b = (char *) b;
    return strcmp(_a,_b);
}


/** funzione di copia di un intero 
    \param a puntatore intero da copiare

    \retval NULL se si sono verificati errori
    \retval p puntatore al nuovo intero allocato (alloca memoria)
*/
void * copy_int(void *a) {
  int * _a;

  if ( ( _a = malloc(sizeof(int) ) ) == NULL ) return NULL;

  *_a = * (int * ) a;

  return (void *) _a;
}

/** funzione di copia di una stringa 
    \param a puntatore stringa da copiare

    \retval NULL se si sono verificati errori
    \retval p puntatore alla stringa allocata (alloca memoria)
*/
void * copy_string(void * a) {
  char * _a;

  if ( ( _a = strdup(( char * ) a ) ) == NULL ) return NULL;

  return (void *) _a;
}


/** main function */
int main (void) {
  int i;
  list_t* lists,*listi;

  mtrace();

  /*** inizio test creazione liste ***/

  /* creo una lista di stringhe payload intero*/
  if ( ( lists = new_List(compare_string,copy_string,copy_int) ) == NULL ) {
    fprintf(stderr,"new_List: impossibile creare 1\n");
    exit(EXIT_FAILURE);
  }

  /* creo una lista di interi payload stringa*/
  if ( ( listi = new_List(compare_int,copy_int,copy_string) ) == NULL ) {
        fprintf(stderr,"new_List: impossibile creare 2\n");
    exit(EXIT_FAILURE);
  }

  /*** fine test creazione liste ***/

  
  /*** inizio test cancellazione liste vuote ***/
  
  free_List(&lists);
  free_List(&listi);
  
  if ( listi != NULL || lists != NULL ) {
    fprintf(stderr,"free_List: puntatore non a NULL\n");
    exit(EXIT_FAILURE);
  }
  
  /*** fine test cancellazione liste vuote ***/
  
  /*** inizio test add/find ***/
  /* creo liste di test */
  /* creo una lista di stringhe */
  if ( ( lists = new_List(compare_string,copy_string,copy_int) ) == NULL ) {
    fprintf(stderr,"new_List: impossibile creare 3\n");
    exit(EXIT_FAILURE);
  }
  
  /* creo una lista di interi */
  if ( ( listi = new_List(compare_int,copy_int,copy_string) ) == NULL ) {
    fprintf(stderr,"new_List: impossibile creare 4\n");
    exit(EXIT_FAILURE);
  }
  
  
  /* inserisco stringhe ed interi nelle liste */
  for( i=0; strings[i]!=NULL; i++) {
    if ( find_ListElement(lists,strings[i]) != NULL ) {
      fprintf(stderr,"find_ListElement: %s gia' presente\n",strings[i]);
      exit(EXIT_FAILURE);
    }
    
    if ( find_ListElement(listi,&i) != NULL ) {
      fprintf(stderr,"find_ListElement: %d gia' presente\n",i);
      exit(EXIT_FAILURE);
    }
    
    if ( add_ListElement(lists,strings[i],&i) == -1 ) {
      fprintf(stderr,"add_ListElement: %s",strings[i]);
      perror("");
      exit(EXIT_FAILURE);
    }
    
    if ( add_ListElement(listi,&i,strings[i]) == -1 ) {
      fprintf(stderr,"add_ListElement: %d",i);
      perror("");
      exit(EXIT_FAILURE);
    }
  }
  
  /* controllo che le chiavi inserite siano presenti con il giusto payload */
  for( i=0; strings[i]!=NULL; i++) {
    elem_t* tmp;
    
    if ( ( tmp = find_ListElement(lists,strings[i])) == NULL || compare_int(&i,tmp->payload) != 0 ) {
      fprintf(stderr,"find_ListElement: %s NON e' presente\n",strings[i]);
      exit(EXIT_FAILURE);
    }
    
    
    if ( ( tmp = find_ListElement(listi,&i) ) == NULL || compare_string(strings[i],tmp->payload) != 0 ) {
      fprintf(stderr,"find_ListElement: %d NON e' presente\n",i);
      exit(EXIT_FAILURE);
    }
    
    /* inserzioni di chiavi replicate ? */
    if ( add_ListElement(lists,strings[i],&i) != -1 ) {
      fprintf(stderr,"add_ListElement: %s inserito replicato\n",strings[i]);
      exit(EXIT_FAILURE);
    }
  }
  
  /* libero memoria */
  free_List(&lists);
  free_List(&listi);
  /*** fine test add/find ***/
  
  /*** inizio test remove ***/
  /* creo liste di test */
  /* creo una lista di stringhe */
if ( ( lists = new_List(compare_string,copy_string, copy_int) ) == NULL ) {
    fprintf(stderr,"new_List: impossibile creare 5\n");
    exit(EXIT_FAILURE);
  }

  /* creo una lista di interi */
if ( ( listi = new_List(compare_int,copy_int,copy_string) ) == NULL ) {
        fprintf(stderr,"new_List: impossibile creare 6\n");
	exit(EXIT_FAILURE);
  }


  /* inserisco stringhe ed interi nelle liste */
  for( i=0; strings[i]!=NULL; i++) {

    if ( add_ListElement(lists,strings[i],&i) == -1 ) {
      fprintf(stderr,"add_ListElement: %s",strings[i]);
      perror("");
      exit(EXIT_FAILURE);
    }

    if ( add_ListElement(listi,&i,strings[i]) == -1 ) {
      fprintf(stderr,"add_ListElement: %d",i);
      perror("");
      exit(EXIT_FAILURE);
    }
  }

  /* rimuovo tutti gli elementi */
  for( i=0; strings[i]!=NULL; i++) {
    if ( remove_ListElement(lists,strings[i]) != 0 ) {
      fprintf(stderr,"remove_ListElement: %s NON rimosso\n",strings[i]);
      exit(EXIT_FAILURE);
    }
    /* e' ancora presente ? */
    if ( find_ListElement(lists,strings[i]) != NULL ) {
      fprintf(stderr,"find_ListElement: %s NON rimosso\n",strings[i]);
      exit(EXIT_FAILURE);
    }
  }
  for(i-- ; i>=0 ; i--) {
    if ( remove_ListElement(listi,&i) != 0 ) {
      fprintf(stderr,"remove_ListElement: %d NON rimosso\n",i);
      exit(EXIT_FAILURE);
    } 
    if ( find_ListElement(listi,&i) != NULL ) {
      fprintf(stderr,"find_ListElement: %d NON rimosso\n",i);
      exit(EXIT_FAILURE);
    }
  }
  /*** fine test remove ***/

  /* dealloco le ultime strutture */
  free(listi);
  free(lists);
  
  return EXIT_SUCCESS;
}


