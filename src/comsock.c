#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include "errors.h"
#include "genList.h"
#include "comsock.h"

#define MAX_ATTEMPT 3
/** fine dello stream su socket, connessione chiusa dal peer */
#define SEOF -2
/** Error Socket Path Too Long (exceeding UNIX_PATH_MAX) */
#define SNAMETOOLONG -11 
/** numero di tentativi di connessione da parte del client */
#define SOCKET_DIR "./tmp"
#define  NTRIALCONN 3
/** tipi dei messaggi scambiati fra server e client */
/** richiesta di connessione utente */
#define MSG_CONNECT        'C'
/** errore */
#define MSG_ERROR          'E' 
/** OK */
#define MSG_OK             '0' 
/** NO */
#define MSG_NO             'N' 
/** messaggio a un singolo utente */
#define MSG_TO_ONE         'T' 
/** messaggio in broadcast */
#define MSG_BCAST          'B' 
/** lista utenti connessi */
#define MSG_LIST           'L' 
/** uscita */
#define MSG_EXIT           'X' 

/** ping*/
#define MSG_PING			'P'

/** lunghezza buffer AF_UNIX */
#define UNIX_PATH_MAX    108
/* -= TIPI =- */

/** <H3>Messaggio</H3>
 * La struttura \c message_t rappresenta un messaggio 
 * - \c type rappresenta il tipo del messaggio
 * - \c length rappresenta la lunghezza in byte del campo buffer
 * - \c buffer e' il puntatore al messaggio (puo' essere NULL se length == 0)
 *
 * <HR>
 */

static int creation_attempt = 0;

/*Copia un elem_t***/
void * copy_elem_t(void *a) {
	elem_t ** _a;
	if (a == NULL) return NULL;
	if ((_a = Malloc(sizeof(elem_t*))) == NULL) return NULL;
	*_a = (elem_t*) a;
	return (void*) _a;
}

void socketWait(socket_lock *sl) {
	if (sl == NULL) {
		errno = EINVAL;
		perror("comsock.h, socketWait");
		return;
	}
	pthread_mutex_lock(&sl->mtx);
	while (sl->waitingEdit || sl->inUse || sl->waitingUse) { 
		pthread_cond_wait(&sl->cond_locks, &sl->mtx);
		pthread_cond_signal(&sl->cond_locks);
	}
	pthread_mutex_unlock(&sl->mtx);
	pthread_mutex_lock(&sl->mtx);
	while (sl->edit) {
		pthread_cond_wait(&sl->cond_struct, &sl->mtx);
	}
	sl->edit = 1;
	sl->waitingEdit = 0;
	pthread_mutex_unlock(&sl->mtx);
}

void socketSignal(socket_lock *sl) {
	pthread_mutex_lock(&sl->mtx);
		sl->edit = 0;
		sl->waitingEdit = 0;
		pthread_cond_signal(&sl->cond_locks);
		pthread_cond_signal(&sl->cond_struct);
	pthread_mutex_unlock(&sl->mtx);
}

/** Inizializza una struttura socket_lock che sarà successivamente pronta all'uso*/
socket_lock *initializeSL() { /*MISSING:GESTERR*/
	socket_lock *sl = Malloc(sizeof(socket_lock));
	sl->size = 0;
	sl->locks = new_List(&compareInt, &copyInt, &copyInt);
	sl->inUse = 0;
	sl->edit = 0;
	sl->waitingUse = 0;
	sl->waitingEdit = 0;
	pthread_cond_init(&sl->cond_locks, NULL);
	pthread_cond_init(&sl->cond_struct, NULL);
	pthread_mutex_init(&sl->mtx, NULL);
	return sl;
}

elem_t* findSL(socket_lock*sl, int fd) {
	elem_t*e;
	e = find_ListElement(sl->locks, &fd);
	return e;
}

elem_t** insertSL(socket_lock*sl, int fd, int lock) {
	int default_value = lock;
	elem_t **retval;
	if (fd < 0 || sl == NULL) {
		errno = EINVAL;
		return NULL;
	}
	retval = Malloc(sizeof(elem_t*));
	socketWait(sl);
	if ((*retval = findSL(sl, fd)) == NULL) {
		if(add_ListElement(sl->locks, &fd, &default_value) == -1) { 
			*retval = NULL;
		} else
			*retval = sl->locks->head;
	}
	if (default_value == 1) sl->inUse++;
	socketSignal(sl);
	return retval;
}

void freeSL(socket_lock **sl) {
	free_List(&((*sl)->locks));
	free(*sl);
}

int removeSL(socket_lock*sl, int fd) {
	int retval = 0;
	retval = remove_ListElement(sl->locks, &fd);
	return retval;
}

int removeSLE(socket_lock*sl, elem_t*e) {
	int *k = NULL, retval = 0;
	if (e == NULL || sl == NULL) {
		errno = EINVAL;
		return -1;
	}
	k = e->key;
	retval = remove_ListElement(sl->locks, k);
	return retval;
}

void requireDirectAccess(socket_lock *sl, elem_t*el) {
	int *actual_lock;
	if (el == NULL || sl == NULL) return;
	pthread_mutex_lock(&sl->mtx);
	/*Se edit è 1 si sta modificando l'intera struttura ed è necessario
	 * sospendersi*/
	while (sl->edit != 0) pthread_cond_wait(&sl->cond_struct, &sl->mtx);
	sl->waitingUse++;
	pthread_mutex_unlock(&sl->mtx);
	pthread_mutex_lock(&sl->mtx);
	while (el != NULL && (*(actual_lock = el->payload) != 0)) {
		
		pthread_cond_wait(&sl->cond_locks, &sl->mtx);
	}
	
	if (el == NULL) {
		pthread_mutex_unlock(&sl->mtx);
		sl->waitingUse--;
		return;
	}
	sl->waitingUse--;
	*actual_lock = 1; /*risorsa occupata*/
	sl->inUse++;
	pthread_mutex_unlock(&sl->mtx);
}

void releaseDirectAccess(socket_lock *sl, elem_t *el) {
	int *actual_lock;
	if (el == NULL || sl == NULL) return;
	pthread_mutex_lock(&sl->mtx);
		actual_lock = el->payload;
		*actual_lock = 0;
		sl->inUse--;
		pthread_cond_signal(&sl->cond_locks);
	pthread_mutex_unlock(&sl->mtx);
}

void requireAccess(socket_lock *sl, int fd) {
	if (sl == NULL || fd <= 0) return;
	requireDirectAccess(sl, find_ListElement(sl->locks, &fd));
}

void releaseAccess(socket_lock *sl, int fd) {
	if (sl == NULL || fd <= 0) return;
	releaseDirectAccess(sl, find_ListElement(sl->locks, &fd));
}

/** Chiude una socket
 *   \param s file descriptor della socket
 *
 *   \retval 0  se tutto ok, 
 *   \retval -1  se errore (sets errno)
 */
int closeSocket(int s) {
	fsync(s);
	if (close(s) == -1) {
		perror("comsock.h: ");
		return -1;
	}
	return 0;
}
void CloseSocket(void *s) {
	int *socket = s;
	closeSocket(*socket);
}



int createServerChannel(char* path) {
	int new_socket, old_errno, attempt = 0;
	struct sockaddr_un socket_address;
	if (creation_attempt == MAX_ATTEMPT) return -1;
	if (strlen(path) > UNIX_PATH_MAX) {
		errno = EINVAL;
		return SNAMETOOLONG;
	}
	if ((new_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("comsock.h");
		return -1; /*errno? si può far qualcosa?*/
	}
	strncpy(socket_address.sun_path, path, UNIX_PATH_MAX);
	socket_address.sun_family = AF_UNIX;
	(void) unlink(path);
	while (-1 == bind(new_socket, (struct sockaddr*) &socket_address, sizeof(socket_address)) && attempt < MAX_ATTEMPT) {
		old_errno = errno;
		switch (errno) {
			case EACCES: /*Non si può accedere alla directory. Cambiarne i permessi non sembra opportuno, visto che non abbiamo informazioni - per quanto riguarda la libreria! - di dove potrebbe risiedere.*/
				perror("comsock.h, createServerChannel - bind:");
				closeSocket(new_socket);	
				return -1;
			break;
			case EBADF: /*Significa che il FileDescriptor non è corretto. Ri-iniziamo l'operazione da capo.*/
				creation_attempt++;
				perror("comsock.h, createServerChannel - bind:");
				closeSocket(new_socket);
				return createServerChannel(path);
		}
		attempt++;
		errno = old_errno;
		if (attempt != MAX_ATTEMPT)
			printf("Bind fallita. Saranno eseguiti altri %d tentativi di connessione\n", (MAX_ATTEMPT-attempt));
		else
			return -1;
	}
	attempt = 0;
	creation_attempt = 0;
	/*A questo punto la BIND è andata a buon fine; ocho: qua in caso di errore non solo dobbiamo 
	 * chiudere il file descriptor, ma anche efettuare un unlink sul vecchio.*/
	if(listen(new_socket, SOMAXCONN) == -1) {
		old_errno = errno;
		perror("comsock.h, createServerChannel - listen:");
		closeSocket(new_socket);
		(void) unlink(path);
		errno = old_errno;
		return -1;
	}
	/*Vedere p.527*/
	return new_socket;
}

int acceptConnection(int s) {
	int connection_socket; /*badare che se il socket è non bloccante, c'è da gestire questo caso.*/
	if ((connection_socket = accept(s, NULL, 0)) == -1) {
		perror("comsock.h:");
	}
	return connection_socket;
}
/** legge un messaggio dalla socket
 *  \param  sc  file descriptor della socket
 *  \param msg  struttura che conterra' il messagio letto 
 *		(deve essere allocata all'esterno della funzione,
 *		tranne il campo buffer)
 *
 *  \retval lung  lunghezza del buffer letto, se OK 
 *  \retval SEOF  se il peer ha chiuso la connessione 
 *                   (non ci sono piu' scrittori sulla socket)
 *  \retval  -1    in tutti gl ialtri casi di errore (sets errno)
 *   
 *  **Non ritorna se si riceve un MSG_PING! **   
 */

int receiveMessage(int sc, message_t * msg) {
	int b_size = 0, a, b;
	if (msg == NULL) { errno = EINVAL; return -1;}
	if ((a = read(sc, &(msg->type), sizeof(char))) == -1) {
		perror("comsock.h, receiveMessage");
		return -1;
	}
	if (a == 0) return SEOF;
	
	if ((b = read(sc, &(msg->length), sizeof(int))) == -1){
		perror("comsock.h, receiveMessage");
		return -1;
	}
	if (b == 0) return SEOF;
	if (msg->type == MSG_PING) return receiveMessage(sc, msg);
	if (msg->length > 0) {
		msg->buffer = Malloc(sizeof(char)*(msg->length+1)); 
		/*il campo buffer contiene la stringa di terminazione*/
		b_size = read(sc,msg->buffer, sizeof(char)*(msg->length+1));
		if (b_size == -1) {
			perror("comsock.h, receiveMessage");
			return -1;
		}
		if (b_size == 0) return SEOF;
		/** Il frammento successivo è una forma di controllo per i messaggi
		 * che riceviamo: ci diamo "rassicurazioni" sul fatto che il messaggio
		 * inviatoci dal client abbia una conformazione che non ci dia problemi
		 * nella gestine della memoria.*/
		if (msg->buffer[msg->length-1] == '\n') 
			msg->buffer[--msg->length] = '\0';
	} else msg->buffer = NULL;	
	return b_size; 	
}

/** scrive un messaggio sulla socket
 *   \param  sc file descriptor della socket
 *   \param msg struttura che contiene il messaggio da scrivere 
 *   
 *   \retval  n    il numero di caratteri inviati (se scrittura OK)
 *   \retval  SEOF se il peer ha chiuso la connessione 
 *                   (non ci sono piu' lettori sulla socket) 
 *   \retval -1   in tutti gl ialtri casi di errore (sets errno)
 
 */
int sendMessage(int sc, message_t *msg) {
	int c = 0;
	if (msg == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (write(sc, &(msg->type), sizeof(char)) == -1) {
		if (EPIPE == errno) return SEOF;
		return -1;
	}
	if (write(sc, &(msg->length), sizeof(int)) == -1){
		if (EPIPE == errno) return SEOF;
		return -1;
	}

	if (msg->length > 0 && msg->buffer != NULL) {
		if((c = write(sc, msg->buffer, sizeof(char)*(msg->length+1))) == -1){
			if (EPIPE == errno) return SEOF;
			return -1;
		}
	}

	return c;
}
/** crea una connessione all socket del server. In caso di errore funzione tenta NTRIALCONN volte la connessione (a distanza di 1 secondo l'una dall'altra) prima di ritornare errore.
 *   \param  path  nome del socket su cui il server accetta le connessioni
 *   
 *   \return 0 se la connessione ha successo
 *   \retval SNAMETOOLONG se il nome del socket eccede UNIX_PATH_MAX
 *   \retval -1 negli altri casi di errore (sets errno)
 *
 *  in caso di errore ripristina la situazione inziale: rimuove eventuali socket create e chiude eventuali file descriptor rimasti aperti
 */
static int connection_attempt = 0;

int openConnection(char* path) {
	int new_socket;
	int attempt = 0;
	struct sockaddr_un socket_address;
	if (sizeof(path) > UNIX_PATH_MAX) return SNAMETOOLONG;
	if ((new_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("comsock.h");
		return -1;
	}
	strncpy(socket_address.sun_path, path, UNIX_PATH_MAX);
	socket_address.sun_family = AF_UNIX;
	while (connect(new_socket, (struct sockaddr*) &socket_address, sizeof(socket_address)) == -1) {
		if (errno == ENOTSOCK) {
			connection_attempt++;
			return openConnection(path);
		}
		if (attempt == NTRIALCONN) {
			closeSocket(new_socket);
			return -1;
		}
		attempt++;
	}
	return new_socket;
}



