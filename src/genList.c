/**
   \file genList.c
   \author Alessandro Lenzi, aless.lenzi@gmail.com
   \brief  implementazione di funzioni e procedura per le liste generiche.
   
Si dichiara che il contenuto di questo file e' in ogni sua parte opera
originale dell' autore.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "genList.h"




list_t * new_List(int (* compare) (void *, void *),void* (* copyk) (void *),void* (*copyp) (void*)) {
	list_t *list = NULL;
	/** La funzione new_List verifica che le funzioni date come parametro siano definite.*/
	if (compare == NULL || copyk == NULL || copyp == NULL) {
		errno = EINVAL; return NULL;
	} 
	list = (list_t *) malloc(sizeof(list_t));
	if (list == NULL)return NULL;
	
	list->head = NULL;
	list->compare = compare;
	list->copyk = copyk;
	list->copyp = copyp;
	return list;
}

int add_ListElement(list_t * t,void * key, void* payload) {
		elem_t *new, *aux;
		if(t == NULL || key == NULL) {errno = EINVAL;return -1;} 
		aux = t->head;
		while(aux != NULL) { 											
			if (t->compare(aux->key, key) == 0){
				/** Ho preferito distinguere tra il caso in cui gli argomenti non fossero definiti,
				 * con errno corrispondente EINVAL, da quello in cui la chiave non potesse venire
				 * accettata in quanto precedentemente usata.*/
				errno = EINVAL;
				return -1;
			}
			aux = aux->next;
		}
		new = malloc(sizeof(elem_t));
		if (new == NULL) return -1;
		new->key = t->copyk(key);
		new->payload = t->copyp(payload);
		new->next = t->head;
		t->head = new;
		return 0;		
}

int remove_ListElement(list_t * t,void * key) {
	elem_t* aux, *prev = NULL;
	if (t == NULL || key == NULL) {errno = EINVAL; return -1;}
	/**Se anche la lista fosse vuota (aux = t->head == NULL) la procedura
	 * prosegue come usuale: eliminare un elemento da una lista vuota
	 * la lascia inalterata; non ho gestito differentemente il caso.*/
	aux = t->head;
	while (aux != NULL) {
		if (t->compare(aux->key, key) == 0) {
			/** Per ovviare al problema di gestire "separatamente" 
			* l'eliminazione di un elemento in testa da uno interno,
			* l'if si limita a mantenere la consistenza della lista
			* attraverso gli opportuni collegamenti.
			* Il puntatore found serve a tener nota dell'elemento 
			* da eliminarsi al fine di liberare la memoria occupata.*/
			elem_t *found = aux; 
			if(prev == NULL)
				t->head = aux->next;
			else
				prev->next = aux->next;
	
			free(found->key);
			free(found->payload);
			free(found);
			return 0;
		}
		
		prev = aux;
		aux = aux->next;
	}
	return 0;
	
}

void free_List (list_t ** pt) {
	elem_t *aux, *temp;
	/** Poiché la funzione non restituisce un valore alla sua uscita,
	 * per essere certo al 100% di poter controllarne l'esito, pongo
	 * all'entrata - prima di ogni possibile errore - errno a zero.
	 * All'uscita dalla funzione, sarà sufficiente controllare se l'errno
	 * è != 0. In seguito e nel genHash.c ho adottato la medesima convenzione.*/
	errno = 0;
	if (pt == NULL || *pt == NULL) {errno = EINVAL; return;}
	aux = (*pt)->head;
	while(aux != NULL) {
		temp = aux->next;
		if (aux->key != NULL) {
			free(aux->key);
			aux->key = NULL;
		}
		if (aux->payload != NULL) { 
			free(aux->payload);
			aux->payload = NULL;
		}
		free(aux);
		aux = temp;
	}
	aux = NULL;
	free(*pt);
	*pt = NULL;
	
} 

elem_t * find_ListElement(list_t * t,void * key) {
	elem_t* aux;
	if (t == NULL || key == NULL) {errno = EINVAL; return NULL;}
	aux = t->head;
	while (aux != NULL) {
		if (t->compare(aux->key, key) == 0) {
			return aux;
		}
		aux = aux->next;
	}
	errno = ENOKEY; /* In questo contesto ho impostato l'errno unicamente per distinguere, in caso di uscita
					*con valore restituito "NULL", tra il caso (corretto) in cui l'elemento non sia stato
					*individuato perchè non presente e quello in cui sia stato passato un valore invalido per la lista.*/
	return aux;
}
