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
#define LOG			44
#define BADREQUEST		400
#define PROHIBIDO		403
#define NOENCONTRADO		404
#define SPECIAL_CHAR		'$'
#define HTML_400		"/error-400.html"
#define HTML_403		"/error-403.html"
#define HTML_404		"/error-404.html"
#define HTML_MALO		"/malo.html"
#define HTML_BUENO		"/bueno.html"
#define FALSE			0
#define TRUE			1
#define STATE_OK		"HTTP/1.1 200 OK"
#define STATE_BADREQUEST 	"HTTP/1.1 400 Bad Request"
#define STATE_FORBIDDEN		"HTTP/1.1 403 Forbidden"
#define STATE_NOTFOUND		"HTTP/1.1 404 Not Found"
#define POST_TYPE		1
#define GET_TYPE		2
#define EXTENSION_HTML  	9

struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },  // 0
	{"jpg", "image/jpg" },  // 1
	{"jpeg","image/jpeg"},  // 2
	{"png", "image/png" },  // 3
	{"ico", "image/ico" },  // 4
	{"zip", "image/zip" },  // 5
	{"gz",  "image/gz"  },  // 6
	{"tar", "image/tar" },  // 7
	{"htm", "text/html" },  // 8
	{"html","text/html" },  // 9
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

void sendHeaders(char * msgType, char * fileType, long int size, int persistencia, int socket_fd){
	char date[1000];
	obtenerHeaderDate(date);
	char server[1000];
	sprintf(server, "Server: 16.04.1 LTS\r\n");
	char cType[1000];
	sprintf(cType, "Content-type: %s\r\n", fileType);
	char cLength [1000];
	sprintf(cLength,"Content-length: %ld\r\n", size);
	char connection [1000];
	sprintf(connection, "Connection: Keep-Alive\r\n");
	char keep_alive [1000];
	sprintf(keep_alive, "Keep-Alive: timeout=5, max=0\r\n");
	char headers[BUFSIZE];
	if(persistencia)
		sprintf(headers, "%s\r\n%s%s%s%s%s%s\r\n", msgType, server, date, cLength, cType, connection, keep_alive);
	else
		sprintf(headers, "%s\r\n%s%s%s%s\r\n", msgType, server, date, cLength, cType);
	write(socket_fd, headers, strlen(headers));
}

int connectionClose(char * lineaHeader){
	if(strcmp(lineaHeader, "Connection: close") == 0)
		return TRUE;
	return FALSE;

}

int generarError(char ** path,char ** state, int code){
	switch(code){
		case BADREQUEST:
			*path = HTML_400;
			*state = STATE_BADREQUEST;
			break;
		case PROHIBIDO:
			*path = HTML_403;
			*state = STATE_FORBIDDEN;
			break;
		case NOENCONTRADO:
			*path = HTML_404;
			*state = STATE_NOTFOUND;
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
			break;
		case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",message, additional_info, socket_fd); break;
	}

	if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)write(fd,"\n",1);
		(void)close(fd);
	}
	// if(log_message_type == ERROR || log_message_type == NOENCONTRADO || log_message_type == PROHIBIDO);// exit(3);
}

int directorioIlegal(char * directorio){
	if (directorio == NULL)
		return -2;
	for(int i = 0; i < strlen(directorio)-1; i++)
		if(directorio[i] == '.' && directorio[i+1] == '.')
			return -1;
	return 0;
}

// Devuelve 1 si es POST y 2 si es GET
int comprobarMetodo(char * metodo){
	if(metodo == NULL)
		return -2;
	if((strcmp(metodo, "GET") != 0) && (strcmp(metodo, "POST") != 0))
		return -1;
	if(strcmp(metodo, "POST") == 0)
		return POST_TYPE;
	return GET_TYPE;
}

int protocoloValido(char * protocolo){
	if(protocolo == NULL)
		return -2;
	if(strcmp(protocolo, "HTTP/1.1"))
		return -1;
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
	while(select(descriptorFichero+1, &setFd, NULL, NULL, &timeWait)){

	debug(LOG,"request","Ha llegado una peticion",descriptorFichero);
	//
	// Definir buffer y variables necesarias para leer las peticiones
	//
	char buf[BUFSIZE];
	for(int i = 0; i < BUFSIZE; i++){
		buf[i] = 0;
	}
	int status = TRUE; // FALSE ERROR, TRUE OK
	char * state = STATE_OK;
	int persistencia = TRUE;

	//
	// Leer la petición HTTP y comprobación de errores de lectura
	//

	int leido = read(descriptorFichero, buf, BUFSIZE);

	//
	// Si la lectura tiene datos válidos terminar el buffer con un \0
	//

	buf[leido] = '\0';

	//
	// Se eliminan los caracteres de retorno de carro y nueva linea
	//

	for(int i = 0; i < leido; i++){
		if(buf[i] == '\n' || buf[i] == '\r')
			buf[i] = SPECIAL_CHAR;
	}
	//
	//	TRATAR LOS CASOS DE LOS DIFERENTES METODOS QUE SE USAN
	//
	char * metodo = strtok(buf, " ");
	char * path = strtok(NULL, " ");
	char * protocolo = strtok(NULL, "$");
// --------------------------------------------------------
	// printf("Path entrante:'%s'\n", path);

	// Casos de error en el método (Debe ser GET o POST)

	int tipoMetodo;
	if((tipoMetodo = comprobarMetodo(metodo)) < 0){
		switch(tipoMetodo){
			case -1:
				debug(BADREQUEST, "Metodo no soportado", metodo, descriptorFichero);
				break;
			case -2:
				debug(BADREQUEST, "No se encontro el metodo", "NULL", descriptorFichero);
				break;
		}
		status = generarError(&path, &state, BADREQUEST);
	}


	//
	//	Caso de acceso ilegal a directorios superiores de la
	//	jerarquia de directorios del sistema
	//

	int directorioError;
	if(status && (directorioError = directorioIlegal(path))){
		int codeERROR;
		switch(directorioError){
			case -1:
				debug(PROHIBIDO, "El archivo solicitado no está disponible para clientes", path, descriptorFichero);
				codeERROR = PROHIBIDO;
				break;
			case -2:
				debug(BADREQUEST, "No se encontro la ruta del archivo", "NULL", descriptorFichero);
				codeERROR = BADREQUEST;
				break;
		}
		status = generarError(&path, &state, codeERROR);
	}


	//
	// Incluyo el caso de que se introduzca un protocolo distinto a HTTP/1.1
	//

	int protocoloV;
	if(status && (protocoloV = protocoloValido(protocolo)) < 0){
		switch(protocoloV){
			case -1:
				debug(BADREQUEST, "Protocolo solicitado no válido", protocolo, descriptorFichero);
				break;
			case -2:
				debug(BADREQUEST, "No se encontro el protocolo", "NULL", descriptorFichero);
				break;
		}
		status = generarError(&path, &state, BADREQUEST);
	}

	// Si el archivo especificado es un directorio añadimos index.html a la ruta como peticion por defecto

	if(path[strlen(path)-1]=='/'){
		char pathCompleto[64];
		sprintf(pathCompleto, "%sindex.html", path);
		path = pathCompleto;
	}

	// Comprobacion de que el fichero existe. El resultado de stat debe ser diferente de -1.
	struct stat fich; // Información del fichero

	if(status && tipoMetodo == GET_TYPE && stat(path + 1, &fich) == -1){
		debug(NOENCONTRADO, "El archivo solicitado no ha sido encontrado", path, descriptorFichero);
		status = generarError(&path, &state, NOENCONTRADO);
	}

	// Comprobacion de que el fichero solicitado tiene como propietario un usuario especial con uid = 1001

	else if(status && tipoMetodo == GET_TYPE && (fich.st_uid != 1001)){ // El fichero solicitado debe ser propiedad del user cliente (UID) = 1001
		debug(PROHIBIDO, "El archivo solicitado no está disponible para clientes", path, descriptorFichero);
		status = generarError(&path, &state, PROHIBIDO);
	}



	// Control de errores en las cabeceras

	char * lineaHeader;
	char lineaB[1000];
	// Recorremos todas las cabeceras con strtok. La ultima no sera una cabecera si no el entity body.
	while(status && (lineaHeader = strtok(NULL, "$")) != NULL){
		if(persistencia && connectionClose(lineaHeader))
			persistencia = FALSE;
		// Si una de estas lineas no contiene ': ' o 'email=' (Entity body) sera una cabecera mal formada
		if(strstr(lineaHeader, ": ") == NULL && strstr(lineaHeader, "email=") == NULL){
			debug(BADREQUEST, "Cabecera mal formada", lineaHeader, descriptorFichero);
			status = generarError(&path, &state, BADREQUEST);
		}
// ----------------------------------------------------------------------------
		// printf("Header entrante:'%s'\n", lineaHeader);
		// Copiamos la ultima linea leida para que en lineaB se almacene el entity body al salir.
		strcpy(lineaB, lineaHeader);
	}

	// Si la solicitud es de tipo POST se hacen las siguientes comprobaciones

	if(status && tipoMetodo == POST_TYPE){
		strtok(lineaB, "=");
		char * mail = strtok(NULL, "$");
		// Si la ruta solicitada no es igual a accion_form.html se devuelve un error
		if(strcmp(path, "/accion_form.html") != 0){
			debug(BADREQUEST, "Archivo solicitado incorrecto (POST)", path, descriptorFichero);
			status = generarError(&path, &state, BADREQUEST);
		}
		else if(mail == NULL)
			path = HTML_MALO;
		else{
			if(strcmp(mail, "oscar.hernandezn%40um.es") != 0)
				path = HTML_MALO;
			else
				path = HTML_BUENO;
		}
	}

	//	Evaluar el tipo de fichero que se está solicitando, y actuar en
	//	consecuencia devolviendolo si se soporta u devolviendo el error correspondiente en otro caso

	char * extension = strrchr(path + 1, '.') + 1;
	int nExtension; // Numero de la extension
	if((nExtension = getFileType(extension)) < 0){
		switch(nExtension){
			case -1 :
				debug(BADREQUEST, "Archivo sin extension solicitado", path, descriptorFichero);
				status = generarError(&path, &state, BADREQUEST);
			case -2 :
				debug(BADREQUEST, "Archivo con extension no soportado", extension, descriptorFichero);
				status = generarError(&path, &state, BADREQUEST);
		}
		nExtension = EXTENSION_HTML;
	}

	//
	//	En caso de que el fichero sea soportado, exista, etc. se envia el fichero con la cabecera
	//	correspondiente, y el envio del fichero se hace en bloques de un maximo de  8kB

	// ******************* ENVIO DEL FICHERO ********************

	path = path + 1;
// ------------------------------------------------------------------------
	// printf("Path saliente: '%s'\n\n", path);
	struct stat fich2;
	stat(path, &fich2);
	sendHeaders(state, extensions[nExtension].filetype, fich2.st_size, persistencia, descriptorFichero);

	char fileSend [BUFSIZE];
	int fd_file = open(path, O_RDONLY);
	size_t bytes_r; // Bytes leidos
	do{
		bytes_r = 0;
		bytes_r = read(fd_file, fileSend, BUFSIZE);
		int offset = 0;
		while((offset += write(descriptorFichero, fileSend+offset, bytes_r-offset)) != bytes_r ){
			if(offset < 0){
            	debug(ERROR, "Error en la escritura", "write file", descriptorFichero);
            	exit(EXIT_FAILURE);
        	}
    	}
	}while(bytes_r != 0);
	close(fd_file);

	// ***** INICIALIZACION DE VARIABLES PARA PERSISTENCIA *****
	FD_ZERO(&setFd);
	FD_SET(descriptorFichero, &setFd);

	if(persistencia == FALSE)
		timeWait.tv_sec = 0;
	timeWait.tv_sec = 5;
	timeWait.tv_usec = 0;
	}
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
