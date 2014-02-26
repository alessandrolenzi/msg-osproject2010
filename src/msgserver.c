#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include "comsock.h"
#include "genList.h"
#include "genHash.h"
#include "errors.h"
#include "messagebuffer.h"

/** Impostazioni per i messaggi*/
#define TO_ONE_FORMAT "[%s] %s"
#define BCAST_FORMAT "[BCAST] [%s] %s"
#define TO_ONE_FC 3
#define BCAST_FC 11
#define LIST_FORMAT "[LIST]"

/** Impostazioni per gli errori*/
#define ERR_FORMAT "[ERROR] %s: %s"
#define ERR_FC 10


/** Alcune impostazioni del server*/
#define HASH_SIZE 17
#define WORKER_BUFFER_SIZE 64
#define NICK_SIZE 256
#define STRICT 1
#define LOG_OPENING_MODE "w" /** Si potrebbe porre ad "a" per non perdere niente.*/
#define SOCKET "./tmp/msgsock"
#define ADD 1
#define REMOVE 0

/*Tabella Hash degli utenti + Mutex & Condition per l'accesso*/
static hashTable_t* users_table = NULL;
static pthread_mutex_t user_table_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t user_table_cond = PTHREAD_COND_INITIALIZER;
static int UT_inUse = 0;
static message_buffer *worker_buffer = NULL;

static socket_lock *msg_locks = NULL;

/*Lista utenti + lunghezza + Mutex & Condition per l'accesso*/
static char *users_list = NULL;
static int users_list_length = 0;
static int UL_inUse = 0;
static pthread_mutex_t users_list_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t users_list_cond = PTHREAD_COND_INITIALIZER;

void listWait(void) {
	pthread_mutex_lock(&users_list_mtx);
	while (UL_inUse) pthread_cond_wait(&users_list_cond, &users_list_mtx);
		UL_inUse = 1;
	pthread_mutex_unlock(&users_list_mtx);
}

void listSignal(void) {
	pthread_mutex_lock(&users_list_mtx);
		UL_inUse = 0;
		pthread_cond_signal(&users_list_cond);
	pthread_mutex_unlock(&users_list_mtx);
}

void tableWait(void) {
	pthread_mutex_lock(&user_table_mtx);
	while(UT_inUse) pthread_cond_wait(&user_table_cond, &user_table_mtx);
		UT_inUse = 1;
	pthread_mutex_unlock(&user_table_mtx);
}

void tableSignal(void) {
	pthread_mutex_lock(&user_table_mtx);
		UT_inUse = 0;
		pthread_cond_signal(&user_table_cond);
	pthread_mutex_unlock(&user_table_mtx);
}

int disconnectUser(char *username) {
	elem_t **payload, *sl;
	elem_t *h;
	if (username == NULL) {
		errno = EINVAL;
		perror("msgserv, disconnectUser");
		return -1;
	}
	tableWait();
		h = hashElement(users_table, username);
		if (h == NULL) return -1;
		payload = h->payload; /*Ricaviamo dapprima l'elemento della hash*/
		sl = *payload;
		h->payload = NULL; /*Indicazione di "utente disconnesso"*/
	tableSignal();
	return removeSLE(msg_locks, sl);
}

/**prende un codice di errore e l'elemento rappresentante un utente nel 
 * sistema, per inviare a questi la stringa di errore corrispondente a 
 * errcode.
 * \param errcode il codice di errore, 0 < errcode < ERR_NUMBER (costante definita in errors.h)
 * \param h l'elemento della struttura socket_lock riguardante questo utente.
 * */
int sendError(int errcode, elem_t*h, char *receiver) {
	message_t *err = NULL;
	char *errstr = NULL;
	int *socket = NULL; 
	if (errcode > ERR_NUMBER || errcode < 0 || receiver == NULL || h == NULL) {
		errno = EINVAL;
		perror("msgserv, sendError");
		return -1;
	}
	err = Malloc(sizeof(message_t));
	err->buffer = NULL;
	errno = 0;
	err->length = errorString(errcode, &errstr);/*Lo spazio viene allocato dalla funzione*/
	if (err->length <= 0 || errstr == NULL) {
		perror("msgserv, sendError");
		free(errstr);
		free(err);
		return -1;
	}
	err->length += strlen(receiver) + ERR_FC;
	err->buffer = Malloc(sizeof(char)*(err->length+1));
	snprintf(err->buffer, err->length+1, ERR_FORMAT, receiver, errstr);
	free(errstr);
	err->type = MSG_ERROR;
	requireDirectAccess(msg_locks, h);
		socket = h->key;
		(void) sendMessage(*socket, err);
	releaseDirectAccess(msg_locks, h);
	free(err->buffer);
	free(err);
	err = NULL;
	return 0;	
}
/*La funzione sendSocketError deve essere utilizzata unicamente se non
 * è già stato inserito il socket in una socket_lock*/
int sendSocketError(int errcode, int socket) {
	message_t *err = NULL;
	char *errstr = NULL;
	elem_t*el;
	if (errcode < 0 || errcode > ERR_NUMBER) {
		errno = EINVAL;
		perror("msgserv, sendSocketError");
		return -1;
	}
	if ((el = findSL(msg_locks, socket)) != NULL) {
		errno = EINVAL;
		perror("msgserv, sendSocketError può essere usato solo se la socket non è inserita nella struttura socket lock.");
		return sendError(errcode, el, "UNKNOWN USER");
	}
	err = Malloc(sizeof(message_t));
	err->buffer = NULL;
	err->length = errorString(errcode, &errstr); /*Lo spazio viene allocato dalla funzione*/
	if (err->length <= 0) {
		free(errstr);
		free(err);
		perror("msgserv, sendError");
		return -1;
	}
	err->buffer = Malloc(sizeof(char)*(err->length+9+1));
	snprintf(err->buffer, err->length+10, "[ERRORE] %s", errstr);
	free(errstr);
	errstr = NULL;
	err->type = MSG_ERROR;
	sendMessage(socket, err);
	free(err->buffer);
	free(err);
	return 0;
}

/** Aggiorna la stringa contenente la lista degli utenti connessi, 
 * aggiungendo in coda la stringa newuser.
 * \param newuser il nome utente da accodare alla fine 
 * MEMORY: OK
 * */
void refreshUserList(char *user, int mode) { 
	char *old_string = NULL;
	int nick_length = 0;
	if (user == NULL || (mode != 0 && mode != 1)) {
		errno = EINVAL;
		perror("msgserver, refreshUserList:");
		return;
	}
	listWait();
	old_string = users_list;
	nick_length = strlen(user);
	if (mode == ADD) {
		users_list_length += nick_length+2; /*+1 è per lo spazio che intercorre tra i nomi*/
		users_list = Malloc(sizeof(char)* (users_list_length+1));
		snprintf(users_list, users_list_length, "%s %s", old_string, user);
	} else {
		char *beginning = NULL, *end = NULL;
		beginning = strstr(old_string, user);
		if (beginning == NULL) {
			listSignal();
			errno = EINVAL;
			perror("msgserver, refreshUserList: l'username da rimuovere non è nella lista");
			return;
		}
		end = beginning+nick_length+1;/*Togliamo anche lo spazio, o eventualmente lo \0*/
		users_list_length -= (nick_length+2);
		beginning[-1] ='\0'; /*Poiché all'inizio della stringa c'è [LIST] siamo sicuri di non uscire fuori dallo spazio di indirizzamento*/
		users_list = Malloc(sizeof(char)*(users_list_length+1));
		snprintf(users_list, users_list_length, "%s %s", old_string, end);
	}
	listSignal();
	free(old_string);
}

/** Riceve una struttura messaggio inviata dal client e la "normalizza"
 * perchè possa venire utilizzata come un messaggio normale.
 * \param message_t*msg, il messaggio da analizzare (del tipo MSG_TO_ONE)
 * 
 * \return receiver, il nome utente di colui a cui è indirizzato il messaggio
 * \return NULL, in caso di errore (sets ERRNO)
 * \allocates sender (return value), msg->buffer (liberando il vecchio)
 * */
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
	old_buffer = NULL;
	return receiver;
}

/** Riceve una struttura messaggio inviata dal client, che sia del tipo
 * MSG_LIST e la normalizza, ponendo nel buffer la risposta al comando.
 * \param message_t *msg, il messaggio da analizzare (tipo:MSG_LIST)
 * 
 * \return 0 on success
 * \return -1 in caso di errore (sets errno)
 * 
 * \allocates msg->buffer
 * */
int normalizeList(message_t*msg) {
	if(msg == NULL) {
		errno = EINVAL;
		perror("msgserver, normalizeList");
		return -1;
	}
	listWait();
	msg->length = users_list_length;
	msg->buffer = Malloc(sizeof(char)*(msg->length+1));
	strncpy(msg->buffer, users_list, msg->length+1);
	listSignal();
	return 0;
}

/** Riceve una struttura messaggio (che è già stata analizzata dal server)
 * e la formatta perchè venga inviata al client. 
 * \param msg, il messaggio da formattare
 * \param sender, colui che ha inviato il messaggio
 * 
 * \return 0 on success, -1 on error (sets errno)
 * 
 * \allocates msg->buffer
 * */
int formatMessage(message_t *msg, char *sender) {
	char *old_buffer;
	char *format;
	int further_chars;
	if (msg == NULL || msg->buffer == NULL || sender == NULL) {
		errno = EINVAL;
		perror("msgserver, formatMessage");
		return -1;
	}
	old_buffer = msg->buffer;
	
	 /*I messaggi "lista" sono già formattati.*/
	if (msg->type == MSG_LIST) return 0;
	
	if (msg->type == MSG_TO_ONE) {
		further_chars = TO_ONE_FC;
		format = TO_ONE_FORMAT;
	} else if (msg->type == MSG_BCAST) {
		further_chars = BCAST_FC;
		format = BCAST_FORMAT;
	} else {
		errno = EINVAL;
		perror("msgserver, formatMessage");
		return -1;
	}
		
	msg->length = msg->length+further_chars+strlen(sender);
	msg->buffer = Malloc(sizeof(char)*(msg->length+1));
	snprintf(msg->buffer, msg->length+1, format, sender, old_buffer);
	return 0;	
}

/** Invia un messaggio msg all'utente rappresentato nella tabella hash
 * da hash_element.
 * \param msg il messggio da inviare
 * \param sender colui che ha inviato il messaggio
 * \param hash_element l'elemento della hash che rappresenta l'utente
 * 
 *  \return 0 on success (il messaggio è stato inviato)
 *  \return -1 on error (il messaggio non è stato inviato (altre cause)
 *  \return -2 se l'utente non era connesso.
 * 
 * MEMORY:OK
 * */
int sendClient(message_t*msg, char *sender, elem_t*hash_element) {
	int retval = 0;
	message_t_expanded *exp;
	elem_t**p, *h;
	int *socket;
	if (hash_element == NULL || msg == NULL || sender == NULL) {
		errno = EINVAL;
		perror("msgserver, sendClient");
		return -1;
	}
	/**L'utente è disconnesso*/
	
	
	if (msg->type == MSG_ERROR) {
		errno = EINVAL;
		perror("msgserver, sendClient, MSG_ERROR is not accepted by this function");
		return -1;
	}
	exp = expand_message(msg, sender, hash_element->key); /*Viene allocato un messaggio esteso*/
	
	if (formatMessage(msg, sender) == -1) {
		free_Message(exp);
		return -1;
	}
	
	if ((p = hash_element->payload) == NULL || (h = *p) == NULL) return -2; /*Qua ci va una mutex? E su cosa?:(*/
	requireDirectAccess(msg_locks, h); /*Richiediamo accesso unico alla struttura h*/
		h = *p;
		socket = h->key;
		retval = sendMessage(*socket, msg);
	releaseDirectAccess(msg_locks, h); /*Rilasciamo l'accesso alla struttura h*/

	if (retval && (msg->type == MSG_TO_ONE || msg->type == MSG_BCAST))
		write_Buffer(worker_buffer,exp);

	free_Message(exp);
	return retval;
} 

int load_authorized_users(const char *auth_path) { /*M:OK*/
	FILE *auth_file;
	char buf[NICK_SIZE+1];
	int nick_length;
	int user_number = 0; 
	msg_locks = initializeSL();
	auth_file = Fopen(auth_path, "r");
	if (auth_file == NULL) {
		perror("msgserver, load_authorized_users");
		printf("Il file degli utenti autorizzati '%s' specificato non e` valido. Controllare che sia un nome valido e i permessi del file.\n", auth_path);
		return -1;
	}
	users_table = new_hashTable(HASH_SIZE,  compare_string, copy_string, copy_elem_t, hash_string);
	/*Dentro buf abbiamo una riga, contenente uno username seguito da \n*/
	 /*Problema, supponiamo di avere un nickname piu` lungo di 256 caratteri. Come si comporta?*/	
	while(fgets(buf, NICK_SIZE+1, auth_file) != NULL) {
		if ((nick_length = strlen(buf)) > 1) {
			int i = 0, valid = 1; 
			/*Eliminiamo l'eventuale newline dalla fine*/			
			if (buf[nick_length-1] == '\n') 
				buf[nick_length-1] = '\0';
			/*Controllo di validita` sulla stringa. Se STRICT e` 1 (impostazione di default), allora usciamo.
			Altrimenti ci limitiamo ad ignorare la riga e a passare oltre.*/
			while (buf[i] != '\0') {
				if (!(isdigit(buf[i]) || isalpha(buf[i]))) { 
					valid = 0;
					break;
				}
				/*Il payload fa riferimento a un'elem_t contenuto nella
				 * struttura socket_lock, che verrà caricata in un 
				 * momento successivo. NULL serve a indicare che l'utente
				 * è disconnesso, ovverosia non c'è una socket lock.*/
				i++;
				
			}
			if (valid) {
				if (add_hashElement(users_table,buf,NULL) == 0)
					user_number++;
				else
					perror("msgserver, load_authorized_users");

			} else if (STRICT) {
				printf("Il file '%s' degli utenti autorizzati contiene caratteri non ammessi\n", auth_path);
				/*Ovviamente dobbiamo liberarci di tutto lo spazio dinamico allocato.*/
				free_hashTable(&users_table);
				freeSL(&msg_locks);
				return -1;
			} 
			valid = 1;	
		}
	}
	fclose(auth_file);
	return user_number;
}

void* writer(void * log_path) { /*MISSING:GESTERR, anche se funzioni in maniera egregia!*/
	FILE *log_file;
	message_t_expanded *msg;
	if (log_path == NULL) {
		errno = EINVAL;
		pthread_exit((void *) -1);
	}
	log_file = Fopen((const char *) log_path, LOG_OPENING_MODE);
	if (log_file == NULL) {
		perror("msgserver: ");
		printf("Il file di log '%s' specificato non e` valido.\n", (char *) log_path);
		pthread_exit((void *) -1);
	}
	
	while(1) {
		if((msg = read_Buffer(worker_buffer)) == NULL) continue; /*Semplicemente, saltiamo questa istruzione*/
		if (msg->type == MSG_BCAST || msg->type == MSG_TO_ONE){
			fprintf(log_file, "%s:%s:%s\n", msg->sender, msg->receiver, msg->buffer);
			fflush(log_file);
		} else {
			printf("msgserver: sono accettati solo messaggi broadcast e verso singoli utenti");
		}
		free_Message(msg);				
	}
	
	fclose(log_file);
	return (void *) 0;
}
void *worker(void *h) { /*h è l'elemento nella tabella hash che rappresenta l'utente.*/
	message_t msg;
	elem_t *hash_element;
	elem_t **sl;
	char *username;
	int *user_socket;
	
	if ((hash_element = h) == NULL || hash_element->key == NULL || hash_element->payload == NULL){
		errno = EINVAL;
		perror("msgserver, worker");
		pthread_exit((void *) -1);
	}
	sl = hash_element->payload; /*Elemento socket_lock relativo all'utente*/
	user_socket = (*sl)->key;
	username = hash_element->key;
	while(1) {
		int res;
		if ((res = receiveMessage(*user_socket, &msg)) >= 0) {
			/*procedura di interpretazione e gestione del messaggio.*/
			char *receiver;
			switch (msg.type) {
				case MSG_TO_ONE:
					receiver = normalizeToOne(&msg); 
					break;
				case MSG_EXIT:
					disconnectUser(username);
					refreshUserList(username, REMOVE);
					free(msg.buffer);
					pthread_exit((void*)0);
				case MSG_LIST:
					normalizeList(&msg); 
					receiver = username;/*Il destinatario del messaggio che invieremo è l'utente stesso che l'ha mandato.*/
					break;
				case MSG_BCAST: /*Il formato dovrebbe già essere consistente*/
					break;
				default:
					errno = EINVAL; 
					continue;
			}
						
			if (msg.type == MSG_TO_ONE && receiver == NULL) { /*Evidentemente il messaggio non aveva una sintassi corretta.*/
				sendError(6, hash_element->payload, username);
				continue;
			}
			/*Ora abbiamo un messaggio "normale" da gestire. Verrà formattato in
			 * maniera differente a seconda del tipo.*/
			if (msg.type == MSG_BCAST) {
				char * original_buffer = msg.buffer;
				int original_length = msg.length, i;
				/*Scorrimento HASH*/
				for (i = 0; i < users_table->size; i++) {
					pthread_mutex_lock(&user_table_mtx);
					if (users_table->table[i] != NULL) {
						elem_t *aux = users_table->table[i]->head;
						while (aux != NULL) {
							elem_t **rsl;
							rsl = aux->payload;
							if (rsl != NULL && *rsl != NULL) {
								/*Niente di ciò dovrebbe mai poter accadere!*/
								switch (sendClient(&msg, username, aux)) {
									case -2:
										sendError(3, *sl, receiver); break;
									case -1:
										sendError(5, *sl, receiver); break;
									default:
										break;
								}
	
								/*Ritorniamo al formato precedente*/
								free(msg.buffer);
								msg.buffer = original_buffer;
								msg.length = original_length;
							}
							aux = aux->next;
						}
					}
					pthread_mutex_unlock(&user_table_mtx);
				}
			} else {
				elem_t *k;
				if ((k = hashElement(users_table, receiver)) == NULL) {
					sendError(4, *sl, username);
				} else {
					switch (sendClient(&msg, username, k)) {
						case -2:
							sendError(3, *sl, receiver); break;
						case -1:
							sendError(5, *sl, receiver); break;
						default: 
							break;
					}
				}
				
				if (msg.type != MSG_LIST) /*Nel caso contrario, liberiamo "mai".*/
					free(receiver);				
			}
			free(msg.buffer);
		} else {
			perror("msgserver, worker");
			free(msg.buffer);
		}	
	}
	
	return NULL;
}

void* dispatcher(void *args) {
	int fd;
	message_t msg;
	elem_t* element;
	
	/** Iniziamo tentando di creare la socket. Qualora non fosse possibile,
	 * continuare non ha senso. CreateServerChannel fa già tutti i tentativi
	 * possibili per connettersi da se.*/
	if((fd = createServerChannel(SOCKET)) <= 0) {
		perror("msgserver, dispatcher");
		printf("Tentativo di creazione della socket %s fallito.\n", SOCKET);
		exit(-1);
	}
	users_list_length = strlen(LIST_FORMAT);
	users_list = Malloc(sizeof(char)*(users_list_length+1));
	strncpy(users_list, LIST_FORMAT, users_list_length+1);
	/** Il ciclo infinito seguente riceve le connessioni, e se rispettano
	 * il protocollo le accetta. Inoltre, non appena è possibile, passa
	 * le competenze al thread worker dell'utente connesso*/
	while (1) {
		pthread_t worker_id;
		int current_socket; 
		
		/** Non è stato possibile stabilire la connessione iniziale*/
		if ((current_socket = acceptConnection(fd)) == -1) {
			perror("msgserver, dispatcher");
			continue;
		}
		
		/** Il primo messaggio ricevuto, una volta stabilita la connessione,
		 * deve essere del tipo MSG_CONNECT, altrimenti si procede a mostrare
		 * un errore, in quanto il protocollo non è stato rispettato.*/
		
		if (receiveMessage(current_socket, &msg) >= 0 && msg.type == MSG_CONNECT) { 
			
			/**Il messaggio iniziale non è valido. L'utente dovrà tentare
			* di inviarne un altro, o rinunciare.*/
			if (msg.length == 0) {			
				sendSocketError(current_socket, 0); /*Questo non accede in mutua esclusione!*/
				closeSocket(current_socket);
				free(msg.buffer);
				continue;
			}
			
			
			/** Se si trova un record nella tabella hash, allora il nome
			 * utente inserito è valido. Tuttavia, potrebbe darsi che 
			 * ci sia un'altro utente connesso con lo stesso nome. In tal caso,
			 * ovviamente, mandiamo un messaggio di errore e andiamo oltre*/
			/*Qua non è necessario accedere in mutua esclusione, a meno che non si verifichino delle parti
			 * interne. Questo perché non è prevista la cancellazione di una chiave dalla tabella hash*/
			if ((element = hashElement(users_table, msg.buffer)) != NULL) {
				elem_t **sl_pointer = element->payload;
				int connected = 0;
				char *username = msg.buffer;
				tableWait();
					connected = sl_pointer != NULL;
				tableSignal();
				if (connected) {
					socketWait(msg_locks);
					connected = connected && (*sl_pointer != NULL);
					socketSignal(msg_locks);
				}
				
				if (connected) {					
					int *old_user_socket = (*sl_pointer)->key;
					/** Al fine di inviare un ping non ci interessa sapere chi sia colui che è connesso.
					 * Se ci viene restituito 1, allora qualcuno è connesso.*/
					if (ping(*old_user_socket) == 1) {
						sendSocketError(2,current_socket);
						closeSocket(current_socket);
						free(msg.buffer);
						continue;
					}
				}
				element->payload = insertSL(msg_locks, current_socket, 1);
				if (element->payload == NULL) {
					perror("msgserver, dispatcher:");
					continue;
				}
				sl_pointer = element->payload;
				/*Prepariamo e inviamo il messaggio "OK" al client.*/
				msg.buffer = NULL;
				msg.length = 0;
				msg.type = MSG_OK;
				sendMessage(current_socket, &msg);
				if (pthread_create(&worker_id, NULL, &worker, element) == -1) {
					perror("msgserver, dispatcher: ");
					releaseDirectAccess(msg_locks, *sl_pointer);
					removeSL(msg_locks, current_socket);
					printf("Connessione rifiutata\n");
					free(username);
					continue;
					return NULL;
				}
				
				pthread_detach(worker_id);
				releaseDirectAccess(msg_locks, *sl_pointer);
				refreshUserList(username, ADD);
				printf("Connessione di %s accettata\n", username);
				free(username);
				continue;					
			
			} else {
				sendSocketError(1, current_socket);
				closeSocket(current_socket);
				free(msg.buffer);				
			}		
			
		} else {
			closeSocket(current_socket);
			free(msg.buffer);
		}
	}
	return NULL;
}

int main(int argc, char* argv[]) {
	pthread_t writer_id, dispatcher_id;
	
	if (argc != 3) {
		printf("Sono richiesti due parametri\n");
		printf("Sintassi corretta: $msgserv file_utenti_autorizzati file_log\n");
		return -1;
	}
	worker_buffer = initialize_Buffer(WORKER_BUFFER_SIZE);
	if(load_authorized_users(argv[1]) <= 0) {
		printf("Il caricamento del file utenti autorizzati non è andato a buon fine.\n");
		return -1;
	}
	if(pthread_create(&writer_id, NULL, &writer, argv[2]) == -1) {
		return -1;
	}
	if (pthread_create(&dispatcher_id, NULL, &dispatcher, NULL) == -1) {
		return -1;
	} 
	pthread_join(writer_id, NULL);
	pthread_join(dispatcher_id, NULL);
	sleep(3);
	free_Buffer(&worker_buffer);
	freeSL(&msg_locks);
	free_hashTable(&users_table);
	return 0;
}
