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

/*Formato con il quale l'errore deve essere stampato a schermo*/
#define ERR_FORMAT "[ERROR] %s"
/*Percorso della socket*/
#define SERVER_SOCKET "./tmp/msgsock"
/*Tentativi massimi di connessione*/
#define MAX_CONN_ATTEMPT 5
/*Pausa tra un tentativo di connessione e il successivo*/
#define CONN_PAUSE 1
/*Grandezza del buffer utilizzato per leggere da standard input*/
#define BUFFER_SIZE 256
/*Grandezza massima del nickname dell'utente*/
#define NICK_SIZE 256

/*Diventa uno quando inviamo al server un messaggio di uscita.*/
static int exit_sent = 0;
/*Diventa uno quando riceviamo un messaggio di uscita dal server*/
static int exit_received = 0;
/*Mutex per l'accesso alle variabili che vengono controllate per la 
 * terminazione.*/
static pthread_mutex_t term_mutex = PTHREAD_MUTEX_INITIALIZER;
static int stop = 0;
static int socket_descriptor = 0;

/** La funzione message_to_server si occupa di interpretare una stringa
 * inserita da standard input trasformandola in una struttura di tipo
 * messaggio.
 * \param message il messaggio inserito da standard input
 * \param length la lunghezza del messaggio
 * 
 * 	\return message_t il messaggio da inviare
 * */
message_t * message_to_server(char *message, int length) {
	char message_types[3] = {MSG_TO_ONE, MSG_EXIT, MSG_LIST};
	char *message_command[3], *buffer;
	int command_number = 3, message_empty[3] = {0, 1, 1};
	/*message_empty contiene 1 se il messaggio in questione deve essere vuoto*/
	message_t *msg = NULL;
	if (message == NULL || length < 0) {
		errno = EINVAL; 
		return NULL;
	}

	/* Si suppone che il messaggio arrivato in questa fase sia già
	* stato epurato di eventuali "%" in caratteri successivi al primo.
	*/
	
	/*Alcune costanti per i messaggi.*/	
	message_command[0] = "%ONE";
	message_command[1] = "%EXIT";
	message_command[2] = "%LIST";
	
	msg = Malloc(sizeof(message_t));
	msg->length = 0;
	
	/** Necessariamente dovremo avere uno di questi 3 tipi. Altrimenti
	 * è stato inserito un tipo di messaggio non valido*/
	if (message[0] == '%') {
		int i = 0;
		/*Dovrebbe essere una sorta di riconoscimento automatico, più "semplice" rispetto a 
		 *uno switch da modificarsi ogni volta.*/
		for (i = 0; i < command_number; i++){
			if (strstr(message, message_command[i]) != NULL) {
				int len;
				/*Len è la lunghezza del comando iniziale che contraddistingue il messaggio
				 * e deve essere tolta alla lunghezza effettiva.*/
				len = strlen(message_command[i])+1;
				msg->type = message_types[i];
				/*Il messaggio effettivo comincia dopo "len" caratteri e la sua lunghezza è length - len.*/
				buffer = message+len;
				msg->length = (message_empty[i]) ? 0 : length-len;	
				break; 
			}
		}
		/*Siamo usciti senza trovare alcun tipo di comando corrispondente, dunque
		 * il messaggio inviato è invalido.*/
		if (i == command_number) {
			fprintf(stderr, ERR_FORMAT, "il comando inserito non e' valido\n");
			fprintf(stderr, "Comandi disponibili:\n");
			fprintf(stderr, "%%LIST\t -> mostra la lista degli utenti connessi\n");
			fprintf(stderr, "%%ONE nomeutente messaggio\t -> invia un messaggio a 'nomeutente'\n");
			fprintf(stderr, "%%EXIT\t -> uscita dall'applicazione\n");
			fprintf(stderr, "Il testo inserito normalmente verra' inviato a tutti gli utenti connessi\n\n");
			errno = EINVAL;
			return NULL;			
		}
	} else {
		/* MSG_BCAST secondo le specifiche non richiede il simbolo "%"
		 * all'inizio. Ergo ogni messaggio valido che non inizi con %
		 * viene automaticamente designato come BCAST.*/
		msg->type = MSG_BCAST;
		msg->length = length;
		buffer = message;
	}
	/** Le fasi successive non sono necessarie nel caso di MSG_EXIT*/
	if(msg->type == MSG_EXIT) return msg;
	
	if (msg->length > 1 && buffer[msg->length-2] == '\n') {
		buffer[msg->length-2] = '\0';
		msg->length--;
	}
	/*Ora che abbiamo la lunghezza esatta, ricopiamo dentro il buffer
	 * della strutture message_t il messaggio da inviare al server.*/ 
	msg->buffer = Malloc(sizeof(char)*(msg->length+1));
	strncpy(msg->buffer, buffer, msg->length+1);
	
	/*procedura per inserire uno \0 tra nickname e messaggio da inviarsi*/
	if (msg->type == MSG_TO_ONE) {
		int i = 0, s = 0, found = 0;
		/*Dobbiamo trovare uno spazio entro "NICK_SIZE" o msg->length, altrimenti
		 * il messaggio è palesemente invalido.*/
		s = (NICK_SIZE <= msg->length) ? NICK_SIZE : msg->length; 
		for (i = 1; i < s; i++) {
			/** Abbiamo trovato la fine del nickname di colui che deve ricevere*/
			if (msg->buffer[i] == ' ') {
				msg->buffer[i] = '\0';
				found = 1;
				break;
			}
		}
		if (!found) {
			fprintf(stderr, ERR_FORMAT, "per inviare un messaggio a un singolo utente digitare %ONE nomeutente messaggio\n");
			return NULL;
		}	
	}
	return msg;
}

void *input_clean(void *bp) {
	char **b = bp;
	if (b == NULL) return (void *) -1;
	if (*b != NULL) {
		free(*b);
	}
	free(b);
	return (void *) 0;
}

/** La funzione input gestisce stdin e invia i messaggi letti al server.
 * \param *s il fd del socket su cui è stabilita la comunicazione.
 * 
 * NOTA: i pthread_clenup_pop/push sono ovviamente riferiti a main, dove
 * vengono chiamati.
 * */
void input(int *socket) {
	char *string = NULL;
	char *buffer = NULL;
	char **bpoint;
	int long_msg_size = 1;
	message_t *msg;
	if (socket == NULL || *socket <= 0) {
		errno = EINVAL;
		perror("msgcli, input:");
	}
	bpoint = Malloc(sizeof(char*));
	buffer = Malloc(sizeof(char)*BUFFER_SIZE);
	*bpoint = buffer;
	pthread_cleanup_push(input_clean, (void *) bpoint);
	pthread_mutex_lock(&term_mutex);
	while (!stop) {
	pthread_mutex_unlock(&term_mutex);
		/*string è la porzione del messaggio letta a questo ciclo, buffer+long_msg_size-1 permette
		 * di scrivere direttamente nella porzione successiva nel caso il messaggio superi la 
		 * lunghezza massima del buffer, BUFFER_SIZE.*/
		 		
		if((string = fgets(buffer+long_msg_size-1, BUFFER_SIZE, stdin)) != NULL) { 
			int msg_length = 0;
			int i = 0, valid = 1;
			msg_length = strlen(string); 
			/* Bisogna efettuare un controllo di validità sul messaggio. 
			 * Ovverosia, è necessario verifiare che non ci sia nessun '%' a
			 * spasso per la stringa, salvo nella prima posizione.*/
			if (msg_length > 1)
			for (i = 1; i < msg_length && string[i] != '\n' && valid; i++) {
				if (string[i] == '%') {
					fprintf(stderr, ERR_FORMAT, "Il messaggio contiene il carattere non ammesso '%' e non sarà inviato.\n");
					valid = 0;
				}
			}
			/*Il messaggio non è valido:cancelliamo il vecchio messaggio distruggendo il buffer
			 * e ne allochiamo uno nuovo da usare nel ciclo successivo.*/
			if (!valid) {
				if (long_msg_size > 1) { /*Lunghezza non standard.*/
					free(buffer);
					buffer = Malloc(sizeof(char)*BUFFER_SIZE);
					*bpoint = buffer;
				}
				long_msg_size = 1;
				pthread_mutex_lock(&term_mutex);
				continue;
			}
			/* Se la lunghezza è < BUFFER_SIZE significa che il buffer è
			 * risultato sufficiente per la stringa inserita. 
			 * Il messaggio non richiede ulteriori letture, quindi si procede con l'invio.*/
			if (msg_length < BUFFER_SIZE-1 || string[BUFFER_SIZE-2] == '\n') {
				msg = message_to_server(buffer, long_msg_size+msg_length-1);
				if (msg != NULL) {
					if(sendMessage(*socket, msg) == -1) {
						perror("msgcli, input");
						free(msg->buffer);
						free(msg);
						pthread_mutex_lock(&term_mutex);
						continue;
					}
					if (msg->type == MSG_EXIT) {
						pthread_mutex_lock(&term_mutex);
							exit_sent = 1;
						pthread_mutex_unlock(&term_mutex);
						free(msg);
						pthread_mutex_lock(&term_mutex);
						break;
					}
					free(msg->buffer);
					free(msg);
				}
				/*Se lo spazio allocato per la lettura era multiplo di
				 * BUFFER_SIZE liberiamo quanto occupato allocando
				 * un buffer più piccolo per la lettura successiva.*/
				if (long_msg_size > 1) {
					free(buffer);
					buffer = Malloc(sizeof(char)*BUFFER_SIZE);
					*bpoint = buffer;
				}
				long_msg_size = 1;
			} else {
				/* In questo ramo si gestisce il caso in cui la lunghezza
				 * di BUFFER_SIZE non è risultata sufficiente al testo inserito
				 * e pertanto si alloca un buffer più grande per leggere
				 * i caratteri successivi.*/
				char *oldbuf = buffer;
				long_msg_size += msg_length;
				buffer = Malloc(sizeof(char) * (long_msg_size + BUFFER_SIZE));
				*bpoint = buffer;
				strncpy(buffer, oldbuf, long_msg_size -1);
				free(oldbuf);				
			}
			
		} else {
			pthread_mutex_lock(&term_mutex);
			break; /*EOF*/
		}
		pthread_mutex_lock(&term_mutex);
	}
	pthread_mutex_unlock(&term_mutex);
	
	/* Se la procedura di lettura non è terminata sotto esplicita indicazione
	 * dell'utente (ES: EOF) notifichiamo comunque al server la disconnessione
	 * del client.*/
	pthread_mutex_lock(&term_mutex);
	if (!exit_sent && !exit_received) {
	pthread_mutex_unlock(&term_mutex); 
		msg = Malloc(sizeof(message_t));
		msg->type = MSG_EXIT;
		msg->length = 0;
		msg->buffer = NULL;
		switch(sendMessage(*socket, msg)) {
			case -1:
				perror("msgcli, input");
				exit(EXIT_FAILURE);
			case -2:
				/** Se stop è 0 allora SEOF è dovuto al server.*/
				pthread_mutex_lock(&term_mutex);
				if(!stop) {
					fprintf(stderr, ERR_FORMAT, "Disconnessi dal server.");
					exit(EXIT_FAILURE);
				}
				pthread_mutex_unlock(&term_mutex);
		}
		pthread_mutex_lock(&term_mutex);
			exit_sent = 1;
		pthread_mutex_unlock(&term_mutex);
		free(msg);
	} else pthread_mutex_unlock(&term_mutex);
	
	pthread_cleanup_pop(1);
}

/** La funzione output, che verrà specializzata in un thread, riceve
 * i messaggi dal server e li stampa su stdout.
 * \param *s il fd della socket su cui output riceve i messaggi
 **/
void* output(void *s) {
	int *user_socket = NULL;
	message_t *msg;
	int res = 0;
	if ((user_socket = s) == NULL || *user_socket <= 0) {
		errno = EINVAL;
		perror("msgcli, output");
		exit(EXIT_FAILURE);
	}
	msg = Malloc(sizeof(message_t));
	pthread_cleanup_push(&free, msg);
	/*Anche qua è necessario controllare la variabile stop in mutua esclusione*/
	pthread_mutex_lock(&term_mutex);
	while(!stop) {
	pthread_mutex_unlock(&term_mutex);
		if ((res = receiveMessage(*user_socket, msg)) >= 0) {
			if (msg->type == MSG_EXIT) {
				pthread_mutex_lock(&term_mutex);
				exit_received = 1;
				break;
			}
			fprintf(stdout, "%s\n", msg->buffer);
			free(msg->buffer);
			fflush(stdout);
		} else {
			pthread_mutex_lock(&term_mutex);
			break;
		}
		pthread_mutex_lock(&term_mutex);
	}
	pthread_mutex_unlock(&term_mutex);

	pthread_mutex_lock(&term_mutex);
	if (!exit_sent) {
	pthread_mutex_unlock(&term_mutex);
		/** La connessione è stata certamente chiusa dal server, visto che 
		 * abbiamo ricevuto un MSG_EXIT senza che ne fosse stata fatta
		 * richiesta.*/
		res = SEOF;
		pthread_mutex_lock(&term_mutex);
			stop = 1;
		pthread_mutex_unlock(&term_mutex);
		fprintf(stderr, ERR_FORMAT, "Disconnessi dal server. \n");
		/**L'espediente di simulare un segnale è necessario per uscire
		 * dalla fgets della funzione input, chiamata in main-*/
		kill(getpid(), SIGINT); 
		pthread_exit((void *) SEOF);
	} 
	if(res < 0) {
		perror("msgcli, output");
		pthread_exit((void *) &res);
	}
	pthread_cleanup_pop(1);
	pthread_exit((void *) &res);
}

/** Procedura che riceve i segnali di terminazione (SIGTERM e SIGSTOP)
 * e avvia la conclusione "dolce" del programma. */
static void manageTerm(int signum) {
	pthread_mutex_lock(&term_mutex);
	stop = 1;
	if(!exit_sent && !exit_received) { /** Notifichiamo al server la disconnessione del client*/
		message_t msg;
		msg.type = MSG_EXIT;
		msg.length = 0;
		msg.buffer = NULL;
		(void) sendMessage(socket_descriptor, &msg);
		exit_sent = 1;
	}
	pthread_mutex_unlock(&term_mutex);
}

int main (int argc, char* argv[]) {
	int i = 0, username_length = 0;
	char *username = NULL;
	message_t *connection = NULL;
	int *res = NULL;
	struct sigaction act;
	sigset_t set;
	pthread_t output_id = 0;
	
	if (argv == NULL || argv[1] == NULL || argc != 2) {
		printf("È richiesto un parametro\n");
		printf("Sintassi corretta: msgcli username\n");
		return -1;
	}
	
	username = argv[1];
	
	/**Controlliamo che lo username inserito sia valido.*/
	i = 0;
	while (username[i] != '\0') {
		if (!(isdigit(username[i]) || isalpha(username[i]))) { 
			printf("Spiacenti, l'username inserito non è valido.\n");
			printf("L'username può contenere solo lettere maiuscole o minuscole dell'alfabeto inglese oppure numeri\n");
			return -1;
		}
		i++;
	}
	/** Proteggiamo dai segnali la fase iniziale del processo.*/
	if (sigfillset(&set) == -1) {
		perror("msgcli, main, impossibile inizializzare la maschera");
		exit(EXIT_FAILURE);
	}	
	if(pthread_sigmask(SIG_SETMASK,&set,NULL) == -1) {
		perror("msgcli, main, impossibile mascherare i segnali");
		exit(EXIT_FAILURE);
	}
	
	username_length = i+1;
	bzero(&act, sizeof(act));
	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) != 0) {
		perror("msgcli");
		exit(EXIT_FAILURE);
	}
	act.sa_handler = manageTerm;
	if (sigaction(SIGTERM, &act, NULL) != 0) {
		perror("msgcli");
		exit(EXIT_FAILURE);
	}
	if (sigaction(SIGINT, &act, NULL) != 0) {
		perror("msgcli");
		exit(EXIT_FAILURE);
	}
	
	
	/** Inizio protocollo di connessione:*/
	for (i = 0; i < MAX_CONN_ATTEMPT; i++) {
		if ((socket_descriptor = openConnection(SERVER_SOCKET)) != -1)
			break;
		else 
			if (i == 0)
				fprintf(stderr, "Connessione in corso");
			else {
				fprintf(stderr, ".");
				fflush(stderr);
			}
		sleep(CONN_PAUSE);
	}
	
	if (i == MAX_CONN_ATTEMPT && socket_descriptor == -1) {
		fprintf(stderr, "\nImpossibile stabilire una connessione.\n");
		exit(EXIT_FAILURE);
	}
	if (i != 0) printf("\nConnessione stabilita.\n");
	/** Creazione di MSG_CONNECT*/
	connection = Malloc(sizeof(message_t));
	connection->buffer = Malloc(sizeof(char)*(username_length+1));
	strncpy(connection->buffer, username, username_length+1);
	connection->type = MSG_CONNECT;
	connection->length = username_length;

	/** Invio di MSG_CONNECT*/
	if (sendMessage(socket_descriptor, connection) == -1) {
		perror("msgcli");
		exit(EXIT_FAILURE);
	}
	
	free(connection->buffer);
	
	/** Attendiamo risposta dal server.*/
	receiveMessage(socket_descriptor, connection);
	if (connection->type == MSG_ERROR) {
		fprintf(stdout, "%s\n", connection->buffer);
		free(connection->buffer);
		free(connection);
		fflush(NULL);
		exit(EXIT_FAILURE);
	}
	if (connection->type != MSG_OK) {
		fprintf(stderr, "Impossibile stabilire una connessione: MSG_CONNECT rifiutato dal server\n");
		if (connection->buffer != NULL) free(connection->buffer);
		free (connection);
		fflush(stderr);
		exit(EXIT_FAILURE);
	}
	free(connection->buffer);
	free(connection);
	connection = NULL;
	
	/** Il client si specializza in due thread, uno che legga i messaggi da 
	 * inviare, e l'altro che riceva..*/
	if(pthread_create(&output_id, NULL, &output, &socket_descriptor) == -1) {
		perror("msgcli");
		exit(EXIT_FAILURE);
	}
	/**Svuotiamo la maschera.*/
	sigemptyset(&set);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	/** Input è la funzione che gestisce l'input da stdin. Il codice
	 * è stato lasciato all'interno della funzione per leggibilità.*/
	 
	input(&socket_descriptor);
	
		
	pthread_join(output_id, (void *) &res);
	closeSocket(socket_descriptor);
	
	fflush(NULL);
	
	return 0;
}
