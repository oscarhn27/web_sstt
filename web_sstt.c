#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <time.h>

#define VERSION			24
#define BUFSIZE			8096
#define ERROR			42
#define LOG				44
#define BADREQUEST		400
#define PROHIBIDO		403
#define NOENCONTRADO	404
#define SPECIAL_CHAR	'$'
#define HTML_400		"/error-400.html"
#define HTML_403		"/error-403.html"
#define HTML_404		"/error-404.html"
#define HTML_MALO		"/malo.html"
#define HTML_BUENO		"/bueno.html"
#define FALSE			0
#define TRUE			1
#define STATE_OK		"HTTP/1.1 200 OK"
#define STATE_BADREQUEST "HTTP/1.1 400 BADREQUEST"
#define STATE_FORBIDDEN	"HTTP/1.1 403 FORBIDDEN"
#define STATE_NOTFOUND	"HTTP/1.1 404 NOTFOUND"

struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpg" },
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"ico", "image/ico" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	{0,0} };

int getFileType(char * ext){
	int i;
	if(ext == NULL){
		return -1;
	}
	else{
		for(i = 0; extensions[i].ext != 0 && strcmp(extensions[i].ext, ext); i++);
		if(extensions[i].ext == 0)
			return -2;
	}
	return i;
}

void obtenerHeaderDate(char * date){
	char buf[1000];
	time_t now = time(0);
	struct tm tm = *gmtime(&now);
	strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
	sprintf(date, "Date: %s\r\n", buf);
}

void sendHeaders(char * msgType, char * fileType, long int size, int socket_fd){
	char date[1000];
	obtenerHeaderDate(date);
	char cType[1000];
	sprintf(cType, "Content-type: %s\r\n", fileType);
	char cLength [1000];
	sprintf(cLength,"Content-length: %ld\r\n", size);
	char headers[8000];
	sprintf(headers, "%s\r\n%s%s%s\r\n", msgType, date, cType, cLength);
	write(socket_fd, headers, strlen(headers));
}

int generarError(char * path,char * state, int code){
	switch(code){
		case BADREQUEST: 
			path = HTML_400;
			state = STATE_BADREQUEST;
			break;
		case PROHIBIDO:
			path = HTML_403;
			state = STATE_FORBIDDEN;
			break;
		case NOENCONTRADO:
			path = HTML_404;
			state = STATE_NOTFOUND;
			break;	
	}
	return FALSE;
}

void debug(int log_message_type, char *message, char *additional_info, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];
	
	switch (log_message_type) {
		case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",message, additional_info, errno,getpid());
			break;
		case PROHIBIDO:
			(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",message, additional_info);
			break;
		case NOENCONTRADO:
			(void)sprintf(logbuffer,"NOT FOUND: %s:%s",message, additional_info);
			break;
		case BADREQUEST:
			(void)sprintf(logbuffer,"BAD REQUEST: %s:%s",message, additional_info);
		case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",message, additional_info, socket_fd); break;
	}

	if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)write(fd,"\n",1);
		(void)close(fd);
	}
	if(log_message_type == ERROR || log_message_type == NOENCONTRADO || log_message_type == PROHIBIDO);// exit(3);
}

int directorioIlegal(char * directorio){
	for(int i = 0; i < strlen(directorio)-1; i++)
		if(directorio[i] == '.' && directorio[i+1] == '.')
			return 1;
	return 0;
}

// Devuelve 1 si es POST y 2 si es GET
int comprobarMetodo(char * metodo){
	if(metodo == NULL)
		return -2;
	if((strcmp(metodo, "GET") != 0) && (strcmp(metodo, "POST") != 0))
		return -1;
	if(strcmp(metodo, "POST") == 0)
		return 1;
	return 2;
}

int protocoloValido(char * protocolo){
	if(protocolo == NULL || strcmp(protocolo, "HTTP/1.1"))
		return 1;
	return 0;
}

void process_web_request(int descriptorFichero)
{
	// Persistencia
	fd_set setFd;
	FD_ZERO(&setFd);
	FD_SET(descriptorFichero, &setFd);
	struct timeval timeWait;
	timeWait.tv_sec = 5;
	timeWait.tv_usec = 0;
	do{

	debug(LOG,"request","Ha llegado una peticion",descriptorFichero);
	//
	// Definir buffer y variables necesarias para leer las peticiones
	//
	char buf[BUFSIZE];
	int status = TRUE; // FALSE ERROR, TRUE OK
	char * state = STATE_OK;

	//
	// Leer la petición HTTP y comprobación de errores de lectura
	//
	
	read(descriptorFichero, buf, BUFSIZE);
	
	//
	// Si la lectura tiene datos válidos terminar el buffer con un \0
	//
	
	buf[strlen(buf)] = '\0';
	
	//
	// Se eliminan los caracteres de retorno de carro y nueva linea
	//
	
	for(int i = 0; i < strlen(buf); i++){
		if(buf[i] == '\n' || buf[i] == '\r')
			buf[i] = SPECIAL_CHAR;
	}
	//
	//	TRATAR LOS CASOS DE LOS DIFERENTES METODOS QUE SE USAN
	//
	
	char * metodo = strtok(buf, " ");
	char * path = strtok(NULL, " ");
	char * protocolo = strtok(NULL, "$");

	int tipoMetodo;
	if((tipoMetodo = comprobarMetodo(metodo)) < 0){
		debug(BADREQUEST, "Metodo no soportado", metodo, descriptorFichero);
		status = generarError(path, state, BADREQUEST);
	}

	char * lineaHeader;
	char lineaB[1000];
	while(status && (lineaHeader = strtok(NULL, "$")) != NULL){
		if(strstr(lineaHeader, ": ") == NULL && strstr(lineaHeader, "mail=") == NULL){
			debug(BADREQUEST, "Peticion mal formada", lineaHeader, descriptorFichero);
			status = generarError(path, state, BADREQUEST);
		}
		strcpy(lineaB, lineaHeader);
	}
	if(status && tipoMetodo == 1){
		strtok(lineaB, "=");
		char * mail = strtok(NULL, "$");
		if(strcmp(mail, "oscar.hernandezn%40um.es") != 0)
			path = HTML_MALO;
		else
			path = HTML_BUENO;
	}



	
	
	//
	//	Como se trata el caso de acceso ilegal a directorios superiores de la
	//	jerarquia de directorios
	//	del sistema
	//
	
	
	struct stat fich; // Información del fichero
	int exist; // Si es igual a -1 el fichero no existe
	// Si el archivo especificado es un directorio añadimos index.html como peticion por defecto
	if(status && path[strlen(path)-1]=='/'){
		char pathCompleto[64];
		sprintf(pathCompleto, "%sindex.html", path);
		path = pathCompleto;
	}
	exist = stat(path + 1, &fich);
	if(status && exist == -1){
		debug(NOENCONTRADO, "El archivo solicitado no ha sido encontrado", path, descriptorFichero);
		printf("Antes del generar error\n");
		status = generarError(path, state, NOENCONTRADO);
	}
	else if(status && directorioIlegal(path) || fich.st_uid != 1001){ // El fichero solicitado debe ser propiedad del user cliente (UID) = 1001
		debug(PROHIBIDO, "El archivo solicitado no está disponible para clientes", path, descriptorFichero);
		status = generarError(path, state, PROHIBIDO);
	}
	//
	// Incluyo el caso de que se introduzca un protocolo distinto a HTTP/1.1
	//

	if(status && protocoloValido(protocolo) != 0){
		debug(BADREQUEST, "Protocolo solicitado no válido", protocolo, descriptorFichero);
		status = generarError(path, state, BADREQUEST);
	}

	//	Evaluar el tipo de fichero que se está solicitando, y actuar en
	//	consecuencia devolviendolo si se soporta u devolviendo el error correspondiente en otro caso
	printf("Path: %s\n", path);
	char * extension = strrchr(path + 1, '.') + 1;
	printf("Extension: %s\n", extension);
	int nExtension; // Numero de la extension
	if((nExtension = getFileType(extension)) < 0){
		switch(nExtension){
			case -1 :
				debug(BADREQUEST, "Archivo sin extension solicitado", path, descriptorFichero);
				status = generarError(path, state, BADREQUEST);
			case -2 :
				debug(BADREQUEST, "Archivo con extension no soportado", extension, descriptorFichero);
				status = generarError(path, state, BADREQUEST);
		}
	}
	/*
		En caso de que el fichero sea soportado, exista, etc. se envia el fichero con la cabecera
		correspondiente, y el envio del fichero se hace en bloques de un maximo de  8kB
	*/
	

	path = path + 1;
	printf("Path: %s\n", path);
	struct stat fich2;
	stat(path, &fich2);
	sendHeaders(state, extensions[nExtension].filetype, fich2.st_size, descriptorFichero);

	fflush(stdout);

	char fileSend [BUFSIZE];
	int fd_file = open(path, O_RDONLY);
	size_t bytes_r; // Bytes leidos
	do{
		bytes_r = 0;
		bytes_r = read(fd_file, fileSend, BUFSIZE);
		// printf("Bytes leidos %lu\n", bytes_r);
		int offset = 0;
		while((offset += write(descriptorFichero, fileSend+offset, bytes_r-offset)) != bytes_r ){
			if(offset < 0){
            	debug(ERROR, "Error en la escritura", "write file", descriptorFichero);
            	exit(EXIT_FAILURE);
        	}
    	}
	}while(bytes_r != 0);
	close(fd_file);

	}while(select(descriptorFichero+1, &setFd, NULL, NULL, &timeWait));
	close(descriptorFichero);
	exit(1);
}

int main(int argc, char **argv) {
	int i, port, pid, listenfd, socketfd;
	socklen_t length;
	static struct sockaddr_in cli_addr;		// static = Inicializado con ceros
	static struct sockaddr_in serv_addr;	// static = Inicializado con ceros
	
	//  Argumentos que se esperan:
	//
	//	argv[1]
	//	En el primer argumento del programa se espera el puerto en el que el servidor escuchara
	//
	//  argv[2]
	//  En el segundo argumento del programa se espera el directorio en el que se encuentran los ficheros del servidor
	//
	//  Verficiar que los argumentos que se pasan al iniciar el programa son los esperados
	//

	//
	//  Verficiar que el directorio escogido es apto. Que no es un directorio del sistema y que se tienen
	//  permisos para ser usado
	//

	if(chdir(argv[2]) == -1){ 
		(void)printf("ERROR: No se puede cambiar de directorio %s\n",argv[2]);
		exit(4);
	}
	// Hacemos que el proceso sea un demonio sin hijos zombies
	if(fork() != 0)
		return 0; // El proceso padre devuelve un OK al shell

	(void)signal(SIGCHLD, SIG_IGN); // Ignoramos a los hijos
	(void)signal(SIGHUP, SIG_IGN); // Ignoramos cuelgues
	
	debug(LOG,"web server starting...", argv[1] ,getpid());
	
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		debug(ERROR, "system call","socket",0);
	
	port = atoi(argv[1]);
	
	if(port < 0 || port >60000)
		debug(ERROR,"Puerto invalido, prueba un puerto de 1 a 60000",argv[1],0);
	
	/*Se crea una estructura para la información IP y puerto donde escucha el servidor*/
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); /*Escucha en cualquier IP disponible*/
	serv_addr.sin_port = htons(port); /*... en el puerto port especificado como parámetro*/
	
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		debug(ERROR,"system call","bind",0);
	
	if( listen(listenfd,64) <0)
		debug(ERROR,"system call","listen",0);
	
	while(1){
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			debug(ERROR,"system call","accept",0);
		if((pid = fork()) < 0) {
			debug(ERROR,"system call","fork",0);
		}
		else {
			if(pid == 0) { 	// Proceso hijo
				(void)close(listenfd);
				process_web_request(socketfd); // El hijo termina tras llamar a esta función
			} else { 	// Proceso padre
				(void)close(socketfd);
			}
		}
	}
}
