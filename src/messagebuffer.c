#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "messagebuffer.h"

/*Per ora questa libreria sembra essere l'unica a posto con la gestione
 * della memoria e tutte le funzioni.*/


int invalid_Message(message_t_expanded *msg) {
	if (msg == NULL) {
		errno = EINVAL;
		return 1;
	}
	return 0;
}

void copy_Message(message_t_expanded* dest, message_t_expanded* src) {
	int receiver_length, sender_length;
	if (invalid_Message(dest) || invalid_Message(src))	return;
	receiver_length = strlen(src->receiver);
	sender_length = strlen(src->sender);
	/**Messaggio:
	 * Attenzione: questo metodo non tiene conto della memoria effettivamente allocata
	 * e "spera" che sia circa quella del messaggio. Ciononostante, potrebbe creare degli sprechi
	 * di memoria piuttosto ingenti. Si può creare una struttura di tracking delle grandezze della memoria.
	 * Vedere se ho tempo.*/
	if (dest->buffer != NULL && (dest->length < src->length || src->length < dest->length/2)){
		free(dest->buffer);
		dest->buffer = NULL;
	}
	if (dest->buffer == NULL)
		dest->buffer = Malloc(sizeof(char)*(src->length+1));
	
	/** Destinatario*/
	if (dest->receiver != NULL) {
		int old_receiver_length = strlen(dest->receiver);
		if (old_receiver_length <= receiver_length || receiver_length < old_receiver_length/2){
			free(dest->receiver);
			dest->receiver = NULL;
		}
	}  	
	if (dest->receiver == NULL)
		dest->receiver = Malloc(sizeof(char)*(receiver_length+1));
	
	/** Mittente*/
	if (dest->sender != NULL) {
		int old_sender_length = strlen(dest->sender);
		if (old_sender_length < sender_length || sender_length < old_sender_length/2){
			free(dest->sender);
			dest->sender = NULL;
		}
	}  	
	if (dest->sender == NULL)
		dest->sender = Malloc(sizeof(char)*(sender_length+1));
		
	strncpy(dest->buffer, src->buffer, src->length+1);
	strncpy(dest->sender, src->sender, sender_length+1);
	strncpy(dest->receiver, src->receiver, receiver_length+1);
	dest->type = src->type;
	dest->length = src->length;
}

message_t_expanded* expand_message(message_t *msg, char *sender, char *dest) {
	message_t_expanded *res;
	int rec_size, send_size, buf_size;	
	if (msg == NULL || sender == NULL || dest == NULL)  {
		errno = EINVAL;
		return NULL;
	}
	res = Malloc(sizeof(message_t_expanded));
	res->receiver = Malloc(sizeof(char)*((rec_size = strlen(dest))+1));
	strncpy(res->receiver, dest, rec_size+1);
	res->sender = Malloc(sizeof(char)*((send_size = strlen(sender))+1));
	strncpy(res->sender, sender,send_size+1);
	res->buffer = Malloc(sizeof(char)*((buf_size = msg->length)+1));
	strncpy(res->buffer, msg->buffer, buf_size+1);
	res->length = buf_size;
	res->type = msg->type;
	return res;
}

message_t* normalize_message(message_t_expanded*msg) {
	message_t*new;
	new = Malloc(sizeof(message_t));
	/*Non allochiamo nuova memoria per la struttura, ne copiamo il vecchio
	 * messaggio nel nuovo, ma semplicemente ci limitiamo a puntarlo,
	 * preservandolo.*/
	new->buffer = msg->buffer;
	new->length = msg->length;
	new->type = new->type;
	free(msg);
	msg = NULL;
	return new;
}

message_buffer * initialize_Buffer(unsigned int size) {
	message_buffer *b;
	int i;
	if (size < 1) {
		printf("messagebuffer.h: il campo 'size' di initialize_Buffer deve essere >= 1\n");
		errno = EINVAL;
		return NULL;
	}
	b = Malloc(sizeof(message_buffer));
	b->read = b->write = b->length = 0;
	b->size = size;
	b->messages = Malloc(sizeof(message_t_expanded)*b->size);
	for (i = 0; i < b->size; i++) {
		b->messages[i].receiver = b->messages[i].sender = b->messages[i].buffer = NULL;
		b->length = 0;
	}
	if (pthread_mutex_init(&b->mtx, NULL) == -1 || pthread_cond_init(&b->full, NULL) == -1
	   || pthread_cond_init(&b->free, NULL) == -1) {
		printf("messagebuffer.h: impossibile inizializzare i semafori e i lock\n");
		perror("messagebuffer.h:");
		return NULL;
	}
	return b;	
}

int write_Buffer(message_buffer* b, message_t_expanded *msg) { 
	message_t_expanded *cur;
	if (b == NULL || invalid_Message(msg)) { errno = EINVAL; return -1;}
	
	pthread_mutex_lock(&b->mtx);
	while (b->length == b->size) /** Il buffer è pieno, ci sospendiamo in attesa*/
		pthread_cond_wait(&b->full, &b->mtx);
	/** Inizio sezione critica*/
		cur = b->messages+b->write;
		if (cur == NULL) return -1;
		copy_Message(cur, msg);
		b->write = (b->write + 1) % b->size;
		b->length++;
	/** Fine Sezione Critica*/
	pthread_cond_signal(&b->free);
	pthread_mutex_unlock(&b->mtx);
	return 0;	
		
	
}

message_t_expanded* read_Buffer(message_buffer* b) {
	message_t_expanded *res;
	message_t_expanded *cur;
	
	pthread_mutex_lock(&b->mtx);
	
	while(b->length == 0) /** Ci sospendiamo in attesa di un messaggio*/
		pthread_cond_wait(&b->free, &b->mtx);
		
	/** Inizio sezione critica*/
		cur = b->messages+b->read;
		res = Malloc(sizeof(message_t_expanded));
		res->buffer = res->sender = res->receiver = NULL;
		
		copy_Message(res, cur);
		
		b->read = (b->read +1) % b->size;
		b->length--;	
	
	/** Fine sezione critica*/
	pthread_cond_signal(&b->full);
	pthread_mutex_unlock(&b->mtx);
	return res;
	
}

void free_Buffer(message_buffer **b) {
	int i = 0;
	message_t_expanded *aux;
	if (b == NULL || *b == NULL || (*b)->messages == NULL) {
		errno = EINVAL;
		return;
	}
	
	for (i = 0; i < (*b)->size; i++) {
		if ((*b)->messages+i != NULL) {
			aux = (*b)->messages+i;
			if (aux->receiver != NULL)free(aux->receiver);
			if (aux->sender != NULL)free(aux->sender);
			if (aux->buffer != NULL)free(aux->buffer);
			
		}
	}
	free((*b)->messages);
	free(*b);
}


void free_Message(void *m) {
	message_t_expanded *msg = m;
	free(msg->buffer);
	free(msg->sender);
	free(msg->receiver);
	free(msg);
	msg = NULL;
}
/************************************************** CHECKED ABOVE ********************************************************/

