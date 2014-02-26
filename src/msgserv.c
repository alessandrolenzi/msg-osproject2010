/**
*	\file msgserv.c
* 	\author Alessandro Lenzi, Mat. N° 438142, aless.lenzi@gmail.com
*   \brief il server che gestisce gli utenti del progetto MSG
*/
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
#include <signal.h>

#include "comsock.h"
#include "genList.h"
#include "genHash.h"
#include "errors.h"
#include "messagebuffer.h"

/** Impostazioni per i messaggi*/
/** Formato MSG_TO_ONE */
#define TO_ONE_FORMAT "[%s] %s"
/** Formato MSG_BCAST */
#define BCAST_FORMAT "[BCAST][%s] %s"
/** Formato della lista */
#define LIST_FORMAT "[LIST]"

/** Caratteri ulteriori necessari per MSG_TO_ONE */
#define TO_ONE_FC 3
/** Caratteri ulteriori necessari per MSG_BCAST */
#define BCAST_FC 10

/** Impostazioni per gli errori*/
/** Formato MSG_ERROR */
#define ERR_FORMAT "[ERROR] %s: %s"
/** Caratteri ulteriori necessari per MSG_ERROR */
#define ERR_FC 10

/** Impostazioni per il file di log*/
/** Modalità apertura del log file */
#define LOG_OPENING_MODE "w"
/** Formato scrittura log (mittente, destinatario, messaggio) (%s) */
#define LOG_FORMAT "%s:%s:%s\n"

/** Alcune impostazioni del server*/
/** Dimensioni tabella HASH */
#define HASH_SIZE 17
/** Dimensione buffer messaggi */
#define writer_buffer_SIZE 64
/** Dimensione massima nickname */
#define NICK_SIZE 256
/** Modalità di tolleranza per input*/
#define STRICT 1
/** Indirizzo Socket */
#define SOCKET "./tmp/msgsock"
/** Costante aggiunta */
#define ADD 1
/** Costante rimozione */
#define REMOVE 0

/**Tabella Hash degli utenti */
static hashTable_t* users_table = NULL;
/** Mutex per l'accesso alla tabella hash */
static pthread_mutex_t user_table_mtx = PTHREAD_MUTEX_INITIALIZER;
/** Condition per l'accesso alla tabella hash */
static pthread_cond_t user_table_cond = PTHREAD_COND_INITIALIZER;
/** Variabile libero/occupato per la tabella hash */
static int UT_inUse = 0;
/** Buffer Messaggi */
static message_buffer *writer_buffer = NULL;
/** Socket Lock */
static socket_lock *msg_locks = NULL;

/** Lista utenti */
static char *users_list = NULL;
/** Lunghezza lista utenti */
static int users_list_length = 0;
/** Variabile libero/occupato per la lista utenti */
static int UL_inUse = 0;
/** Mutex accesso lista utenti */
static pthread_mutex_t users_list_mtx = PTHREAD_MUTEX_INITIALIZER;
/** Condition accesso lista utenti */
static pthread_cond_t users_list_cond = PTHREAD_COND_INITIALIZER;

/** Serve a verificare se è stato ricevuto un segnale di uscita */
static int signal_exit = 0;
/** Mutex per il controllo di signal_exit*/
static pthread_mutex_t delete_mtx = PTHREAD_MUTEX_INITIALIZER;

/*Procedure per l'accesso in mutua esclusione alle strutture dati condivise
che non hanno queste funzioni implementate tramite qualche libreria.*/

/** Funzione per l'accesso in mutua esclusione alla lista degli utenti.
 * Ritorna quando è stato ottenuto.*/
void listWait(void) {
	pthread_mutex_lock(&users_list_mtx);
	while (UL_inUse) pthread_cond_wait(&users_list_cond, &users_list_mtx);
		UL_inUse = 1;
	pthread_mutex_unlock(&users_list_mtx);
}

/** Funzione per liberare l'accesso alla lista degli utenti.
 */
void listSignal(void) {
	pthread_mutex_lock(&users_list_mtx);
		UL_inUse = 0;
		pthread_cond_signal(&users_list_cond);
	pthread_mutex_unlock(&users_list_mtx);
}
/** Funzione per l'accesso in mutua esclusione alla tabella hash degli 
 * utenti*/
void tableWait(void) {
	pthread_mutex_lock(&user_table_mtx);
	while(UT_inUse) pthread_cond_wait(&user_table_cond, &user_table_mtx);
		UT_inUse = 1;
	pthread_mutex_unlock(&user_table_mtx);
}
/** Funzione per liberare l'accesso alla tabella hash degli utenti.
*/
void tableSignal(void) {
	pthread_mutex_lock(&user_table_mtx);
		UT_inUse = 0;
		pthread_cond_signal(&user_table_cond);
	pthread_mutex_unlock(&user_table_mtx);
}

/** Apre il file contenente la lista degli utenti, verifica la correttezza
 * degli username contenuti e carica quelli validi all'interno della tabella
 * hash, che inizializza.
 * \param auth_path riferimento al file contenente la lista utenti.
 **/
int load_authorized_users(const char *auth_path) {
	FILE *auth_file;
	char buf[NICK_SIZE+1];
	int nick_length;
	int user_number = 0; 
	
	auth_file = Fopen(auth_path, "r");
	if (auth_file == NULL) {
		perror("msgserver, load_authorized_users");
		printf("Il file degli utenti autorizzati '%s' specificato non e` valido. Controllare che sia un nome valido e i permessi del file.\n", auth_path);
		return -1;
	}
	users_table = new_hashTable(HASH_SIZE,  compareString, copyString, copy_elem_t, hash_string);
	/*Dentro buf abbiamo una riga, contenente uno username seguito da \n*/
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

/** Aggiorna la stringa contenente la lista degli utenti connessi, 
* aggiungendo in coda la stringa newuser.
* \param newuser il nome utente che deve subire una trasformazione nella lista
* \param mode ADD oppure REMOVE, rispettivamente per connessione e disconnessione		
* */
void refreshUserList(char *user, int mode) { 
	char *old_string = NULL;
	int nick_length = 0;
	if (user == NULL || (mode != 0 && mode != 1)) {
		errno = EINVAL;
		perror("msgserver, refreshUserList:");
		return;
	}
	nick_length = strlen(user);
	listWait();
	old_string = users_list;
	if (mode == ADD) {
		users_list_length += nick_length+1; /*+1 è per lo spazio che intercorre tra i nomi*/
		users_list = Malloc(sizeof(char)* (users_list_length+2));
		snprintf(users_list, users_list_length+2, "%s %s", old_string, user);
	} else {
		char *beginning = NULL, *end = NULL;
		beginning = strstr(old_string, user);
		if (beginning == NULL) {
			listSignal();
			errno = EINVAL;
			perror("msgserver, refreshUserList: l'username da rimuovere non è nella lista");
			return;
		}
		end = beginning+nick_length;/*Togliamo anche lo spazio, o eventualmente lo \0*/
		users_list_length -= nick_length;
		beginning[-1] ='\0';
		if (end[0] == '\0') { /*Significa che l'utente e` l'ultimo della lista.*/
			users_list = Malloc(sizeof(char)*(users_list_length));
			strncpy(users_list, old_string, users_list_length);
			users_list[users_list_length-1] = '\0';
		} else {
			end++; /*Eliminiamo lo spazio dopo l'username che stiamo cancellando*/
			users_list_length--;
			users_list = Malloc(sizeof(char)*(users_list_length+1));
			snprintf(users_list, users_list_length+1, "%s %s", old_string, end);
		}			
	}
	listSignal();
	free(old_string);
}


/**Invia un messaggio di errore corrispondente a errcode all'utente rappresentato nella socket_lock
 * dall'elemento h.
 * \param errcode il codice di errore, 0 < errcode < ERR_NUMBER (costante definita in msglib.h)
 * \param h l'elemento della struttura socket_lock riguardante questo utente.
 * \param receiver l'utente a cui era destinato il messaggio in origine
 * 
 * \retval -1 in caso di errore (sets errno)
 * \retval 0 se tutto è andato a buon fine.
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
	/*Lo spazio viene allocato dalla funzione*/
	err->length = errorString(errcode, &errstr);
	if (err->length <= 0 || errstr == NULL) {
		perror("msgserv, sendError");
		free(errstr);
		free(err);
		return -1;
	}
	/*ERR_FC e` una costante che denota il numero di caratteri aggiuntivi
	necessari per la formattazione dell'errore.*/
	err->length += strlen(receiver) + ERR_FC;
	err->buffer = Malloc(sizeof(char)*(err->length+1));
	snprintf(err->buffer, err->length+1, ERR_FORMAT, receiver, errstr);
	free(errstr);
	err->type = MSG_ERROR;
	/*Richiediamo di essere gli unici ad accedere alla socket rappresentante l'utente*/
	requireDirectAccess(msg_locks, h);
		socket = h->key;
		(void) sendMessage(*socket, err);
	releaseDirectAccess(msg_locks, h);
	free(err->buffer);
	free(err);
	err = NULL;
	return 0;	
}

/** La funzione sendSocketError sostituisce sendError nella fase di connessione
 * preliminare dell'utente. Puo' essere utilizzata solo se la socket su cui 
 * questi e` in ascolto non e` stata ancora inserita all'interno della struttura
 * msg_locks.
 * \param errcode il numero che identifica l'errore all'interno del sisetma
 * \param socket il fd della socket su cui inviare il messaggio di errore.
 * 
 * \retval 0 se tutto va bene
 * \retval -1 in caso di errore (imposta errno)
*/
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
		perror("msgserv, sendSocketError può essere usato solo se la socket non è inserita nella struttura socket lock");
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
	err->length += 8;
	err->buffer = Malloc(sizeof(char)*(err->length+1));
	snprintf(err->buffer, err->length+1, "[ERROR] %s", errstr);
	free(errstr);
	errstr = NULL;
	err->type = MSG_ERROR;
	sendMessage(socket, err);
	free(err->buffer);
	free(err);
	return 0;
}

/** Riceve una struttura messaggio inviata dal client e la "normalizza"
 * perchè possa venire utilizzata come un messaggio normale.
 * \param message_t*msg, il messaggio da analizzare (del tipo MSG_TO_ONE)
 * 
 * \retval receiver, il nome utente di colui a cui è indirizzato il messaggio
 * \retval NULL, in caso di errore (sets ERRNO)
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
 * \retval 0 on success
 * \retval -1 in caso di errore (sets errno)
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
 * \retval 0 se la procedura viene eseguita correttamente
 * \retval -1 se avviene un errore (sets errno)
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
	
	 /*I MSG_LIST sono già formattati.*/
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
	if (msg->type == MSG_TO_ONE) free(old_buffer);
	return 0;	
}

/** Invia un messaggio msg all'utente rappresentato nella tabella hash
 * da hash_element.
 * \param msg il messggio da inviare
 * \param sender colui che ha inviato il messaggio
 * \param hash_element l'elemento della hash che rappresenta l'utente
 * 
 *  \retval 0 on success (il messaggio è stato inviato)
 *  \retval -1 on error (il messaggio non è stato inviato (altre cause)
 *  \retval -2 se l'utente non era connesso.
 * */
int sendClient(message_t*msg, char *sender, elem_t*hash_element) {
	int retval = 0;
	message_t_expanded *exp;
	elem_t**p, *h;
	int *socket;
	if (hash_element == NULL || msg == NULL || (sender == NULL && msg->type != MSG_EXIT)) {
		errno = EINVAL;
		perror("msgserver, sendClient");
		return -1;
	}
	/*L'utente è disconnesso*/
	if ((p = hash_element->payload) == NULL || (h = *p) == NULL) return -2;
	
	if (msg->type == MSG_ERROR) {
		errno = EINVAL;
		perror("msgserver, sendClient, MSG_ERROR is not accepted by this function");
		return -1;
	}
	if (msg->type == MSG_EXIT) {
		requireDirectAccess(msg_locks, h); /*Richiediamo accesso unico alla struttura h*/
			h = *p;
			socket = h->key;
			retval = sendMessage(*socket, msg);
		releaseDirectAccess(msg_locks, h);
		return 0;
	}
	exp = expand_message(msg, sender, hash_element->key); /*Viene allocato un messaggio esteso*/
	
	if (formatMessage(msg, sender) == -1) {
		free_Message(exp);
		return -1;
	}
	
	requireDirectAccess(msg_locks, h); /*Richiediamo accesso unico alla struttura h*/
		h = *p;
		socket = h->key;
		retval = sendMessage(*socket, msg);
	releaseDirectAccess(msg_locks, h); /*Rilasciamo l'accesso alla struttura h*/
	
	if (retval && (msg->type == MSG_TO_ONE || msg->type == MSG_BCAST))
		write_Buffer(writer_buffer,exp);

	free_Message(exp);
	return retval;
} 

/** Disconnette l'utente in questione 
 * \param username il nome dell'utente da disconnettere
 * 
 * \retval 0 se ha successo
 * \retval -1 se fallisce
 * */
int disconnectUser(char *username) {
	elem_t **payload, *sl;
	elem_t *h;
	int *socket;
	message_t endmsg;
	if (username == NULL) {
		errno = EINVAL;
		perror("msgserv, disconnectUser");
		return -1;
	}
	/*Si deve controllare il valore di signal_exit, perché
	 * in tal caso la procedura di uscita è già stata eseguita/in procinto
	 * di essere eseguita da cancelWorkers.*/
	pthread_mutex_lock(&delete_mtx);
		if (signal_exit) {
			pthread_mutex_unlock(&delete_mtx);
			return -1;
		}
	pthread_mutex_unlock(&delete_mtx);
	
	/*Rimozione dell'utente dalla lista*/
	refreshUserList(username, REMOVE);
	
	tableWait();
		/*Ricerca dell'elemento nella tabella hash*/
		h = hashElement(users_table, username);
		if (h == NULL || h->payload == NULL) {
			errno = EINVAL;
			fprintf(stderr, "[ERROR] l'utente %s risulta gia` disconnesso\n", username);
			tableSignal();
			return -1;	
		}
		payload = h->payload;
		sl = *payload;
		h->payload = NULL; /*Indicazione di "utente disconnesso"*/
	tableSignal();
	/*Preparazione e invio del messaggio di uscita*/
	endmsg.buffer = NULL;
	endmsg.length = 0;
	endmsg.type = MSG_EXIT;
	
	socketWait(msg_locks);
		socket = sl->key;
		(void) sendMessage(*socket, &endmsg);
		shutdown(*socket, SHUT_RDWR);
		if(removeSLE(msg_locks, sl) < 0) 
			perror("msgcli, disconnectUser");
	socketSignal(msg_locks);
	
	free(payload);
	return 0;
}

/** Si occupa di svuotare il buffer, terminando la scrittura sul file di log
 * e di chiudere log_file.
 * \param a	il puntatore al file utilizzato come log
 * */
void writer_clean(void *a) {
	message_t_expanded *msg;
	FILE *log_file = a;
	if (a == NULL) return;
	while(writer_buffer->length > 0) { /*Quando è a zero, termina. Altrimenti si sospenderebbe a tempo indefinito*/
		msg = read_Buffer(writer_buffer);
		if (msg->type == MSG_BCAST || msg->type == MSG_TO_ONE){
			fprintf(log_file, "%s:%s:%s\n", msg->sender, msg->receiver, msg->buffer);
			fflush(log_file);
		} else {
			printf("msgserver: sono accettati solo messaggi broadcast e verso singoli utenti");
		}
		free_Message(msg);
	}
	fclose(log_file);
}

/** Si specializzerà in un thread il cui ruolo è scrivere i messaggi che passano
 * attraverso il server nel file specificato.
 * \param log_path il file di log su cui scrivere
 * */
void* writer(void * log_path) {
	FILE *log_file;
	message_t_expanded *msg;
	if (log_path == NULL) {
		errno = EINVAL;
		perror("msgserv, worker");
		pthread_exit((void *) -1);
	}
	log_file = Fopen((const char *) log_path, LOG_OPENING_MODE);
	if (log_file == NULL) {
		perror("msgserver, worker");
		printf("Il file di log '%s' specificato non è valido.\n", (char *) log_path);
		exit(-1);
	}
	pthread_cleanup_push(&writer_clean, log_file);
	while(1) {
		if((msg = read_Buffer(writer_buffer)) != NULL) {
			/*Questo cleanup è specificato per prevenire allocazioni di memoria nel caso in cui
			 * arrivi un segnale di terminazione mentre il worker ancora esegue.*/
			pthread_cleanup_push(&free_Message, (void*) msg);
			if (msg->type == MSG_BCAST || msg->type == MSG_TO_ONE)	{
				fprintf(log_file, "%s:%s:%s\n", msg->sender, msg->receiver, msg->buffer);
				fflush(log_file);
			}
			pthread_cleanup_pop(1);
		}				
	}
	pthread_cleanup_pop(1);
	return (void *) 0;
}

/*Worker si specializzerà in più thread (uno per ogni utente*/
void *worker(void *h) {
	message_t msg;
	elem_t *hash_element = h;
	elem_t **sl;
	char *username;
	int *user_socket;
	if (hash_element == NULL || hash_element->key == NULL || hash_element->payload == NULL){
		errno = EINVAL;
		perror("msgserver, worker");
		pthread_exit((void *) -1);
	}
	
	sl = hash_element->payload; /*Elemento socket_lock relativo all'utente*/
	user_socket = (*sl)->key;
	username = hash_element->key;
	
	while(1) {
		int res;
		if ((res = receiveMessage(*user_socket, &msg)) >= 0) { /*Allocazione di msg.buffer*/
			char *receiver;
			switch (msg.type) {
				case MSG_TO_ONE:
					receiver = normalizeToOne(&msg);
					break;
				case MSG_EXIT:
					disconnectUser(username);
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
				free(msg.buffer);
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
				tableWait();
					if (users_table->table[i] != NULL) {
						elem_t *aux = users_table->table[i]->head;
						while (aux != NULL) {
							elem_t **rsl;
							rsl = aux->payload;
							tableSignal();
							if (rsl != NULL && *rsl != NULL) {
								/*Niente di ciò dovrebbe mai poter accadere. */
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
							tableWait();
							aux = aux->next;
						}
					}
					tableSignal();
				}
				free(original_buffer);
			} else {
				elem_t *k;
				tableWait();
					k = hashElement(users_table, receiver);
				tableSignal();
				if (k == NULL) {
					sendError(3, *sl, receiver);
				} else {
					switch (sendClient(&msg, username, k)) {
						case -2:
							sendError(3, *sl, receiver); break;
						case -1:
							sendError(5, *sl, receiver); break;
					}
				}
				free(msg.buffer);
				if (msg.type != MSG_LIST) /*Nel caso contrario, liberiamo "mai".*/
					free(receiver);
						
			}
		} else {
			if (res == SEOF) {
				disconnectUser(username);
				pthread_exit((void*)-1);
			} else if (errno != EINTR) {
				disconnectUser(username);
				perror("msgserver, worker");
				pthread_exit((void *)-1);
			}		
		}
	}
	pthread_exit((void *) 0);
}

/** Thread che si occupa della ricezione delle connessioni e della creazione, 
 * per ogni utente, di un thread che gestisca le richieste di questi.
 */
void* dispatcher(void *args) {
	message_t msg;
	elem_t* element;
	int fd;
	/** Iniziamo tentando di creare la socket. Qualora non fosse possibile,
	 * continuare non ha senso. CreateServerChannel fa già tutti i tentativi
	 * possibili per connettersi da se.*/
	if((fd = createServerChannel(SOCKET)) <= 0) {
		perror("msgserver, dispatcher");
		printf("Tentativo di creazione della socket %s fallito.\n", SOCKET);
		exit(-1);
	}
	pthread_cleanup_push(&CloseSocket, &fd);
	listWait();
		users_list_length = strlen(LIST_FORMAT);
		users_list = Malloc(sizeof(char)*(users_list_length+1)); /*Viene pulito all'uscita del programma, essendo una struttura globale*/
		strncpy(users_list, LIST_FORMAT, users_list_length+1);
	listSignal();
	/* Il ciclo infinito seguente riceve le connessioni, e se rispettano
	 * il protocollo le accetta. Inoltre, non appena è possibile, passa
	 * le competenze al thread worker dell'utente connesso*/
	while (1) {
		pthread_t worker_id;
		int current_socket; 
		
		/*Non è stato possibile stabilire la connessione iniziale*/
		if ((current_socket = acceptConnection(fd)) == -1) {
			perror("msgserver, dispatcher");
			continue;
		}
		
		/*Il primo messaggio ricevuto, una volta stabilita la connessione,
		 * deve essere del tipo MSG_CONNECT, altrimenti si procede a mostrare
		 * un errore, in quanto il protocollo non è stato rispettato.*/
		
		if (receiveMessage(current_socket, &msg) >= 0 && msg.type == MSG_CONNECT) { 
			
			/*Il messaggio iniziale non è valido. L'utente dovrà tentare a riconnettersi*/
			if (msg.length == 0) {
				sendSocketError(current_socket, 0);
				closeSocket(current_socket);
				free(msg.buffer);
				continue;
			}
						
			/* Se si trova un record nella tabella hash, allora il nome
			 * utente inserito è valido. Tuttavia, potrebbe darsi che 
			 * ci sia un'altro utente connesso con lo stesso nome. In tal caso,
			 * ovviamente, mandiamo un messaggio di errore e andiamo oltre*/
			 
			/*Qua non è necessario accedere in mutua esclusione, a meno che non si verifichino delle parti
			 * interne. Questo perché non è prevista la cancellazione di una chiave dalla tabella hash*/
			if ((element = hashElement(users_table, msg.buffer)) != NULL) {
				elem_t **sl_pointer;
				int connected = 0;
				char *username = msg.buffer;
				tableWait();
					connected = ((elem_t **) element->payload) != NULL;
				tableSignal();
				if (connected) {
					elem_t **tmp;
					tmp = element->payload;
					if (tmp == NULL || *tmp == NULL) {
						connected = 0;
					} else {
						socketWait(msg_locks);
							connected = connected && (*tmp != NULL);
						socketSignal(msg_locks);
					}
				}				
				if (connected) {					
					sendSocketError(2,current_socket);
					closeSocket(current_socket);
					free(msg.buffer);
					continue;
				}
				
				msg.buffer = NULL;
				msg.length = 0;
				msg.type = MSG_OK;
			
				sendMessage(current_socket, &msg);
		
				refreshUserList(username, ADD);
				
				
				sl_pointer = insertSL(msg_locks, current_socket, 0);
				tableWait();
					element->payload = sl_pointer;
				tableSignal();
	
				if (element->payload == NULL) {
					perror("msgserver, dispatcher:");
					continue;
				}

				sl_pointer = element->payload;
				if (pthread_create(&worker_id, NULL, &worker, element) == -1) {
					perror("msgserver, dispatcher: ");
					releaseDirectAccess(msg_locks, *sl_pointer);
					removeSL(msg_locks, current_socket);
					refreshUserList(username, REMOVE);
					printf("Connessione rifiutata\n");
					free(username);
					continue;
				}
				pthread_detach(worker_id);
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
	pthread_cleanup_pop(1);
	pthread_exit((void *) 0);
}

/** Funzione chiamata dal gestore dei segnali quando si riceve SIGBUS o
 * SIGSEGV*/
void manageMemorySignals(int sig) {
	write(2, "Errore interno nell'accesso alla memoria\n", 41);
	exit(EXIT_FAILURE);
}

/** Funzione chiamata all'arrivo di un segnale di terminazione (SIGTERM o
 * SIGSTOP) per far terminare dolcemente tutti i thread worker attivati
 * per ognuno degli utenti connessi.
 *  */
void cancelWorkers() {
	int i = 0;  
	elem_t *aux = NULL;
	elem_t **pld = NULL;
	message_t endmsg;
	pthread_mutex_lock(&delete_mtx);
		signal_exit = 1;
	pthread_mutex_unlock(&delete_mtx);
	
	if ((errno = pthread_mutex_trylock(&user_table_mtx)) != 0) {
		perror("msgserv, cancelWorkers");
		if (pthread_mutex_unlock(&user_table_mtx) != 0) {
			perror("msgserv, cancelWorkers");
			/*Non sarà possibile cancellare tutti i worker, quindi meglio saltare.*/
			return;
		}
	}
	
	while (UT_inUse) pthread_cond_wait(&user_table_cond, &user_table_mtx);
	UT_inUse = 1;
	pthread_mutex_unlock(&user_table_mtx);
	for (i = 0; i < users_table->size; i++) {
		if (users_table->table[i] != NULL) {
			aux = users_table->table[i]->head;
			while (aux != NULL) {
				if ((pld = aux->payload) != NULL)
					/*Indicazione di "utente disconnesso"*/
					free(aux->payload);
					aux->payload = NULL;
				aux = aux->next;
			}
		}
	}
	tableSignal();
	/*Scorrimento della struttura socket lock, invio messaggi di uscita e eliminazione*/
	if((errno = pthread_mutex_trylock(&msg_locks->mtx)) != 0) {
		if (errno == EBUSY) {
			pthread_mutex_unlock(&msg_locks->mtx);
			pthread_mutex_lock(&msg_locks->mtx);
		}
	}
	while (msg_locks->inUse > 0) {
		pthread_cond_wait(&msg_locks->cond_locks, &msg_locks->mtx);
	}
	msg_locks->edit = 1;
	pthread_mutex_unlock(&msg_locks->mtx);
	/*
	pthread_mutex_lock(&msg_locks->mtx);
		pthread_cond_signal(&msg_locks->cond_locks);
	pthread_mutex_unlock(&msg_locks->mtx);
	*/
		aux = msg_locks->locks->head;
		while (aux != NULL) {
			int *socket;			
			endmsg.buffer = NULL;
			endmsg.type = MSG_EXIT;
			endmsg.length = 0;
			socket = aux->key;
			(void) sendMessage(*socket, &endmsg);
			shutdown(*socket, SHUT_RDWR);
			aux = aux->next;
	}
	pthread_mutex_lock(&msg_locks->mtx);
		msg_locks->edit = 0;
	pthread_mutex_unlock(&msg_locks->mtx);
}

int main(int argc, char* argv[]) {
	int e;
	sigset_t set;
	struct sigaction sa;
	pthread_t writer_id, dispatcher_id;	
	if (argc != 3) {
		printf("Sono richiesti due parametri\n");
		printf("Sintassi corretta: $msgserv file_utenti_autorizzati file_log\n");
		return -1;
	}
	/*Ignoriamo tutti i segnali: questo comportamento sarà ereditato dai
	 * thread dispatcher e writer; dispatcher attiva i worker, e pertanto
	 * anche questi avranno il medesimo comportamento.*/
	if (sigfillset(&set) == -1) {
		perror("msgcli, main, impossibile inizializzare la maschera");
		exit(-1);
	}	
	if(pthread_sigmask(SIG_SETMASK,&set,NULL) == -1) {
		perror("msgcli, main, impossibile mascherare i segnali");
		exit(-1);
	}
	/*Tutti i segnali, in questo momento, sono ignorati. è un ottimo momento
	 * per fare tutte le inizializzazioni e impostare i gestori.*/
	bzero(&sa, sizeof(sa)); /*Inizializziamo il signal handler array*/
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) == -1) {
		perror("msgcli, main, impossibile cambiare l'handler di SIGPIPE");
		exit(-1);
	}
	sa.sa_handler = manageMemorySignals;
	if (sigaction(SIGSEGV, &sa, NULL) == -1) {
		perror("msgcli, main, impossibile cambiare l'handler di SIGPIPE");
		exit(-1);
	}
	if (sigaction(SIGBUS, &sa, NULL) == -1) {
		perror("msgcli, main, impossibile cambiare l'handler di SIGPIPE");
		exit(-1);
	}		
	writer_buffer = initialize_Buffer(writer_buffer_SIZE);
	msg_locks = initializeSL();
	if(load_authorized_users(argv[1]) <= 0) {
		printf("Il caricamento del file utenti autorizzati non è andato a buon fine.\n");
		return -1;
	}
		
	if(pthread_create(&writer_id, NULL, &writer, argv[2]) == -1) {
		return -1;
	}
	
	if(pthread_create(&dispatcher_id, NULL,&dispatcher, NULL) == -1) {
		return -1;
	}
	/*Eliminiamo dalla maschera tutti i segnali*/
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	if(pthread_sigmask(SIG_SETMASK,&set,NULL) == -1) {
		perror("msgcli, main, impossibile mascherare i segnali");
		exit(-1);
	}
	
	sigwait(&set, &e); /*Attendiamo SIGTERM o SIGINT per fermarci.*/
	printf("UL: %d, UT: %d\n", UL_inUse, UT_inUse);
	printf("SLE: %d, SLWE: %d, SLU: %d, SLWU: %d\n", msg_locks->edit, msg_locks->waitingEdit, msg_locks->inUse, msg_locks->waitingUse);
	
	printf("Terminazione del server\n");
	pthread_cancel(dispatcher_id);	
	
	unlink(SOCKET);
	pthread_join(dispatcher_id, NULL);
	printf("tornato dal dispatcher\n");
	cancelWorkers(); /*Ritorna una volta che tutti i worker sono stati terminati*/
	pthread_cancel(writer_id);
	pthread_join(writer_id, NULL);
	printf("tornato dal writer\n"); 
	free_Buffer(&writer_buffer);
	freeSL(&msg_locks);
	free_hashTable(&users_table);
	free(users_list);	
	exit(0);
}
