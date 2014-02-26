#include <stdio.h>
#include <unistd.h>
#include <sys/un.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include "libmsg.h"

char* normalizeToOne(message_t*msg) {
	char *receiver, *old_buffer;
	int receiver_length, buffer_length;
	if (msg == NULL || msg->buffer == NULL) {
		errno = EINVAL;
		return NULL;
	}
	old_buffer = msg->buffer;
	receiver_length = strlen(msg->buffer);
	buffer_length = strlen(msg->buffer+receiver_length+1);
	
	receiver = Malloc(sizeof(char)*(receiver_length+1));
	strncpy(receiver, msg->buffer, receiver_length+1);
	
	msg->buffer = Malloc(sizeof(char)*(buffer_length+1));
	strncpy(msg->buffer, old_buffer+receiver_length+1, buffer_length+1);
	
	msg->length = buffer_length;
	free(old_buffer);
	return receiver;
}

int normalizeList(message_t*msg) {
	if(msg == NULL) {
		errno = EINVAL;
		return -1;
	}
	msg->length = strlen(users_list);
	free(msg->buffer);
	msg->buffer = Malloc(sizeof(char)*(msg->length+1));
	strncpy(msg->buffer, users_list, msg->length+1);
	return 0;
}

int formatMessage(message_t *msg, char *sender) {
	char *old_buffer;
	char *format;
	int further_chars;
	if (msg == NULL || msg->buffer == NULL || sender == NULL) {
		errno = EINVAL;
		return -1;
	}
	old_buffer = msg->buffer;
	if (msg->type == MSG_LIST) return 0;
	if (msg->type == MSG_TO_ONE) {
		further_chars = TO_ONE_FC;
		format = TO_ONE_FORMAT;
	} else {
		further_chars = BCAST_FC;
		format = BCAST_FORMAT;
	}
	msg->length += further_chars+strlen(sender);
	msg->buffer = Malloc(sizeof(char)*(msg->length+1));
	snprintf(msg->buffer, msg->length+1, format, sender, old_buffer);
	if (msg->type != MSG_BCAST)
		free(old_buffer);
	return 0;	
}

int sendClient(message_t*msg, char *sender, elem_t*hash_element) {
	message_t_expanded *exp;
	elem_t**p, *h;
	int *socket, retval = 1;
	if (hash_element == NULL || msg == NULL || sender == NULL) {
		errno = EINVAL;
		perror("");
		return -1;
	}
	
	if ((p = hash_element->payload) == NULL || (h = *p) == NULL) {
		printf("Error2\n"); /*L'utente non è connesso*/
		return -1;
	}
	/*Da qui in poi dovrebbe andare tutto a buon fine*/
	exp = expand_message(msg, sender, hash_element->key);
	h = *p;
	formatMessage(msg, sender);
	socket = h->key;
	requireDirectAccess(msg_locks, h);
		sendMessage(*socket, msg);
	releaseDirectAccess(msg_locks, h);
	
	if (retval)
		write_Buffer(worker_buffer,exp);
		
	free_Message(exp);
	return 0;
} 

int processMessage(message_t *msg, char*sender) {
	char *receiver;
	int retval;
	if (msg == NULL || msg->buffer == NULL || sender == NULL) {
		errno = EINVAL;
		return -1;
	}
	/**Prologo:
	 * 1. MSG_TO_ONE e MSG_LIST si "preparano" il messaggio con il formato
	 * 2. MSG_BCAST è già pronto
	 * 3. L'operazione di prologo per MSG_EXIT pone termine a tutto.
	*/
	
	switch (msg->type) {
		case MSG_TO_ONE:
			receiver = normalizeToOne(msg); break;
		case MSG_EXIT: /*Per ora non ci interessa.*/
			return 0;
		case MSG_LIST:
			normalizeList(msg); 
			receiver = sender;
			break;
		case MSG_BCAST:
			break;
		default:
			errno = EINVAL; return -1;
	}
	printf("%c %s\n",  msg->type, sender);
	if (msg->type == MSG_TO_ONE && receiver == NULL) {
		perror("msglib: ");
		return -1;
	}

	/*Ora abbiamo un messaggio "normale" da gestire. Verrà formattato in
	 * maniera differente a seconda del tipo.*/
	
	if (msg->type == MSG_BCAST) {
		char * original_buffer = msg->buffer;
		int original_length = msg->length, i;
		for (i = 0; i < users_table->size; i++) {
			pthread_mutex_lock(&user_table_mtx);
			if (users_table->table[i] != NULL) {
				elem_t *aux = users_table->table[i]->head;
				while (aux != NULL) {
					elem_t **sl;
					sl = aux->payload;
					if (sl != NULL && *sl != NULL) {
						sendClient(msg, sender, aux);
						/*Ritorniamo al formato precedente*/
						free(msg->buffer);
						msg->buffer = original_buffer;
						msg->length = original_length;
					}
					aux = aux->next;
				}
			}
			pthread_mutex_unlock(&user_table_mtx);
		}
	} else {
		elem_t *h;
		if ((h = hashElement(users_table, receiver)) == NULL) {
			printf("ERROR\n"); return -1;
		}
		retval = sendClient(msg, sender, h);
		if (MSG_LIST != msg->type) /*In tal caso receiver = sender e perciò sta alla funzione chiamante liberare.*/
		free(receiver);				
	}
	
	
	return retval;
}

int createError(const char* error, message_t* msg) {
	msg = Malloc(sizeof(message_t));
	msg->length = strlen(error)+ERR_FC;
	msg->buffer = Malloc(sizeof(char)*(msg->length+1));
	snprintf(msg->buffer, msg->length+1, ERR_FORMAT, error);
	return 0;
}
