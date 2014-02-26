#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc, char* argv[]) {
	
	FILE *auth_file, *newfile;
	char buf[256];
	int user_number = 0; 
	char *auth_path = argv[1];
	srand((unsigned int) time(NULL));
	auth_file = fopen(auth_path, "r");
	newfile = fopen("newfile", "w");
	fprintf(newfile, "#!/bin/bash\nchmod u+x *.ktst\n");
	while(fgets(buf, 256, auth_file) != NULL) {
		/*Per ogni utente, le sue storie.*/
		int m_num = rand() % 1000;
		FILE *thisfile;
		char *message;
		int i, exit = 0;
		char file_name[256];
		sprintf(file_name, "mcli%d.ktst", user_number);
		chmod(file_name, 777);
		thisfile = fopen(file_name, "w");
		
		for (i = 0; i < strlen(buf); i++)
				if (buf[i] == '\n')	buf[i] = '\0';
		fprintf(thisfile, "./msgcli %s<<EOF\n", buf);
		for (i = 0; i < m_num; i++) {
			int j;
			int m_type = rand() % 4;
			int m_length = rand() % 6000;
			int redice = rand() %4;
			if (m_type == 4) 
				m_type = redice;
			message = malloc(sizeof(char)*(m_length+12));
			for (j = 0; j < m_length-1; j++) {
				int code = (rand() % 69);
				if (code < 10) {
					message[j] = (char) code+48;
				} else if (code < 17) {
					switch (code){
						case 10:
							message[j] = '?';
						case 11:
							message[j] = '!';
						case 12:
							message[j] = '`';
						case 13:
							message[j] = '.';
						case 14:
							message[j] = ',';
						case 15:
							message[j] = ';';
						case 16:
							message[j] = ':';
					}
				} else if (code < (17+26)) {
					message[j] = (char) code+48;
				} else {
					message[j] = (char) code+54;
				}
				
			}
			message[j] = '\n';
			message[j+1] = '\0';
				
			if (m_type == 1) {
				fprintf(thisfile, "%s\n", message);
				
			} else if (m_type == 2) {
				fprintf(thisfile, "%s %s %s\n", "%ONE", buf, message);
			} else if (m_type == 3) {
				fprintf(thisfile, "%s\n", "%LIST");
			} else {
				fprintf(thisfile, "%s\n", "%EXIT\n");
				break;
			}
			free(message);
			fflush(newfile);
		}
		fprintf(thisfile, "EOF");
		fclose(thisfile);
		fprintf(newfile, "./%s &\n", file_name);
		user_number++;
	}
	fclose(auth_file);
	return user_number;
}
