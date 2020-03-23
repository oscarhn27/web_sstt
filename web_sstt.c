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
#define PROHIBIDO		403
#define NOENCONTRADO	404
#define SPECIAL_CHAR	'$'

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
	int nExtension, i;
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
	printf("Llega a la func\n");
	char date[1000];
	obtenerHeaderDate(date);
	printf("Llega a cType\n");
	char cType[1000];
	sprintf(cType, "Content-type: %s\r\n", fileType);
	printf("Llega a cLength\n");
	char cLength [1000];
	sprintf(cLength,"Content-length: %ld\r\n", size);
	char headers[8000];
	sprintf(headers, "%s%s%s%s\r\n", msgType, date, cType, cLength);
	printf("Llega a write\n");
	write(socket_fd, headers, strlen(headers));
}

void mensajeDeError(int code_error, int socket_fd){

	char * msg;
	switch(code_error){
		case 403:
			msg = "HTTP/1.1 403 Forbidden\r\n";
			break;
		case 404:
			msg = "HTTP/1.1 404 Not Found\r\n";
			break;
		default:
			return;
	} 
	write(socket_fd, msg, strlen(msg));
}

void debug(int log_message_type, char *message, char *additional_info, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];
	
	switch (log_message_type) {
		case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",message, additional_info, errno,getpid());
			break;
		case PROHIBIDO:
			// Enviar como respuesta 403 Forbidden
			mensajeDeError(403, socket_fd);
			(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",message, additional_info);
			break;
		case NOENCONTRADO:
			// Enviar como respuesta 404 Not Found
			mensajeDeError(404, socket_fd);
			(void)sprintf(logbuffer,"NOT FOUND: %s:%s",message, additional_info);
			break;
		case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",message, additional_info, socket_fd); break;
	}

	if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)write(fd,"\n",1);
		(void)close(fd);
	}
	if(log_message_type == ERROR || log_message_type == NOENCONTRADO || log_message_type == PROHIBIDO) exit(3);
}

int directorioIlegal(char * directorio){
	for(int i = 0; i < strlen(directorio)-1; i++)
		if(directorio[i] == '.' && directorio[i+1] == '.')
			return 1;
	return 0;
}

int comprobarMetodo(char * metodo){
	if(strcmp(metodo, "GET")){
		fprintf(stderr, "El metodo solicitado %s es distinto de GET\n", metodo);
		return 1;
	}
	return 0;
}

int protocoloValido(char * protocolo){
	if(strcmp(protocolo, "HTTP/1.1")){
		fprintf(stderr, "El protocolo %s solicitado es distinto de HTTP/1.1\n", protocolo);
		return 1;
	}
	return 0;
}

void process_web_request(int descriptorFichero)
{
	int retVal;
	do{
	debug(LOG,"request","Ha llegado una peticion",descriptorFichero);
	//
	// Definir buffer y variables necesarias para leer las peticiones
	//
	char buf[BUFSIZE];
	char * token;
	char * subtoken;
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
	//	(Se soporta solo GET)
	//
	
	token = strtok(buf, "$$");
	char * metodo = strtok(token, " ");
	if(comprobarMetodo(metodo)){
		debug(ERROR, "Metodo no soportado.", metodo, descriptorFichero);
		break;
	}
	
	//
	//	Como se trata el caso de acceso ilegal a directorios superiores de la
	//	jerarquia de directorios
	//	del sistema
	//
	
	char * path = strtok(NULL, " ");
	struct stat fich; // Información del fichero
	int exist; // Si es igual a -1 el fichero no existe
	// Si el archivo especificado es un directorio añadimos index.html como peticion por defecto
	if(path[strlen(path)-1]=='/'){
		char pathCompleto[64];
		sprintf(pathCompleto, "%sindex.html", path+1);
		exist = stat(pathCompleto, &fich);
		path = pathCompleto;
	}
	else
		exist = stat(path+1, &fich);
	if(exist == -1){
		debug(NOENCONTRADO, "El archivo solicitado no ha sido encontrado", path, descriptorFichero);
		printf("Error el fichero %s no existe\n", path);
		break;
	}
	else if(directorioIlegal(path) || fich.st_uid != 1001){ // El fichero solicitado debe ser propiedad del user cliente (UID) = 1001
		debug(PROHIBIDO, "El archivo solicitado no está disponible para clientes", path, descriptorFichero);
		printf("Error el fichero solicitado %s no tiene permisos para get\n", path);
		break;
	}
	
	//
	// Incluyo el caso de que se introduzca un protocolo distinto a HTTP/1.1
	//
	
	char * protocolo;
	protocolo = strtok(NULL, " ");

	if(protocoloValido(protocolo) != 0){
		debug(ERROR, "Protocolo solicitado no válido.", protocolo, descriptorFichero);
		break;
	}
	
	/*
	//	Como se trata el caso excepcional de la URL que no apunta a ningún fichero
	//	html
	//

	//
	//	Evaluar el tipo de fichero que se está solicitando, y actuar en
	//	consecuencia devolviendolo si se soporta u devolviendo el error correspondiente en otro caso
	*/
	
	char * extension = strrchr(path, '.') + 1;
	int nExtension; // Numero de la extension
	nExtension = getFileType(extension);/*{
		switch(nExtension){
			case -1 : 
				debug(ERROR, "Archivo sin extension solicitado",path,descriptorFichero);
				break;
			case -2 :
				debug(ERROR, "Archivo con extension no soportado", extension, descriptorFichero);
				break;
		}
		break;


		En caso de que el fichero sea soportado, exista, etc. se envia el fichero con la cabecera
		correspondiente, y el envio del fichero se hace en bloques de un maximo de  8kB
	*/
	

	char ok[1000] = "HTTP/1.1 200 OK\r\n";
	sendHeaders(ok, extensions[nExtension].filetype, fich.st_size, descriptorFichero);
	printf("Llega al ok\n");

	char * fileSend = malloc(sizeof(char) * BUFSIZE);
	int fd_file = open(path, O_RDONLY);
	long int tamanoRestante = fich.st_size;
	do{
		read(fd_file, fileSend, BUFSIZE);

		int offset = 0, len = strlen(fileSend);
		while ((offset += write(descriptorFichero, fileSend+offset, len)) != len){
            len -= offset;
            if(offset < 0){
                debug(ERROR, "Error en la escritura", "write file", descriptorFichero);
                exit(EXIT_FAILURE);
            }
        }
        tamanoRestante -= offset;
	}while(tamanoRestante > 0);
	close(fd_file);
	free(fileSend);


	// Persistencia
	fd_set setFd;
	FD_ZERO(&setFd);
	FD_SET(descriptorFichero, &setFd);
	struct timeval timeWait;
	timeWait.tv_sec = 45;
	retVal = select(descriptorFichero+1, &setFd, NULL, NULL, &timeWait);
	}while(retVal);
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
