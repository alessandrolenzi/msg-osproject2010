/**
   \file genHash.c
   \author Alessandro Lenzi, aless.lenzi@gmail.com
   \brief  implementazione di funzioni e procedura per tabelle Hash attraverso la libreria genList di liste generiche.
   
Si dichiara che il contenuto di questo file e' in ogni sua parte opera
originale dell' autore.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "genHash.h"


hashTable_t * new_hashTable (unsigned int size,  int (* compare) (void *, void *), void* (* copyk) (void *),void* (*copyp) (void*),unsigned int (*hashfunction)(void*,unsigned int)) {
		
		int i, t_errno; /** t_errno viene usata per tenere conto di errno che potrebbero essere sovrascritti.*/
		hashTable_t *new;
		if (compare == NULL || copyk == NULL || copyp == NULL || hashfunction == NULL || size == 0){
			errno = EINVAL; return NULL;
		}
		
		new = malloc(sizeof(hashTable_t));
		if (new == NULL) return NULL;
		
		new->table = (list_t **) malloc(sizeof(list_t*)*size);
		if (new->table == NULL) {
			/** Nonostante nel manuale non sia specificato se free imposta degli errori, 
			 * si copia il valore impostato dalla malloc per sicurezza.
			 * La free è utilizzata per eliminare la hashTable la cui allocazione è fallita.*/
			t_errno = errno;
			free(new);
			errno = t_errno;
			return NULL;
		}
		
		new->size = size;
		new->compare = compare;
		new->copyk = copyk;
		new->copyp = copyp;
		new->hash = hashfunction;
		for (i = 0; i < size; i++) new->table[i] = NULL;
		return new;
}

unsigned int hash_int (void * key, unsigned int size) {
	int *k;
	errno = 0;
	if (key == NULL || size == 0) {
		errno = EINVAL;
		/** Nonostante il tipo della funzione sia unsigned int, da delle 
		 * prove su gcc sembra che il valore -1 venga riconosciuto all'uscita.
		 * In ogni caso, il test all'uscita sarà da effettuarsi sull'errno.*/
		return -1;
	}
	k = (int *) key;
	return *k % size; 
}

unsigned int hash_string (void * key, unsigned int size) {
	unsigned int hash = 0;
	int c;
	char *str;
	errno = 0;
	if (key == NULL || size == 0) {
		errno = EINVAL;
		return -1;
	}
	str = key;
 	/**la funzione è una mappatura da caratteri e interi absata su ASCII
 	* leggermente migliorata - spero!- dalla moltiplicazione per la lunghezza
 	* della stringa meno l'indice, in modo da attribuire pesi piuttosto diversi a 
 	* ogni lettera.
 	*/
	while ((c = *str++)) {
		hash += c;
	}
	return hash % size;
}

int add_hashElement(hashTable_t * t,void * key, void* payload ) {
	
	unsigned int h;
	
	/** Da questo punto in poi, si verificherà sempre che la tabella 
	 * contenuta nella struttura hashTable_t passata, sia stata
	 * inizializzata. Difatti, in caso contrario, non otterremmo
	 * i risultati voluti.*/	
	if (t == NULL || t->table == NULL || key == NULL) {
		errno = EINVAL; return -1;
	}
	
	/** errno = 0. Ipotizziamo che il programmatore abbia seguito le specifiche
	 * date nella documentazione, settando errno a 0 all'entrata nella funzione
	 * hash, e non ripetiamo l'assegnazione a zero della variabile globale.*/	
	h = t->hash(key, t->size);
		
	if (errno != 0 || h < 0)	return -1;
	
	/** Se la condizione sottostante è vera, ciò implica che non abbiamo mai allocato un valore;
	 * non avremo pertanto una lista! La aggiungiamo ora.
	 * Assumiamo, inoltre, che un eventuale errore nella creazione o nell'aggiunta verrebbe lanciato
	 * dalla new_List o da add_ListElement.*/
	
	if (t->table[h] == NULL) t->table[h] = new_List(t->compare, t->copyk, t->copyp);
	return add_ListElement(t->table[h], key, payload);	
}

void * find_hashElement(hashTable_t * t,void * key) { 
	
	unsigned int h;
	void *payload;
	elem_t *found; /** found memorizzerà l'elemento della lista il cui payload dovrà essere copiato.*/
	
	if(t == NULL ||t->table == NULL || key == NULL) {
		errno = EINVAL; return NULL;
	}
	
	/** Viene fatta la stessa assunzione della funzione add_ .*/
	h = t->hash(key, t->size);
	if (errno != 0 || h < 0)	return NULL;
	
	if (t->table[h] == NULL) {	errno = ENOKEY; return NULL; } 
	
	if((found = find_ListElement(t->table[h], key)) == NULL) {
		/** Se la lista non è vuota, l'eventuale errno ENOKEY è impostato da find_ListElement*/
		return NULL;
	}
	/**Si ipotizza che un eventuale messaggio di errore venga impostato dalla funzione copyp.
	 * Inoltre non ci preoccupiamo di controllare se copyp restituisce un valore effettivo.
	 * Anche qualora non lo fosse, avrebbe valore NULL e restituiremmo NULL come previsto
	 * nelle specifiche.*/
	payload = t->copyp(found->payload); 
	return payload;
}

/** la funzione hashElement restituisce un puntatore alla locazione di
 * memoria dell'elemento. è così possibile modificarlo*/
elem_t * hashElement(hashTable_t *t, void *key) {
	unsigned int h;
	elem_t *found; /** found memorizzerà l'elemento della lista il cui payload dovrà essere copiato.*/
	
	if(t == NULL ||t->table == NULL || key == NULL) {
		errno = EINVAL; return NULL;
	}
	
	/** Viene fatta la stessa assunzione della funzione add_ .*/
	h = t->hash(key, t->size);
	if (errno != 0 || h < 0)	return NULL;
	
	if (t->table[h] == NULL) {	errno = ENOKEY; return NULL; } 
	
	if((found = find_ListElement(t->table[h], key)) == NULL) {
		/** Se la lista non è vuota, l'eventuale errno ENOKEY è impostato da find_ListElement*/
		return NULL;
	}
	return found;
}

int remove_hashElement(hashTable_t * t,void * key) {
	unsigned int h;
	
	if(t == NULL || t->table == NULL || key == NULL) {
		errno = EINVAL; 
		return -1;
	}
	
	h = t->hash(key, t->size);
	if (errno != 0 || h < 0)
		return -1;
	
	if (t->table[h] != NULL)
		return remove_ListElement(t->table[h], key);
		
	return 0;
}

void free_hashTable (hashTable_t ** pt) {
	int i;
	errno = 0;
	if (*pt == NULL) {
		errno = EINVAL;
		return;
	}
	
	/** Se eseguiamo il controllo successivo, significa che abbiamo una
	 * tabella non inizializzata e, tuttavia, la struttura lo è.
	 * Sicuramente, questa è una tabella hash non ben strutturata,
	 * ergo possiamo parlare di argomento invalido. In ogni caso,
	 * liberiamo la struttura dati *pt prima di uscire. */
	if ((*pt)->table == NULL) {
		free(*pt);
		*pt = NULL;		
		errno = EINVAL;
		return;
	}
	for (i = 0; i < (*pt)->size; i++) {
		if((*pt)->table[i] != NULL)
			free_List(&((*pt)->table)[i]);
	}
	free((*pt)->table);
	(*pt)->table = NULL; /* Errore */
	free(*pt);
	*pt = NULL;
}

void print_Table(hashTable_t *t) {
	int i;
	if (t == NULL) return;
	for (i = 0; i < t->size; i++) {
		printf("%d:", i);
		if (t->table[i] == NULL) printf("NULL\n\n");
		else {
			elem_t *aux = t->table[i]->head;
			while (aux != NULL) {
				char *key = aux->key;
				elem_t *payload = aux->payload;
				if (payload == NULL) printf("(%s, NULL)\t", key);
				else {
					int *socket, *lock;
					socket = payload->key;
					lock = payload->payload;
					printf("(%s, %d, %d)", key, *socket, *lock);
				}
				aux = aux->next;
			}
			printf("\n\n");
		}
	}
}

