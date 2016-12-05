/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef WITH_LIBMAGIC
#include <magic.h>
#endif

#include "picows.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define NB_ELEM( tab ) (sizeof(tab)/sizeof(tab[0]))

#define LOG( ... ) {                                            \
        printf( "%s [%d]", __FUNCTION__, __LINE__);             \
		printf(__VA_ARGS__ ) ;                                  \
}

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: Embedded picows/0.1.0\r\n"


typedef struct {
	short        port;
	const char * rootPath;
	int          nbWebService;
	WEBSERVICE * aWebService;

} HTTPD_WKSP;



static HTTPD_WKSP Httpd = { .port = 0,
						   .nbWebService = 0,
						   .aWebService = NULL,
};

typedef struct {
	char * extention;
	char * mine_type;
	char * header;

} T_MIME_DESC;

static T_MIME_DESC aMime[] = {
    { "html",   "text/html"                , "<html"  },
    { "stml",   "text/html"                , "<html"  },
    { "htm",    "text/html"                , "<html"  },
    { "stm",    "text/html"                , NULL     },
    { "xml",    "text/xml"                 , "<?xml"  },
    { "xsl",    "text/xml"                 , NULL     },
    { "xslt",   "text/xml"                 , NULL     },
    { "txt",    "text/plain"               , NULL     },
    { "gif",    "image/gif"                , NULL     },
    { "bmp",    "image/bmp"                , NULL     },
    { "jpg",    "image/jpeg"               , NULL     },
    { "jpeg",   "image/jpeg"               , NULL     },
    { "js",     "application/octet-stream" , NULL     },
    { "xpm",    "image/x-xbitmap"          , NULL     },
    { "png",    "image/png"                , NULL     },
    { "css",    "text/css"                 , NULL     },
    { "bz2",    "application/x-bzip2"      , NULL     },
    { "gz",     "application/x-gzip"       , NULL     },
    { "tgz",    "application/x-gzip"       , NULL     },
    { "z",      "application/x-compress"   , NULL     },
    { "tar",    "application/x-tar"        , NULL     },
    { "lha",    "application/octet-stream" , NULL     },
    { "lzh",    "application/octet-stream" , NULL     },
    { "rtf",    "application/rtf"          , NULL     }
};



static void accept_request(int);
static void bad_request(int);
static void cat(int, FILE *);
static void cannot_execute(int);
static void error_die(const char *);
static void execute_cgi(int, const char *, const char *, const char *);
static int get_line(int, char *, int);
static void headers(int, const char *);
static void headers_from_buffer(int client, const char * buffer);
static void headers_ok(int client);
static void not_found(int);
static void serve_file(HTTPD_REQUEST *);
static int startup(u_short *);
static void unimplemented(int);
static void execute_webservice(HTTPD_REQUEST *);


/**********************************************************************/
/* Return the query_string of the URL (ie pointer after the ?)
 **********************************************************************/
static int read_next_word( const char * buf, char * dest, int size)
{
	int iDest = 0, iBuf = 0;
	int buf_size = strlen( buf );

	//Go the first valid char
	while (ISspace( buf[iBuf] ) && (iBuf < buf_size))
		iBuf++;

	//Copy the char
	while (!ISspace(buf[iBuf])  && (iBuf < buf_size) && (iDest < size - 1))
	{
		dest[iDest] = buf[iBuf];
		iDest++; iBuf++;
	}

	return iBuf;
}


void LOG_request( HTTPD_REQUEST * request )
{
	LOG("request %p\n", request);
	if (request == NULL)
		return;

	LOG("\tclient=%d\n",request->client);
	LOG("\tkind=%d\n"  ,request->kind);

	LOG("\tmethod=%s\n",request->method);
	LOG("\tpath  =%s\n",request->path  );
	LOG("\tquery =%s\n",request->query );
	LOG("\turl   =%s\n",request->url   );
}

void request_init( HTTPD_REQUEST * request, int client )
{
	int i;
	char buf[1024]   = {0};
	char path[1024]  = {0};

	request->client  = client;
	get_line( request->client, buf, sizeof(buf));

	LOG("request from client %d : %s\n", request->client, buf);

	i = read_next_word( buf, request->method, sizeof(request->method) );
	LOG("method:%s\n", request->method );

	//Error case
	if (strlen(request->method) == 0)
	{
		request->kind =  HTTPD_REQUEST_CLOSE;
		return;
	}

	if (strcasecmp(request->method, "GET") && strcasecmp(request->method, "POST"))
	{
		request->kind =  HTTPD_REQUEST_UNIMPLEMENTED;
		return;
	}

	i = read_next_word( &buf[i], request->url, sizeof(request->url));
	LOG("url:%s\n", request->url);

	//Search path and query
	char * marker = strchr( request->url, '?');


	if (marker == NULL)
	{
		strncat( path, request->url, sizeof(path) );
	}
	else
	{
		int pathSize = request->url - marker;
		strncat( path,  request->url, MIN(pathSize, sizeof(path)) );

		int querySize = strlen( marker +1 );
		strncpy( request->query, marker+1,     MIN(querySize, sizeof(request->query)) );
	}

	//Patch path
	char lastChar = path[ strlen( path ) - 1 ];
	if (   lastChar  == '/' )
	{
		strcat(path, "index.html");
	}

	//Set the request type and add the rootPath to url
	if ( strncasecmp(path, "/WebServices", strlen("/WebServices") ) == 0 )
	{
		strcat( request->path, path);
		request->kind = HTTPD_REQUEST_WEBSERVICE;
	}
	else if (strstr(path, ".cgi") )
	{
		struct stat st;

		strncpy( request->path, Httpd.rootPath, sizeof(request->path) );
		strcat( request->path, path );

		if (   stat(request->path, &st) == 0
			&& ( (st.st_mode & S_IXUSR) ||
			     (st.st_mode & S_IXGRP) ||
			     (st.st_mode & S_IXOTH)   ) )
		{
			request->kind = HTTPD_REQUEST_CGI;
		}
		else
		{
			request->kind = HTTPD_REQUEST_FILE;
		}
	}
	else
	{
		strncpy( request->path, Httpd.rootPath, sizeof(request->path) );
		strcat( request->path, path );

		request->kind = HTTPD_REQUEST_FILE;
	}

}
/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
static void accept_request(int client)
{
	HTTPD_REQUEST request = { 0 };

	request_init( &request, client);
	LOG_request( &request );

	switch ( request.kind )
	{
	case HTTPD_REQUEST_FILE           : serve_file( &request ); break;
	case HTTPD_REQUEST_CGI            : execute_cgi(client, request.path, request.method, request.query); break;
	case HTTPD_REQUEST_WEBSERVICE     : execute_webservice( &request );  break;
	case HTTPD_REQUEST_UNIMPLEMENTED  : unimplemented( request.client);  break;
	case HTTPD_REQUEST_CLOSE          : break;
	default:
		LOG("Unknow request %d\n", request.kind)
		break;
	}

	close( request.client);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
static void execute_webservice(HTTPD_REQUEST * request)
{

	int i;
	WEBSERVICE * service = NULL;

	for (i=0; i<Httpd.nbWebService; i++)
	{
		if (strncmp( Httpd.aWebService[ i ].name,  request->query, strlen(Httpd.aWebService[ i ].name) ) == 0 )
				service = &Httpd.aWebService[ i ];
	}

	if (service != NULL)
	{
		LOG("service %s is found\n", service->name);

		if (service->service != NULL)
			service->service( service, request );

		headers_from_buffer( request->client, service->page );

		LOG("%s\n", service->page);

		send(request->client, service->page, strlen(service->page), MSG_NOSIGNAL);

		return;
	}

	LOG("service not implemented\n");
	unimplemented(request->client);
	return;


}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
static void cat(int client, FILE *resource)
{
	char buf[1024] = {0};
	ssize_t size ;
	while ( (size = fread( buf, 1, sizeof(buf), resource )) > 0 )
	{
		send(client, buf, size, MSG_NOSIGNAL);
	}

}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
static void error_die(const char *sc)
{
	perror(sc);
	exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
static void execute_cgi(int client, const char *path,
		const char *method, const char *query_string)
{
	pid_t pid;
	int status;
	int i;
	char c;
	int numchars = 1;
	int content_length = -1;

	char buf[1024]     = {0};
	char cgi_output[2] = {0};
	char cgi_input[2]  = {0};


	buf[0] = 'A'; buf[1] = '\0';
	if (strcasecmp(method, "GET") == 0)
		while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
			numchars = get_line(client, buf, sizeof(buf));
	else    /* POST */
	{
		numchars = get_line(client, buf, sizeof(buf));
		while ((numchars > 0) && strcmp("\n", buf))
		{
			buf[15] = '\0';
			if (strcasecmp(buf, "Content-Length:") == 0)
				content_length = atoi(&(buf[16]));
			numchars = get_line(client, buf, sizeof(buf));
		}
		if (content_length == -1) {
			bad_request(client);
			return;
		}
	}

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);

	if (pipe(cgi_output) < 0) {
		cannot_execute(client);
		return;
	}
	if (pipe(cgi_input) < 0) {
		cannot_execute(client);
		return;
	}

	if ( (pid = fork()) < 0 ) {
		cannot_execute(client);
		return;
	}
	if (pid == 0)  /* child: CGI script */
	{
		char meth_env[255];
		char query_env[255];
		char length_env[255];

		dup2(cgi_output[1], 1);
		dup2(cgi_input[0], 0);
		close(cgi_output[0]);
		close(cgi_input[1]);
		sprintf(meth_env, "REQUEST_METHOD=%s", method);
		putenv(meth_env);
		if (strcasecmp(method, "GET") == 0) {
			sprintf(query_env, "QUERY_STRING=%s", query_string);
			putenv(query_env);
		}
		else {   /* POST */
			sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
			putenv(length_env);
		}
		execl(path, path, NULL);
		exit(0);
	} else {    /* parent */
		close(cgi_output[1]);
		close(cgi_input[0]);
		if (strcasecmp(method, "POST") == 0)
			for (i = 0; i < content_length; i++) {
				recv(client, &c, 1, 0);
				write(cgi_input[1], &c, 1);
			}
		while (read(cgi_output[0], &c, 1) > 0)
			send(client, &c, 1, MSG_NOSIGNAL);

		close(cgi_output[0]);
		close(cgi_input[1]);
		waitpid(pid, &status, 0);
	}
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
static int get_line(int sock, char *buf, int size)
{
	int n;

	if ( (n = recv(sock, buf, size-1, 0)) > 0)
		buf[n] = '\0';

	return n;
}


/**********************************************************************/
/* Get the mine type of the file                                      */
/**********************************************************************/
static char * get_mime_type(const char * filename)
{
	static const char *  mime_type_text = "text/plain";
	static char          mime_type[256];

	//Initialize the default return value with text/html
	strncpy(mime_type, mime_type_text, sizeof(mime_type));

#ifdef WITH_LIBMAGIC
	const char *  mime_type_search;
	magic_t cookie;

	if ( (cookie = magic_open(MAGIC_MIME_TYPE)) == NULL )
		return mime_type_text;

	if (    ( magic_load( cookie, NULL ) == 0)
		&&	( mime_type_search = magic_file( cookie, filename )) != NULL )
	{
		strncpy(mime_type, mime_type_search, sizeof(mime_type));
	}

	magic_close( cookie );

#else
	int nbElem = NB_ELEM(aMime);

	int i;
	for (i=0; i<nbElem; i++)
	{
		T_MIME_DESC * m = &aMime[i];

		char * dot = NULL;

		if (    (dot = rindex(filename, '.')) != NULL
			 &&  strcmp(dot+1, m->extention) == 0)
		{
			strncpy(mime_type, m->mine_type, sizeof(mime_type));
			break;
		}
	}

#endif

	LOG("mime_type=%s\n",mime_type);
	return mime_type;
}

/**********************************************************************/
/* Get the mine type of the file                                      */
/**********************************************************************/
static char * get_mime_type_from_buffer(const char * buffer)
{
	static const char *  mime_type_text = "text/plain";
	static char          mime_type[256];

	//Initialize the default return value with text/html
	strncpy(mime_type, mime_type_text, sizeof(mime_type));

#ifdef WITH_LIBMAGIC
	const char *  mime_type_search;
	magic_t cookie;

	if ( (cookie = magic_open(MAGIC_MIME_TYPE)) == NULL )
		return mime_type_text;

	if (    ( magic_load( cookie, NULL ) == 0)
		&&	( mime_type_search = magic_buffer( cookie, buffer, strlen(buffer) )) != NULL )
	{
		strncpy(mime_type, mime_type_search,sizeof(mime_type));
	}


	magic_close( cookie );

#else
	int nbElem = NB_ELEM(aMime);

	int i;
	for (i=0; i<nbElem; i++)
	{
		T_MIME_DESC * m = &aMime[i];
		char * header   = m->header;

		if (   header != NULL
			&& strncmp(header, buffer, strlen(header)) == 0 )
		{
			strncpy(mime_type, m->mine_type, sizeof(mime_type));
			break;
		}
	}

#endif

	LOG("mime_type=%s\n",mime_type);
	return mime_type;
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
static void headers(int client, const char *filename)
{
	char buf[1024] = {0};
	char * mime_type = get_mime_type(filename);

	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	strcpy(buf, SERVER_STRING);
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "Content-Type: %s\r\n", mime_type);
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);

}


/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
static void headers_from_buffer(int client, const char * buffer)
{
	char buf[1024] = {0};
	char * mime_type = get_mime_type_from_buffer(buffer);

	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	strcpy(buf, SERVER_STRING);
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "Content-Type: %s\r\n", mime_type);
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);

}


/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
static void headers_ok(int client)
{
	char buf[1024] = {0};

	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	strcpy(buf, SERVER_STRING);
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "Content-Type: application/xml\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);

}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
static void serve_file( HTTPD_REQUEST * request)
{
	FILE *resource = NULL;

	LOG("%s\n", request->path);

	resource = fopen(request->path, "r");
	if (resource == NULL)
		not_found(request->client);
	else
	{
		headers(request->client, request->path);
		cat(request->client, resource);

		fclose(resource);
	}

}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
static int startup(u_short *port)
{
	int httpd = 0;
	struct sockaddr_in name;

	httpd = socket(PF_INET, SOCK_STREAM, 0);
	if (httpd == -1)
		error_die("socket");

	memset(&name, 0, sizeof(name));

	name.sin_family = AF_INET;
	name.sin_port = htons(*port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);

	int reuse = 1;
    if(setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (int *)&reuse, sizeof(reuse))<0)
        error_die("reuseaddr");

    int fdflags = fcntl(httpd, F_GETFD);
    if(fdflags == -1)
    	error_die("fcntl F_GETFD");
    else if(fcntl(httpd,F_SETFD, fdflags|FD_CLOEXEC) == -1)
     	error_die("fcntl F_SETFD");


	if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
		error_die("bind");

	if (*port == 0)  /* if dynamically allocating a port */
	{
		socklen_t namelen = sizeof(name);
		if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
			error_die("getsockname");
		*port = ntohs(name.sin_port);
	}

	if (listen(httpd, 5) < 0)
		error_die("listen");

	return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
static void unimplemented(int client)
{
	char buf[1024] = {0};

	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "</TITLE></HEAD>\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
}


/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
static void not_found(int client)
{
	char buf[1024] = {0};

	sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "your request because the resource specified\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "is unavailable or nonexistent.\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
}


/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
static void bad_request(int client)
{
	char buf[1024] = {0};

	sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "Content-type: text/html\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "<P>Your browser sent a bad request, ");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "such as a POST without a Content-Length.\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
static void cannot_execute(int client)
{
	char buf[1024] = {0};


	sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "Content-type: text/html\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);;
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
	sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
	send(client, buf, strlen(buf), MSG_NOSIGNAL);
}

/**********************************************************************/

int picows_start( unsigned short port, const char * root_path )
{
	int server_sock = -1;
	int client_sock = -1;
	struct sockaddr_in client;
	socklen_t          client_len = sizeof(client);


	Httpd.port = port;
	Httpd.rootPath = root_path;

	server_sock = startup(&port);
	LOG("picoWs running on port %d\n", port);

	while (1)
	{
		client_sock = accept(server_sock,
				(struct sockaddr *)&client,
				&client_len);
		if (client_sock == -1)
			error_die("accept");
#ifdef WITH_LIBPTHREAD
		pthread_t newthread;

		if (pthread_create(&newthread , NULL, accept_request, client_sock) != 0)
			perror("pthread_create");
#else
		accept_request(client_sock);
#endif
	}

	close(server_sock);

	return(0);
}

int picows_load( int nb, WEBSERVICE * aWebService )
{
	Httpd.nbWebService = nb;
	Httpd.aWebService  = aWebService;

	return nb;
}
