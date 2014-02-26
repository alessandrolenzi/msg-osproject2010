/** Il messaggio esteso viene normalizzato e formattato secondo le 
 * regole imposte per il messaggio verso un unico utente.
 * \param msg puntatore a un messaggio esteso di tipo MSG_TO_ONE
 * 
 * \return message_t il messaggio da inviare formattato correttamente
 * \return NULL in caso di errore (sets errno)
 * 
 * \allocates message_t + new buffer
 * */
void message_to_one(message_t *msg, char * sender) {
	char *old_buf; 
	if(msg == NULL || msg->buffer == NULL)
	if (msg->type != MSG_TO_ONE) {
		printf("message_to_one: può essere usati solo con messaggi del tipo MSG_TO_ONE\n");
		errno = EINVAL;
		return;
	}
	old_buf = msg->buffer;
	
	/** 4 caratteri sono necessari per le due parentesi quadre, lo spazio e il terminatore*/
	msg->buffer = Malloc(sizeof(char)*(msg->length = strlen(old_buf)+4+strlen(sender)));
	
	snprintf(msg->buffer, msg->length, "[%s] %s", sender, old_buf);
	msg->length --;
	free(old_buf);
	old_buf = NULL;
}

/** Invia il messaggio al client richiesto.
 * \param msg puntatore al messaggio da inviare
 * 
 * \return 0 se tutto è andato a buon fine
 * \return 1 se qualcosa è andato storto
 */
int send_client_msg(message_t *msg, char *sender, char *receiver) {
		elem_t *h, *p;
		int *sock;
		
		if (msg == NULL || msg->buffer == NULL) {
			errno = EINVAL;
			return -1;
		}
		
		if ((h = hashElement(users_table, receiver)) == NULL) {
			/*Questo significa che l'utente a cui abbiamo mandato il
			 * messaggio non solo non è connesso, ma non è nemmeno tra gli autorizzati*/
			printf("Spiacenti, %s non è un utente di questo servizio\n", receiver);
			return -1;
		}
		
		if (h->payload == NULL) {
			/*L'utente non è connesso*/
			printf("Spiacenti, %s non è attualmente connesso. Il messaggio non può essere inviato\n", receiver);
			return -1;
		}
		p = h->payload;
		sock = p->key;
		
		write_Buffer(&worker_buffer, expand_message(msg, sender, receiver));
		if (msg->type == MSG_TO_ONE)	message_to_one(msg, sender);
		
		requireDirectAccess(&msg_locks, p); 
		/** Abbiamo accesso esclusivo sulla socket dell'utente. Si noti
		 * che a causa della formattazione, si trattano diversamente
		 * i messaggi verso uno e gli altri*/
		sendMessage(*sock, msg);
		releaseDirectAccess(&msg_locks, p);
	
		return 0;
}

/** Interpreta un messaggio di tipo normale */
int client_msg_interpreter(message_t*msg, char * sender) {	
	int sender_length;
	if (msg == NULL) return -1;
	sender_length = strlen(sender);
	
	if (msg->type == MSG_TO_ONE) {	
		int receiver_length = strlen(msg->buffer);
		int buffer_length = strlen(msg->buffer+receiver_length+1);
		char *receiver, *old_buffer = msg->buffer;
		
		/** Destinatario */
		receiver = Malloc(sizeof(char)*(receiver_length+1));
		strncpy(receiver, msg->buffer, receiver_length+1);
		/** Messaggio */				
		msg->buffer = Malloc(sizeof(char)*(buffer_length+1));
		strncpy(msg->buffer, old_buffer+receiver_length+1, buffer_length+1); 	
		/**Copiamo nel nuovo messaggio solo quello che ci serve*/
		send_client_msg(msg, sender, receiver);
		free(old_buffer); old_buffer = NULL;
		free(msg->buffer);
		msg = NULL;
		
	}
	
	return -1;
}
