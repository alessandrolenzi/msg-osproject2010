#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "errors.h"

static char *error_messages[ERR_NUMBER];
static int error_lengths[ERR_NUMBER];
static int error_initialized = 0;

/*Sull'accesso a error_initialized ci potrebbe essere un problema di mutua esclusione.*/
void errorInitialize(void) {
	if (error_initialized == 0) {
		int i;
		error_messages[0] = "Impossibile stabilire una connessione: il messaggio MSG_CONNECT doveva contenere il nome utente.";
		error_messages[1] = "Nome utente non valido";
		error_messages[2] = "Nome utente già in uso";
		error_messages[3] = "utente non connesso";
		error_messages[4] = "non è un nome utente registrato su questo servizio. Impossibile inviare il messaggio.";
		error_messages[5] =	"non ha ricevuto il messaggio a causa di problemi tecnici.";
		error_messages[6] = "il messaggio è malformato: MSG_TO_ONE richiede il formato '%ONE username messaggio'";
		
		for (i = 0; i < ERR_NUMBER; i++)
			error_lengths[i] = strlen(error_messages[i]);
		error_initialized = 1;
	}
}

/** Riceve un errcode e un buffer. Copia il messaggio corrispondente a
 * errcode in una nuova area di memoria, appositamente allocata, che 
 * viene assegnata a buffer. Se buffer è vuoto, deve essere posto a NULL.
 * \param errcode il codice corrispondente all'errore che si vuole generare
 * \param buffer dove si vuole copiare il codice. */

int errorString(int errcode, char **buffer) {
	if (error_initialized == 0) errorInitialize();
	if (errcode < 0 || errcode >= ERR_NUMBER || buffer == NULL) {
		errno = EINVAL;
		perror("errors.h, errorString");
		return -1;
	}
	*buffer = Malloc(sizeof(char)*(error_lengths[errcode]+1));
	strncpy(*buffer, error_messages[errcode], error_lengths[errcode]+1);
	return error_lengths[errcode]; 
}

FILE * Fopen(const char* path, const char* mode) {
	FILE *opened_file;
	int attempt = 0;
	while ((opened_file = fopen(path, mode)) == NULL && attempt++ == 0) {
		if (EACCES == errno)
			chmod(path, S_IROTH);
		else
			return NULL;
	}
	if (attempt == 1 && opened_file == NULL) {
		perror("");
		exit(-1);
	}
	
	return opened_file;
}
void* Malloc(size_t size) {
	void *ptr;
	ptr = malloc(size);
	if (ptr == NULL) {
		perror("");
		exit(-1);
	}
	return ptr;
}

void Fclose(void *f) {
	FILE *file = f;
	fclose(file);
}

/*Compare tra due stringhe generalizzato*/
int compareString(void *a, void *b) {
    char *_a, *_b;
    _a = (char *) a;
    _b = (char *) b;
    return strcmp(_a,_b);
}
/* funzione di copia di un intero */
void * copyInt(void *a) {
  int * _a;

  if ( ( _a = malloc(sizeof(int) ) ) == NULL ) return NULL;

  *_a = * (int * ) a;

  return (void *) _a;
}
/* funzione di copia di una stringa */
void * copyString(void * a) {
  char * _a;

  if ( ( _a = strdup(( char * ) a ) ) == NULL ) return NULL;

  return (void *) _a;
}

int compareInt(void *a, void *b) {
    int *_a, *_b;
    _a = (int *) a;
    _b = (int *) b;
    return ((*_a) - (*_b));
}
